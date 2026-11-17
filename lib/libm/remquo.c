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

#include <float.h>
#include <errno.h>
#include <math.h>

/*
 * C17 7.12.10.3: the IEEE remainder r = x - n*y, n the integer nearest
 * x/y with ties to even, and the low three bits of n with the sign of
 * x/y.  fmod() is exact, so reducing |x| modulo 8|y| keeps those three
 * bits; three exact conditional subtractions then bring the remainder
 * below |y| while counting the quotient.  The final step compares r with
 * |y| - r rather than 2r with |y|, which cannot overflow, and is exact
 * whenever the two are close enough to matter (Sterbenz: a - b is exact
 * for b/2 <= a <= 2b).
 */
double
remquo(double x, double y, int *quo)
{
	double ax = fabs(x), ay = fabs(y), r;
	int negative = signbit(x) != 0;
	int opposite = negative != (signbit(y) != 0);
	int q = 0;

	*quo = 0;
	if (x != x || y != y)
		return x + y;
	if (ax - ax != 0.0 || ay == 0.0) {
		errno = EDOM;
		return (x * y) / (x * y);	/* NaN */
	}
	if (ay - ay != 0.0)			/* infinite y */
		return x;
	r = ay <= DBL_MAX / 8 ? fmod(ax, 8 * ay) : ax;
	if (ay <= DBL_MAX / 4 && r >= 4 * ay) {
		r -= 4 * ay;
		q += 4;
	}
	if (ay <= DBL_MAX / 2 && r >= 2 * ay) {
		r -= 2 * ay;
		q += 2;
	}
	if (r >= ay) {
		r -= ay;
		q += 1;
	}
	if (r > ay - r || (r == ay - r && (q & 1) != 0)) {
		r -= ay;
		q += 1;
	}
	q &= 7;
	*quo = opposite ? -q : q;
	if (r == 0.0)
		return negative ? -0.0 : 0.0;
	return negative ? -r : r;
}
