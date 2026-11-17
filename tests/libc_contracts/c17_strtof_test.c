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
 * strtof() per C17 7.22.1.3 over the tree's strtod(): value, end pointer
 * and range errors.  Every in-range input is exactly representable as a
 * float, so the expected value does not depend on how strtod() rounds.
 * The underflow inputs are nonzero doubles that narrow to zero; an input
 * strtod() itself returns as zero carries no error, which 7.22.1.3p10
 * leaves to the implementation.
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static float (*volatile convert)(const char *restrict, char **restrict) =
    strtof;
static unsigned checks;

struct exact {
	const char *text;
	float value;
	int consumed;
};

/*
 * The oracle's strtof: the driver renames the tree's to tree_strtof, and
 * the plain name reaches the hosting library or, with --netbsd, NetBSD's
 * gdtoa strtof, both correctly rounded.
 */
#undef strtof
float strtof(const char *restrict, char **restrict);

static uint64_t state = 0x853c49e6748fea9bULL;

static uint64_t
next(void)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

/*
 * Random decimal strings, and decimal expansions of float rounding
 * midpoints cut to 9 to 25 significant digits: those land within a few
 * units of the 17th digit of a midpoint, where a conversion that rounds
 * to double first and then to float can round the wrong way.  The tree's
 * value and end pointer must equal the oracle's.
 *
 * newlib's strtof(), which hosts the qemu-user variant, rounds through
 * double itself, so that build (C17_STRTOF_UNTRUSTED_ORACLE) compares
 * nothing with it.  Every result also enters an FNV-1a digest, which
 * STRTOF_DIGEST pins from a run checked against glibc; every run must
 * reproduce it.  C17_STRTOF_PRINT_DIGEST prints the digest instead.
 */
#ifdef C17_STRTOF_UNTRUSTED_ORACLE
#define TRUSTED	0
#else
#define TRUSTED	1
#endif
static int
differential(unsigned *count)
{
	char text[64];
	unsigned long i;
	uint64_t digest = 0xcbf29ce484222325ULL;

	for (i = 0; i < 200000; i++) {
		char *tree_end, *oracle_end;
		float tree_value, oracle_value;

		if (i & 1) {
			int digits = 1 + (int)(next() % 20), k;
			char *p = text;

			if (next() & 1)
				*p++ = '-';
			for (k = 0; k < digits; k++) {
				*p++ = (char)('0' + next() % 10);
				if (k == 0)
					*p++ = '.';
			}
			sprintf(p, "e%d", (int)(next() % 90) - 45);
		} else {
			uint32_t bits = (uint32_t)(next() % 0x7f7fffffU);
			float low, high;
			double middle;

			memcpy(&low, &bits, sizeof(low));
			bits++;
			memcpy(&high, &bits, sizeof(high));
			middle = ((double)low + (double)high) / 2.0;
			sprintf(text, "%.*e", 8 + (int)(next() % 17), middle);
		}
		tree_value = convert(text, &tree_end);
		if (TRUSTED) {
			oracle_value = strtof(text, &oracle_end);
		} else {
			oracle_value = tree_value;
			oracle_end = tree_end;
		}
		{
			uint32_t bits;
			unsigned k;

			memcpy(&bits, &tree_value, sizeof(bits));
			bits ^= (uint32_t)(tree_end - text) << 24;
			for (k = 0; k < 4; k++) {
				digest ^= (bits >> (8 * k)) & 0xff;
				digest *= 0x100000001b3ULL;
			}
		}
		(*count)++;
		if (memcmp(&tree_value, &oracle_value, sizeof(float)) != 0 ||
		    tree_end != oracle_end) {
			printf("FAIL strtof: \"%s\" gives %.9g, oracle %.9g\n",
			    text, (double)tree_value, (double)oracle_value);
			return 1;
		}
	}
#ifdef C17_STRTOF_PRINT_DIGEST
	printf("digest %016llx\n", (unsigned long long)digest);
#else
	(*count)++;
	if (digest != STRTOF_DIGEST) {
		printf("FAIL strtof: result digest %016llx, want %016llx\n",
		    (unsigned long long)digest, (unsigned long long)STRTOF_DIGEST);
		return 1;
	}
#endif
	return 0;
}

struct stated {
	const char *text;
	uint32_t bits;		/* the float encoding */
	int consumed;
};

/*
 * Inputs whose correct conversion is known from their binary value:
 * C17's non-decimal forms, hexadecimal ties, and a decimal midpoint with a
 * nonzero digit far beyond the 800 the converter keeps.
 */
static int
stated(unsigned *count)
{
	static char beyond[900], exact[900];
	static const struct stated table[] = {
		{ "inf", 0x7f800000U, 3 }, { "-INFINITY", 0xff800000U, 9 },
		{ "Infinit", 0x7f800000U, 3 }, { "nan", 0x7fc00000U, 3 },
		{ "-nan", 0xffc00000U, 4 }, { "nan(x_1)", 0x7fc00000U, 8 },
		{ "nan(", 0x7fc00000U, 3 }, { "0x1.8p1", 0x40400000U, 7 },
		{ "0X10", 0x41800000U, 4 }, { "0x.8", 0x3f000000U, 4 },
		{ "0x", 0x00000000U, 1 }, { "0x1p-149", 0x00000001U, 8 },
		{ "0x1p-150", 0x00000000U, 8 }, { "0x1.8p-150", 0x00000001U, 10 },
		{ "0x1.000001p0", 0x3f800000U, 12 },
		{ "0x1.000003p0", 0x3f800002U, 12 },
		{ "0x1.0000011p0", 0x3f800001U, 13 },
		{ "1.000000059604644775390625", 0x3f800000U, 26 },
		{ "1.0000001788139343261718750", 0x3f800002U, 27 },
		{ "3.4028235677973366e38", 0x7f7fffffU, 21 },
		{ "3.4028235677973367e38", 0x7f800000U, 21 }
	};
	struct stated run[2];
	unsigned i;
	size_t n;

	/* The tie 1 + 2**-24 rounds to even; any digit past it rounds up. */
	strcpy(exact, "1.000000059604644775390625");
	n = strlen(exact);
	memset(exact + n, '0', 820);
	exact[n + 820] = '\0';
	strcpy(beyond, exact);
	beyond[n + 819] = '1';
	run[0] = (struct stated){ exact, 0x3f800000U, (int)(n + 820) };
	run[1] = (struct stated){ beyond, 0x3f800001U, (int)(n + 820) };
	for (i = 0; i < sizeof(table) / sizeof(table[0]) + 2; i++) {
		const struct stated *t = i < 2 ? &run[i] : &table[i - 2];
		char *end;
		float value = convert(t->text, &end);
		uint32_t bits;

		memcpy(&bits, &value, sizeof(bits));
		(*count)++;
		/* Any quiet NaN of the right sign meets a NaN row. */
		if ((t->bits & 0x7fc00000U) == 0x7fc00000U &&
		    (bits & 0xffc00000U) == t->bits)
			bits = t->bits;
		if (bits != t->bits || end != t->text + t->consumed) {
			printf("FAIL strtof: \"%.40s\" gives %08lx after %d bytes, "
			    "want %08lx after %d\n", t->text, (unsigned long)bits,
			    (int)(end - t->text), (unsigned long)t->bits,
			    t->consumed);
			return 1;
		}
	}
	return 0;
}

int
main(void)
{
	static const struct exact exact[] = {
		{ "0", 0.0f, 1 }, { "1", 1.0f, 1 }, { "-1", -1.0f, 2 },
		{ "1.5", 1.5f, 3 }, { "-2.25", -2.25f, 5 },
		{ "  12.5x", 12.5f, 6 }, { "1e3", 1000.0f, 3 },
		{ "65536", 65536.0f, 5 }, { "0.125", 0.125f, 5 },
		{ "16777216", 16777216.0f, 8 }, { "+4e0z", 4.0f, 4 },
		{ "x", 0.0f, 0 }
	};
	static const char *const over[] = { "1e39", "3.5e38", "1e400" };
	static const char *const under[] = { "1e-50", "1e-46" };
	unsigned i;
	char *end;
	float value;

	for (i = 0; i < sizeof(exact) / sizeof(exact[0]); i++) {
		errno = 0;
		value = convert(exact[i].text, &end);
		checks++;
		if (value != exact[i].value ||
		    end != exact[i].text + exact[i].consumed || errno != 0) {
			printf("FAIL strtof: \"%s\" gives %g, %d bytes, "
			    "errno %d\n", exact[i].text, (double)value,
			    (int)(end - exact[i].text), errno);
			return 1;
		}
	}
	for (i = 0; i < sizeof(over) / sizeof(over[0]); i++) {
		errno = 0;
		value = convert(over[i], &end);
		checks++;
		if (value != HUGE_VALF || errno != ERANGE || *end != '\0') {
			printf("FAIL strtof: overflow \"%s\" gives %g, "
			    "errno %d\n", over[i], (double)value, errno);
			return 1;
		}
		errno = 0;
		checks++;
		if (convert(over[i] - 0, NULL) != HUGE_VALF) {
			printf("FAIL strtof: null end pointer\n");
			return 1;
		}
	}
	errno = 0;
	value = convert("-1e39", &end);
	checks++;
	if (value != -HUGE_VALF || errno != ERANGE) {
		printf("FAIL strtof: negative overflow gives %g, errno %d\n",
		    (double)value, errno);
		return 1;
	}
	for (i = 0; i < sizeof(under) / sizeof(under[0]); i++) {
		errno = 0;
		value = convert(under[i], &end);
		checks++;
		if (value != 0.0f || errno != ERANGE) {
			printf("FAIL strtof: underflow \"%s\" gives %g, "
			    "errno %d\n", under[i], (double)value, errno);
			return 1;
		}
	}
	if (stated(&checks) != 0 || differential(&checks) != 0)
		return 1;
	printf("c17 strtof: %u checks passed\n", checks);
	return 0;
}
