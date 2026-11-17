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

#include "fpbits.h"

/*
 * C17 7.12.11.3: the next representable value after x in the direction of
 * y.  Encodings of one sign are ordered as integers, so the neighbor is
 * the encoding plus or minus one: away from zero when y lies beyond x,
 * toward zero otherwise.  The step from the largest finite value reaches
 * the infinity encoding and the step from zero reaches the smallest
 * subnormal with y's sign.
 */
double
nextafter(double x, double y)
{
	uint64_t bits = fp_bits(x);

	if (x != x || y != y)
		return x + y;
	if (x == y)
		return y;
	if (x == 0.0)
		return fp_double((fp_bits(y) & FP_SIGN_BIT) | 1);
	if ((x < y) == (x > 0.0)) {
		bits++;
		/* The step from the largest finite value overflows (C17 7.12.1). */
		if (FP_EXPONENT(bits) == 0x7ff)
			errno = ERANGE;
	} else
		bits--;
	return fp_double(bits);
}
