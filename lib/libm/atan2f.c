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
 * atan2f(): C17 7.12.4.4 for float, with the special cases of F.10.1.4
 * stated here so that they do not depend on the double atan2() they narrow.
 * The result takes the sign of y.  A zero or infinite operand fixes it
 * without a quotient: y zero gives zero or pi by the sign of x, x zero gives
 * pi/2, an infinite y gives pi/2, or pi/4 and 3 pi/4 against an infinite x,
 * and an infinite x with finite y gives zero or pi.
 */
float
atan2f(float y, float x)
{
	static const float pi = 3.14159265358979323846f;
	static const float pi_2 = 1.57079632679489661923f;
	static const float pi_4 = 0.78539816339744830962f;
	static const float pi_3_4 = 2.35619449019234492885f;
	float magnitude;
	int negative = (fp_bitsf(y) >> 31) != 0;

	if (x != x || y != y)
		return x + y;
	if (y == 0.0f)
		magnitude = (fp_bitsf(x) >> 31) != 0 ? pi : 0.0f;
	else if (x == 0.0f)
		magnitude = pi_2;
	else if (y - y != 0.0f)
		magnitude = x - x != 0.0f ? (x < 0.0f ? pi_3_4 : pi_4) : pi_2;
	else if (x - x != 0.0f)
		magnitude = x < 0.0f ? pi : 0.0f;
	else
		return (float)atan2((double)y, (double)x);
	return fp_float(fp_bitsf(magnitude) | (negative ? 0x80000000U : 0));
}
