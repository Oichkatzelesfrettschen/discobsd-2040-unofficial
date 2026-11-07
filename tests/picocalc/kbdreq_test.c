/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Host test of the keyboard-controller register mailbox
 * (sys/arch/rp2040/dev/picokbd_req.c).
 *
 * The model controller answers as the vendor firmware's receiveEvent does
 * (code/picocalc_keyboard/picocalc_keyboard.ino): a one-byte write names a
 * register to read, a two-byte write with bit 7 set stores a value, and the
 * reply to the following two-byte read is (register, value), or (0, 0) for
 * a register the firmware does not serve. Register 0x09 yields scripted
 * key events. The poller below walks the write, read and collect phases of
 * picokbd.c, one phase per step, so a request never shares an exchange with
 * a key event.
 */

#include <stdio.h>
#include <string.h>

#include "picokbd_req.h"

static int failures;

static void
expect(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

/* Model controller. */
static unsigned char reg_bkl = 0x80, reg_bk2 = 0x40, reg_bat = 0x8a;
static unsigned char off_seconds;
static unsigned char fifo[8][2];
static int fifo_n, fifo_i;
static unsigned char reply[2];
static unsigned char bus[64][2];	/* Bytes of each write transaction. */
static int bus_len[64], nbus;

static void
ctl_receive(const unsigned char *d, int n)
{
	int is_write = d[0] & 0x80, reg = d[0] & 0x7f, v;

	if (nbus < 64) {
		memcpy(bus[nbus], d, (size_t)n);
		bus_len[nbus++] = n;
	}
	reply[0] = reply[1] = 0;
	switch (reg) {
	case 0x05:
		if (is_write) {
			v = d[1] / 16 * 16;
			reg_bkl = (unsigned char)(v < 16 ? 16 : v > 240 ? 240 : v);
		}
		reply[0] = (unsigned char)reg;
		reply[1] = reg_bkl;
		break;
	case 0x0a:
		if (is_write) {
			v = d[1] / 32 * 32;
			reg_bk2 = (unsigned char)(v < 32 || v > 240 ? 0 : v);
		}
		reply[0] = (unsigned char)reg;
		reply[1] = reg_bk2;
		break;
	case 0x0b:
		reply[0] = (unsigned char)reg;
		reply[1] = reg_bat;
		break;
	case 0x0e:
		if (is_write)
			off_seconds = d[1] < 6 ? 6 : d[1];
		reply[0] = (unsigned char)reg;
		reply[1] = 1;
		break;
	case 0x09:
		if (fifo_i < fifo_n) {
			reply[0] = fifo[fifo_i][0];
			reply[1] = fifo[fifo_i][1];
			fifo_i++;
		}
		break;
	default:
		break;
	}
}

/*
 * The poller: PH_WRITE sends a waiting request or the FIFO register,
 * PH_READ and PH_COLLECT fetch the reply. A request reply goes to the
 * mailbox; any other reply is a key event the test counts.
 */
static struct kbdreq q;
static int phase, inflight, events;
static unsigned char last_event[2];

static void
poll_step(int fault)
{
	unsigned char out[2];
	int n;

	if (fault && phase != 0) {
		/* picokbd.c: exchange_lost() on a fault. */
		inflight = 0;
		kbdreq_abort(&q);
		phase = 0;
		return;
	}
	switch (phase) {
	case 0:
		n = kbdreq_next(&q, out);
		inflight = n > 0;
		if (n == 0) {
			out[0] = 0x09;
			n = 1;
		}
		ctl_receive(out, n);
		phase = 1;
		break;
	case 1:
		phase = 2;
		break;
	default:
		phase = 0;
		if (inflight) {
			inflight = 0;
			kbdreq_done(&q, reply[0], reply[1]);
		} else if (reply[0] != 0 || reply[1] != 0) {
			events++;
			last_event[0] = reply[0];
			last_event[1] = reply[1];
		}
		break;
	}
}

static void
poll_exchanges(int n)
{
	int i;

	for (i = 0; i < 3 * n; i++)
		poll_step(0);
}

/* Run a request to completion; returns kbdreq_finish() and the value. */
static int
transact(int reg, int write, int val, int *out)
{
	int i;

	if (kbdreq_submit(&q, reg, write, val) != 0)
		return -99;
	for (i = 0; i < 12 && kbdreq_pending(&q); i++)
		poll_step(0);
	return kbdreq_finish(&q, out);
}

static void
test_encoding(void)
{
	unsigned char out[2] = { 0, 0 };

	expect("submit idle", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	expect("read length", kbdreq_next(&q, out), 1);
	expect("read byte", out[0], 0x0b);
	q.state = KR_IDLE;
	expect("submit idle 2", kbdreq_submit(&q, KBD_REG_BKL, 1, 0x58), 0);
	expect("write length", kbdreq_next(&q, out), 2);
	expect("write register byte", out[0], 0x85);
	expect("write value byte", out[1], 0x58);
	q.state = KR_IDLE;
	expect("submit masks bit 7", kbdreq_submit(&q, 0x8a, 0, 0), 0);
	expect("masked read", kbdreq_next(&q, out), 1);
	expect("masked register", out[0], 0x0a);
	q.state = KR_IDLE;
	expect("second next while idle", kbdreq_next(&q, out), 0);
}

static void
test_values(void)
{
	expect("percent of 0x8a", kbdreq_battery_percent(0x8a), 10);
	expect("charging of 0x8a", kbdreq_battery_charging(0x8a), 1);
	expect("percent of 100", kbdreq_battery_percent(100), 100);
	expect("charging of 100", kbdreq_battery_charging(100), 0);
	expect("percent of 0xe4", kbdreq_battery_percent(0xe4), 100);
	expect("level -5", kbdreq_level(-5), 0);
	expect("level 200", kbdreq_level(200), 200);
	expect("level 240", kbdreq_level(240), 240);
	expect("level 241", kbdreq_level(241), 240);
	expect("level 300", kbdreq_level(300), 240);
	expect("off 0", kbdreq_off_seconds(0), 6);
	expect("off 5", kbdreq_off_seconds(5), 6);
	expect("off 6", kbdreq_off_seconds(6), 6);
	expect("off 30", kbdreq_off_seconds(30), 30);
	expect("off 300", kbdreq_off_seconds(300), 255);
}

static void
test_registers(void)
{
	int v = -1;

	expect("battery read", transact(KBD_REG_BAT, 0, 0, &v), KRE_NONE);
	expect("battery value", v, 0x8a);
	expect("lcd write", transact(KBD_REG_BKL, 1, 0x58, &v), KRE_NONE);
	expect("lcd quantized by firmware", v, 0x50);
	expect("lcd read back", transact(KBD_REG_BKL, 0, 0, &v), KRE_NONE);
	expect("lcd read value", v, 0x50);
	expect("kbd write", transact(KBD_REG_BK2, 1,
	    kbdreq_level(255), &v), KRE_NONE);
	expect("kbd clamped then quantized", v, 224);
	expect("poweroff write", transact(KBD_REG_OFF, 1,
	    kbdreq_off_seconds(0), &v), KRE_NONE);
	expect("controller countdown", off_seconds, 6);
	expect("busy mailbox", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	expect("second submit refused",
	    kbdreq_submit(&q, KBD_REG_BKL, 0, 0), -1);
	q.state = KR_IDLE;
}

/* A request never shares an exchange with a key event, and keys survive. */
static void
test_interleave(void)
{
	int v = 0, i;

	fifo[0][0] = 1; fifo[0][1] = 'a';
	fifo[1][0] = 3; fifo[1][1] = 'a';
	fifo[2][0] = 1; fifo[2][1] = 'b';
	fifo_n = 3;
	fifo_i = 0;
	events = 0;
	nbus = 0;
	phase = 0;
	poll_step(0);			/* Write phase of a FIFO exchange. */
	expect("request queued mid-exchange",
	    kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	for (i = 0; i < 2; i++)
		poll_step(0);		/* The FIFO exchange completes. */
	expect("first event delivered", events, 1);
	expect("event key", last_event[1], 'a');
	for (i = 0; i < 3; i++)
		poll_step(0);		/* The request's own exchange. */
	expect("request answered", kbdreq_finish(&q, &v), KRE_NONE);
	expect("request value", v, 0x8a);
	poll_exchanges(2);
	expect("remaining events delivered", events, 3);
	expect("bus first", bus[0][0], 0x09);
	expect("bus second is the request", bus[1][0], 0x0b);
	expect("bus third", bus[2][0], 0x09);
	expect("bus transaction count", nbus, 4);
}

static void
test_faults(void)
{
	int v = 0, i;

	/* One abort: the request is sent again and succeeds. */
	phase = 0;
	nbus = 0;
	expect("submit", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	poll_step(0);
	poll_step(1);			/* Fault after the write. */
	expect("requeued after one fault", q.state, KR_QUEUED);
	for (i = 0; i < 3; i++)
		poll_step(0);
	expect("retried request done", kbdreq_finish(&q, &v), KRE_NONE);
	expect("retried value", v, 0x8a);
	expect("the register was sent twice", nbus, 2);

	/* Three aborts fail with an I/O error. */
	phase = 0;
	expect("submit 2", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	for (i = 0; i < KBDREQ_TRIES; i++) {
		poll_step(0);
		poll_step(1);
	}
	expect("failed after the retry bound", q.state, KR_FAILED);
	expect("finish reports I/O", kbdreq_finish(&q, &v), KRE_IO);
	expect("mailbox idle after failure", q.state, KR_IDLE);

	/* A reply for another register is stale and counts as a fault. */
	phase = 0;
	expect("submit 3", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	(void)kbdreq_next(&q, (unsigned char[2]){ 0, 0 });
	kbdreq_done(&q, 0x05, 0x50);
	expect("mismatched reply requeues", q.state, KR_QUEUED);
	q.state = KR_IDLE;

	/* A register the firmware does not serve answers (0, 0). */
	phase = 0;
	expect("unserved register", transact(0x02, 0, 0, &v), KRE_IO);
}

static void
test_timeout(void)
{
	int v = 0, i;
	unsigned char out[2];

	/* Expired while queued. */
	phase = 0;
	expect("submit", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	kbdreq_expire(&q);
	expect("expired queued request is not pending",
	    kbdreq_pending(&q), 0);
	expect("timeout cause", kbdreq_finish(&q, &v), KRE_TIMEOUT);

	/*
	 * Expired while in flight: the exchange still completes on the bus,
	 * and its reply must not reach the next request.
	 */
	phase = 0;
	expect("submit 2", kbdreq_submit(&q, KBD_REG_BAT, 0, 0), 0);
	poll_step(0);			/* Sent; waits in the controller. */
	kbdreq_expire(&q);
	expect("timeout in flight", kbdreq_finish(&q, &v), KRE_TIMEOUT);
	expect("submit next", kbdreq_submit(&q, KBD_REG_BKL, 0, 0), 0);
	poll_step(0);
	poll_step(0);			/* The stale reply is collected. */
	expect("stale reply ignored", q.state, KR_QUEUED);
	inflight = 0;
	for (i = 0; i < 3; i++)
		poll_step(0);
	expect("next request answered", kbdreq_finish(&q, &v), KRE_NONE);
	expect("next request value", v, 0x50);

	/* Expiry of an idle or finished mailbox changes nothing. */
	kbdreq_expire(&q);
	expect("idle expiry", q.state, KR_IDLE);
	(void)kbdreq_next(&q, out);
	expect("next on idle", q.state, KR_IDLE);
}

int
main(void)
{
	test_encoding();
	test_values();
	memset(&q, 0, sizeof q);
	test_registers();
	test_interleave();
	test_faults();
	test_timeout();
	if (failures != 0) {
		fprintf(stderr, "kbdreq_test: %d failures\n", failures);
		return 1;
	}
	printf("kbdreq_test: ok\n");
	return 0;
}
