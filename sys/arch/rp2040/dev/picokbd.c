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
 * The PicoCalc keyboard controller, polled over I2C1.
 *
 * Reading a register is a one-byte write of its number followed by a
 * two-byte read; the controller's receive handler prepares the reply when
 * the write lands. FIFO register 0x09 yields one key event per exchange
 * as (state, key), and (0, 0) when the FIFO is empty. The board routes no
 * interrupt from the controller, so a callout polls.
 *
 * The exchange is split across callouts so that no poll waits on the bus:
 * one queues the write with STOP, the next queues two reads with STOP, and
 * the one after that collects the bytes and queues the next write.
 * DW_apb_i2c (datasheet 4.3) holds the queued commands in its transmit FIFO
 * and runs them on its own. While the keyboard is idle the callouts run
 * KBD_POLL_MS apart, the wait the vendor's reference driver
 * (picocalc_helloworld/i2ckbd.c) leaves between write and read; its FUZIX
 * port splits the exchange the same way. Once an event arrives they run
 * KBD_FAST_TICKS apart until KBD_FAST_EXCHANGES exchanges in a row come back
 * empty. The fast rate matters for more than latency: the controller's FIFO
 * holds 31 events and drops new ones when full (picocalc_keyboard
 * conf_app.h and fifo.ino), and a dropped Ctrl release would leave Ctrl
 * applied to every later key. Two rules bound that: an I2C restart, which
 * may have discarded the release, clears Ctrl; and a latched Ctrl expires
 * KBD_CTRL_EXPIRE_MS after the last Ctrl event, because the firmware
 * reports a held key again every 100 ms once it has been held for
 * KEY_HOLD_TIME, 300 ms (keyboard.ino KEY_STATE_HOLD, keyboard.h), so a
 * Ctrl still held is never silent that long.
 *
 * A NACK or lost arbitration raises TX_ABRT, which flushes the queued
 * commands; the poll counts it and restarts the exchange. An abort after
 * the FIFO register write has landed loses that one event, because the
 * controller dequeues it when the write arrives.
 *
 * The controller is a DW_apb_i2c master in fast-mode timing programmed
 * for KBD_I2C_HZ. ic_clk is clk_sys (datasheet 4.3.2), and the counts
 * follow the Pico SDK's i2c_set_baudrate: a 3:2 low-to-high split of the
 * SCL period, a spike filter of one sixteenth of the low count, and a
 * 300 ns SDA hold. Datasheet 4.3.14.1 adds one ic_clk to the low count and
 * SPKLEN + 7 to the high count, so SCL runs near 96 kHz.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/picocalcvar.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/picokbd_map.h>
#include <rp2040/dev/picokbd_evq.h>
#include <rp2040/dev/picokbd_req.h>
#include <rp2040/dev/lcdterm.h>
#ifdef SD_ENABLED
#include <rp2040/dev/sd.h>
#endif

#define	I2C1_BASE		0x40048000UL
#define	IC_CON			0x00
#define	IC_TAR			0x04
#define	IC_DATA_CMD		0x10
#define	IC_FS_SCL_HCNT		0x1c
#define	IC_FS_SCL_LCNT		0x20
#define	IC_INTR_MASK		0x30
#define	IC_RAW_INTR_STAT	0x34
#define	IC_RX_TL		0x38
#define	IC_TX_TL		0x3c
#define	IC_CLR_TX_ABRT		0x54
#define	IC_CLR_STOP_DET		0x60
#define	IC_ENABLE		0x6c
#define	IC_ENABLE_ABORT		(1UL << 1)
#define	IC_RXFLR		0x78
#define	IC_SDA_HOLD		0x7c
#define	IC_ENABLE_STATUS	0x9c
#define	IC_FS_SPKLEN		0xa0

#define	IC_CON_MASTER		(1UL << 0)
#define	IC_CON_SPEED_FAST	(2UL << 1)
#define	IC_CON_RESTART_EN	(1UL << 5)
#define	IC_CON_SLAVE_DISABLE	(1UL << 6)
#define	IC_CON_TX_EMPTY_CTRL	(1UL << 8)

#define	IC_CMD_READ		(1UL << 8)
#define	IC_CMD_STOP		(1UL << 9)

#define	IC_INTR_TX_ABRT		(1UL << 6)
#define	IC_INTR_STOP_DET	(1UL << 9)

#define	I2C_REG(off)		PC_REG32(I2C1_BASE + (off))

#define	KBD_I2C_HZ		100000
#define	KBD_REG_VER		0x01
#define	KBD_REG_FIFO		0x09

#define	KBD_POLL_MS		16
#define	KBD_POLL_TICKS		((KBD_POLL_MS * HZ + 999) / 1000)
#define	KBD_FAST_TICKS		((2 * HZ + 999) / 1000)
#define	KBD_FAST_EXCHANGES	8
#define	KBD_CTRL_EXPIRE_MS	600
#define	KEY_MOD_CTRL		0xa5
#define	KBD_STALL_POLLS		8	/* Phases without progress before reset. */
#define	KBD_SYNC_TIMEOUT_US	50000

#define	KBD_RING		32	/* Power of two. */

enum {
	PH_WRITE,		/* Next: queue the register write. */
	PH_READ,		/* Write queued; next: queue the reads. */
	PH_COLLECT,		/* Reads queued; next: take two bytes. */
};

static int phase;
static int stall;
static int fast;		/* Exchanges left at the fast rate. */
static u_int ctrl_seen_us;	/* pc_micros() of the last Ctrl event. */
static int ctrl;
static int polling;
static int fw_version = -1;
static u_int kbd_errors;

static char ring[KBD_RING];
static u_int ring_head, ring_tail;

/*
 * The register request the write phase sends in place of the FIFO read
 * (picokbd_req.h). rq_inflight marks an exchange that carries it, so the
 * collect phase hands the reply to the mailbox instead of the key map.
 */
static struct kbdreq kbdq;
static int rq_inflight;

/* The exchange in flight was discarded: retry or fail its request. */
static void
exchange_lost(void)
{
	rq_inflight = 0;
	kbdreq_abort(&kbdq);
	if (kbdq.state == KR_FAILED)
		wakeup((caddr_t)&kbdq);
}

/* Set or clear IC_ENABLE and wait for IC_ENABLE_STATUS; 0 on a timeout. */
static int
i2c_enable(int on)
{
	u_int t0 = pc_micros();

	I2C_REG(IC_ENABLE) = on ? 1 : 0;
	while ((I2C_REG(IC_ENABLE_STATUS) & 1) != (u_int)(on ? 1 : 0)) {
		if (pc_micros() - t0 >= 10000)
			return 0;
	}
	return 1;
}

static void i2c_setup(void);

/*
 * Return the block to an idle, enabled master. A disable that does not
 * complete within 10 ms (datasheet 4.3.10.3 makes the poll end in an
 * error) is followed by IC_ENABLE.ABORT, which relinquishes the bus
 * (4.3.10.4); if the block still reports enabled, it goes through its
 * reset and is programmed again.
 */
static void
i2c_restart(void)
{
	u_int t0;

	if (!i2c_enable(0)) {
		kbd_errors++;
		/* ABORT is honored only while the block is enabled. */
		I2C_REG(IC_ENABLE) = 1;
		I2C_REG(IC_ENABLE) = 1 | IC_ENABLE_ABORT;
		t0 = pc_micros();
		while ((I2C_REG(IC_ENABLE) & IC_ENABLE_ABORT) &&
		    pc_micros() - t0 < 10000)
			continue;
		if (!i2c_enable(0)) {
			PC_REG32(PC_RESETS_BASE + PC_ATOMIC_SET +
			    PC_RESETS_RESET) = PC_RESET_I2C1;
			pc_unreset(PC_RESET_I2C1);
			i2c_setup();
		}
	}
	(void)I2C_REG(IC_CLR_TX_ABRT);
	(void)I2C_REG(IC_CLR_STOP_DET);
	(void)i2c_enable(1);
	phase = PH_WRITE;
	stall = 0;
	ctrl = 0;	/* The restart may have discarded a Ctrl release. */
	exchange_lost();
}

/*
 * Recovery the poll callout runs without waiting. The callout runs inside
 * the SysTick exception, which cannot preempt itself, so a 10 ms wait
 * there would lose clock ticks; instead each poll advances one step:
 * disable, then ABORT if the disable does not complete in 10 ms, then a
 * block reset if ABORT does not, then enable. kbd_exchange(), which runs
 * with interrupts masked anyway, recovers with i2c_restart() instead.
 */
enum {
	REC_IDLE,
	REC_DISABLE,		/* IC_ENABLE cleared; waiting for status. */
	REC_ABORT,		/* ABORT set; waiting for it to clear. */
	REC_ENABLE,		/* IC_ENABLE set; waiting for status. */
};

static int recovering;
static u_int recover_t0;
static int sync_mode;		/* kbd_exchange() is running. */

static void
recover_begin(void)
{
	I2C_REG(IC_ENABLE) = 0;
	recovering = REC_DISABLE;
	recover_t0 = pc_micros();
	phase = PH_WRITE;
	stall = 0;
	ctrl = 0;	/* The fault may have discarded a Ctrl release. */
	exchange_lost();
}

static void
recover_enable(void)
{
	(void)I2C_REG(IC_CLR_TX_ABRT);
	(void)I2C_REG(IC_CLR_STOP_DET);
	I2C_REG(IC_ENABLE) = 1;
	recovering = REC_ENABLE;
	recover_t0 = pc_micros();
}

static void
recover_reset(void)
{
	kbd_errors++;
	PC_REG32(PC_RESETS_BASE + PC_ATOMIC_SET + PC_RESETS_RESET) =
	    PC_RESET_I2C1;
	pc_unreset(PC_RESET_I2C1);
	i2c_setup();
	recover_enable();
}

/* One step of the callout's recovery; nonzero once the block is enabled. */
static int
recover_step(void)
{
	u_int elapsed = pc_micros() - recover_t0;
	int enabled = (I2C_REG(IC_ENABLE_STATUS) & 1) != 0;

	switch (recovering) {
	case REC_DISABLE:
		if (!enabled)
			recover_enable();
		else if (elapsed >= 10000) {
			kbd_errors++;
			/* ABORT is honored only while the block is enabled. */
			I2C_REG(IC_ENABLE) = 1;
			I2C_REG(IC_ENABLE) = 1 | IC_ENABLE_ABORT;
			recovering = REC_ABORT;
			recover_t0 = pc_micros();
		}
		return 0;
	case REC_ABORT:
		if ((I2C_REG(IC_ENABLE) & IC_ENABLE_ABORT) == 0) {
			recover_begin();	/* Disable again. */
			return 0;
		}
		if (elapsed >= 10000)
			recover_reset();
		return 0;
	case REC_ENABLE:
		if (enabled) {
			recovering = REC_IDLE;
			return 1;
		}
		if (elapsed >= 10000)
			recover_reset();
		return 0;
	default:
		return 1;
	}
}

/* A fault in the exchange: recover by waiting or by callout steps. */
static void
kbd_fault(void)
{
	kbd_errors++;
	if (sync_mode)
		i2c_restart();
	else
		recover_begin();
}

/* Program the block, which must be disabled. */
static void
i2c_setup(void)
{
	u_int period, lcnt, hcnt;

	I2C_REG(IC_CON) = IC_CON_MASTER | IC_CON_SPEED_FAST |
	    IC_CON_RESTART_EN | IC_CON_SLAVE_DISABLE | IC_CON_TX_EMPTY_CTRL;
	I2C_REG(IC_TX_TL) = 0;
	I2C_REG(IC_RX_TL) = 0;
	I2C_REG(IC_INTR_MASK) = 0;

	period = (CPU_KHZ * 1000UL + KBD_I2C_HZ / 2) / KBD_I2C_HZ;
	lcnt = period * 3 / 5;
	hcnt = period - lcnt;
	I2C_REG(IC_FS_SCL_HCNT) = hcnt;
	I2C_REG(IC_FS_SCL_LCNT) = lcnt;
	I2C_REG(IC_FS_SPKLEN) = lcnt < 16 ? 1 : lcnt / 16;
	I2C_REG(IC_SDA_HOLD) = (CPU_KHZ * 3UL) / 10000 + 1;
	I2C_REG(IC_TAR) = PICOCALC_KBD_ADDR;
}

void
picokbd_init(void)
{
	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_I2C1);
	(void)i2c_enable(0);
	i2c_setup();

	pc_pin(PICOCALC_KBD_SDA, PC_FUNC_I2C, PC_PADS_PUE | PC_PADS_SCHMITT);
	pc_pin(PICOCALC_KBD_SCL, PC_FUNC_I2C, PC_PADS_PUE | PC_PADS_SCHMITT);

	i2c_restart();
}

#define	STEP_WAIT	(-1)	/* The bus has not finished the phase. */
#define	STEP_DONE	0	/* Phase advanced; no key event. */
#define	STEP_EVENT	1	/* Collected a non-empty event. */

/*
 * Advance the exchange of register reg by one phase. An abort restarts
 * the controller and reports STEP_DONE with phase back at PH_WRITE. The
 * write phase sends a waiting register request instead of reg, except in
 * the polled exchange, whose caller reads the reply itself; a collected
 * request reply reports STEP_DONE.
 */
static int
kbd_step(int reg, int *state, int *key)
{
	u_int raw = I2C_REG(IC_RAW_INTR_STAT);
	u_char out[2];
	int n;

	if (raw & IC_INTR_TX_ABRT) {
		kbd_fault();
		return STEP_DONE;
	}

	switch (phase) {
	case PH_WRITE:
		(void)I2C_REG(IC_CLR_STOP_DET);
		n = sync_mode ? 0 : kbdreq_next(&kbdq, out);
		rq_inflight = n > 0;
		if (n == 2) {
			I2C_REG(IC_DATA_CMD) = out[0];
			I2C_REG(IC_DATA_CMD) = out[1] | IC_CMD_STOP;
		} else
			I2C_REG(IC_DATA_CMD) = (n == 1 ? out[0] : (u_int)reg) |
			    IC_CMD_STOP;
		phase = PH_READ;
		return STEP_DONE;
	case PH_READ:
		if ((raw & IC_INTR_STOP_DET) == 0)
			return STEP_WAIT;
		(void)I2C_REG(IC_CLR_STOP_DET);
		I2C_REG(IC_DATA_CMD) = IC_CMD_READ;
		I2C_REG(IC_DATA_CMD) = IC_CMD_READ | IC_CMD_STOP;
		phase = PH_COLLECT;
		return STEP_DONE;
	default:
		if (I2C_REG(IC_RXFLR) < 2)
			return STEP_WAIT;
		*state = (int)(I2C_REG(IC_DATA_CMD) & 0xff);
		*key = (int)(I2C_REG(IC_DATA_CMD) & 0xff);
		phase = PH_WRITE;
		if (rq_inflight) {
			rq_inflight = 0;
			kbdreq_done(&kbdq, *state, *key);
			if (!kbdreq_pending(&kbdq))
				wakeup((caddr_t)&kbdq);
			return STEP_DONE;
		}
		return (*state != 0 || *key != 0) ? STEP_EVENT : STEP_DONE;
	}
}

static void
ring_put(const char *s, int n)
{
	while (n-- > 0) {
		if (ring_head - ring_tail >= KBD_RING)
			return;
		ring[ring_head++ % KBD_RING] = *s++;
	}
}

static void
kbd_poll(caddr_t arg __unused)
{
	char seq[PICOKBD_MAXSEQ];
	int state, key, n, r;

#ifdef SD_ENABLED
	sd_detect_poll();
#endif
	if (recovering != REC_IDLE && !recover_step()) {
		timeout(kbd_poll, (caddr_t)0, KBD_FAST_TICKS);
		return;
	}
	r = kbd_step(KBD_REG_FIFO, &state, &key);
	if (r == STEP_WAIT) {
		if (++stall > KBD_STALL_POLLS)
			kbd_fault();
	} else if (recovering != REC_IDLE) {
		/* kbd_step() began a recovery; the next polls step it. */
	} else {
		stall = 0;
		if (r == STEP_EVENT) {
			fast = KBD_FAST_EXCHANGES;
			if (key == KEY_MOD_CTRL)
				ctrl_seen_us = pc_micros();
			else if (ctrl && pc_micros() - ctrl_seen_us >
			    KBD_CTRL_EXPIRE_MS * 1000U)
				ctrl = 0;
			lcdcons_keycheck();
			n = picokbd_event(state, key, lcdterm_app_cursor(),
			    &ctrl, seq);
			if (n > 0)
				lcdcons_input(seq, n);
		} else if (phase == PH_WRITE && fast > 0)
			fast--;		/* An empty exchange completed. */
		/* A collected exchange queues the next write at once. */
		if (phase == PH_WRITE)
			(void)kbd_step(KBD_REG_FIFO, &state, &key);
	}
	timeout(kbd_poll, (caddr_t)0,
	    fast > 0 ? KBD_FAST_TICKS : KBD_POLL_TICKS);
}

/*
 * Start polling. timeout() needs the callout list that main() builds after
 * config() probes devices, so the console's first open starts the poll.
 */
void
picokbd_poll_start(void)
{
	int s = spltty();

	if (!polling) {
		polling = 1;
		timeout(kbd_poll, (caddr_t)0, KBD_POLL_TICKS);
	}
	splx(s);
}

/* Step until the phase completes; STEP_WAIT on a timeout. */
static int
kbd_step_sync(int reg, int *state, int *key)
{
	u_int t0 = pc_micros();
	int r;

	while ((r = kbd_step(reg, state, key)) == STEP_WAIT) {
		if (pc_micros() - t0 >= KBD_SYNC_TIMEOUT_US) {
			kbd_errors++;
			i2c_restart();
			break;
		}
	}
	return r;
}

/*
 * Run one whole exchange of register reg by busy waiting, KBD_POLL_MS
 * between write and read, with interrupts masked so that the poll callout
 * cannot advance the same exchange. Used at probe time, before callouts
 * run, and by the console's polled getc. Returns 1 with a non-empty
 * reply, 0 for an empty one, a timeout or an abort.
 */
static int
kbd_exchange(int reg, int *state, int *key)
{
	u_int t0;
	int s, r = STEP_DONE;

	s = spltty();
	sync_mode = 1;
	*state = *key = 0;
	if (phase != PH_WRITE || recovering != REC_IDLE) {
		i2c_restart();
		recovering = REC_IDLE;
	}
	(void)kbd_step(reg, state, key);
	t0 = pc_micros();
	while (pc_micros() - t0 < KBD_POLL_MS * 1000)
		continue;
	if (kbd_step_sync(reg, state, key) == STEP_DONE && phase == PH_COLLECT)
		r = kbd_step_sync(reg, state, key);
	sync_mode = 0;
	splx(s);
	return r == STEP_EVENT;
}

int
picokbd_version(void)
{
	int state, key;

	if (fw_version < 0 && kbd_exchange(KBD_REG_VER, &state, &key))
		fw_version = key;
	return fw_version;
}

/* Polled read of one byte for the console's getc; waits for a key. */
int
picokbd_getc(void)
{
	char seq[PICOKBD_MAXSEQ];
	int state, key, n;

	while (ring_head == ring_tail) {
		if (kbd_exchange(KBD_REG_FIFO, &state, &key)) {
			n = picokbd_translate(state, key, lcdterm_app_cursor(),
			    &ctrl, seq);
			ring_put(seq, n);
		}
	}
	return (u_char)ring[ring_tail++ % KBD_RING];
}

u_int
picokbd_errors(void)
{
	return kbd_errors;
}

#define	KBDREQ_TIMEOUT_TICKS	HZ	/* A reply takes three polls, ~50 ms. */

static void
kbdreq_timeout(caddr_t arg __unused)
{
	kbdreq_expire(&kbdq);
	wakeup((caddr_t)&kbdq);
}

/*
 * Exchange one controller register through the poller and sleep until the
 * reply arrives. The caller sleeps uninterruptibly because the mailbox
 * and the deadline callout belong to it until the request finishes; the
 * KBDREQ_TIMEOUT_TICKS callout ends a request the poller never answers, so
 * the wait is bounded. Returns 0 with the controller's value in *out, EIO
 * after KBDREQ_TRIES faulted exchanges or ETIMEDOUT.
 */
static int
picokbd_reg(int reg, int write, int val, int *out)
{
	int s, error;

	picokbd_poll_start();
	s = spltty();
	while (kbdreq_submit(&kbdq, reg, write, val) < 0)
		sleep((caddr_t)&kbdq, PRIBIO);
	timeout(kbdreq_timeout, (caddr_t)0, KBDREQ_TIMEOUT_TICKS);
	while (kbdreq_pending(&kbdq))
		sleep((caddr_t)&kbdq, PRIBIO);
	untimeout(kbdreq_timeout, (caddr_t)0);
	error = kbdreq_finish(&kbdq, out);
	wakeup((caddr_t)&kbdq);
	splx(s);
	return error == KRE_NONE ? 0 : error == KRE_TIMEOUT ? ETIMEDOUT : EIO;
}

/* Seconds the last power-off request set; the controller cannot report it. */
static int poweroff_seconds;

/*
 * machdep.picocalc: the keyboard controller's battery, backlight and
 * power-off registers. The syscall layer admits a write only from root
 * (__sysctl in kern_sysctl.c).
 */
int
picocalc_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int v = 0, reg, error;

	if (namelen != 1)
		return ENOTDIR;
	switch (name[0]) {
	case CPU_PICOCALC_BATTERY:
	case CPU_PICOCALC_CHARGING:
		error = picokbd_reg(KBD_REG_BAT, 0, 0, &v);
		if (error)
			return error;
		v = name[0] == CPU_PICOCALC_BATTERY ?
		    kbdreq_battery_percent(v) : kbdreq_battery_charging(v);
		return sysctl_rdint(oldp, oldlenp, newp, v);
	case CPU_PICOCALC_LCD_BACKLIGHT:
	case CPU_PICOCALC_KBD_BACKLIGHT:
		reg = name[0] == CPU_PICOCALC_LCD_BACKLIGHT ?
		    KBD_REG_BKL : KBD_REG_BK2;
		error = oldp ? picokbd_reg(reg, 0, 0, &v) : 0;
		if (error == 0)
			error = sysctl_int(oldp, oldlenp, newp, newlen, &v);
		if (error == 0 && newp != NULL)
			error = picokbd_reg(reg, 1, kbdreq_level(v), &v);
		return error;
	case CPU_PICOCALC_POWEROFF:
		v = poweroff_seconds;
		error = sysctl_int(oldp, oldlenp, newp, newlen, &v);
		if (error == 0 && newp != NULL) {
			v = kbdreq_off_seconds(v);
			sync();
			error = picokbd_reg(KBD_REG_OFF, 1, v, &reg);
			if (error == 0)
				poweroff_seconds = v;
		}
		return error;
	default:
		return EOPNOTSUPP;
	}
}
