/*
 * %g and %G of the tree formatter against the host C library, C17
 * 7.21.6.1p8: the precision P is the number of significant digits; style e
 * is used when the exponent X of the style e conversion is below -4 or at
 * least P, and style f with precision P - 1 - X otherwise; trailing zeros
 * of the fraction go unless # is given.
 *
 * The tree formatter is compiled from lib/libc/stdio under tree_ names, so
 * the host snprintf below is the oracle. The tree digit generator is not a
 * correctly rounded converter: it multiplies the fraction by ten in double
 * arithmetic, rounds an exact tie up, and stores at most DBL_DIG digits.
 * Two corpora keep the oracle exact: arbitrary doubles at up to 10
 * significant digits, where the generator and the host agree on every
 * value tried, and doubles nearest to decimal numbers of at most 12
 * digits at up to 12 digits, where the digits to reproduce are short. A
 * tie in the dropped tail of the second corpus is skipped, because the
 * host rounds it to even. Negative zero is left out: the tree prints it
 * without its sign under every decimal conversion.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tree_snprintf(char *, size_t, const char *, ...);

/* A software-float emulation run draws a fraction of each corpus. */
#ifndef PRINTF_G_ITERATIONS_DIVISOR
#define PRINTF_G_ITERATIONS_DIVISOR 1
#endif

static int failures;
static int shown;
static unsigned long compared;

static void
fail(const char *what, const char *format, double value, const char *got,
    const char *want)
{
	failures++;
	if (shown++ < 25)
		printf("FAIL %s: %s of %.17g: tree \"%s\", host \"%s\"\n", what,
		    format, value, got, want);
}

/* Compare the tree and host conversions of one value under one format. */
static void
compare(const char *what, const char *format, double value)
{
	char got[512], want[512];

	tree_snprintf(got, sizeof got, format, value);
	snprintf(want, sizeof want, format, value);
	compared++;
	if (strcmp(got, want) != 0)
		fail(what, format, value, got, want);
}

/* A fixed vector: the tree result must equal the literal, and so must the host's. */
static void
vector(const char *format, double value, const char *expected)
{
	char got[512], want[512];

	tree_snprintf(got, sizeof got, format, value);
	snprintf(want, sizeof want, format, value);
	compared++;
	if (strcmp(got, expected) != 0)
		fail("vector", format, value, got, expected);
	if (strcmp(want, expected) != 0) {
		failures++;
		printf("FAIL vector oracle: %s of %.17g: host \"%s\", "
		    "literal \"%s\"\n", format, value, want, expected);
	}
}

static uint32_t rng_state = 0x2545f491u;

static uint32_t
rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/*
 * Whether rounding the decimal digits of text to precision digits meets an
 * exact tie, which the tree rounds up and the host to even. text is the
 * "%.30e" form of the nearest double's decimal parent.
 */
static int
is_tie(const char *digits, int precision)
{
	int n = (int)strlen(digits);

	if (precision >= n)
		return 0;
	if (digits[precision] != '5')
		return 0;
	for (int i = precision + 1; i < n; i++)
		if (digits[i] != '0')
			return 0;
	return 1;
}

int
main(void)
{
	static const int precisions[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
	static const char *const flags[] = { "", "#", "+", " " };
	char format[32];

	/*
	 * The defect: a value below 1 loses one significant digit for every
	 * zero after the point, because the zeros were counted against P.
	 */
	vector("%.12g", 0.000123456789012345, "0.000123456789012");
	vector("%.3g", 0.000123456, "0.000123");
	vector("%.3g", 0.0123456, "0.0123");
	vector("%g", 0.00123456, "0.00123456");
	vector("%.1g", 0.0123456, "0.01");
	vector("%.5g", 0.0000123456, "1.2346e-05");
	vector("%.4g", 0.0001, "0.0001");
	vector("%.4g", 0.00009999, "9.999e-05");
	vector("%.3g", 0.123456, "0.123");
	vector("%.3g", 0.0999999, "0.1");

	/* style selection at X = -5, -4, P - 1 and P */
	vector("%g", 123456.0, "123456");
	vector("%g", 1234567.0, "1.23457e+06");
	vector("%g", 999999.0, "999999");
	vector("%g", 999999.5, "1e+06");
	vector("%.3g", 99.95, "100");
	vector("%.3g", 999.5, "1e+03");
	vector("%.3g", 100.0, "100");
	vector("%.3g", 1000.0, "1e+03");
	vector("%g", 0.0001, "0.0001");
	vector("%g", 0.00001, "1e-05");
	vector("%g", 0.000099999, "9.9999e-05");

	/* rounding that carries into a new decade */
	vector("%.7g", 9.9999996e-5, "0.0001");
	vector("%.7g", 9.9999994e-5, "9.999999e-05");
	vector("%.2g", 9.96, "10");
	vector("%.2g", 99.6, "1e+02");
	vector("%.1g", 0.96, "1");
	vector("%.1g", 9.6, "1e+01");

	/* precision 0 is precision 1; the default is 6 */
	vector("%.0g", 123.0, "1e+02");
	vector("%.0g", 0.5, "0.5");
	vector("%.0g", 7.0, "7");
	vector("%.g", 0.000123, "0.0001");
	vector("%g", 100000.0, "100000");
	vector("%g", 3.14159265358979, "3.14159");
	vector("%g", 0.0, "0");

	/* # keeps the trailing zeros and the point */
	vector("%#g", 1.0, "1.00000");
	vector("%#g", 0.5, "0.500000");
	vector("%#g", 0.000123, "0.000123000");
	vector("%#.3g", 1.0, "1.00");
	vector("%#.3g", 1000.0, "1.00e+03");
	vector("%#.0g", 3.0, "3.");
	vector("%#.0g", 3000.0, "3.e+03");
	vector("%#.1g", 0.0123456, "0.01");
	vector("%#.12g", 0.000123, "0.000123000000000");

	/* signs, case and width */
	vector("%g", -0.000123456789, "-0.000123457");
	vector("%.12G", -1.5e-10, "-1.5E-10");
	vector("%G", 1e-5, "1E-05");
	vector("%G", 0.00012345678, "0.000123457");
	vector("%12.4g|", 0.000123456, "   0.0001235|");
	vector("%-12.4g|", 0.000123456, "0.0001235   |");
	vector("%+g", 0.25, "+0.25");
	vector("% g", 0.25, " 0.25");

	/* a precision past the digits the generator stores */
	vector("%.20g", 1.0, "1");
	vector("%#.20g", 1.0, "1.0000000000000000000");
	vector("%.17g", 1e16, "10000000000000000");
	vector("%.16g", 1e16, "1e+16");
	vector("%.18g", 0.5, "0.5");
	vector("%#.18g", 0.5, "0.500000000000000000");
	vector("%#.20g", 0x1p-20, "9.5367431640625000000e-07");
	vector("%#.20g", 1e16, "10000000000000000.000");
	vector("%.20g", 0x1p-20, "9.5367431640625e-07");

	/* extremes of the exponent range */
	vector("%g", 1e100, "1e+100");
	vector("%g", 1e-100, "1e-100");
	vector("%g", 1e300, "1e+300");
	vector("%.3g", 1.5e-300, "1.5e-300");

	/*
	 * Generated corpus. Each value is the double nearest to a decimal
	 * of up to 12 digits scaled by a power of ten, so the digits the
	 * tree generator must reproduce are short. A precision below the
	 * digit count rounds, and the nearest double is within an ulp of
	 * the decimal, which only differs from rounding the decimal when
	 * the dropped tail is a tie.
	 */
	for (int iteration = 0; iteration < 3000 / PRINTF_G_ITERATIONS_DIVISOR; iteration++) {
		char text[32], digits[16];
		int ndigits = 1 + (int)(rng() % 12);
		int exponent = (int)(rng() % 41) - 20;
		double value;

		/*
		 * The digits are built as text: twelve decimal digits overflow
		 * the 32-bit long of the Thumb-1 build this test also runs as.
		 */
		for (int i = 0; i < ndigits; i++)
			digits[i] = (char)('0' + (i == 0 ? 1 + (int)(rng() % 9) : (int)(rng() % 10)));
		digits[ndigits] = '\0';
		snprintf(text, sizeof text, "%sE%d", digits, exponent - ndigits + 1);
		value = strtod(text, NULL);
		if (iteration % 3 == 0)
			value = -value;
		for (size_t p = 0; p < sizeof precisions / sizeof precisions[0]; p++) {
			int precision = precisions[p];

			if (is_tie(digits, precision == 0 ? 1 : precision))
				continue;
			snprintf(format, sizeof format, "%%%s.%dg",
			    flags[rng() % 4], precision);
			compare("corpus g", format, value);
			if (rng() % 4 == 0) {
				snprintf(format, sizeof format, "%%#.%dG", precision);
				compare("corpus G", format, value);
			}
		}
	}

	/*
	 * Arbitrary doubles across the whole exponent range, subnormals
	 * excluded, at every precision the generator rounds exactly.
	 */
	for (int iteration = 0; iteration < 4000 / PRINTF_G_ITERATIONS_DIVISOR; iteration++) {
		uint64_t bits = ((uint64_t)rng() << 32) | rng();
		uint64_t biased = 1 + (bits >> 52) % 2046;
		double value;

		bits = (bits & 0x800fffffffffffffULL) | (biased << 52);
		memcpy(&value, &bits, sizeof value);
		for (int precision = 0; precision <= 10; precision++) {
			snprintf(format, sizeof format, "%%.%dg", precision);
			compare("random g", format, value);
			snprintf(format, sizeof format, "%%#.%dG", precision);
			compare("random G", format, value);
		}
	}

	if (failures != 0) {
		printf("printf %%g contract: %d of %lu comparisons failed\n",
		    failures, compared);
		return 1;
	}
	printf("printf %%g contract tests passed (%lu comparisons)\n", compared);
	return 0;
}
