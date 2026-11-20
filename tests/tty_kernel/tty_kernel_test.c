/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host gate for the tty.c code that holds VMIN and VTIME. extract.py lifts
 * ttychars() and the TIOCGMINTIME and TIOCSMINTIME cases of ttioctl() out of
 * the production source; they compile here against the kernel's own struct
 * tty, so the two padding bytes the fields occupy and the request that
 * reads and writes them are the shipped ones.
 *
 * The kernel headers are included without the host's (-nostdinc); the
 * reporting half of the gate is the host-side libc_shim.c of
 * tests/libc_sysctl.
 */
#define KERNEL 1
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/tty.h>

#include "libc_shim.h"

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/*
 * The two lines of tty.c above ttywait(), and the kernel's priority and
 * sleep calls, which the model replaces: spltty() and splx() keep a level,
 * and sleep() is one transmit-complete interrupt or a signal.
 */
#define CLR(t, f) (t) &= ~(f)
#define ISSET(t, f) ((t) & (f))

static int spl_level;
static void *xmit_env[5];
static int xmit_outcome;
static struct tty *xmit_tty;

static int
model_spltty(void)
{
	int old = spl_level;

	spl_level = 1;
	return old;
}

static void
model_splx(int s)
{
	spl_level = s;
}

/* A mutant that never sleeps leaves it unused. */
static void model_sleep(caddr_t chan, int pri) __attribute__((unused));

#undef spltty
#undef splx
#define spltty() model_spltty()
#define splx(s) model_splx(s)
#define sleep(chan, pri) model_sleep(chan, pri)

#ifndef TTY_EXTRACTED
#define TTY_EXTRACTED "tty_extracted.c"
#endif
#include TTY_EXTRACTED

_Static_assert(sizeof(struct tty) == 100, "struct tty size changed");
_Static_assert(__builtin_offsetof(struct tty, t_vmin) == 98,
    "t_vmin offset changed");
_Static_assert(__builtin_offsetof(struct tty, t_vtime) == 99,
    "t_vtime offset changed");

/* A tty whose every byte is a value no default produces. */
static void
fill(struct tty *tp)
{
	unsigned char *p = (unsigned char *)tp;
	unsigned i;

	for (i = 0; i < sizeof *tp; i++)
		p[i] = 0xa5;
}

static int
same_bytes(const void *a, const void *b, unsigned n)
{
	const unsigned char *p = a, *q = b;

	while (n-- > 0)
		if (*p++ != *q++)
			return 0;
	return 1;
}

static void
check_default(void)
{
	struct tty t;
	int v = -1;

	/* ttychars() gives a fresh tty the read the shim has always reported. */
	fill(&t);
	ttychars(&t);
	CHECK(t.t_vmin == 1 && t.t_vtime == 0);
	CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&v) == 0);
	CHECK(v == 1);
}

static void
check_round_trip(void)
{
	struct tty t, before;
	int v, got;

	/* Every VMIN and VTIME pair reads back as it was written. */
	for (v = 0; v <= 0xffff; v++) {
		ttychars(&t);
		got = v;
		CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
		CHECK(t.t_vmin == (v & 0xff) && t.t_vtime == ((v >> 8) & 0xff));
		got = -1;
		CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&got) == 0);
		CHECK(got == v);
	}

	/* Bits above the two bytes are ignored. */
	ttychars(&t);
	got = 0x7f12ab34;
	CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
	CHECK(t.t_vmin == 0x34 && t.t_vtime == 0xab);
	got = -1;
	CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&got) == 0);
	CHECK(got == 0xab34);
	got = -1;
	CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
	CHECK(t.t_vmin == 0xff && t.t_vtime == 0xff);

	/* The request touches no other byte of the tty. */
	fill(&t);
	ttychars(&t);
	t.t_flags = 0x12345678;
	t.t_winsize.ws_row = 24;
	t.t_winsize.ws_col = 80;
	t.t_ospeed = 9;
	before = t;
	got = 0x0203;
	CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
	before.t_vmin = 3;
	before.t_vtime = 2;
	CHECK(same_bytes(&t, &before, sizeof t));
	got = 0;
	CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&got) == 0);
	CHECK(same_bytes(&t, &before, sizeof t));

	/* A request the cases do not own is left to the rest of ttioctl(). */
	CHECK(mintime_request(&t, TIOCGWINSZ, (caddr_t)&got) == -1);
	CHECK(same_bytes(&t, &before, sizeof t));
}

/*
 * A transmitter model. The start routine moves queued bytes into a FIFO and
 * sets TS_BUSY; the transmit-complete interrupt, which each sleep() stands
 * for, empties the FIFO and clears TS_BUSY. A sleep can instead be cut short
 * by a signal, which leaves the kernel by a longjmp as sleep() does, and
 * with hang set the interrupt never comes.
 */
static struct {
	int fifo;
	int sleeps;
	int starts;
	int hang;
	int signal_at;		/* sleep that a signal interrupts, or 0 */
} xmit;

#define STEP_LIMIT 50

static void
model_start(struct tty *tp)
{
	if (++xmit.starts > STEP_LIMIT) {
		xmit_outcome = 2;
		__builtin_longjmp(xmit_env, 1);
	}
	while (tp->t_outq.c_cc > 0) {
		tp->t_outq.c_cc--;
		xmit.fifo++;
	}
	if (xmit.fifo > 0)
		tp->t_state |= TS_BUSY;
}

static void
model_sleep(caddr_t chan, int pri)
{
	CHECK(chan == (caddr_t)&xmit_tty->t_outq);
	CHECK(pri == TTOPRI);
	/* A sleep at TTOPRI, above PZERO, is the kind a signal interrupts. */
	CHECK(pri > PZERO);
	xmit.sleeps++;
	if (xmit.signal_at == xmit.sleeps) {
		xmit_outcome = 1;
		__builtin_longjmp(xmit_env, 1);
	}
	if (!xmit.hang) {
		xmit.fifo = 0;
		xmit_tty->t_state &= ~TS_BUSY;
	}
}

static void
drain_setup(struct tty *tp, int queued, int fifo)
{
	static const struct tty zero;
	static const typeof(xmit) idle;

	*tp = zero;
	xmit = idle;
	xmit.fifo = fifo;
	xmit_tty = tp;
	spl_level = 0;
	tp->t_oproc = model_start;
	tp->t_state = TS_CARR_ON | (fifo ? TS_BUSY : 0);
	tp->t_outq.c_cc = queued;
}

/* 0 when the request returned, 1 when a signal left it, 2 when it spun. */
static int
drain(struct tty *tp)
{
	xmit_outcome = 0;
	if (__builtin_setjmp(xmit_env) == 0) {
		CHECK(drain_request(tp, TIOCDRAIN, 0) == 0);
		return 0;
	}
	return xmit_outcome;
}

static void
check_drain(void)
{
	static const int sizes[] = { 1, 7, 32, 100, 255 };
	struct tty t;
	int i;

	/* Nothing queued and the transmitter idle: no wait and no start. */
	drain_setup(&t, 0, 0);
	CHECK(drain(&t) == 0);
	CHECK(xmit.sleeps == 0 && xmit.starts == 0 && spl_level == 0);

	/* Queued bytes leave the queue and the transmitter before the return. */
	for (i = 0; i < COUNT_OF(sizes); i++) {
		drain_setup(&t, sizes[i], 0);
		CHECK(drain(&t) == 0);
		CHECK(t.t_outq.c_cc == 0);
		CHECK((t.t_state & TS_BUSY) == 0);
		CHECK(xmit.fifo == 0);
		CHECK(xmit.sleeps == 1 && spl_level == 0);
	}

	/* An empty queue with bytes still in the FIFO waits for them. */
	drain_setup(&t, 0, 5);
	CHECK(drain(&t) == 0);
	CHECK(xmit.fifo == 0 && (t.t_state & TS_BUSY) == 0);
	CHECK(xmit.sleeps == 1);

	/* The wait is a wait: a transmitter that stalls keeps the caller asleep. */
	drain_setup(&t, 10, 0);
	xmit.hang = 1;
	xmit.signal_at = 4;
	CHECK(drain(&t) == 1);
	CHECK(xmit.sleeps == 4);
	/* The signal leaves the bytes where they were; nothing is flushed. */
	CHECK(xmit.fifo == 10 && (t.t_state & TS_BUSY) != 0);

	/* A signal during the first sleep leaves the same way. */
	drain_setup(&t, 3, 0);
	xmit.signal_at = 1;
	CHECK(drain(&t) == 1);
	CHECK(xmit.fifo == 3 && (t.t_state & TS_BUSY) != 0);

	/* Without carrier or without a start routine there is nothing to wait for. */
	drain_setup(&t, 9, 0);
	t.t_state &= ~TS_CARR_ON;
	CHECK(drain(&t) == 0);
	CHECK(t.t_outq.c_cc == 9 && xmit.sleeps == 0 && xmit.starts == 0);
	drain_setup(&t, 9, 0);
	t.t_oproc = 0;
	CHECK(drain(&t) == 0);
	CHECK(t.t_outq.c_cc == 9 && xmit.sleeps == 0);

	/* A request the drain case does not own is left to the rest of ttioctl(). */
	drain_setup(&t, 4, 0);
	CHECK(drain_request(&t, TIOCSMINTIME, (caddr_t)&i) == -1);
	CHECK(xmit.sleeps == 0 && xmit.starts == 0);
}

static void
check_request_codes(void)
{
	/* One int in each direction, in the tty group, distinct from all else. */
	CHECK(TIOCGMINTIME != TIOCSMINTIME);
	CHECK((TIOCGMINTIME & IOC_OUT) && !(TIOCGMINTIME & IOC_IN));
	CHECK((TIOCSMINTIME & IOC_IN) && !(TIOCSMINTIME & IOC_OUT));
	CHECK(((TIOCGMINTIME >> 16) & IOCPARM_MASK) == sizeof(int));
	CHECK(((TIOCSMINTIME >> 16) & IOCPARM_MASK) == sizeof(int));
	CHECK(TIOCGMINTIME != TIOCGWINSZ && TIOCSMINTIME != TIOCSWINSZ);
	CHECK(TIOCGMINTIME != TIOCGETP && TIOCSMINTIME != TIOCSETP);
	CHECK((TIOCDRAIN & IOC_VOID) && ((TIOCDRAIN >> 16) & IOCPARM_MASK) == 0);
	CHECK(TIOCDRAIN != TIOCSTOP && TIOCDRAIN != TIOCSTART);
}

int
main(void)
{
	check_default();
	check_round_trip();
	check_drain();
	check_request_codes();
	return lsys_verdict("tty_kernel");
}
