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
#include <limits.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

/*
 * Add seconds and microseconds to *total as clock ticks.  The result is 0
 * when the sum would leave unsigned long, in either the product with
 * CLOCKS_PER_SEC or the addition.
 */
static int
add_ticks(unsigned long *total, unsigned long seconds,
    unsigned long microseconds)
{
	unsigned long ticks;

	if (seconds > (ULONG_MAX - microseconds) / CLOCKS_PER_SEC)
		return 0;
	ticks = seconds * CLOCKS_PER_SEC + microseconds;
	if (ticks > ULONG_MAX - *total)
		return 0;
	*total += ticks;
	return 1;
}

/*
 * C17 7.27.2.1: processor time used by the program, in units of
 * 1/CLOCKS_PER_SEC, or (clock_t)-1 when it is unavailable or cannot be
 * represented.  The value is user plus system time from getrusage().
 * CLOCKS_PER_SEC is the 1000000 POSIX requires under XSI, so clock_t holds
 * about 71.6 minutes of processor time on a 32-bit unsigned long; beyond
 * that the function reports the failure value rather than a wrapped count.
 * A total of exactly ULONG_MAX ticks is that failure value.
 */
clock_t
clock(void)
{
	struct rusage usage;
	unsigned long ticks = 0;

	if (getrusage(RUSAGE_SELF, &usage) < 0 ||
	    !add_ticks(&ticks, (unsigned long)usage.ru_utime.tv_sec,
	    (unsigned long)usage.ru_utime.tv_usec) ||
	    !add_ticks(&ticks, (unsigned long)usage.ru_stime.tv_sec,
	    (unsigned long)usage.ru_stime.tv_usec))
		return (clock_t)-1;
	return (clock_t)ticks;
}
