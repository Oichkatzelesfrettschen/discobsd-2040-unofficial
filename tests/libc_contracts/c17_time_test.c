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
 * clock() and timespec_get() per C17 7.27.2.1 and 7.27.2.5 over the
 * hosting system's getrusage() and gettimeofday().
 */
#include <limits.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

/*
 * The driver renames the tree's clock, timespec_get and the getrusage that
 * clock() calls, in the object, to tree_NAME.  The processor time clock()
 * reads therefore comes from the tree_getrusage defined here, which lets a
 * case supply the reading and otherwise passes the call to the hosting
 * library's getrusage().
 */
clock_t tree_clock(void);
int tree_timespec_get(struct timespec *, int);
int tree_getrusage(int, struct rusage *);

static int injected;			/* 0 real, 1 a reading, 2 a failure */
static struct rusage reading;

int
tree_getrusage(int who, struct rusage *usage)
{
	if (injected == 1) {
		*usage = reading;
		return 0;
	}
	if (injected == 2)
		return -1;
	return getrusage(who, usage);
}

static clock_t (*volatile processor_time)(void) = tree_clock;
static int (*volatile calendar)(struct timespec *, int) = tree_timespec_get;

/* Ticks as seconds and microseconds of one rusage timeval. */
static void
split(struct timeval *value, unsigned long ticks)
{
	value->tv_sec = (time_t)(ticks / 1000000UL);
	value->tv_usec = (long)(ticks % 1000000UL);
}

/* clock() for a user and a system time given in ticks. */
static clock_t
clock_of(unsigned long user, unsigned long system)
{
	clock_t result;

	split(&reading.ru_utime, user);
	split(&reading.ru_stime, system);
	injected = 1;
	result = processor_time();
	injected = 0;
	return result;
}

/*
 * clock() counts in an unsigned long.  The total of user and system time
 * must come out exactly, and any total the type cannot hold, or that is
 * (clock_t)-1 itself, must be reported as (clock_t)-1: a wrapped count
 * would read as a small plausible time.
 */
static int
boundary(unsigned *checks)
{
	unsigned long top = ULONG_MAX, half = ULONG_MAX / 2 + 1;
	struct { unsigned long user, system; int valid; } cases[] = {
		{ 1500000UL, 2250000UL, 1 },
		{ 0, 0, 1 },
		{ top - 2, 1, 1 },
		{ top - 1, 0, 1 },
		{ top, 0, 0 },
		{ top - 1, 1, 0 },
		{ half, half, 0 },
	};
	unsigned i;
	struct rusage wide;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		clock_t got = clock_of(cases[i].user, cases[i].system);

		(*checks)++;
		if (cases[i].valid ? (unsigned long)got != cases[i].user + cases[i].system :
		    got != (clock_t)-1) {
			printf("FAIL clock: %lu + %lu ticks gives %lu\n",
			    cases[i].user, cases[i].system, (unsigned long)got);
			return 1;
		}
	}
	/* Seconds whose product with CLOCKS_PER_SEC overflows by itself. */
	wide = reading;
	reading.ru_utime.tv_sec = (time_t)(top / 1000000UL + 1);
	reading.ru_utime.tv_usec = 0;
	reading.ru_stime = wide.ru_stime;
	reading.ru_stime.tv_sec = 0;
	reading.ru_stime.tv_usec = 0;
	injected = 1;
	(*checks)++;
	if (processor_time() != (clock_t)-1) {
		injected = 0;
		printf("FAIL clock: a product that overflows gives a count\n");
		return 1;
	}
	injected = 2;
	(*checks)++;
	if (processor_time() != (clock_t)-1) {
		injected = 0;
		printf("FAIL clock: a failed getrusage gives a count\n");
		return 1;
	}
	injected = 0;
	return 0;
}

int
main(void)
{
	struct timespec stamp = { 0, -7 };
	clock_t first, last;
	volatile unsigned long spin = 0;
	time_t now;
	unsigned checks = 0;

	first = processor_time();
	checks++;
	if (first == (clock_t)-1) {
		printf("FAIL clock: unavailable\n");
		return 1;
	}
	/* Burn processor time until the reading moves, within a bound. */
	do {
		spin++;
		last = processor_time();
	} while (last == first && spin < 3000000UL);
	checks++;
	if (last == (clock_t)-1 || last <= first) {
		printf("FAIL clock: processor time does not advance\n");
		return 1;
	}
	checks++;
	if ((unsigned long)(last - first) > 30UL * CLOCKS_PER_SEC) {
		printf("FAIL clock: implausible step\n");
		return 1;
	}

	if (boundary(&checks) != 0)
		return 1;
	checks++;
	if (calendar(&stamp, 0) != 0 || calendar(&stamp, TIME_UTC + 1) != 0 ||
	    stamp.tv_sec != 0 || stamp.tv_nsec != -7) {
		printf("FAIL timespec_get: accepts an unknown base\n");
		return 2;
	}
	checks++;
	if (calendar(&stamp, TIME_UTC) != TIME_UTC) {
		printf("FAIL timespec_get: refuses TIME_UTC\n");
		return 2;
	}
	now = time(NULL);
	checks++;
	if (stamp.tv_nsec < 0 || stamp.tv_nsec > 999999999L ||
	    stamp.tv_nsec % 1000 != 0) {
		printf("FAIL timespec_get: tv_nsec %ld\n", stamp.tv_nsec);
		return 2;
	}
	checks++;
	if (now < stamp.tv_sec || now - stamp.tv_sec > 2) {
		printf("FAIL timespec_get: tv_sec disagrees with time()\n");
		return 2;
	}
	printf("c17 time: %u checks passed\n", checks);
	return 0;
}
