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

#include <errno.h>
#include <math.h>

/*
 * C17 7.12.6.9: log(1 + x) without the cancellation near zero.  With
 * u = 1 + x as rounded, log(u) * x / (u - 1) corrects for the rounding of
 * u; u - 1 is exact for u in [0.5, 2] (W. Kahan).
 */
double
log1p(double x)
{
	double u;

	if (x != x)
		return x;
	if (x < -1.0) {
		errno = EDOM;
		return (x - x) / (x - x);	/* NaN */
	}
	if (x == -1.0) {
		errno = ERANGE;
		return -HUGE_VAL;
	}
	if (x - x != 0.0)
		return x;			/* +infinity */
	u = 1.0 + x;
	if (u == 1.0)
		return x;
	/*
	 * Above 2**1000 the product log(u) * x overflows although the result
	 * is finite, and u - 1 equals x to every bit, so the correction is 1.
	 */
	if (x > 0x1p1000)
		return log(u);
	return log(u) * x / (u - 1.0);
}
