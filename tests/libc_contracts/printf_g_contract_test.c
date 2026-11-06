/*
 * The decimal and %g conversions of the tree formatter against C17
 * 7.21.6.1, with the host C library as the oracle for the generated
 * corpora and exact literals for the fixed vectors.
 *
 * The tree formatter is compiled from lib/libc/stdio under tree_ names, so
 * the host snprintf is the oracle. A vector pins one expected text; the
 * literals were computed with CPython's "%" operator, which converts
 * through a correctly rounded binary-to-decimal routine, and each plain
 * %.Nf and %.Ne literal was recomputed from the exact binary value with
 * the decimal module under round-half-even. vector() also requires the
 * host snprintf to print the literal, so a host that disagrees with the
 * literal fails the run instead of masking a tree defect. The generated
 * corpora compare tree and host output on every value: decimals of up to
 * 12 digits at up to 12 digits, arbitrary doubles across the exponent
 * range, and arbitrary bit patterns, subnormals and negative values
 * included, under random flags, widths and precisions up to 20. The
 * conversions covered are C17 7.21.6.1p6 (the 0 flag pads with zeros
 * after the sign and the 0x prefix unless - is given, and an infinity or
 * a NaN is padded with spaces), p8 (the sign of a negative zero, the
 * default precision of six whatever the l modifier) and p13 (correct
 * rounding to at most DECIMAL_DIG significant digits, and the digits of
 * the exact value beyond them, ties to even under the default rounding
 * direction of F.3). A conversion that the Thumb-1 run's newlib lacks (F)
 * goes through tree_vector, which checks the tree alone.
 */
#include <math.h>
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
	uint64_t bits;

	memcpy(&bits, &value, sizeof bits);
	failures++;
	if (shown++ < 25)
		printf("FAIL %s: format \"%s\" value %.17g bits 0x%016llx: "
		    "tree \"%s\", want \"%s\"\n", what, format, value,
		    (unsigned long long)bits, got, want);
}

/*
 * C17 7.21.6.1p13 asks for a correctly rounded result only up to
 * DECIMAL_DIG (17 for a double) significant digits; beyond them any decimal
 * between the two neighbors with 17 digits is allowed, so a host libc that
 * prints another such decimal is conforming and cannot judge the tree. The
 * macOS libc is not assumed to print the exact digits there, so a build with
 * HOST_DIGITS_LIMITED, defined by default under __APPLE__, asks the host
 * only for results of at most 17 significant digits. The tree is held to the
 * exact digits by the literals of the vectors, which CPython computed, and by
 * the Linux and Thumb-1 runs.
 */
#if defined(__APPLE__) && !defined(HOST_DIGITS_LIMITED)
#define HOST_DIGITS_LIMITED 1
#endif
#ifdef HOST_DIGITS_LIMITED
#define DECIMAL_DIG_DOUBLE 17
static unsigned long host_skipped;
#endif

/*
 * The number of significant digits text prints before any exponent: the
 * digits from the first nonzero one to the end of the mantissa, trailing
 * zeros included.
 */
#ifdef HOST_DIGITS_LIMITED
static int
significant_digits(const char *text)
{
	int count = 0, started = 0;

	for (; *text != '\0' && *text != 'e' && *text != 'E' && *text != 'p' &&
	    *text != 'P'; text++) {
		if (*text < '0' || *text > '9')
			continue;
		if (*text != '0')
			started = 1;
		count += started;
	}
	return count;
}
#endif

#ifdef HOST_G_TIE_BUG
/*
 * Whether rounding the exact decimal digits of value to the precision of
 * format meets an exact tie. The tree rounds a tie to even and removes the
 * zero a %g result may end in; newlib keeps that zero ("%.2g" of 8050 is
 * "8.0e+03"), so the Thumb-1 run cannot judge a %g tie against it. The
 * fixed vectors pin the ties against literals. "%.60e" is exact for the
 * values drawn here: their binary expansions end well inside 60 digits.
 */
static int
host_cannot_judge(const char *format, double value)
{
	char text[96], digits[96];
	const char *dot = strchr(format, '.');
	size_t n = strlen(format), used = 0;
	int precision;

	if ((format[n - 1] != 'g' && format[n - 1] != 'G') || dot == NULL || value == 0)
		return 0;
	precision = atoi(dot + 1);
	if (precision == 0)
		precision = 1;
	snprintf(text, sizeof text, "%.60e", value);
	for (const char *c = text; *c != 'e' && *c != '\0'; c++)
		if (*c >= '0' && *c <= '9')
			digits[used++] = *c;
	digits[used] = '\0';
	if (precision >= (int)used || digits[precision] != '5')
		return 0;
	for (size_t i = (size_t)precision + 1; i < used; i++)
		if (digits[i] != '0')
			return 0;
	return 1;
}
#endif

/* Compare the tree and host conversions of one value under one format. */
static void
compare(const char *what, const char *format, double value)
{
	char got[512], want[512];

#ifdef HOST_G_TIE_BUG
	if (host_cannot_judge(format, value))
		return;
#endif
	tree_snprintf(got, sizeof got, format, value);
	snprintf(want, sizeof want, format, value);
#ifdef HOST_DIGITS_LIMITED
	if (significant_digits(want) > DECIMAL_DIG_DOUBLE) {
		host_skipped++;
		return;
	}
#endif
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
#ifdef HOST_DIGITS_LIMITED
	if (significant_digits(expected) > DECIMAL_DIG_DOUBLE) {
		host_skipped++;
		return;
	}
#endif
	if (strcmp(want, expected) != 0) {
		uint64_t bits;

		memcpy(&bits, &value, sizeof bits);
		failures++;
		printf("FAIL vector oracle: format \"%s\" value %.17g bits "
		    "0x%016llx: host \"%s\", literal \"%s\"\n", format, value,
		    (unsigned long long)bits, want, expected);
	}
}

/*
 * A vector whose expected text is head, zeros zeros, then tail: a precision
 * past what the conversion buffer holds, where the digits of the exact
 * value are followed by zeros that the caller prints.
 */
static void
filled_vector(const char *format, double value, const char *head, int zeros,
    const char *tail)
{
	char expected[1024];
	size_t length = strlen(head);

	memcpy(expected, head, length);
	memset(expected + length, '0', (size_t)zeros);
	strcpy(expected + length + (size_t)zeros, tail);
	vector(format, value, expected);
}

/* The tree result must equal the literal; the host lacks the conversion. */
static void
tree_vector(const char *format, double value, const char *expected)
{
	char got[512];

	tree_snprintf(got, sizeof got, format, value);
	compared++;
	if (strcmp(got, expected) != 0)
		fail("tree vector", format, value, got, expected);
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

int
main(void)
{
	static const int precisions[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 20 };
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
	 * C17 7.21.6.1 conformance vectors: the 0 flag, the sign of a negative
	 * zero, exact ties, the digits past the 11th, 15th and 17th
	 * significant digit, the default precision under l, and F.
	 */
	vector("%010.2f", 3.14159, "0000003.14");
	vector("%+010.2f", 3.14159, "+000003.14");
	vector("% 010.2f", 3.14159, " 000003.14");
	vector("%-010.2f|", 3.14159, "3.14      |");
	vector("%010.2f", -3.14159, "-000003.14");
	vector("%010.2e", 3.14159, "003.14e+00");
	vector("%010.3g", 3.14159, "0000003.14");
	vector("%010.2E", 3.14159, "003.14E+00");
	vector("%010.3G", 3.1415e-05, "003.14E-05");
	tree_vector("%012.4F", -2.5, "-000002.5000");
	vector("%#010.0f", 3.0, "000000003.");
	vector("%010.0f", 2.5, "0000000002");
	vector("%010f", 3.14159, "003.141590");
	vector("%0-10.2f|", 3.14159, "3.14      |");
	vector("%+-010.2e|", 3.14159, "+3.14e+00 |");
	vector("%08.3g", 1234567.0, "1.23e+06");
	vector("%08.3g", -0.0001234, "-0.000123");
	vector("%0+8.1f", 0.25, "+00000.2");
	vector("%010.2f", 10000000000.0, "10000000000.00");
	vector("%05.1f", 123456.0, "123456.0");
	vector("%.0e", 0.0, "0e+00");
	vector("%g", -0.0, "-0");
	vector("%f", -0.0, "-0.000000");
	vector("%e", -0.0, "-0.000000e+00");
	vector("%G", -0.0, "-0");
	vector("%E", -0.0, "-0.000000E+00");
	tree_vector("%F", -0.0, "-0.000000");
	vector("%.0f", -0.0, "-0");
	vector("%#g", -0.0, "-0.00000");
	vector("%+g", -0.0, "-0");
	vector("% g", -0.0, "-0");
	vector("%8.2f|", -0.0, "   -0.00|");
	vector("%-8.2f|", -0.0, "-0.00   |");
	vector("%08.2f", -0.0, "-0000.00");
	vector("%.0f", -0.4, "-0");
	vector("%.1f", -0.04, "-0.0");
	vector("%.3f", -0.0004, "-0.000");
	vector("%.2e", -0.0, "-0.00e+00");
	vector("%.3g", -0.0, "-0");
	vector("%5.1f", -0.04, " -0.0");
	vector("%.0f", -0.5, "-0");
	vector("%.0f", -1.5, "-2");
	vector("%.0f", 0.5, "0");
	vector("%.0f", 1.5, "2");
	vector("%.0f", 2.5, "2");
	vector("%.0f", 3.5, "4");
	vector("%.0f", 4.5, "4");
	vector("%.0f", 0.5000000000000001, "1");
	vector("%.0f", 0.49999999999999994, "0");
	vector("%.1f", 0.25, "0.2");
	vector("%.1f", 0.75, "0.8");
	vector("%.1f", 0.35, "0.3");
	vector("%.1f", 0.05, "0.1");
	vector("%.2f", 0.125, "0.12");
	vector("%.2f", 0.375, "0.38");
	vector("%.2f", 1.005, "1.00");
	vector("%.2f", 2.675, "2.67");
	vector("%.3f", 1.0005, "1.000");
	vector("%.0e", 2.5, "2e+00");
	vector("%.0e", 3.5, "4e+00");
	vector("%.0e", 4.5, "4e+00");
	vector("%.1e", 1.25, "1.2e+00");
	vector("%.1e", 1.35, "1.4e+00");
	vector("%.2e", 1.125, "1.12e+00");
	vector("%.2e", 1.375, "1.38e+00");
	vector("%.3g", 1.125, "1.12");
	vector("%.3g", 1.375, "1.38");
	vector("%.2g", 2.5, "2.5");
	vector("%.1g", 2.5, "2");
	vector("%.1g", 3.5, "4");
	vector("%.0f", 1e+22, "10000000000000000000000");
	vector("%.0f", 4503599627370498.0, "4503599627370498");
	vector("%.0f", 9007199254740991.0, "9007199254740991");
	vector("%.0f", 1500000000000000.0, "1500000000000000");
	vector("%.1f", 9.95, "9.9");
	vector("%.1f", 9.25, "9.2");
	vector("%.1f", 99.95, "100.0");
	vector("%.0f", 9.5, "10");
	vector("%.0f", 0.9999999999999999, "1");
	vector("%.15e", 0.1, "1.000000000000000e-01");
	vector("%.16e", 0.1, "1.0000000000000001e-01");
	vector("%.17e", 0.1, "1.00000000000000006e-01");
	vector("%.20e", 0.1, "1.00000000000000005551e-01");
	vector("%.12e", 0.3333333333333333, "3.333333333333e-01");
	vector("%.15e", 0.3333333333333333, "3.333333333333333e-01");
	vector("%.16e", 0.6666666666666666, "6.6666666666666663e-01");
	vector("%.12f", 3.141592653589793, "3.141592653590");
	vector("%.15f", 3.141592653589793, "3.141592653589793");
	vector("%.17f", 3.141592653589793, "3.14159265358979312");
	vector("%.20f", 0.1, "0.10000000000000000555");
	vector("%.25f", 0.1, "0.1000000000000000055511151");
	vector("%.40f", 0.1, "0.1000000000000000055511151231257827021182");
	vector("%.30f", 1e-10, "0.000000000100000000000000003643");
	vector("%.12g", 0.30000000000000004, "0.3");
	vector("%.15g", 0.30000000000000004, "0.3");
	vector("%.16g", 0.30000000000000004, "0.3");
	vector("%.17g", 0.30000000000000004, "0.30000000000000004");
	vector("%.17g", 0.1, "0.10000000000000001");
	vector("%.17g", 0.3333333333333333, "0.33333333333333331");
	vector("%.20g", 0.1, "0.10000000000000000555");
	vector("%.30g", 0.1, "0.100000000000000005551115123126");
	vector("%.25g", 1e+23, "99999999999999991611392");
	vector("%.17g", 1e+23, "9.9999999999999992e+22");
	vector("%.16g", 1e+23, "9.999999999999999e+22");
	vector("%.12e", 123456789012345.0, "1.234567890123e+14");
	vector("%.12g", 123456789012.5, "123456789012");
	vector("%.11e", 2.5e-05, "2.50000000000e-05");
	vector("%.13e", 1.5e+300, "1.5000000000000e+300");
	vector("%f", 1e+23, "99999999999999991611392.000000");
	vector("%.0f", 1e+23, "99999999999999991611392");
	vector("%.3f", 123456789.12345679, "123456789.123");
	vector("%.15f", 0.30000000000000004, "0.300000000000000");
	vector("%.18e", 1e-300, "1.000000000000000025e-300");
	vector("%.14e", 4.35, "4.35000000000000e+00");
	vector("%.1f", 1000000000000000.5, "1000000000000000.5");
	vector("%.10e", 9.999999999995, "1.0000000000e+01");
	vector("%.17g", 5e-324, "4.9406564584124654e-324");
	vector("%.17g", 2.2250738585072014e-308, "2.2250738585072014e-308");
	vector("%.17g", 1.7976931348623157e+308, "1.7976931348623157e+308");
	vector("%.20e", 5e-324, "4.94065645841246544177e-324");
	vector("%.20e", 1.7976931348623157e+308, "1.79769313486231570815e+308");
	vector("%.3e", 1.7976931348623157e+308, "1.798e+308");
	vector("%.3e", 2.225073858507201e-308, "2.225e-308");
	vector("%.0f", 1.7976931348623157e+308, "1797693134862315708145274237317043567980705675258449965989174768"
	    "0315726078002853876058955863276687817154045895351438246423432132"
	    "6889464182768467546703537516986049910576551282076245490090389328"
	    "9440758685084551339423045832369032229481658085593321233482747978"
	    "26204144723168738177180919299881250404026184124858368");
	vector("%.1f", 5e-324, "0.0");
	vector("%.50f", 5e-324, "0.00000000000000000000000000000000000000000000000000");
	vector("%lf", 0.3333333333333333, "0.333333");
	vector("%lg", 0.3333333333333333, "0.333333");
	vector("%le", 0.3333333333333333, "3.333333e-01");
	tree_vector("%lF", 0.6666666666666666, "0.666667");
	vector("%lG", 0.3333333333333333, "0.333333");
	vector("%10lg|", 3.5, "       3.5|");
	vector("%-10lg|", 3.5, "3.5       |");
	vector("%10lf|", 3.5, "  3.500000|");
	vector("%-12le|", 3.5, "3.500000e+00|");
	vector("%+lg", 3.142857142857143, "+3.14286");
	vector("%.3lf", 0.3333333333333333, "0.333");
	vector("%lg", 1e-05, "1e-05");
	vector("%lg", 123456789.0, "1.23457e+08");
	tree_vector("%F", 1.5, "1.500000");
	tree_vector("%.2F", 123.456, "123.46");
	vector("%E", 12345.678, "1.234568E+04");
	vector("%.3E", 1e-10, "1.000E-10");
	vector("%G", 1e-05, "1E-05");
	vector("%G", 1e+20, "1E+20");
	vector("%#.3G", 100.0, "100.");
	tree_vector("%F", 0.0, "0.000000");
	tree_vector("%F", 99999.9999995, "99999.999999");
	vector("%.17g", 0.1, "0.10000000000000001");
	vector("%.18g", 0.1, "0.100000000000000006");
	vector("%.19g", 0.1, "0.1000000000000000056");
	vector("%.20g", 0.3, "0.2999999999999999889");
	vector("%.22g", 0.3333333333333333, "0.3333333333333333148296");
	vector("%#.20g", 0.1, "0.10000000000000000555");
	vector("%.24g", 1e+22, "10000000000000000000000");
	vector("%#.30g", 0.5, "0.500000000000000000000000000000");
	vector("%.40g", 5e-324, "4.940656458412465441765687928682213723651e-324");

	/*
	 * C17 7.21.6.1p6: the 0 flag does not pad an infinity or a NaN. The
	 * host is not asked: a newlib 0 flag pads them with zeros.
	 */
	tree_vector("%010f", (double)INFINITY, "       inf");
	tree_vector("%+010E", (double)INFINITY, "      +INF");
	tree_vector("%010g", -(double)INFINITY, "      -inf");
	tree_vector("%010.3G", (double)NAN, "       NAN");
	tree_vector("%-010f|", (double)NAN, "nan       |");

	/* a precision past the conversion buffer: exact digits, then zeros */
	filled_vector("%#.330g", 0.5, "0.5", 329, "");
	filled_vector("%#.330g", 1e20, "100000000000000000000.", 309, "");
	filled_vector("%#.330g", 1e-5,
	    "1.0000000000000000818030539140313095458623138256371021270751953125", 265, "e-05");
	filled_vector("%#.340G", 123.5, "123.5", 336, "");
	filled_vector("%#.350e", 0.5, "5.", 350, "e-01");
	filled_vector("%.350e", -1.0, "-1.", 350, "e+00");
	filled_vector("%.400f", 0.5, "0.5", 399, "");
	filled_vector("%.400f", 2.25, "2.25", 398, "");

	/*
	 * Generated corpus. Each value is the double nearest to a decimal
	 * of up to 12 digits scaled by a power of ten, so the digits to
	 * reproduce are short and a precision below the digit count meets
	 * the rounding of a near tie, which the exact binary value decides.
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
	 * included, at precisions up to the digits a double needs to
	 * round-trip and beyond.
	 */
	for (int iteration = 0; iteration < 4000 / PRINTF_G_ITERATIONS_DIVISOR; iteration++) {
		uint64_t bits = ((uint64_t)rng() << 32) | rng();
		uint64_t biased = (bits >> 52) % 2047;
		double value;

		bits = (bits & 0x800fffffffffffffULL) | (biased << 52);
		memcpy(&value, &bits, sizeof value);
		for (int precision = 0; precision <= 20; precision++) {
			snprintf(format, sizeof format, "%%.%dg", precision);
			compare("random g", format, value);
			snprintf(format, sizeof format, "%%#.%dG", precision);
			compare("random G", format, value);
		}
	}

	/*
	 * Every decimal conversion under random flags, widths and
	 * precisions, over arbitrary bit patterns: a quarter are negative,
	 * a quarter subnormal, and some are exact binary fractions, which
	 * produce the ties. The 0 flag meets every conversion, the - flag
	 * that overrides it, and the sign flags.
	 */
	for (int iteration = 0; iteration < 3000 / PRINTF_G_ITERATIONS_DIVISOR; iteration++) {
		static const char conversions[] = "eEfgG";
		static const char *const zflags[] = {
			"", "0", "-", "-0", "+", "+0", " ", " 0", "#", "#0", "+-", "0+ "
		};
		uint64_t bits = ((uint64_t)rng() << 32) | rng();
		unsigned kind = rng() % 4;
		double value;

		if (kind == 0)
			bits &= 0x800fffffffffffffULL;
		else if (kind == 1)
			bits = (bits & 0x8000000000000000ULL) |
			    ((uint64_t)(1000 + rng() % 46) << 52) |
			    (bits & 0xfffffffff0000ULL);
		else if (kind == 2)
			bits = (bits & 0x8000000000000000ULL) |
			    ((uint64_t)(1023 + rng() % 12) << 52) |
			    (bits & 0xfc00000000000ULL);
		else
			bits = (bits & 0x800fffffffffffffULL) |
			    ((uint64_t)(rng() % 2047) << 52);
		memcpy(&value, &bits, sizeof value);
		for (int n = 0; n < 6; n++) {
			const char *flag = zflags[rng() % (sizeof zflags / sizeof zflags[0])];
			int width = rng() % 3 == 0 ? 0 : 1 + (int)(rng() % 30);
			int precision = (int)(rng() % 21);
			char conversion = conversions[rng() % (sizeof conversions - 1)];

			if (width == 0)
				snprintf(format, sizeof format, "%%%s.%d%c", flag,
				    precision, conversion);
			else
				snprintf(format, sizeof format, "%%%s%d.%d%c", flag,
				    width, precision, conversion);
			compare("random flags", format, value);
		}
	}

	if (failures != 0) {
		printf("printf %%g contract: %d of %lu comparisons failed\n",
		    failures, compared);
		return 1;
	}
#ifdef HOST_DIGITS_LIMITED
	printf("host comparisons beyond %d significant digits skipped: %lu\n",
	    DECIMAL_DIG_DOUBLE, host_skipped);
#endif
	printf("printf %%g contract tests passed (%lu comparisons)\n", compared);
	return 0;
}
