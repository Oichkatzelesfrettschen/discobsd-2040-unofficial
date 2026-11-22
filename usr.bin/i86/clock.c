/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include "clock.h"

/*
 * 8254 input clocks from the reading *last to now, in microseconds, with
 * the fraction of a clock carried in *rem. A reading before *last (the
 * host's clock set back) counts none, and a gap longer than CLK_GAP_US
 * (the process stopped, or the clock set forward) counts CLK_GAP_US, so
 * neither moves the guest's timers by more than a second. The gap is
 * converted 3598 us at a time, the most whose product with CLK_PIT_HZ
 * plus a remainder below 1000000 fits 32 bits.
 */
uint32_t
clk_pit(unsigned long *last, unsigned long now, uint32_t *rem)
{
	unsigned long d = now - *last;
	uint32_t clocks = 0, k, n;

	*last = now;
	if ((long)d < 0)
		d = 0;
	else if (d > CLK_GAP_US)
		d = CLK_GAP_US;
	while (d) {
		k = d > 3598 ? 3598 : (uint32_t)d;
		d -= k;
		n = k * CLK_PIT_HZ + *rem;
		clocks += n / 1000000u;
		*rem = n % 1000000u;
	}
	return (clocks);
}

/* The forward part of the step from the reading *last to now: a reading
 * before *last counts none, so a sum of these never goes back. */
unsigned long
clk_forward(unsigned long *last, unsigned long now)
{
	unsigned long d = now - *last;

	*last = now;
	return ((long)d > 0 ? d : 0);
}

/*
 * -S: milliseconds to sleep so that the host's clock, in microseconds,
 * reaches *due, the time the guest's instructions have earned. A guest
 * behind keeps CLK_CREDIT_US of credit at most; a *due more than
 * CLK_BACK_US ahead means the host's clock went back, and the pace starts
 * again from now; under a millisecond ahead sleeps none.
 */
unsigned
clk_pace(unsigned long *due, unsigned long now)
{
	if ((long)(now - *due) > CLK_CREDIT_US)
		*due = now - (unsigned long)CLK_CREDIT_US;
	else if ((long)(*due - now) > CLK_BACK_US)
		*due = now;
	else if ((long)(*due - now) >= 1000L)
		return ((unsigned)((*due - now) / 1000UL));
	return (0);
}

/*
 * The timer interrupts waiting once one more period of a count of period
 * clocks has passed, from due waiting: one more, up to as many periods as
 * fit in CLK_CREDIT_US and never fewer than CLK_TICKS_MIN. A host that
 * sleeps or deschedules the emulator for up to the time -S lets a guest
 * make up therefore loses none of a fast timer's interrupts (20 at 1 kHz),
 * while a longer stall still drops the excess instead of delivering a
 * burst; at 18.2 Hz the bound stays four.
 */
unsigned
clk_owe(unsigned due, uint32_t period)
{
	unsigned cap = period ? CLK_CREDIT_CLOCKS / period : 0;

	if (cap < CLK_TICKS_MIN)
		cap = CLK_TICKS_MIN;
	return (due < cap ? due + 1 : due);
}
