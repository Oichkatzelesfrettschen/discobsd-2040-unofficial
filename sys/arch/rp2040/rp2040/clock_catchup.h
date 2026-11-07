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

#ifndef _RP2040_RP2040_CLOCK_CATCHUP_H_
#define _RP2040_RP2040_CLOCK_CATCHUP_H_

/*
 * Clock ticks that SysTick cannot deliver while PRIMASK is set.
 *
 * SysTick keeps counting with interrupts masked, but its exception has a
 * single pending bit (ICSR.PENDSTSET, ARMv6-M ARM B3.3), so a masked window
 * of N tick periods delivers one tick when it ends and drops the other N-1.
 * The flash erase and program paths mask for tens of milliseconds, which
 * leaves the time of day and the callout queue behind the free-running
 * microsecond timer (TIMERAWL, RP2040 datasheet 4.6).
 *
 * The tick handler records TIMERAWL at each delivered tick. A masked window
 * closes by asking how many whole tick periods have elapsed since that
 * record, subtracting the tick the hardware still holds pending, and
 * advancing the record by the ticks it hands back. The record moves by whole
 * periods, so a second call from an enclosing window counts only the time
 * the first call did not, and the arithmetic on 32-bit microsecond values
 * is modular, so a TIMERAWL wrap inside the window changes nothing.
 * Ticks the handler delivers late stay uncounted rather than counted twice:
 * the result is never above the ticks an unmasked SysTick would have raised
 * and at most one below.
 */

/* sys/callout.h carries no include guard; this is its only inclusion here. */
#include <sys/callout.h>

struct clock_catchup {
	unsigned int	stamp_us;	/* TIMERAWL at the last accounted tick. */
	int		armed;		/* SysTick is running. */
};

/* The handler delivered a tick at now_us. */
static inline void
clock_catchup_tick(struct clock_catchup *c, unsigned int now_us)
{
	c->stamp_us = now_us;
}

/*
 * Whole ticks lost to masking as of now_us. hw_pending is the SysTick
 * pending bit, which stands for one tick that will still be delivered.
 * Call with interrupts masked so the handler cannot run and move the record;
 * clock_catchup_sample() supplies both values in the order that keeps a
 * boundary between the two reads from being counted twice.
 */
static inline unsigned int
clock_catchup_owed(struct clock_catchup *c, unsigned int now_us,
    int hw_pending, unsigned int tick_us)
{
	unsigned int due, held, owed;

	if (!c->armed)
		return 0;
	due = (unsigned int)(now_us - c->stamp_us) / tick_us;
	held = hw_pending ? 1U : 0U;
	owed = due > held ? due - held : 0U;
	c->stamp_us += owed * tick_us;
	return owed;
}

/*
 * Apply owed ticks to the time of day and the callout queue exactly as the
 * same number of hardclock calls would, without running any callout.
 *
 * Per tick, hardclock decrements the head entry and each following entry
 * while the ones before it have gone negative, stopping at the first entry
 * that stays positive or reaches zero. Over owed ticks that makes entry j
 * lose owed - S(j) ticks, where S(j) is the sum of the positive times ahead
 * of it, and nothing once S(j) reaches owed. A due entry stays queued and the
 * next hardclock marks it for softclock, so callouts run on the tick after
 * the masked window and never inside the flash path.
 */
static inline void
clock_catchup_apply(unsigned int owed, int hz, int *lbolt, long *seconds,
    struct callout *head)
{
	struct callout *p;
	unsigned int ahead = 0;

	*lbolt += (int)owed;
	if (*lbolt >= hz) {
		*seconds += *lbolt / hz;
		*lbolt %= hz;
	}
	for (p = head->c_next; p != 0 && ahead < owed; p = p->c_next) {
		int t = p->c_time;

		p->c_time = t - (int)(owed - ahead);
		if (t > 0)
			ahead += (unsigned int)t;
	}
}

#ifdef KERNEL
/* clock.c: record the tick the SysTick handler is delivering. */
void clock_tick_stamp(void);

/* clock.c: account the ticks lost to the masked window now closing. */
void clock_catchup(void);
#endif

/*
 * Read the timer, then the pending bit, and return the owed ticks. PRIMASK
 * keeps the handler from running but not SysTick from becoming pending, so a
 * boundary can fall between the two reads. Read in this order, that tick is
 * set pending after the timer sample excluded it, and the result is at most
 * one low. Read the other way, a clear pending bit followed by a timer
 * sample past the boundary owes the tick that the hardware also delivers at
 * unmask. The two reads are separate statements because C leaves the order
 * of argument evaluation unspecified.
 */
static inline unsigned int
clock_catchup_sample(struct clock_catchup *c, unsigned int (*read_timer)(void),
    int (*read_pending)(void), unsigned int tick_us)
{
	unsigned int now;
	int held;

	now = read_timer();
	held = read_pending();
	return clock_catchup_owed(c, now, held, tick_us);
}

#endif /* !_RP2040_RP2040_CLOCK_CATCHUP_H_ */
