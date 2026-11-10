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

#ifndef _RP2040_RP2040_CORE1_H_
#define _RP2040_RP2040_CORE1_H_

/*
 * The second Cortex-M0+ core as a service processor: the two decisions the
 * cores make about each other, written as functions of plain values so that
 * tests/core1 runs them on a host. core1.c supplies the registers.
 *
 * Each core watches the other, and neither shares a writable word with it.
 *
 * Core 1 watches core 0's clock. Core 0 records the free-running
 * microsecond timer at every tick it takes (clock_catchup.h), and every
 * masking level on ARMv6-M is PRIMASK, so a record that stops moving is a
 * kernel that stopped taking interrupts. core1_observe() turns the record,
 * the timer and the masked section the kernel declared (machine/watchdog.h)
 * into at most one action per sample. A record that stood still across two
 * samples with no section declared at either is a stall: past
 * C1_STALL_US core 1 asks core 0 where it is, once per stall, through an
 * interrupt that PRIMASK cannot hold off. A record that stood still for
 * C1_HANG_US, declared section or not, is a hang: core 1 asks once more,
 * and on the following sample stores what it learned where a reset
 * preserves it and resets the chip.
 *
 * Core 0 watches core 1's message loop. Once a period it sends a numbered
 * ping through the request ring (corering.h) and expects the same number
 * back. core1_sup_tick() counts the periods without an answer; after
 * C1_MISS_MAX it has core 1 reset and relaunched, waiting twice as long
 * before each attempt, and after C1_RESTART_MAX attempts it leaves core 1
 * parked. Core 0 depends on core 1 for nothing, so a parked core 1 costs
 * the observer and no kernel function.
 */

/*
 * The three intervals, each settable from a kernel Config as, say,
 * options "C1_HANG_US=60000000U" for a kernel run under a debugger. The
 * hang bound has to exceed the longest section the kernel masks on
 * purpose, a sector erase of at most 400 ms, and the sample period has to
 * fit core 1's 24-bit SysTick at the configured clock.
 */
#ifndef C1_PERIOD_US
#define	C1_PERIOD_US	10000U		/* core 1 sample period */
#endif
#ifndef C1_STALL_US
#define	C1_STALL_US	100000U		/* tick record this old is a stall */
#endif
#ifndef C1_HANG_US
#define	C1_HANG_US	4000000U	/* and this old ends in a reset */
#endif

_Static_assert(2 * C1_PERIOD_US <= C1_STALL_US && C1_STALL_US < C1_HANG_US &&
    C1_HANG_US >= 1000000U,
    "a stall spans two samples and a hang outlasts every masked section");

#define	C1_SITE_NONE	0U		/* WD_SITE_NONE of machine/watchdog.h */

/* Actions of core1_observe(), ORed. */
#define	C1_ACT_PROBE	1U		/* interrupt core 0 for its PC */
#define	C1_ACT_RESET	2U		/* reset the chip */
#define	C1_ACT_RECORD	4U		/* with RESET: no section was declared */

/* Every field is stored by core 1 alone. */
struct core1_obs {
	unsigned int	co_stamp;	/* tick record at the last sample */
	unsigned int	co_site;	/* declared section at the last sample */
	unsigned int	co_probed;	/* 1 asked for this stall, 2 for the hang */
	unsigned int	co_stalls;	/* stalls with no section declared */
	unsigned int	co_gap_us;	/* oldest tick record among them */
};

/*
 * One sample: now_us is the microsecond timer, stamp_us core 0's tick
 * record read before it, site the declared section. The subtraction is
 * modular, so a timer wrap between the two reads changes nothing.
 *
 * A stall needs the same record and no declared section at two samples.
 * One sample alone can pair a record read during a flash erase with a
 * section read after the erase cleared it; the erase path advances the
 * record before it clears the section, so the pair cannot repeat.
 */
static inline unsigned int
core1_observe(struct core1_obs *o, unsigned int now_us, unsigned int stamp_us,
    unsigned int site)
{
	unsigned int gap = now_us - stamp_us;
	int moved = stamp_us != o->co_stamp;
	int quiet = site == C1_SITE_NONE && o->co_site == C1_SITE_NONE;

	o->co_stamp = stamp_us;
	o->co_site = site;
	if (moved) {
		o->co_probed = 0;
		return 0;
	}
	if (gap >= C1_HANG_US) {
		if (o->co_probed == 2)
			return C1_ACT_RESET |
			    (site == C1_SITE_NONE ? C1_ACT_RECORD : 0);
		o->co_probed = 2;
		return C1_ACT_PROBE;
	}
	if (!quiet || gap < C1_STALL_US)
		return 0;
	if (gap > o->co_gap_us)
		o->co_gap_us = gap;
	if (o->co_probed != 0)
		return 0;
	o->co_probed = 1;
	o->co_stalls++;
	return C1_ACT_PROBE;
}

/* Ring operations. A reply repeats the request's slot and argument. */
#define	C1_OP_PING	1U
#define	C1_OP_PONG	2U
#define	C1_OP_BAD	3U		/* reply to an unknown operation */

/* cs_state. */
#define	C1_OFF		0U		/* parked by request */
#define	C1_RUN		1U		/* launched and answering */
#define	C1_WAIT		2U		/* parked until cs_wait periods pass */
#define	C1_DEAD		3U		/* parked after C1_RESTART_MAX attempts */

#define	C1_MISS_MAX	2U		/* unanswered periods before a restart */
#define	C1_RESTART_MAX	3U		/* restarts before core 1 stays parked */

/* Actions of the supervisor functions. */
#define	C1_SUP_PING	1U		/* send ping number cs_seq */
#define	C1_SUP_PARK	2U		/* reset core 1 into the boot ROM */
#define	C1_SUP_LAUNCH	3U		/* launch, then core1_sup_launched() */

/* Every field is stored by core 0 alone. */
struct core1_sup {
	unsigned int	cs_state;
	unsigned int	cs_epoch;	/* launches so far; stamps each ping */
	unsigned int	cs_seq;		/* number of the last ping sent */
	unsigned int	cs_pong;	/* number of the last ping answered */
	unsigned int	cs_missed;	/* consecutive unanswered periods */
	unsigned int	cs_wait;	/* periods left before the launch */
	unsigned int	cs_restarts;	/* restarts since core1_sup_start() */
};

/* Park core 1 and schedule the next launch, or give up. */
static inline unsigned int
core1_sup_fail(struct core1_sup *s)
{
	s->cs_missed = 0;
	if (s->cs_restarts >= C1_RESTART_MAX) {
		s->cs_state = C1_DEAD;
		return C1_SUP_PARK;
	}
	s->cs_wait = 1U << s->cs_restarts++;
	s->cs_state = C1_WAIT;
	return C1_SUP_PARK;
}

/* Request a first launch at the next period. */
static inline void
core1_sup_start(struct core1_sup *s)
{
	s->cs_state = C1_WAIT;
	s->cs_wait = 1;
	s->cs_restarts = 0;
	s->cs_missed = 0;
}

/* The outcome of the launch core1_sup_tick() asked for. */
static inline unsigned int
core1_sup_launched(struct core1_sup *s, int ok)
{
	if (!ok)
		return core1_sup_fail(s);
	s->cs_state = C1_RUN;
	s->cs_epoch++;
	s->cs_seq = 0;
	s->cs_pong = 0;
	s->cs_missed = 0;
	return 0;
}

/*
 * A reply taken from the ring. Only the answer to the newest ping of the
 * current launch counts; a reply from before a restart carries an older
 * epoch.
 */
static inline void
core1_sup_reply(struct core1_sup *s, unsigned int op, unsigned int slot,
    unsigned int arg)
{
	if (op == C1_OP_PONG && arg == s->cs_epoch &&
	    slot == (s->cs_seq & 0xffffU))
		s->cs_pong = s->cs_seq;
}

/* Once a period, after the reply ring has been emptied. */
static inline unsigned int
core1_sup_tick(struct core1_sup *s)
{
	if (s->cs_state == C1_WAIT) {
		if (--s->cs_wait != 0)
			return 0;
		return C1_SUP_LAUNCH;
	}
	if (s->cs_state != C1_RUN)
		return 0;
	if (s->cs_pong == s->cs_seq)
		s->cs_missed = 0;
	else if (++s->cs_missed >= C1_MISS_MAX)
		return core1_sup_fail(s);
	s->cs_seq++;
	return C1_SUP_PING;
}

#ifdef KERNEL
void	core1_init(void);
void	core1_park(void);
int	core1_sysctl(int which, void *oldp, size_t *oldlenp, void *newp,
	    size_t newlen);

/*
 * The interrupt core 1 raises on core 0 reaches it as the NMI, whose vector
 * and handler are in flash. core1_nmi_mask is the SYSCFG.PROC0_NMI_MASK
 * value that routes it while core 1 runs and zero otherwise;
 * watchdog_site() in machdep.c withdraws the route for as long as a declared
 * section has execute-in-place down.
 */
extern unsigned int core1_nmi_mask;
#endif

#endif /* !_RP2040_RP2040_CORE1_H_ */
