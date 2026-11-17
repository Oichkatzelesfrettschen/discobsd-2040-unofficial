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
#include <stdio.h>
#include <time.h>

static clock_t (*volatile processor_time)(void) = clock;
static int (*volatile calendar)(struct timespec *, int) = timespec_get;

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
