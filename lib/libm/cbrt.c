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

#include <math.h>

/*
 * C17 7.12.7.1: the real cube root.  The argument is reduced to
 * m * 2**(3q) with m in [0.5, 4), so the exponent contributes an exact
 * power of two.  Newton's iteration t <- t - (t*t*t - m)/(3*t*t) converges
 * quadratically; from the linear starting value the relative error is
 * below 0.12, and six steps carry it under the rounding error.
 */
double
cbrt(double x)
{
	double m, t;
	int e, q, i;

	if (x == 0.0 || x != x || x - x != 0.0)
		return x;
	m = frexp(fabs(x), &e);		/* [0.5, 1) */
	q = e / 3;
	e -= 3 * q;
	if (e < 0) {
		e += 3;
		q--;
	}
	m = scalbn(m, e);		/* [0.5, 4) */
	t = 0.63 + 0.26 * m;
	for (i = 0; i < 6; i++)
		t -= (t * t * t - m) / (3.0 * t * t);
	return copysign(scalbn(t, q), x);
}
