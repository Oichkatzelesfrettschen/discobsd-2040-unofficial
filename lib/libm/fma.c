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
 * C17 7.12.13.1: x*y + z rounded once, as if computed to infinite
 * precision.  The significands are integers, so the product is formed
 * exactly in 128 bits, z is aligned to it, and the sum is rounded to
 * nearest with ties to even in a single step.
 *
 * Alignment shifts the operand of smaller exponent right and ORs every bit
 * shifted out into bit 0 (a sticky bit).  That is sound because the
 * operand that is not shifted has zero low bits, so a sum or difference
 * with sticky input is odd, lies within one unit of the true value and on
 * the same side of every even rounding boundary; and because a shift large
 * enough to lose bits (more than 21 places) cannot coincide with a
 * cancellation of more than one bit, the sticky bit stays at least 50
 * places below the rounding position.
 *
 * ARMv6-M has no 32x32->64 multiply and libc links no 64-bit multiply
 * routine, so the product is assembled from 16-bit limbs with the 32-bit
 * multiply instruction.
 */
struct wide {
	uint64_t hi;
	uint64_t lo;
};

static uint64_t
multiply32(uint32_t a, uint32_t b)
{
	uint32_t al = a & 0xffff, ah = a >> 16;
	uint32_t bl = b & 0xffff, bh = b >> 16;
	uint64_t middle = (uint64_t)(al * bh) + (uint64_t)(ah * bl);

	return ((uint64_t)(ah * bh) << 32) + (middle << 16) + (al * bl);
}

static struct wide
multiply64(uint64_t a, uint64_t b)
{
	uint32_t al = (uint32_t)a, ah = (uint32_t)(a >> 32);
	uint32_t bl = (uint32_t)b, bh = (uint32_t)(b >> 32);
	uint64_t ll = multiply32(al, bl), lh = multiply32(al, bh);
	uint64_t hl = multiply32(ah, bl), hh = multiply32(ah, bh);
	uint64_t middle = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
	struct wide product;

	product.lo = (middle << 32) | (uint32_t)ll;
	product.hi = hh + (lh >> 32) + (hl >> 32) + (middle >> 32);
	return product;
}

/* Shift left by 0 < count < 64. */
static struct wide
shift_left(struct wide value, int count)
{
	value.hi = value.hi << count | value.lo >> (64 - count);
	value.lo <<= count;
	return value;
}

/* Shift right by count > 0, keeping a sticky bit for what is lost. */
static struct wide
shift_right_sticky(struct wide value, int count)
{
	struct wide result;
	uint64_t lost;

	if (count >= 128) {
		result.hi = 0;
		result.lo = (value.hi | value.lo) != 0;
	} else if (count >= 64) {
		lost = value.lo;
		if (count > 64)
			lost |= value.hi << (128 - count);
		result.hi = 0;
		result.lo = value.hi >> (count - 64) | (lost != 0);
	} else {
		lost = value.lo << (64 - count);
		result.hi = value.hi >> count;
		result.lo = value.hi << (64 - count) | value.lo >> count |
		    (lost != 0);
	}
	return result;
}

static int
leading_zeros(uint64_t value)
{
	int count = 0;

	while ((value >> 63) == 0) {
		value <<= 1;
		count++;
	}
	return count;
}

/* Split a finite nonzero value into a 53-bit significand and exponent. */
static uint64_t
split_value(uint64_t bits, int *exponent)
{
	uint64_t mantissa = bits & FP_FRACTION;
	int e = FP_EXPONENT(bits);

	if (e == 0) {
		for (e = 1; (mantissa & FP_IMPLICIT) == 0; mantissa <<= 1)
			e--;
	} else
		mantissa |= FP_IMPLICIT;
	*exponent = e - 1075;
	return mantissa;
}

double
fma(double x, double y, double z)
{
	uint64_t xbits = fp_bits(x), ybits = fp_bits(y), zbits = fp_bits(z);
	uint64_t sign, rest, mantissa, bits;
	struct wide a, b, r;
	int ex, ey, ez, ea, eb, e, shift, biased, negative_product;

	if (FP_EXPONENT(xbits) == 0x7ff || FP_EXPONENT(ybits) == 0x7ff) {
		double sum = x * y + z;

		/* infinity * 0 and infinity - infinity are domain errors. */
		if (sum != sum && x == x && y == y && z == z)
			errno = EDOM;
		return sum;
	}
	if (FP_EXPONENT(zbits) == 0x7ff)
		return z != z ? x * y + z : z;
	if (x == 0.0 || y == 0.0)
		return x * y + z;	/* the product is an exact zero */
	if (z == 0.0) {
		double product = x * y;	/* one rounding, and the sign of zero */

		if (product - product != 0.0)
			errno = ERANGE;
		return product;
	}

	/* a = |x*y| and b = |z|, each with its top bit at 125 or 126. */
	a = shift_left(multiply64(split_value(xbits, &ex),
	    split_value(ybits, &ey)), 21);
	ea = ex + ey - 21;
	b.hi = split_value(zbits, &ez) << 9;
	b.lo = 0;
	eb = ez - 73;
	negative_product = ((xbits ^ ybits) & FP_SIGN_BIT) != 0;

	if (ea > eb) {
		b = shift_right_sticky(b, ea - eb);
		e = ea;
	} else {
		if (ea < eb)
			a = shift_right_sticky(a, eb - ea);
		e = eb;
	}
	if (negative_product == ((zbits & FP_SIGN_BIT) != 0)) {
		r.lo = a.lo + b.lo;
		r.hi = a.hi + b.hi + (r.lo < a.lo);
		sign = zbits & FP_SIGN_BIT;
	} else if (a.hi > b.hi || (a.hi == b.hi && a.lo > b.lo)) {
		r.lo = a.lo - b.lo;
		r.hi = a.hi - b.hi - (a.lo < b.lo);
		sign = negative_product ? FP_SIGN_BIT : 0;
	} else if (a.hi == b.hi && a.lo == b.lo) {
		return 0.0;		/* exact cancellation rounds to +0 */
	} else {
		r.lo = b.lo - a.lo;
		r.hi = b.hi - a.hi - (b.lo < a.lo);
		sign = zbits & FP_SIGN_BIT;
	}

	/* Normalize the top bit to 127; the value is r * 2**e. */
	if (r.hi == 0) {
		r.hi = r.lo;
		r.lo = 0;
		e -= 64;
	}
	shift = leading_zeros(r.hi);
	if (shift != 0) {
		r = shift_left(r, shift);
		e -= shift;
	}
	biased = e + 127 + 1023;
	if (biased >= 0x7ff) {
		errno = ERANGE;
		return fp_double(sign | 0x7ff0000000000000ULL);
	}
	if (biased < 1) {
		/* Subnormal: the exponent field is 0, without implicit bit. */
		r = shift_right_sticky(r, 1 - biased);
		biased = 0;
	}
	mantissa = r.hi >> 11;
	rest = r.hi & 0x7ff;
	bits = ((uint64_t)biased << 52) + (mantissa & ~FP_IMPLICIT);
	if (biased == 0)
		bits = mantissa;
	if (rest > 0x400 ||
	    (rest == 0x400 && (r.lo != 0 || (mantissa & 1) != 0)))
		bits++;		/* a carry moves into the exponent field */
	return fp_double(sign | bits);
}
