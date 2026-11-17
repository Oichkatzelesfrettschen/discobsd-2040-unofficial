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
 * C17 7.12.13.1 for float: x*y + z rounded once.  The product of two
 * 24-bit significands is exact in a double.  Adding z in double and then
 * narrowing would round twice, so the sum is rounded to odd first: when
 * the double addition is inexact and its result is even, the result moves
 * one place toward the discarded part.  A value rounded to odd at 53 bits
 * narrows to the same float as the exact sum, because the sticky low bit
 * keeps it off every 24-bit rounding boundary it was not already on.  The
 * error term comes from the 2Sum sequence, which is exact in the absence
 * of overflow; an overflowing or non-finite sum needs no correction.
 */
float
fmaf(float x, float y, float z)
{
	double product = (double)x * (double)y;
	double sum = product + (double)z;
	double addend, error;
	uint64_t bits;

	if (sum - sum != 0.0 || sum == 0.0)
		return (float)sum;
	addend = sum - product;
	error = (product - (sum - addend)) + ((double)z - addend);
	if (error != 0.0) {
		bits = fp_bits(sum);
		if ((bits & 1) == 0) {
			if ((error > 0.0) == (sum > 0.0))
				bits++;
			else
				bits--;
			sum = fp_double(bits);
		}
	}
	return (float)sum;
}
