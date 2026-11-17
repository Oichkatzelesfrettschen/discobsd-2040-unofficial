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
#include <stdint.h>

#include "fpbits.h"

/*
 * The first 256 bits of 2/pi, most significant word first.  A float of
 * magnitude 2**16 or more reaches here: its quotient by pi/2 has more bits
 * than a double keeps below the integer part, so the quotient is formed
 * from the product of its 24-bit significand with the bits of 2/pi that can
 * reach the quadrant and the fraction (Payne and Hanek).
 */
static const uint32_t two_over_pi[8] = {
	0xa2f9836eU, 0x4e441529U, 0xfc2757d1U, 0xf534ddc0U,
	0xdb629599U, 0x3c439041U, 0xfe5163abU, 0xdebbc561U
};

/* Bit i of 2/pi, whose weight is 2**-i; zero outside the stored range. */
static uint32_t
two_over_pi_bit(long i)
{
	if (i < 1 || i > 256)
		return 0;
	return (two_over_pi[(i - 1) >> 5] >> (31 - ((i - 1) & 31))) & 1;
}

/*
 * Reduce a finite float of magnitude 2**16 or more by quarter turns:
 * return n modulo 4 and set *y to |x| - n * pi/2, with |*y| <= pi/4, so
 * that sin(|x|) is sin(*y), cos(*y), -sin(*y) or -cos(*y) for n = 0 to 3.
 *
 * |x| is m * 2**q for a 24-bit integer m.  Taken modulo 4 the product
 * m * 2**q * (2/pi) keeps only the bits of 2/pi from position q - 1 on, and
 * 96 of them (down to weight 2**(q - 95) times m, below 2**-71) fix the
 * fraction to far more bits than a float result needs.  W is those bits as
 * an integer, m * W modulo 2**96 has the quadrant in its top two bits and
 * the fraction in the remaining 94.
 */
int
__discobsd_rem_pio2f(float x, double *y)
{
	uint32_t bits = fp_bitsf(x) & 0x7fffffffU;
	uint32_t m = (bits & 0x007fffffU) | 0x00800000U;
	long start = (long)(bits >> 23) - 150 - 1;
	uint32_t w[3] = { 0, 0, 0 };
	uint32_t r[3];
	uint64_t product, top;
	int j, n, negative = 0;

	for (j = 0; j < 96; j++) {
		w[0] = w[0] << 1 | w[1] >> 31;
		w[1] = w[1] << 1 | w[2] >> 31;
		w[2] = w[2] << 1 | two_over_pi_bit(start + j);
	}
	product = (uint64_t)w[2] * m;
	r[2] = (uint32_t)product;
	product = (uint64_t)w[1] * m + (product >> 32);
	r[1] = (uint32_t)product;
	product = (uint64_t)w[0] * m + (product >> 32);
	r[0] = (uint32_t)product;

	n = (int)(r[0] >> 30);
	r[0] &= 0x3fffffffU;
	if ((r[0] >> 29) != 0) {
		/* The fraction is at least one half: the nearer quarter turn is the next. */
		uint32_t borrow2 = r[2] != 0;
		uint32_t borrow1 = r[1] != 0 || borrow2;

		r[2] = 0U - r[2];
		r[1] = 0U - r[1] - borrow2;
		r[0] = (0U - r[0] - borrow1) & 0x3fffffffU;
		n++;
		negative = 1;
	}
	top = (uint64_t)r[0] << 32 | r[1];
	*y = (double)top * 0x1p-62 * 1.5707963267948966;
	if (negative)
		*y = -*y;
	return n & 3;
}
