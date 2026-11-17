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

#ifndef _LIBM_FPBITS_H_
#define _LIBM_FPBITS_H_

/*
 * Access to the IEEE 754 binary64 and binary32 encodings.  A union read is
 * the conversion C17 6.5.2.3 footnote 97 defines; it compiles to register
 * moves.  A binary64 value is sign (bit 63), biased exponent (bits 62..52,
 * bias 1023) and 52 fraction bits; binary32 is sign, 8 exponent bits with
 * bias 127 and 23 fraction bits.
 */
#include <errno.h>
#include <stdint.h>

#define FP_SIGN_BIT	0x8000000000000000ULL
#define FP_FRACTION	0x000fffffffffffffULL
#define FP_IMPLICIT	0x0010000000000000ULL
#define FP_EXPONENT(u)	((int)((u) >> 52 & 0x7ff))

static inline uint64_t
fp_bits(double value)
{
	union { double value; uint64_t bits; } view;

	view.value = value;
	return view.bits;
}

static inline double
fp_double(uint64_t bits)
{
	union { double value; uint64_t bits; } view;

	view.bits = bits;
	return view.value;
}

static inline uint32_t
fp_bitsf(float value)
{
	union { float value; uint32_t bits; } view;

	view.value = value;
	return view.bits;
}

static inline float
fp_float(uint32_t bits)
{
	union { float value; uint32_t bits; } view;

	view.bits = bits;
	return view.value;
}

/*
 * The float form of a double result.  A finite double too large for float
 * narrows to an infinity without any other sign, which C17 7.12.1 makes a
 * range error: ERANGE.  An infinity or NaN the double member returned
 * carries the error that member already reported.
 */
static inline float
fp_narrow(double value)
{
	float result = (float)value;

	if ((fp_bitsf(result) & 0x7fffffffU) == 0x7f800000U &&
	    FP_EXPONENT(fp_bits(value)) != 0x7ff)
		errno = ERANGE;
	return result;
}

#endif /* !_LIBM_FPBITS_H_ */
