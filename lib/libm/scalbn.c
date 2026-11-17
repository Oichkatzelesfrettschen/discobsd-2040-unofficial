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

#include "fpbits.h"

/*
 * C17 7.12.6.13: x times 2 to the n, computed on the exponent field.  A
 * result below the normal range is shifted into the subnormal encoding and
 * rounded to nearest with ties to even; because the subnormal encoding
 * continues the normal one, a round-up out of the fraction lands on the
 * smallest normal number without a special case.  Overflow returns an
 * infinity of the argument's sign.
 */
double
scalbn(double x, int n)
{
	uint64_t bits = fp_bits(x);
	uint64_t sign = bits & FP_SIGN_BIT;
	uint64_t mantissa = bits & FP_FRACTION;
	uint64_t rest, half;
	long e = FP_EXPONENT(bits);
	int shift;

	if (e == 0x7ff || (bits & ~FP_SIGN_BIT) == 0)
		return x;
	if (e == 0) {
		for (e = 1; (mantissa & FP_IMPLICIT) == 0; mantissa <<= 1)
			e--;
	} else
		mantissa |= FP_IMPLICIT;
	/* Beyond these bounds every finite input overflows or vanishes. */
	if (n > 5000)
		n = 5000;
	else if (n < -5000)
		n = -5000;
	e += n;
	if (e >= 0x7ff) {
		errno = ERANGE;
		return fp_double(sign | 0x7ff0000000000000ULL);
	}
	if (e >= 1)
		return fp_double(sign | (uint64_t)e << 52 |
		    (mantissa & FP_FRACTION));
	if (e < -53)
		return fp_double(sign);
	shift = (int)(1 - e);		/* 1 .. 54 */
	half = 1ULL << (shift - 1);
	rest = mantissa & ((half << 1) - 1);
	mantissa >>= shift;
	if (rest > half || (rest == half && (mantissa & 1) != 0))
		mantissa++;
	return fp_double(sign | mantissa);
}
