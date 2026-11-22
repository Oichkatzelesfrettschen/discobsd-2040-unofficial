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

/*
 * Callable div, ldiv, lldiv and llabs against C17 7.22.6.  Truncating
 * division is the one (quot, rem) pair that satisfies three conditions:
 * numerator == quot * denominator + rem, |rem| < |denominator|, and a
 * nonzero rem has the numerator's sign.  The grid checks those conditions
 * rather than a second evaluation of / and %, so a provider and its oracle
 * share zero arithmetic; floored division meets the first two and fails the
 * third.  Grid operands stay small enough that the identity cannot
 * overflow.  Width boundaries carry literal expected results.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static div_t (*volatile int_divide)(int, int) = div;
static ldiv_t (*volatile long_divide)(long, long) = ldiv;
static lldiv_t (*volatile wide_divide)(long long, long long) = lldiv;
static long long (*volatile wide_absolute)(long long) = llabs;
static unsigned long checks;

#define GRID	181

static const char *
violation(long long numerator, long long denominator, long long quot,
    long long rem)
{
	long long bound = denominator < 0 ? -denominator : denominator;

	if (quot * denominator + rem != numerator)
		return "identity";
	if (rem >= bound || rem <= -bound)
		return "bound";
	if (rem != 0 && (rem < 0) != (numerator < 0))
		return "sign";
	return NULL;
}

static int
report(const char *name, int status, long long numerator,
    long long denominator, long long quot, long long rem, const char *why)
{
	printf("FAIL %s(%lld, %lld): quot %lld rem %lld: %s\n", name,
	    numerator, denominator, quot, rem, why);
	return status;
}

static int
grid(void)
{
	const char *why;
	int numerator, denominator;

	for (numerator = -GRID; numerator <= GRID; numerator++) {
		for (denominator = -GRID; denominator <= GRID; denominator++) {
			div_t narrow;
			ldiv_t middle;
			lldiv_t wide;

			if (denominator == 0)
				continue;
			narrow = int_divide(numerator, denominator);
			checks++;
			why = violation(numerator, denominator, narrow.quot,
			    narrow.rem);
			if (why != NULL)
				return report("div", 1, numerator,
				    denominator, narrow.quot, narrow.rem, why);
			middle = long_divide(numerator, denominator);
			checks++;
			why = violation(numerator, denominator, middle.quot,
			    middle.rem);
			if (why != NULL)
				return report("ldiv", 2, numerator,
				    denominator, middle.quot, middle.rem, why);
			wide = wide_divide(numerator, denominator);
			checks++;
			why = violation(numerator, denominator, wide.quot,
			    wide.rem);
			if (why != NULL)
				return report("lldiv", 3, numerator,
				    denominator, wide.quot, wide.rem, why);
		}
	}
	return 0;
}

/*
 * Each row is numerator, denominator, quotient, remainder.  MIN / -1 has
 * no representable quotient and stays out of every table.
 */
#define BOUNDARIES(max, min) {						\
	{ max, 1, max, 0 }, { max, -1, -max, 0 },			\
	{ min, 1, min, 0 }, { min, 2, min / 2, 0 },			\
	{ min, max, -1, -1 }, { max, min, 0, max },			\
	{ min + 1, -1, max, 0 }, { max, max, 1, 0 },			\
	{ min, min, 1, 0 }, { max, 2, max / 2, 1 },			\
	{ -max, 2, -(max / 2), -1 }, { 0, min, 0, 0 },			\
	{ 7, -2, -3, 1 }, { -7, 2, -3, -1 }, { -7, -2, 3, -1 }		\
}

static int
boundaries(void)
{
	static const int narrow[][4] = BOUNDARIES(INT_MAX, INT_MIN);
	static const long middle[][4] = BOUNDARIES(LONG_MAX, LONG_MIN);
	static const long long wide[][4] = BOUNDARIES(LLONG_MAX, LLONG_MIN);
	static const long long split[][4] = {
		/* Operands on both sides of the 32-bit word boundary. */
		{ 0x100000000LL, 3, 0x55555555LL, 1 },
		{ -0x100000000LL, 3, -0x55555555LL, -1 },
		{ 0x123456789abcdefLL, 0x10000LL, 0x123456789abLL, 0xcdef },
		{ 0x7fffffffffffffffLL, 0x100000001LL, 0x7fffffffLL,
		    0x80000000LL },
		{ 1, 0x100000000LL, 0, 1 }
	};
	unsigned i;

	for (i = 0; i < sizeof(narrow) / sizeof(narrow[0]); i++) {
		div_t result = int_divide(narrow[i][0], narrow[i][1]);

		checks++;
		if (result.quot != narrow[i][2] || result.rem != narrow[i][3])
			return report("div", 1, narrow[i][0], narrow[i][1],
			    result.quot, result.rem, "boundary");
	}
	for (i = 0; i < sizeof(middle) / sizeof(middle[0]); i++) {
		ldiv_t result = long_divide(middle[i][0], middle[i][1]);

		checks++;
		if (result.quot != middle[i][2] || result.rem != middle[i][3])
			return report("ldiv", 2, middle[i][0], middle[i][1],
			    result.quot, result.rem, "boundary");
	}
	for (i = 0; i < sizeof(wide) / sizeof(wide[0]); i++) {
		lldiv_t result = wide_divide(wide[i][0], wide[i][1]);

		checks++;
		if (result.quot != wide[i][2] || result.rem != wide[i][3])
			return report("lldiv", 3, wide[i][0], wide[i][1],
			    result.quot, result.rem, "boundary");
	}
	for (i = 0; i < sizeof(split) / sizeof(split[0]); i++) {
		lldiv_t result = wide_divide(split[i][0], split[i][1]);

		checks++;
		if (result.quot != split[i][2] || result.rem != split[i][3])
			return report("lldiv", 3, split[i][0], split[i][1],
			    result.quot, result.rem, "boundary");
	}
	return 0;
}

static int
check_magnitude(long long value, long long expected)
{
	long long result = wide_absolute(value);

	checks++;
	if (result != expected) {
		printf("FAIL llabs(%lld): %lld != %lld\n", value, result,
		    expected);
		return 4;
	}
	return 0;
}

static int
magnitudes(void)
{
	static const long long high[] = {
		0x100000000LL, 0x1ffffffffLL, 0x7fffffff00000000LL, LLONG_MAX
	};
	long long value;
	unsigned i;
	int status;

	for (value = 0; value <= 4096; value++) {
		if ((status = check_magnitude(value, value)) != 0 ||
		    (status = check_magnitude(-value, value)) != 0)
			return status;
	}
	for (i = 0; i < sizeof(high) / sizeof(high[0]); i++) {
		if ((status = check_magnitude(high[i], high[i])) != 0 ||
		    (status = check_magnitude(-high[i], high[i])) != 0)
			return status;
	}
	return check_magnitude(LLONG_MIN + 1, LLONG_MAX);
}

int
main(void)
{
	int status;

	if ((status = grid()) != 0 || (status = boundaries()) != 0 ||
	    (status = magnitudes()) != 0)
		return status;
	printf("integer division: %lu checks passed\n", checks);
	return 0;
}
