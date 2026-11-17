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

/* Whether y, a finite float, is an integer: all of 2**23 and up are. */
static int
integral(float y)
{
	return (fp_bitsf(y) & 0x7fffffffU) >= 0x4b000000U || (float)(long)y == y;
}

/* Whether y is an odd integer: from 2**24 up every float is even. */
static int
odd(float y)
{
	return (fp_bitsf(y) & 0x7fffffffU) < 0x4b800000U && integral(y) &&
	    ((long)y & 1) != 0;
}

/*
 * powf(): C17 7.12.7.4 for float, with the special cases of F.10.4.4 stated
 * here so that they do not depend on the double pow() they narrow.  A
 * negative base reaches pow() as its magnitude, and the sign follows an odd
 * integer power.  A negative base with a power that is not an integer is a
 * domain error; a zero base with a negative power is a pole.
 */
float
powf(float x, float y)
{
	float magnitude, result;

	if (y == 0.0f || x == 1.0f)
		return 1.0f;
	if (x != x || y != y)
		return x + y;
	magnitude = fp_float(fp_bitsf(x) & 0x7fffffffU);
	if (y - y != 0.0f) {
		/* An infinite power: |x| against 1 decides, and |x| = 1 gives 1. */
		if (magnitude == 1.0f)
			return 1.0f;
		return (magnitude < 1.0f) == (y < 0.0f) ? HUGE_VALF : 0.0f;
	}
	if (x - x != 0.0f || x == 0.0f) {
		/* Zero to a negative power and infinity to a positive one are infinite. */
		result = (x == 0.0f) == (y < 0.0f) ? HUGE_VALF : 0.0f;
		if (x == 0.0f && y < 0.0f)
			errno = ERANGE;
		if (odd(y) && (fp_bitsf(x) >> 31) != 0)
			result = -result;
		return result;
	}
	if (x < 0.0f && !integral(y)) {
		errno = EDOM;
		return NAN;
	}
	result = fp_narrow(pow((double)magnitude, (double)y));
	return x < 0.0f && odd(y) ? -result : result;
}
