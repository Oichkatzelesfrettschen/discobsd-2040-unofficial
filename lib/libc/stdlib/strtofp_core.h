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

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


#ifndef _STRTOFP_CORE_H_
#define _STRTOFP_CORE_H_

#include "strtofp.h"

/*
 * Correctly rounded conversion of a decimal or hexadecimal string to a
 * binary floating-point encoding, for strtof() and strtod().  Every
 * function is static: strtod.c includes this header directly, so that
 * member builds and links alone, and strtofp.c includes it for strtof(),
 * wcstof() and wcstod().
 *
 * Hexadecimal input is exact binary: its bits are rounded once
 * (round_bits).  Decimal input is the value D * 10**k for an integer D.
 * A candidate encoding comes from double arithmetic on D's leading
 * digits; it is within a few units of the answer.  The candidate b is
 * then corrected by exact comparison: the result is the encoding whose
 * neighboring midpoints, (value(b - 1) + value(b)) / 2 and
 * (value(b) + value(b + 1)) / 2, bracket the input, a tie going to the
 * even encoding.  Every encoding has an exact value A * 2**e with A below
 * 2**55, and the encoding past the largest finite one has the value
 * 2**(emax + 1), so the same comparison decides overflow.
 *
 * A comparison of D * 10**k with A * 2**e is a comparison of the integers
 * D * 5**k * 2**(k - e) and A, with the powers moved to whichever side
 * keeps them nonnegative.  The integers are kept in 16-bit limbs so that
 * every partial product fits 32 bits: ARMv6-M has no 32 x 32 -> 64-bit
 * multiply and libc links no 64-bit multiply routine.
 *
 * Only the first MAX_DIGITS significant digits enter D.  When a later
 * digit is nonzero, a final digit 1 is appended, which puts D strictly
 * between the truncated value and the next one at that resolution.  No
 * binary64 midpoint has more than 767 significant digits, so no midpoint
 * lies inside that interval, and every comparison keeps the sign it
 * would have had against the full input.
 */
#define MAX_DIGITS	800
#define LIMBS		192	/* 3072 bits; the largest operand needs 2730 */

struct big {
	int		size;
	uint16_t	limb[LIMBS];
};

struct format {
	int		precision;	/* significand bits, implicit bit included */
	int		bias;
	int		field_max;	/* exponent field of the infinities */
	int		max_order;	/* decimal orders above which all overflows */
	int		min_order;	/* and below which all rounds to zero */
};

static const double tens[23] = {
	1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12,
	1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

static const struct format formats[2] = {
	{ 24, 127, 255, 40, -46 },
	{ 53, 1023, 2047, 310, -325 }
};

static void
big_small(struct big *b, uint32_t value)
{
	b->size = 0;
	while (value != 0) {
		b->limb[b->size++] = (uint16_t)value;
		value >>= 16;
	}
}

static void
big_from64(struct big *b, uint64_t value)
{
	b->size = 0;
	while (value != 0) {
		b->limb[b->size++] = (uint16_t)value;
		value >>= 16;
	}
}

/* b = b * factor + addend, factor and addend below 2**16. */
static void
big_muladd(struct big *b, uint32_t factor, uint32_t addend)
{
	uint32_t carry = addend;
	int i;

	for (i = 0; i < b->size; i++) {
		carry += b->limb[i] * factor;
		b->limb[i] = (uint16_t)carry;
		carry >>= 16;
	}
	if (carry != 0 && b->size < LIMBS)
		b->limb[b->size++] = (uint16_t)carry;
}

/* b = b * 5**count; 5**6 is the largest power below 2**16. */
static void
big_pow5(struct big *b, int count)
{
	for (; count >= 6; count -= 6)
		big_muladd(b, 15625, 0);
	for (; count > 0; count--)
		big_muladd(b, 5, 0);
}

static void
big_shift(struct big *b, int count)
{
	int words = count / 16, bits = count % 16, i;

	if (b->size == 0)
		return;
	if (bits != 0) {
		uint32_t carry = 0;

		for (i = 0; i < b->size; i++) {
			uint32_t v = (uint32_t)b->limb[i] << bits | carry;

			b->limb[i] = (uint16_t)v;
			carry = v >> 16;
		}
		if (carry != 0 && b->size < LIMBS)
			b->limb[b->size++] = (uint16_t)carry;
	}
	if (words != 0) {
		if (b->size + words > LIMBS)
			words = LIMBS - b->size;
		for (i = b->size - 1; i >= 0; i--)
			b->limb[i + words] = b->limb[i];
		for (i = 0; i < words; i++)
			b->limb[i] = 0;
		b->size += words;
	}
}

static int
big_compare(const struct big *x, const struct big *y)
{
	int i;

	if (x->size != y->size)
		return x->size > y->size ? 1 : -1;
	for (i = x->size - 1; i >= 0; i--) {
		if (x->limb[i] != y->limb[i])
			return x->limb[i] > y->limb[i] ? 1 : -1;
	}
	return 0;
}

/* The exact value of encoding bits as significand * 2**exponent. */
static uint64_t
encoding_value(const struct format *f, uint64_t bits, int *exponent)
{
	int field = (int)(bits >> (f->precision - 1));
	uint64_t fraction = bits & ((1ULL << (f->precision - 1)) - 1);

	if (field == 0) {
		*exponent = 1 - f->bias - (f->precision - 1);
		return fraction;
	}
	*exponent = field - f->bias - (f->precision - 1);
	return fraction | 1ULL << (f->precision - 1);
}

/* (value(bits) + value(bits + 1)) / 2, exactly. */
static uint64_t
midpoint(const struct format *f, uint64_t bits, int *exponent)
{
	int low_exponent, high_exponent;
	uint64_t low = encoding_value(f, bits, &low_exponent);
	uint64_t high = encoding_value(f, bits + 1, &high_exponent);

	/* Neighbors differ in exponent by at most one. */
	if (high_exponent > low_exponent)
		high <<= high_exponent - low_exponent;
	*exponent = low_exponent - 1;
	return low + high;
}

/* The sign of digits * 10**scale - significand * 2**exponent. */
static int
compare(const struct big *digits, int scale, uint64_t significand,
    int exponent)
{
	struct big x, y;
	int shift;

	x = *digits;
	big_from64(&y, significand);
	if (scale >= 0)
		big_pow5(&x, scale);
	else
		big_pow5(&y, -scale);
	shift = scale - exponent;
	if (shift > 0)
		big_shift(&x, shift);
	else
		big_shift(&y, -shift);
	return big_compare(&x, &y);
}

/* Round significand * 2**exponent (plus sticky below it) to the format. */
static uint64_t
round_bits(const struct format *f, uint64_t significand, int exponent,
    int sticky, int *range)
{
	int precision = f->precision, top, field, shift;
	uint64_t rest, half, bits;

	if (significand == 0)
		return 0;
	for (top = 63; (significand >> top) == 0; top--)
		continue;
	/* The unbiased exponent of the leading bit. */
	field = exponent + top + f->bias;
	if (field >= f->field_max) {
		*range = 1;
		return (uint64_t)f->field_max << (precision - 1);
	}
	/* Bits of significand below the result's last place. */
	shift = top - (precision - 1);
	if (field < 1)
		shift += 1 - field;
	if (shift <= 0) {
		bits = significand << -shift;
		if (field < 1)
			field = 0;
		rest = 0;
		half = 1;
	} else if (shift > 64) {
		*range = 1;
		return 0;
	} else {
		rest = shift == 64 ? significand : significand & ((1ULL << shift) - 1);
		half = 1ULL << (shift - 1);
		bits = shift == 64 ? 0 : significand >> shift;
		if (field < 1)
			field = 0;
	}
	if (field == 0) {
		/* A subnormal encoding is its significand. */
		if (rest != 0 || sticky)
			*range = 1;
	} else
		bits = (bits & ((1ULL << (precision - 1)) - 1)) |
		    (uint64_t)field << (precision - 1);
	if (rest > half || (rest == half && (sticky || (bits & 1) != 0)))
		bits++;		/* a carry moves into the exponent field */
	if ((bits >> (precision - 1)) >= (uint64_t)f->field_max) {
		*range = 1;
		return (uint64_t)f->field_max << (precision - 1);
	}
	return bits;
}

static int
lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/*
 * The input is read through at(), so one parser serves byte and wide
 * strings: strtof() and strtod() read char, wcstof() and wcstod() read
 * wchar_t.  A character outside the basic character set reads as a value
 * no rule accepts, which ends the subject sequence there.
 */
struct input {
	const void	*base;
	int		wide;
};

static int
at(const struct input *in, size_t i)
{
	if (in->wide) {
		unsigned long c = (unsigned long)((const wchar_t *)in->base)[i];

		return c < 128 ? (int)c : 0x100;
	}
	return ((const unsigned char *)in->base)[i];
}

static int
is_digit(int c)
{
	return c >= '0' && c <= '9';
}

static int
is_space(int c)
{
	return c == ' ' || (c >= '\t' && c <= '\r');
}

/* Match a lowercase word case-insensitively; return its length or 0. */
static size_t
word(const struct input *in, size_t i, const char *w)
{
	size_t n = 0;

	while (w[n] != '\0' && lower(at(in, i + n)) == w[n])
		n++;
	return w[n] == '\0' ? n : 0;
}

static int
hexdigit(int c)
{
	if (is_digit(c))
		return c - '0';
	c = lower(c);
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

/* An exponent part at i, the letter: [+-]digits, saturated; else i. */
static size_t
exponent_part(const struct input *in, size_t i, long *value)
{
	size_t q = i + 1;
	int negative = 0;
	long e = 0;

	if (at(in, q) == '+' || at(in, q) == '-')
		negative = at(in, q++) == '-';
	if (!is_digit(at(in, q)))
		return i;
	while (is_digit(at(in, q))) {
		if (e < 100000)
			e = e * 10 + (at(in, q) - '0');
		q++;
	}
	*value = negative ? -e : e;
	return q;
}

static uint64_t
hexadecimal(const struct format *f, const struct input *in, size_t *pos,
    int *range)
{
	uint64_t significand = 0;
	long exponent = 0, power = 0;
	int sticky = 0, point = 0, d;
	size_t i = *pos;

	for (;; i++) {
		if (at(in, i) == '.' && !point) {
			point = 1;
			continue;
		}
		if ((d = hexdigit(at(in, i))) < 0)
			break;
		if (significand >> 60 == 0) {
			significand = significand << 4 | (uint64_t)d;
			if (point)
				exponent -= 4;
		} else {
			sticky |= d != 0;
			if (!point)
				exponent += 4;
		}
	}
	if (lower(at(in, i)) == 'p')
		i = exponent_part(in, i, &power);
	*pos = i;
	exponent += power;
	if (exponent > 100000)
		exponent = 100000;
	else if (exponent < -100000)
		exponent = -100000;
	return round_bits(f, significand, (int)exponent, sticky, range);
}

/* Returns with *pos unchanged when no digit is present. */
static uint64_t
decimal(const struct format *f, const struct input *in, size_t *pos,
    int *range)
{
	struct big digits;
	uint64_t bits, significand;
	uint32_t high = 0, low = 0;	/* leading digits, nine to a group */
	long scale = 0, power = 0;
	int count = 0, taken = 0, point = 0, sticky = 0, seen = 0, e, order, c;
	size_t i = *pos;
	double estimate;

	big_small(&digits, 0);
	for (;; i++) {
		c = at(in, i);
		if (c == '.' && !point) {
			point = 1;
			continue;
		}
		if (!is_digit(c))
			break;
		seen = 1;
		if (count == 0 && c == '0') {
			if (point)
				scale--;
			continue;
		}
		if (count < MAX_DIGITS) {
			big_muladd(&digits, 10, (uint32_t)(c - '0'));
			/* Two 32-bit groups: no 64-bit multiply. */
			if (taken < 9)
				high = high * 10 + (uint32_t)(c - '0');
			else if (taken < 18)
				low = low * 10 + (uint32_t)(c - '0');
			if (taken < 18)
				taken++;
			count++;
			if (point)
				scale--;
		} else {
			sticky |= c != '0';
			if (!point)
				scale++;
		}
	}
	if (!seen)
		return 0;
	if (lower(at(in, i)) == 'e')
		i = exponent_part(in, i, &power);
	*pos = i;
	if (count == 0)
		return 0;
	if (sticky) {
		big_muladd(&digits, 10, 1);
		count++;
		scale--;
	}
	scale += power;
	order = (int)(scale > 100000 ? 100000 : scale < -100000 ? -100000 :
	    scale) + count;
	if (order > f->max_order) {
		*range = 1;
		return (uint64_t)f->field_max << (f->precision - 1);
	}
	if (order < f->min_order) {
		*range = 1;
		return 0;
	}

	/* The candidate: the leading digits scaled in double arithmetic. */
	estimate = taken > 9 ?
	    (double)high * tens[taken - 9] + (double)low : (double)high;
	for (e = (int)scale + count - taken; e > 0; e -= e > 22 ? 22 : e)
		estimate *= tens[e > 22 ? 22 : e];
	for (e = (int)scale + count - taken; e < 0; e += e < -22 ? 22 : -e)
		estimate /= tens[e < -22 ? 22 : -e];
	if (f->precision == 24) {
		union { float value; uint32_t bits; } narrow;

		narrow.value = (float)estimate;
		bits = narrow.bits;
	} else {
		union { double value; uint64_t bits; } wide;

		wide.value = estimate;
		bits = wide.bits;
	}
	if ((bits >> (f->precision - 1)) > (uint64_t)f->field_max)
		bits = (uint64_t)f->field_max << (f->precision - 1);

	/* Correct the candidate until the midpoints bracket the input. */
	for (;;) {
		if ((bits >> (f->precision - 1)) < (uint64_t)f->field_max) {
			significand = midpoint(f, bits, &e);
			c = compare(&digits, (int)scale, significand, e);
			if (c > 0 || (c == 0 && (bits & 1) != 0)) {
				bits++;
				continue;
			}
		}
		if (bits > 0) {
			significand = midpoint(f, bits - 1, &e);
			c = compare(&digits, (int)scale, significand, e);
			if (c < 0 || (c == 0 && (bits & 1) != 0)) {
				bits--;
				continue;
			}
		}
		break;
	}
	if ((bits >> (f->precision - 1)) == (uint64_t)f->field_max)
		*range = 1;
	else if ((bits >> (f->precision - 1)) == 0) {
		significand = encoding_value(f, bits, &e);
		if (compare(&digits, (int)scale, significand, e) != 0)
			*range = 1;
	}
	return bits;
}

static uint64_t
convert(const struct input *in, size_t *length, int format, int *range)
{
	const struct format *f = &formats[format];
	uint64_t sign, bits;
	size_t i = 0, n, digits;
	int c;

	*range = 0;
	*length = 0;
	while (is_space(at(in, i)))
		i++;
	sign = 0;
	c = at(in, i);
	if (c == '+' || c == '-') {
		sign = c == '-';
		i++;
	}
	sign <<= f->precision - 1 + (format == STRTOFP_BINARY32 ? 8 : 11);
	if ((n = word(in, i, "inf")) != 0) {
		i += n;
		if ((n = word(in, i, "inity")) != 0)
			i += n;
		*length = i;
		return sign | (uint64_t)f->field_max << (f->precision - 1);
	}
	if ((n = word(in, i, "nan")) != 0) {
		size_t q;

		i += n;
		if (at(in, i) == '(') {
			for (q = i + 1; is_digit(at(in, q)) ||
			    (lower(at(in, q)) >= 'a' && lower(at(in, q)) <= 'z') ||
			    at(in, q) == '_'; q++)
				continue;
			if (at(in, q) == ')')
				i = q + 1;
		}
		*length = i;
		return sign | (uint64_t)f->field_max << (f->precision - 1) |
		    1ULL << (f->precision - 2);
	}
	if (at(in, i) == '0' && lower(at(in, i + 1)) == 'x' &&
	    (hexdigit(at(in, i + 2)) >= 0 ||
	    (at(in, i + 2) == '.' && hexdigit(at(in, i + 3)) >= 0))) {
		i += 2;
		bits = hexadecimal(f, in, &i, range);
	} else {
		digits = i;
		bits = decimal(f, in, &i, range);
		if (i == digits)
			return 0;	/* no subject sequence */
	}
	*length = i;
	return sign | bits;
}

#endif /* !_STRTOFP_CORE_H_ */
