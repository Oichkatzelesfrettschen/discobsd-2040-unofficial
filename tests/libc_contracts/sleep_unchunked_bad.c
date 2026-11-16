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
 * The negative control for sleep_contract_test.c: sleep() as a single
 * select() of the whole request, with the elapsed time read from the
 * clock. A request over the kernel's select bound fails with EINVAL and
 * returns unslept, one over the saturated tick count wakes early, and a
 * failed or interrupted select() is not told from a completed one, which
 * the kernel cases of the contract reject.
 */
#include <stddef.h>
#include <sys/time.h>
#include <sys/select.h>

unsigned int	db_sleep(unsigned int);
int		test_gettimeofday(struct timeval *, struct timezone *);
int		test_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

unsigned int
db_sleep(unsigned int seconds)
{
	struct timeval f, s;
	unsigned long elapsed_seconds;

	if (seconds) {
		test_gettimeofday(&f, NULL);
		s.tv_sec = seconds;
		s.tv_usec = 0;
		test_select(0, NULL, NULL, NULL, &s);
		test_gettimeofday(&s, NULL);
		elapsed_seconds = ((unsigned long)s.tv_sec -
		    (unsigned long)f.tv_sec) & 0xffffffffUL;
		if (elapsed_seconds >= seconds)
			return 0;
		seconds -= (unsigned int)elapsed_seconds;
	}
	return (seconds);
}
