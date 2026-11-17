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
 * C17 7.12.9.4: round to an integer in the current rounding direction,
 * which on this system is always to nearest with ties to even.  The
 * discarded fraction is compared with one half; a tie rounds up exactly
 * when the integer part is odd.  At e == 0 the integer part is the
 * implicit one, and the parity bit read below is the low exponent bit,
 * which is set for the bias 1023, so the same test covers it.
 */
double
rint(double x)
{
	uint64_t bits = fp_bits(x);
	uint64_t fraction, half, unit;
	int e = FP_EXPONENT(bits) - 1023;

	if (e >= 52)
		return x;
	if (e < -1)
		return fp_double(bits & FP_SIGN_BIT);
	if (e == -1) {	/* [0.5, 1): only exactly one half goes to zero */
		if ((bits & FP_FRACTION) == 0)
			return fp_double(bits & FP_SIGN_BIT);
		return fp_double((bits & FP_SIGN_BIT) | 0x3ff0000000000000ULL);
	}
	half = 0x0008000000000000ULL >> e;
	unit = half << 1;
	fraction = bits & (FP_FRACTION >> e);
	bits &= ~(FP_FRACTION >> e);
	if (fraction > half || (fraction == half && (bits & unit) != 0))
		bits += unit;
	return fp_double(bits);
}
