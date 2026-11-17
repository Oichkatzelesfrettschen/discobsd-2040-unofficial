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
 * C17 7.12.8.4: the gamma function.  The argument is split as x = n + z
 * with n = round(x) and z in [-1/2, 1/2].  On that interval
 *
 *	1/gamma(1 + z) = sum over k of c[k] * z**k
 *
 * converges quickly (the reciprocal gamma function is entire; the
 * coefficients are those of Abramowitz and Stegun 6.1.34 shifted by one
 * power).  The recurrence gamma(x + 1) = x * gamma(x) then carries the
 * value to x as a product of n - 1 factors upward, or a quotient of 1 - n
 * factors downward.  No logarithm or power is involved, so the error is
 * that of the series plus half a unit per factor.  The product overflows
 * to an infinity above 171.6 and the quotient underflows to zero below
 * about -180, with the sign the factors give.  Zero is a pole and a
 * negative integer is a domain error.
 */
static const double reciprocal[] = {
	1.0,
	0.5772156649015329,
	-0.6558780715202538,
	-0.0420026350340952,
	0.1665386113822915,
	-0.0421977345555443,
	-0.0096219715278770,
	0.0072189432466630,
	-0.0011651675918591,
	-0.0002152416741149,
	0.0001280502823882,
	-0.0000201348547807,
	-0.0000012504934821,
	0.0000011330272320,
	-0.0000002056338417,
	0.0000000061160950,
	0.0000000050020075,
	-0.0000000011812746,
	0.0000000001043427,
	0.0000000000077823,
	-0.0000000000036968,
	0.0000000000005100,
	-0.0000000000000206,
	-0.0000000000000054,
	0.0000000000000014,
	0.0000000000000001
};
#define TERMS	((int)(sizeof(reciprocal) / sizeof(reciprocal[0])))

double
tgamma(double x)
{
	double n, z, sum, result, factor;
	int i;

	if (x != x)
		return x;
	if (x == 0.0) {
		errno = ERANGE;
		return copysign(HUGE_VAL, x);
	}
	if (x - x != 0.0) {
		if (x > 0.0)
			return x;
		errno = EDOM;
		return (x - x) / (x - x);	/* NaN */
	}
	n = round(x);
	if (n == x && x < 0.0) {
		errno = EDOM;
		return (x - x) / (x - x);	/* NaN */
	}
	if (x > 172.0) {
		errno = ERANGE;
		return HUGE_VAL;
	}
	if (x < -190.0)		/* below the smallest subnormal */
		return fmod(n, 2.0) != 0.0 ? -0.0 : 0.0;
	z = x - n;
	sum = reciprocal[TERMS - 1];
	for (i = TERMS - 2; i >= 0; i--)
		sum = sum * z + reciprocal[i];
	result = 1.0 / sum;			/* gamma(1 + z) */
	if (n >= 1.0) {
		for (factor = 1.0; factor < n; factor += 1.0)
			result *= factor + z;
	} else {
		for (factor = x; factor < 1.0 + z - 0.5; factor += 1.0)
			result /= factor;
	}
	if (result - result != 0.0)
		errno = ERANGE;		/* 1/x overflows for tiny x (7.12.1p4) */
	return result;
}
