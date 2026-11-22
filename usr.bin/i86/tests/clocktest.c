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

/*
 * clocktest: the host-clock steps of clock.c driven with chosen readings,
 * the cases a running emulator meets only when the host's clock is set:
 * a reading 30 s back, one 5 s ahead, readings across the wrap of an
 * unsigned long, and a second in 1 ms steps, whose 8254 clocks must come
 * to CLK_PIT_HZ exactly. Prints "clock: N checks pass" and exits 0, or
 * names each failed check and exits 1 (tests/check.py).
 */
#include <limits.h>
#include <stdio.h>

#include "../clock.h"

static int checks, failed;

static void
want(const char *what, unsigned long got, unsigned long expect)
{
	checks++;
	if (got != expect) {
		failed++;
		printf("FAIL %s: %lu, expected %lu\n", what, got, expect);
	}
}

static void
pit(void)
{
	unsigned long last = 5000000UL, sum = 0;
	uint32_t rem = 0;
	int i;

	for (i = 0; i < 1000; i++)
		sum += clk_pit(&last, last + 1000UL, &rem);
	want("pit: a second in 1 ms steps", sum, CLK_PIT_HZ);
	want("pit: remainder after a second", rem, 0);
	want("pit: one second in one step", clk_pit(&last, last + 1000000UL, &rem),
	    CLK_PIT_HZ);
	want("pit: 30 s back", clk_pit(&last, last - 30000000UL, &rem), 0);
	want("pit: last follows a step back", last, 5000000UL + 1000000UL +
	    1000000UL - 30000000UL);
	want("pit: 1 ms after a step back", clk_pit(&last, last + 1000UL, &rem),
	    1193);
	rem = 0;
	want("pit: 5 s ahead counts one", clk_pit(&last, last + 5000000UL, &rem),
	    CLK_PIT_HZ);
	last = ULONG_MAX - 499UL;
	rem = 0;
	want("pit: 1 ms across the wrap", clk_pit(&last, 500UL, &rem), 1193);
	want("pit: same reading", clk_pit(&last, last, &rem), 0);
}

static void
forward(void)
{
	unsigned long last = 1000UL, mono = 0;

	mono += clk_forward(&last, 1055UL);
	want("forward: 55 ms", mono, 55);
	mono += clk_forward(&last, 1055UL - 30000UL);
	want("forward: 30 s back adds none", mono, 55);
	mono += clk_forward(&last, 1055UL - 30000UL + 10UL);
	want("forward: 10 ms after a step back", mono, 65);
	last = ULONG_MAX - 4UL;
	mono += clk_forward(&last, 5UL);
	want("forward: 10 ms across the wrap", mono, 75);
}

static void
pace(void)
{
	unsigned long due;

	due = 1000000UL + 5000UL;
	want("pace: 5 ms ahead sleeps 5", clk_pace(&due, 1000000UL), 5);
	want("pace: 5 ms ahead keeps due", due, 1005000UL);
	due = 1000000UL + 500UL;
	want("pace: 0.5 ms ahead sleeps none", clk_pace(&due, 1000000UL), 0);
	due = 1000000UL + (unsigned long)CLK_BACK_US;
	want("pace: 100 ms ahead still sleeps", clk_pace(&due, 1000000UL), 100);
	due = 40000000UL;
	want("pace: clock 30 s back sleeps none", clk_pace(&due, 10000000UL), 0);
	want("pace: clock 30 s back restarts", due, 10000000UL);
	due = 1000000UL - 50000UL;
	want("pace: 50 ms behind sleeps none", clk_pace(&due, 1000000UL), 0);
	want("pace: 50 ms behind keeps 20 ms credit", due,
	    1000000UL - (unsigned long)CLK_CREDIT_US);
	due = 2000UL;
	want("pace: 3 ms ahead across the wrap",
	    clk_pace(&due, ULONG_MAX - 999UL), 3);
}

/* Waiting interrupts: up to CLK_CREDIT_US worth of periods, never fewer
 * than four. */
static unsigned
fill(uint32_t period, unsigned calls)
{
	unsigned due = 0;

	while (calls-- > 0)
		due = clk_owe(due, period);
	return (due);
}

static void
owe(void)
{
	unsigned long last = 0, now, cross;
	uint32_t rem = 999999u;

	/* 20 ms from a reading whose fraction is all but a clock counts one
	 * clock more than 20 ms holds. */
	now = 20000UL;
	want("owe: 20 ms counts at most CLK_CREDIT_CLOCKS",
	    clk_pit(&last, now, &rem), CLK_CREDIT_CLOCKS);
	want("owe: CLK_CREDIT_CLOCKS", CLK_CREDIT_CLOCKS, 23864);
	/* A gap that starts one clock before a 1 kHz deadline crosses 21. */
	cross = (CLK_CREDIT_CLOCKS - 1) / 1193 + 1;
	want("owe: deadlines a phased 20 ms gap crosses at 1 kHz", cross, 21);
	want("owe: 1 kHz keeps every one of them", fill(1193, 21), 21);
	want("owe: 1 kHz keeps no more", fill(1193, 30), 21);
	want("owe: 1 kHz below the bound counts each", fill(1193, 7), 7);
	want("owe: 18.2 Hz keeps four", fill(65536, 30), 4);
	want("owe: a 5 ms period keeps five", fill(5966, 30), 5);
	want("owe: the fastest count keeps 20 ms of them", fill(1, 30000), 23865);
	want("owe: a zero period keeps four", fill(0, 9), 4);
}

int
main(void)
{
	pit();
	forward();
	pace();
	owe();
	if (failed) {
		printf("clock: %d of %d checks fail\n", failed, checks);
		return (1);
	}
	printf("clock: %d checks pass\n", checks);
	return (0);
}
