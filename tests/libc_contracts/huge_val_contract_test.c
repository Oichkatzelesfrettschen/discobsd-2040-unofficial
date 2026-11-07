/*
 * HUGE_VAL and the overflow returns of the tree's math library, C17 7.12
 * and 7.22.1.3: HUGE_VAL is a positive double, and under IEEE 754 (Annex
 * F) it is +infinity, so an overflowing result must be an infinity rather
 * than a large finite number. lib/libm, lib/libc/gen (ldexp, frexp, atof)
 * and lib/libc/stdlib (strtod) are compiled from the tree under tree_
 * names; the formatter is the one printf_g_contract_test compares with
 * the host snprintf, here held to the C17 text of an infinity and of a
 * NaN.
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tree_snprintf(char *, size_t, const char *, ...);
extern int tree_errno;

double tree_exp(double), tree_log(double), tree_log10(double);
double tree_pow(double, double);
double tree_sinh(double), tree_cosh(double), tree_tan(double);
double tree_sqrt(double), tree_ldexp(double, int), tree_frexp(double, int *);
double tree_y0(double), tree_y1(double), tree_yn(int, double);
double tree_atof(const char *), tree_strtod(const char *, char **);

double header_huge_val(void);
float header_huge_valf(void);
long double header_huge_vall(void);
float header_infinity(void);
int header_types_match(void);
int header_edom(void);
int header_erange(void);

static int failures;
static unsigned long checks;

static void
check(int condition, const char *what)
{
	checks++;
	if (!condition) {
		failures++;
		printf("FAIL %s\n", what);
	}
}

static volatile double zero = 0.0;

static int
is_inf(double x, int sign)
{
	return x == x && x - x != 0.0 && (sign > 0 ? x > 0.0 : x < 0.0);
}

static void
check_call(const char *what, double value, int sign, int errno_expected)
{
	char message[128];

	snprintf(message, sizeof message, "%s is %cinfinity (got %.17g)", what,
	    sign > 0 ? '+' : '-', value);
	check(is_inf(value, sign), message);
	if (errno_expected != 0) {
		snprintf(message, sizeof message, "%s sets errno %d (got %d)",
		    what, errno_expected, tree_errno);
		check(tree_errno == errno_expected, message);
	}
}

/*
 * A finite result within a relative tolerance of an exact value. The exact
 * values come from the decimal module's exp at 60 digits, rounded to the
 * nearest double, and agree with CPython's math.exp.
 */
static void
check_near(const char *what, double value, double want, double tolerance)
{
	char message[128];
	double error = (value - want) / want;

	snprintf(message, sizeof message, "%s is %.17g, exact %.17g", what,
	    value, want);
	check(value == value && value - value == 0.0 &&
	    (error < 0.0 ? -error : error) <= tolerance, message);
}

#define CALL(expression, sign, errno_expected) \
	do { \
		tree_errno = 0; \
		check_call(#expression, (expression), (sign), (errno_expected)); \
	} while (0)

/*
 * The text C17 7.21.6.1p8 gives an infinity or a NaN under a floating
 * conversion: "inf" or "nan", in capitals for F, E, G and A, signed as a
 * number is (a leading '-', or '+' or ' ' by flag), padded with spaces to
 * the width on the side '-' selects; precision, '#' and '0' do not apply.
 * The expectation is computed here rather than taken from the host's
 * snprintf, because the newlib the Thumb-1 run links has no %F or %a.
 */
static void
expected_text(char *out, size_t size, const char *format, double value)
{
	const char *p = format + 1, *word;
	char body[16];
	int left = 0, plus = 0, space = 0, width = 0, upper;

	for (;; p++) {
		if (*p == '-')
			left = 1;
		else if (*p == '+')
			plus = 1;
		else if (*p == ' ')
			space = 1;
		else if (*p != '#' && *p != '0')
			break;
	}
	while (*p >= '0' && *p <= '9')
		width = width * 10 + (*p++ - '0');
	if (*p == '.')
		for (p++; *p >= '0' && *p <= '9'; p++)
			continue;
	upper = *p == 'F' || *p == 'E' || *p == 'G' || *p == 'A';
	if (isnan(value))
		word = upper ? "NAN" : "nan";
	else
		word = upper ? "INF" : "inf";
	snprintf(body, sizeof body, "%s%s",
	    !isnan(value) && value < 0 ? "-" : plus ? "+" : space ? " " : "",
	    word);
	snprintf(out, size, left ? "%-*s%s" : "%*s%s", width, body, p + 1);
}

static void
compare_print(const char *format, double value)
{
	char got[64], want[64], message[256];

	tree_snprintf(got, sizeof got, format, value);
	expected_text(want, sizeof want, format, value);
	snprintf(message, sizeof message,
	    "%s of %g prints \"%s\", C17 gives \"%s\"", format, value, got,
	    want);
	check(strcmp(got, want) == 0, message);
}

int
main(void)
{
	static const char *const formats[] = {
		"%f", "%F", "%e", "%E", "%g", "%G", "%a", "%A", "%.0f",
		"%.3e", "%#f", "%#g", "%8f|", "%-8f|", "%08f", "%+f",
		"% f", "%+8.2e", "%-+10g|", "%010G", "%3f", "%1.1f",
	};
	const double inf = (double)INFINITY;
	int erange = header_erange();
	int edom = header_edom();
	size_t i;

	/* The header: type, sign, magnitude and use as a constant. */
	check(header_types_match(), "HUGE_VAL, HUGE_VALF, HUGE_VALL and INFINITY have the C17 types");
	check(is_inf(header_huge_val(), 1), "HUGE_VAL is +infinity");
	check(header_huge_val() > DBL_MAX, "HUGE_VAL exceeds DBL_MAX");
	check(is_inf(-header_huge_val(), -1), "-HUGE_VAL is -infinity");
	check(header_huge_valf() > FLT_MAX && header_huge_valf() - header_huge_valf() != 0.0f,
	    "HUGE_VALF is a float infinity");
	check(header_huge_vall() > LDBL_MAX, "HUGE_VALL is a long double infinity");
	check(header_infinity() > FLT_MAX && header_infinity() == header_huge_valf(),
	    "INFINITY is the float infinity");

	/* Overflow returns of the tree libm are infinities. */
	CALL(tree_exp(710.0), 1, erange);
	CALL(tree_exp(709.9), 1, erange);
	CALL(tree_exp(1000.0), 1, erange);
	check(tree_exp(-1000.0) == 0.0, "exp(-1000) underflows to zero");
	check(tree_exp(709.0) > 8.2e307 && tree_exp(709.0) < 8.3e307,
	    "exp(709) stays finite");

	/*
	 * C17 7.12.6.1: exp overflows above ln(DBL_MAX) =
	 * 709.782712893384 and not earlier, and is accurate across its range;
	 * reduction by arg * log2e alone is 5e-14 off near 706.
	 */
	CALL(tree_exp(709.7827128933841), 1, erange);
	CALL(tree_exp(709.783), 1, erange);
	CALL(tree_exp(769.9), 1, erange);
	CALL(tree_exp(771.0), 1, erange);
	CALL(tree_exp(1e30), 1, erange);
	tree_errno = 0;
	check_near("exp(ln DBL_MAX)", tree_exp(709.782712893384),
	    1.7976931348622732e+308, 2e-15);
	check(tree_errno == 0, "exp(ln DBL_MAX) leaves errno alone");
	check_near("exp(709)", tree_exp(709.0), 8.218407461554972e+307, 2e-15);
	check_near("exp(706.73)", tree_exp(706.73), 8.490615916644771e+306, 2e-15);
	check_near("exp(700)", tree_exp(700.0), 1.0142320547350045e+304, 2e-15);
	check_near("exp(500)", tree_exp(500.0), 1.4035922178528375e+217, 2e-15);
	check_near("exp(100)", tree_exp(100.0), 2.6881171418161356e+43, 2e-15);
	check_near("exp(10)", tree_exp(10.0), 22026.465794806718, 2e-15);
	check_near("exp(1)", tree_exp(1.0), 2.718281828459045, 2e-15);
	check_near("exp(0.5)", tree_exp(0.5), 1.6487212707001282, 2e-15);
	check_near("exp(-1)", tree_exp(-1.0), 0.36787944117144233, 2e-15);
	check_near("exp(-300)", tree_exp(-300.0), 5.148200222412013e-131, 2e-15);
	check_near("exp(-700)", tree_exp(-700.0), 9.85967654375977e-305, 2e-15);
	/* the underflow side: results down to 2^-1024 are computed, the rest are zero */
	check_near("exp(-709.5)", tree_exp(-709.5), 7.38014831401258e-309, 1e-14);
	check(tree_exp(-745.2) == 0.0, "exp(-745.2) underflows to zero");
	check(tree_exp(-800.0) == 0.0, "exp(-800) underflows to zero");
	check(tree_exp(-1e30) == 0.0, "exp(-1e30) underflows to zero");
	CALL(tree_pow(10.0, 400.0), 1, erange);
	CALL(tree_pow(-10.0, 401.0), -1, erange);
	CALL(tree_cosh(1000.0), 1, erange);
	CALL(tree_cosh(-1000.0), 1, erange);
	CALL(tree_sinh(1000.0), 1, erange);
	CALL(tree_sinh(-1000.0), -1, erange);
	CALL(tree_ldexp(1.0, 2000), 1, erange);
	CALL(tree_ldexp(-1.0, 2000), -1, erange);
	CALL(tree_ldexp(DBL_MAX, 1), 1, erange);

	/*
	 * C17 7.12.1p4: a pole is a range error, ERANGE under this libc, and
	 * Annex F returns an infinity of the sign of the limit; a negative
	 * argument is a domain error, EDOM.
	 */
	CALL(tree_log(0.0), -1, erange);
	CALL(tree_log(-1.0), -1, edom);
	CALL(tree_log10(0.0), -1, erange);
	CALL(tree_log10(-1.0), -1, edom);
	CALL(tree_y0(0.0), -1, erange);
	CALL(tree_y1(0.0), -1, erange);
	CALL(tree_yn(2, 0.0), -1, erange);
	CALL(tree_yn(-1, 0.0), -1, erange);
	CALL(tree_y0(-1.0), -1, edom);
	CALL(tree_y1(-1.0), -1, edom);
	CALL(tree_yn(2, -3.0), -1, edom);
	CALL(tree_pow(0.0, -1.0), 1, erange);
	CALL(tree_pow(0.0, -2.5), 1, erange);
	/* F.10.4.4: an odd integer power keeps the sign of the zero. */
	CALL(tree_pow(-0.0, -1.0), -1, erange);
	CALL(tree_pow(-0.0, -3.0), -1, erange);
	CALL(tree_pow(-0.0, -2.0), 1, erange);
	CALL(tree_pow(-0.0, -2.5), 1, erange);
	CALL(tree_pow(-0.0, -1e300), 1, erange);
	tree_errno = 0;
	check(tree_pow(-0.0, 3.0) == 0.0 && 1.0 / tree_pow(-0.0, 3.0) < 0.0 &&
	    tree_errno == 0, "pow(-0, 3) is -0 and leaves errno alone");
	check(1.0 / tree_pow(-0.0, 2.0) > 0.0 && 1.0 / tree_pow(-0.0, 0.5) > 0.0 &&
	    1.0 / tree_pow(-0.0, 1e300) > 0.0,
	    "pow(-0, y) is +0 for y not an odd integer");
	check(1.0 / tree_pow(0.0, 3.0) > 0.0, "pow(+0, 3) is +0");
	tree_errno = 0;
	check(tree_pow(0.0, 0.0) == 1.0 && tree_errno == 0,
	    "pow(0, 0) is 1 and leaves errno alone");
	tree_errno = 0;
	check(tree_pow(0.0, 2.0) == 0.0 && tree_pow(0.0, 0.5) == 0.0 &&
	    tree_errno == 0, "pow(0, y) is 0 for positive y and leaves errno alone");
	check(tree_ldexp(1.0, -2000) == 0.0, "ldexp(1, -2000) underflows to zero");
	check(tree_ldexp(1.0, 10) == 1024.0, "ldexp(1, 10) is 1024");

	/* Infinities and NaNs pass through frexp and ldexp without a loop. */
	{
		int e = 7;

		check(is_inf(tree_frexp(inf, &e), 1) && e == 0, "frexp(+inf) returns +inf and exponent 0");
		check(is_inf(tree_frexp(-inf, &e), -1) && e == 0, "frexp(-inf) returns -inf and exponent 0");
		e = 7;
		check(tree_frexp(zero / zero, &e) != tree_frexp(zero / zero, &e) && e == 0,
		    "frexp(nan) returns a NaN and exponent 0");
		check(tree_frexp(8.0, &e) == 0.5 && e == 4, "frexp(8) is 0.5 * 2^4");
	}
	check(is_inf(tree_ldexp(inf, 1), 1), "ldexp(+inf, 1) is +inf");
	check(is_inf(tree_ldexp(-inf, -2000), -1), "ldexp(-inf, -2000) is -inf");
	check(tree_ldexp(zero / zero, 3) != tree_ldexp(zero / zero, 3), "ldexp(nan, 3) is a NaN");
	check(is_inf(tree_sqrt(inf), 1), "sqrt(+inf) is +inf");
	check(is_inf(tree_log(inf), 1), "log(+inf) is +inf");
	check(is_inf(tree_exp(inf), 1), "exp(+inf) is +inf");
	check(tree_exp(-inf) == 0.0, "exp(-inf) is zero");
	check(is_inf(tree_pow(2.0, inf), 1), "pow(2, +inf) is +inf");
	check(is_inf(tree_pow(inf, 2.0), 1), "pow(+inf, 2) is +inf");
	check(is_inf(tree_cosh(inf), 1), "cosh(+inf) is +inf");

	/* Decimal text that overflows a double converts to an infinity. */
	check(is_inf(tree_atof("1e400"), 1), "atof(\"1e400\") is +inf");
	check(is_inf(tree_atof("-1e400"), -1), "atof(\"-1e400\") is -inf");
	check(is_inf(tree_atof("1e999"), 1), "atof(\"1e999\") is +inf");
	check(is_inf(tree_atof("-1e99999"), -1), "atof(\"-1e99999\") is -inf");
	check(tree_atof("1e308") > 9.99e307 && tree_atof("1e308") < 1.01e308,
	    "atof(\"1e308\") is finite");
	check(tree_atof("1e-999") == 0.0, "atof(\"1e-999\") underflows to zero");
	check(tree_atof("0e999") == 0.0, "atof(\"0e999\") is zero, not a NaN");
	tree_errno = 0;
	check(is_inf(tree_strtod("1e400", NULL), 1) && tree_errno == erange,
	    "strtod(\"1e400\") is +inf with ERANGE");
	tree_errno = 0;
	check(is_inf(tree_strtod("-1e400", NULL), -1) && tree_errno == erange,
	    "strtod(\"-1e400\") is -inf with ERANGE");
	tree_errno = 0;
	check(is_inf(tree_strtod("1e999", NULL), 1) && tree_errno == erange,
	    "strtod(\"1e999\") is +inf with ERANGE");
	tree_errno = 0;
	check(tree_strtod("1.7e308", NULL) > 1.69e308 && tree_errno == 0,
	    "strtod(\"1.7e308\") is finite and leaves errno alone");
	check(tree_strtod("0e999", NULL) == 0.0, "strtod(\"0e999\") is zero, not a NaN");
	check(tree_strtod("1e-999", NULL) == 0.0, "strtod(\"1e-999\") underflows to zero");

	/* The text of an infinity and of a NaN under every conversion. */
	for (i = 0; i < sizeof formats / sizeof formats[0]; i++) {
		compare_print(formats[i], inf);
		compare_print(formats[i], -inf);
		compare_print(formats[i], (double)NAN);
	}

	if (failures != 0) {
		printf("huge-val contract: %d of %lu checks failed\n", failures, checks);
		return 1;
	}
	printf("huge-val contract tests passed (%lu checks)\n", checks);
	return 0;
}
