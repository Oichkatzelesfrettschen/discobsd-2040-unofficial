/*
 * Acceptance test for libcalc. Every expected value comes from section 5 of
 * docs/research/picomite-maths-clean-room-spec.md (the complex table through
 * complex_vectors.h); the vectors marked "extra" are additional checks of
 * behavior the specification states in prose.
 *
 * Tolerances follow the specification: 1e-12 relative to max(1, |expected|)
 * unless a check names its own. A failed check prints a line that starts
 * with "FAIL " and the program exits 1.
 */

/* sigsetjmp and sigaction are POSIX; the NULL-pointer sweep recovers from a fault with them. */
#define _POSIX_C_SOURCE 200809L

#include <complex.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CALC_TEST_NO_SIGNALS
#include <setjmp.h>
#include <signal.h>
#endif

#include "mathematics.h"
#include "calc_internal.h"

static unsigned long checks;
static unsigned long failures;

#define TOL 1e-12

static int
near(double got, double want, double tol)
{
	double scale = fabs(want) > 1.0 ? fabs(want) : 1.0;

	if (got != got || want != want)
		return got != got && want != want;
	if (got == want)
		return 1;
	return fabs(got - want) <= tol * scale;
}

#define CHECK(cond) do {							\
	checks++;								\
	if (!(cond)) {								\
		failures++;							\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);		\
	}									\
} while (0)

#define CHECK_TOL(got, want, tol) do {						\
	double g_ = (got), w_ = (want);						\
	checks++;								\
	if (!near(g_, w_, (tol))) {						\
		failures++;							\
		printf("FAIL %s:%d: %s = %.17g, expected %.17g\n", __FILE__,	\
		    __LINE__, #got, g_, w_);					\
	}									\
} while (0)

#define CHECK_NEAR(got, want) CHECK_TOL(got, want, TOL)

#define CHECK_ST(expr, want) do {						\
	calc_status s_ = (expr);						\
	checks++;								\
	if (s_ != (want)) {							\
		failures++;							\
		printf("FAIL %s:%d: %s returned %d, expected %d\n", __FILE__,	\
		    __LINE__, #expr, (int)s_, (int)(want));			\
	}									\
} while (0)

/* Exact equality of two doubles, signed zeros included; NaN equals NaN. */
#define CHECK_EXACT(got, want) do {						\
	double g_ = (got), w_ = (want);						\
	checks++;								\
	if (!((g_ == w_ && (signbit(g_) != 0) == (signbit(w_) != 0)) ||	\
	    (g_ != g_ && w_ != w_))) {						\
		failures++;							\
		printf("FAIL %s:%d: %s = %.17g, expected exactly %.17g\n",	\
		    __FILE__, __LINE__, #got, g_, w_);				\
	}									\
} while (0)

#define CHECK_U64(got, want) do {						\
	uint64_t g_ = (got), w_ = (want);					\
	checks++;								\
	if (g_ != w_) {								\
		failures++;							\
		printf("FAIL %s:%d: %s = 0x%llx, expected 0x%llx\n", __FILE__,	\
		    __LINE__, #got, (unsigned long long)g_,			\
		    (unsigned long long)w_);					\
	}									\
} while (0)

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* --- 5.1 ATAN3 ----------------------------------------------------------- */

static void
test_atan3(void)
{
	static const struct { double y, x, rad, deg; } v[] = {
		{ 1, 1, 0.785398163397448, 45 },
		{ -1, 1, 5.49778714378214, 315 },
		{ -1, -1, 3.92699081698724, 225 },
		{ 0, -1, 3.14159265358979, 180 },
		{ -1, 0, 4.71238898038469, 270 },
		{ 0.5, 2, 0.244978663126864, 14.0362434679265 },
		{ 3, 0, 1.5707963267949, 90 },
	};
	size_t i;

	for (i = 0; i < COUNT(v); i++) {
		CHECK_NEAR(calc_atan3(v[i].y, v[i].x, CALC_RADIANS), v[i].rad);
		CHECK_NEAR(calc_atan3(v[i].y, v[i].x, CALC_DEGREES), v[i].deg);
	}
	/* extra: origin and negative zero give +0; the range is half open */
	CHECK(calc_atan3(0, 0, CALC_RADIANS) == 0 && !signbit(calc_atan3(0, 0, CALC_RADIANS)));
	CHECK(!signbit(calc_atan3(-0.0, 1, CALC_RADIANS)));
	CHECK(!signbit(calc_atan3(-0.0, 5, CALC_DEGREES)));
	CHECK(calc_atan3(-1e-300, 1, CALC_RADIANS) < 6.283185307179587);
	CHECK(calc_atan3(-1e-300, 1, CALC_DEGREES) < 360.0);
	CHECK_NEAR(calc_angle_to_rad(CALC_DEGREES, 180), 3.141592653589793);
	CHECK_NEAR(calc_angle_from_rad(CALC_DEGREES, 3.141592653589793), 180);
	CHECK(calc_angle_to_rad(CALC_RADIANS, 1.25) == 1.25);
}

/* --- 5.2 SINH COSH TANH, 5.3 LOG10 and the inverse hyperbolics ------------ */

static void
test_scalar(void)
{
	static const struct { double x, sh, ch, th; } v[] = {
		{ 0, 0, 1, 0 },
		{ 1, 1.1752011936438, 1.54308063481524, 0.761594155955765 },
		{ -2.5, -6.05020448103979, 6.13228947966369, -0.98661429815143 },
		{ 0.5, 0.521095305493747, 1.12762596520638, 0.46211715726001 },
		{ 20, 242582597.704895, 242582597.704895, 1 },
	};
	static const struct { double x, l; } lg[] = {
		{ 1000, 3 }, { 0.001, -3 }, { 2, 0.301029995663981 }, { 1, 0 },
	};
	double out;
	size_t i;

	for (i = 0; i < COUNT(v); i++) {
		CHECK_ST(calc_sinh(v[i].x, &out), CALC_OK);
		CHECK_NEAR(out, v[i].sh);
		CHECK_ST(calc_cosh(v[i].x, &out), CALC_OK);
		CHECK_NEAR(out, v[i].ch);
		CHECK_ST(calc_tanh(v[i].x, &out), CALC_OK);
		CHECK_NEAR(out, v[i].th);
	}
	/* extra: overflow is a range status, saturation and NaN are not */
	CHECK_ST(calc_sinh(1000, &out), CALC_E_RANGE);
	CHECK_ST(calc_sinh(-1000, &out), CALC_E_RANGE);
	CHECK_ST(calc_cosh(1000, &out), CALC_E_RANGE);
	CHECK_ST(calc_cosh(-710.5, &out), CALC_E_RANGE);
	CHECK_ST(calc_tanh(1000, &out), CALC_OK);
	CHECK(out == 1.0);
	CHECK_ST(calc_tanh(-1000, &out), CALC_OK);
	CHECK(out == -1.0);
	CHECK_ST(calc_sinh(700, &out), CALC_OK);
	CHECK_TOL(out, 5.071160273675022547e+303, 1e-12);
	CHECK_ST(calc_sinh(NAN, &out), CALC_OK);
	CHECK(out != out);
	CHECK_ST(calc_sinh(1e-10, &out), CALC_OK);
	CHECK_TOL(out / 1e-10, 1.0, 1e-15);
	CHECK_ST(calc_sinh(0.4999, &out), CALC_OK);
	CHECK_NEAR(out, 0.5209825455025153);
	CHECK_ST(calc_sinh(0.5001, &out), CALC_OK);
	CHECK_NEAR(out, 0.5212080706959325);

	for (i = 0; i < COUNT(lg); i++) {
		CHECK_ST(calc_log10(lg[i].x, &out), CALC_OK);
		CHECK_NEAR(out, lg[i].l);
	}
	/* extra: exact powers of ten give exact results */
	CHECK_ST(calc_log10(1000, &out), CALC_OK);
	CHECK(out == 3.0);
	CHECK_ST(calc_log10(0.001, &out), CALC_OK);
	CHECK(out == -3.0);
	CHECK_ST(calc_log10(1e22, &out), CALC_OK);
	CHECK(out == 22.0);
	{
		static const struct { double x, l; } ex[] = {
			{ 1e15, 15 }, { 1e-15, -15 }, { 1e-4, -4 }, { 1e-2, -2 }, { 1e-1, -1 },
		};

		for (i = 0; i < COUNT(ex); i++) {
			CHECK_ST(calc_log10(ex[i].x, &out), CALC_OK);
			CHECK(out == ex[i].l);
		}
	}
	CHECK_ST(calc_log10(0, &out), CALC_E_DOMAIN);
	CHECK_ST(calc_log10(-1, &out), CALC_E_DOMAIN);
	CHECK_ST(calc_log10(NAN, &out), CALC_OK);
	CHECK(out != out);

	/* extra: inverse hyperbolics against closed forms and round trips */
	CHECK_NEAR(calc_asinh(1), 0.881373587019543);
	CHECK_NEAR(calc_asinh(-1), -0.881373587019543);
	CHECK_NEAR(calc_acosh(2), 1.31695789692482);
	CHECK_NEAR(calc_atanh(0.5), 0.549306144334055);
	CHECK_NEAR(calc_atanh(-0.5), -0.549306144334055);
	CHECK_TOL(calc_asinh(1e-10) / 1e-10, 1.0, 1e-15);
	CHECK_TOL(calc_atanh(1e-10) / 1e-10, 1.0, 1e-15);
	CHECK_TOL(calc_acosh(1.0 + 1e-10) / 1.414213620867586e-5, 1.0, 1e-13);
	CHECK_NEAR(calc_asinh(1e300), 691.4686750787736);
	CHECK(calc_acosh(1) == 0.0);
	CHECK(calc_acosh(0.5) != calc_acosh(0.5));
	CHECK(calc_atanh(2) != calc_atanh(2));
	CHECK(calc_atanh(1) > 1e308 && calc_atanh(-1) < -1e308);
	for (i = 0; i < 12; i++) {
		double x = -3.0 + 0.55 * (double)i;

		CHECK_NEAR(calc_asinh(sinh(x)), x);
		CHECK_NEAR(calc_atanh(tanh(x / 2)), x / 2);
		CHECK_NEAR(calc_acosh(cosh(x)), fabs(x));
	}

	/* extra: rounding is half away from zero */
	CHECK(calc_round(2.5) == 3.0);
	CHECK(calc_round(-2.5) == -3.0);
	CHECK(calc_round(0.49999999999999994) == 0.0);
	CHECK(calc_round(-0.4) == 0.0);
	CHECK(calc_round(4503599627370497.0) == 4503599627370497.0);
	CHECK(calc_round(7.07106781186548) == 7.0);
}

/* --- 5.4 statistics ------------------------------------------------------ */

static void
test_statistics(void)
{
	static const double A[] = { 2, 4, 4, 4, 5, 5, 7, 9 };
	static const double B[] = { 1, 2, 3, 4, 5, 6 };
	static const double C[] = { 3, 1, 2 };
	static const double D[] = { -1.5, 0, 2.25, 8 };
	static const double E[] = { 10 };
	static const struct {
		const double *a;
		size_t n;
		double sum, mean, median, svar, ssd, pvar;
	} v[] = {
		{ A, 8, 40, 5, 4.5, 4.57142857142857, 2.1380899352994, 4 },
		{ B, 6, 21, 3.5, 3.5, 3.5, 1.87082869338697, 2.91666666666667 },
		{ C, 3, 6, 2, 2, 1, 1, 0.666666666666667 },
		{ D, 4, 8.75, 2.1875, 1.125, 17.390625, 4.17020682940307, 13.04296875 },
	};
	double scratch[8], out;
	size_t i, at;

	for (i = 0; i < COUNT(v); i++) {
		CHECK_ST(calc_sum(v[i].a, v[i].n, &out), CALC_OK);
		CHECK_NEAR(out, v[i].sum);
		CHECK_ST(calc_mean(v[i].a, v[i].n, &out), CALC_OK);
		CHECK_NEAR(out, v[i].mean);
		CHECK_ST(calc_median(v[i].a, v[i].n, scratch, &out), CALC_OK);
		CHECK_NEAR(out, v[i].median);
		CHECK_ST(calc_variance(v[i].a, v[i].n, 1, &out), CALC_OK);
		CHECK_NEAR(out, v[i].svar);
		CHECK_ST(calc_stddev(v[i].a, v[i].n, 1, &out), CALC_OK);
		CHECK_NEAR(out, v[i].ssd);
		CHECK_ST(calc_variance(v[i].a, v[i].n, 0, &out), CALC_OK);
		CHECK_NEAR(out, v[i].pvar);
	}
	/* the median leaves the caller's data alone */
	CHECK(C[0] == 3 && C[1] == 1 && C[2] == 2);
	CHECK_ST(calc_sum(E, 1, &out), CALC_OK);
	CHECK_NEAR(out, 10);
	CHECK_ST(calc_mean(E, 1, &out), CALC_OK);
	CHECK_NEAR(out, 10);
	CHECK_ST(calc_median(E, 1, scratch, &out), CALC_OK);
	CHECK_NEAR(out, 10);
	CHECK_ST(calc_variance(E, 1, 1, &out), CALC_E_SIZE);
	CHECK_ST(calc_stddev(E, 1, 1, &out), CALC_E_SIZE);
	CHECK_ST(calc_variance(E, 1, 0, &out), CALC_OK);
	CHECK(out == 0.0);
	CHECK_ST(calc_sum(E, 0, &out), CALC_E_SIZE);
	CHECK_ST(calc_mean(E, 0, &out), CALC_E_SIZE);
	CHECK_ST(calc_median(E, 0, scratch, &out), CALC_E_SIZE);
	CHECK_ST(calc_median(A, 8, (double *)(uintptr_t)&A[2], &out), CALC_E_ALIAS);

	/* extra: compensated summation and a large unsorted median */
	{
		static const double cancel[] = { 1e16, 1.0, -1e16, 1.0 };
		double big[101], tmp[101];

		CHECK_ST(calc_sum(cancel, 4, &out), CALC_OK);
		CHECK(out == 2.0);
		for (i = 0; i < 101; i++)
			big[i] = (double)((i * 37) % 101);
		CHECK_ST(calc_median(big, 101, tmp, &out), CALC_OK);
		CHECK(out == 50.0);
		CHECK_ST(calc_variance(big, 101, 0, &out), CALC_OK);
		CHECK_NEAR(out, 850.0);
	}

	{
		static const double m1[] = { 3, 9, 2, 9, 2 };
		static const double m2[] = { 5, -1, -1, 7 };
		static const double m3[] = { 4.5 };
		static const struct {
			const double *a;
			size_t n, imax, imin;
			double max, min;
		} mm[] = {
			{ m1, 5, 1, 2, 9, 2 },
			{ m2, 4, 3, 1, 7, -1 },
			{ m3, 1, 0, 0, 4.5, 4.5 },
		};

		for (i = 0; i < COUNT(mm); i++) {
			CHECK_ST(calc_max(mm[i].a, mm[i].n, &out, &at), CALC_OK);
			CHECK(out == mm[i].max && at == mm[i].imax);
			CHECK_ST(calc_min(mm[i].a, mm[i].n, &out, &at), CALC_OK);
			CHECK(out == mm[i].min && at == mm[i].imin);
		}
		CHECK_ST(calc_max(m1, 5, &out, NULL), CALC_OK);
		CHECK(out == 9);
		CHECK_ST(calc_min(m1, 0, &out, &at), CALC_E_SIZE);
	}
	{
		/* extra: values beyond the legacy sentinels */
		static const double hugev[] = { 1e308, 1.7e308, 1.2e308 };
		static const double lowv[] = { -1.7e308, -1.6e308 };

		CHECK_ST(calc_max(hugev, 3, &out, &at), CALC_OK);
		CHECK(out == 1.7e308 && at == 1);
		CHECK_ST(calc_min(lowv, 2, &out, &at), CALC_OK);
		CHECK(out == -1.7e308 && at == 0);
	}

	{
		static const double x1[] = { 1, 2, 3, 4, 5 };
		static const double y1[] = { 2, 4, 5, 4, 5 };
		static const double y2[] = { 5, 4, 3, 2, 1 };
		static const double x3[] = { 1, 2, 3, 4 };
		static const double y3[] = { 2, 4, 6, 8 };
		static const double x4[] = { 0.5, 1.5, -2, 4 };
		static const double y4[] = { 3, -1, 0.25, 2 };
		static const double flat[] = { 1, 1, 1 };
		static const double ramp[] = { 1, 2, 3 };

		CHECK_ST(calc_correl(x1, y1, 5, &out), CALC_OK);
		CHECK_NEAR(out, 0.774596669241483);
		CHECK_ST(calc_correl(x1, y2, 5, &out), CALC_OK);
		CHECK_NEAR(out, -1);
		CHECK_ST(calc_correl(x3, y3, 4, &out), CALC_OK);
		CHECK_NEAR(out, 1);
		CHECK_ST(calc_correl(x4, y4, 4, &out), CALC_OK);
		CHECK_NEAR(out, 0.244549555993055);
		CHECK_ST(calc_correl(flat, ramp, 3, &out), CALC_E_DOMAIN);
		CHECK_ST(calc_correl(ramp, flat, 3, &out), CALC_E_DOMAIN);
		CHECK_ST(calc_correl(ramp, ramp, 0, &out), CALC_E_SIZE);
	}
}

static void
test_chi_square(void)
{
	static const double t1[] = { 12, 5, 7, 9 };
	static const double t2[] = { 20, 15, 10, 30, 35, 40 };
	static const double t3[] = { 10, 20, 30, 20, 20, 20, 30, 20, 10 };
	static const struct {
		const double *t;
		size_t rows, cols;
		double chi;
		size_t df;
		double p;
	} v[] = {
		{ t1, 2, 2, 2.43057551968156, 1, 11.8989205532145 },
		{ t2, 2, 3, 4.76190476190476, 2, 9.246247606292 },
		{ t3, 3, 3, 20, 4, 0.0499399227387334 },
	};
	static const struct { double chi; size_t df; double p; } pv[] = {
		{ 3.84145882069412, 1, 4.99999999999999 },
		{ 9.48772903678115, 4, 5.00000000000001 },
		{ 0.5, 2, 77.8800783071405 },
		{ 20, 3, 0.0169742435552826 },
		{ 0, 5, 100 },
	};
	double chi, p;
	size_t df, i;

	for (i = 0; i < COUNT(v); i++) {
		CHECK_ST(calc_chi_square(v[i].t, v[i].rows, v[i].cols, &chi, &df), CALC_OK);
		CHECK_NEAR(chi, v[i].chi);
		CHECK(df == v[i].df);
		CHECK_ST(calc_chi_square_p(chi, df, &p), CALC_OK);
		CHECK(fabs(p - v[i].p) <= 1e-8);
	}
	for (i = 0; i < COUNT(pv); i++) {
		CHECK_ST(calc_chi_square_p(pv[i].chi, pv[i].df, &p), CALC_OK);
		CHECK(fabs(p - pv[i].p) <= 1e-8);
	}
	/* extra: degrees of freedom beyond the legacy 50-row table, both series branches */
	CHECK_ST(calc_chi_square_p(60, 80, &p), CALC_OK);
	CHECK(fabs(p - 95.3746962354158) <= 1e-8);
	CHECK_ST(calc_chi_square_p(100, 60, &p), CALC_OK);
	CHECK(fabs(p - 0.09168288614560845) <= 1e-8);
	CHECK_ST(calc_chi_square_p(-1, 3, &p), CALC_E_DOMAIN);
	CHECK_ST(calc_chi_square_p(3, 0, &p), CALC_E_SIZE);
	CHECK_ST(calc_chi_square_p(1e6, 3, &p), CALC_OK);
	CHECK(p == 0.0);
	{
		static const double neg[] = { 1, -1, 2, 3 };
		static const double zrow[] = { 0, 0, 2, 3 };
		static const double zcol[] = { 0, 2, 0, 3 };

		CHECK_ST(calc_chi_square(neg, 2, 2, &chi, &df), CALC_E_DOMAIN);
		CHECK_ST(calc_chi_square(zrow, 2, 2, &chi, &df), CALC_E_DOMAIN);
		CHECK_ST(calc_chi_square(zcol, 2, 2, &chi, &df), CALC_E_DOMAIN);
		CHECK_ST(calc_chi_square(t1, 1, 4, &chi, &df), CALC_E_SIZE);
		CHECK_ST(calc_chi_square(t1, 4, 1, &chi, &df), CALC_E_SIZE);
	}
}

static void
test_crossing(void)
{
	static const double a1[] = { 0, 1, 2, 3, 4 };
	static const double a2[] = { 0, 0, 5, 5 };
	static const double a3[] = { 4, 3, 2, 1, 0 };
	static const double a4[] = { 0, 1, 2, 3 };
	static const double a5[] = { 1, 2 };
	static const double a6[] = { 0, 1, 0, 1, 2, 3, 4, 5 };
	static const double a7[] = { -2, -1, 1, 2 };
	/* extra: second pass, rising and falling plateau, and a noise spike */
	static const double a8[] = { 0, 1, 2, 2, 3, 4 };
	static const double a9[] = { 5, 4, 3, 3, 2, 1 };
	static const double a10[] = { 0, 9, 0, 0, 0, 0, 0 };
	static const struct {
		const double *a;
		size_t n;
		double level;
		int dir;
		long want;
	} v[] = {
		{ a1, 5, 2.5, 1, 2 }, { a2, 4, 2.5, 1, 1 }, { a3, 5, 2.5, -1, 1 },
		{ a4, 4, 10, 1, -1 }, { a5, 2, 0, 1, -1 }, { a6, 8, 3.5, 1, 5 },
		{ a7, 4, 0, 1, 1 }, { a8, 6, 2, 1, 2 }, { a9, 6, 3, -1, 2 },
		{ a10, 7, 4.5, 1, -1 }, { a4, 4, 2.5, -1, -1 },
	};
	long idx;
	size_t i;

	for (i = 0; i < COUNT(v); i++) {
		idx = 12345;
		CHECK_ST(calc_crossing(v[i].a, v[i].n, v[i].level, v[i].dir, &idx), CALC_OK);
		CHECK(idx == v[i].want);
	}
	CHECK_ST(calc_crossing(a1, 5, 2, 0, &idx), CALC_E_ARG);
	CHECK_ST(calc_crossing(a1, 5, 2, 2, &idx), CALC_E_ARG);
	CHECK_ST(calc_crossing(a1, 0, 2, 1, &idx), CALC_E_SIZE);
}

/* --- 5.5 vectors --------------------------------------------------------- */

static void
test_vectors(void)
{
	double out, v3[3], v4[4];
	size_t i;

	{
		static const double a[] = { 1, 2, 3 }, b[] = { 4, 5, 6 };
		static const double c[] = { 1, 0, -1, 2 }, d[] = { 3, 3, 3, 0.5 };
		static const double e[] = { 0.5, 0.25 }, f[] = { 8, -4 };

		CHECK_ST(calc_dot(a, b, 3, &out), CALC_OK);
		CHECK_NEAR(out, 32);
		CHECK_ST(calc_dot(c, d, 4, &out), CALC_OK);
		CHECK_NEAR(out, 1);
		CHECK_ST(calc_dot(e, f, 2, &out), CALC_OK);
		CHECK_NEAR(out, 3);
		CHECK_ST(calc_dot(a, b, 0, &out), CALC_E_SIZE);
	}
	{
		static const double a[] = { 3, 4 }, b[] = { 1, 2, 2 }, z[] = { 0, 0, 0 };
		static const double h[] = { 1e200, 1e200 }, t[] = { 1e-200, 1e-200 };

		CHECK_ST(calc_magnitude(a, 2, &out), CALC_OK);
		CHECK_NEAR(out, 5);
		CHECK_ST(calc_magnitude(b, 3, &out), CALC_OK);
		CHECK_NEAR(out, 3);
		CHECK_ST(calc_magnitude(z, 3, &out), CALC_OK);
		CHECK(out == 0);
		CHECK_ST(calc_magnitude(h, 2, &out), CALC_OK);
		CHECK_TOL(out / 1e200, 1.4142135623731, 1e-13);
		CHECK_ST(calc_magnitude(t, 2, &out), CALC_OK);
		CHECK_TOL(out / 1e-200, 1.4142135623731, 1e-13);
		CHECK_ST(calc_magnitude(a, 0, &out), CALC_E_SIZE);
	}
	{
		static const double a[] = { 3, 4 }, b[] = { 1, 2, 2 }, c[] = { 0, 5, 0, 0 };
		static const double z[] = { 0, 0, 0 };
		double o2[2], o3[3], o4[4];

		CHECK_ST(calc_normalize(a, 2, o2), CALC_OK);
		CHECK_NEAR(o2[0], 0.6);
		CHECK_NEAR(o2[1], 0.8);
		CHECK_ST(calc_normalize(b, 3, o3), CALC_OK);
		CHECK_NEAR(o3[0], 0.333333333333333);
		CHECK_NEAR(o3[1], 0.666666666666667);
		CHECK_NEAR(o3[2], 0.666666666666667);
		CHECK_ST(calc_normalize(c, 4, o4), CALC_OK);
		CHECK_NEAR(o4[0], 0);
		CHECK_NEAR(o4[1], 1);
		CHECK_NEAR(o4[2], 0);
		CHECK_NEAR(o4[3], 0);
		CHECK_ST(calc_normalize(z, 3, o3), CALC_E_DOMAIN);
		/* in place */
		v3[0] = 1;
		v3[1] = 2;
		v3[2] = 2;
		CHECK_ST(calc_normalize(v3, 3, v3), CALC_OK);
		CHECK_NEAR(v3[2], 0.666666666666667);
		/* shifted overlap is rejected */
		v4[0] = 1;
		v4[1] = 2;
		v4[2] = 3;
		v4[3] = 4;
		CHECK_ST(calc_normalize(v4, 3, v4 + 1), CALC_E_ALIAS);
	}
	{
		static const struct { double a[3], b[3], want[3]; } v[] = {
			{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
			{ { 1, 2, 3 }, { 4, 5, 6 }, { -3, 6, -3 } },
			{ { 2, -1, 0.5 }, { 0, 3, 1 }, { -2.5, -2, 6 } },
		};
		double o[3];

		for (i = 0; i < COUNT(v); i++) {
			CHECK_ST(calc_cross3(v[i].a, v[i].b, o), CALC_OK);
			CHECK_NEAR(o[0], v[i].want[0]);
			CHECK_NEAR(o[1], v[i].want[1]);
			CHECK_NEAR(o[2], v[i].want[2]);
		}
		/* output may alias an input */
		o[0] = 1;
		o[1] = 2;
		o[2] = 3;
		CHECK_ST(calc_cross3(o, v[1].b, o), CALC_OK);
		CHECK_NEAR(o[0], -3);
		CHECK_NEAR(o[1], 6);
		CHECK_NEAR(o[2], -3);
	}
	{
		static const double ma[] = { 1, 2, 3, 4, 5, 6 };
		static const double mb[] = { 2, 0, 0, 3, 1, 1 };
		static const double mc[] = { 0.5 };
		static const double v1[] = { 1, 0, -1 }, v2[] = { 5, 7 }, v3a[] = { 4 };
		double o[3];

		CHECK_ST(calc_mat_vec(ma, 2, 3, v1, o), CALC_OK);
		CHECK_NEAR(o[0], -2);
		CHECK_NEAR(o[1], -2);
		CHECK_ST(calc_mat_vec(mb, 3, 2, v2, o), CALC_OK);
		CHECK_NEAR(o[0], 10);
		CHECK_NEAR(o[1], 21);
		CHECK_NEAR(o[2], 12);
		CHECK_ST(calc_mat_vec(mc, 1, 1, v3a, o), CALC_OK);
		CHECK_NEAR(o[0], 2);
		CHECK_ST(calc_mat_vec(ma, 0, 3, v1, o), CALC_E_SIZE);
		{
			double mm[6] = { 1, 2, 3, 4, 5, 6 };

			CHECK_ST(calc_mat_vec(mm, 2, 3, v1, mm), CALC_E_ALIAS);
		}
	}
	{
		static const struct {
			double cx, cy, ang;
			calc_angle_unit u;
			double xi[2], yi[2];
			size_t n;
			double xo[2], yo[2];
		} r[] = {
			{ 0, 0, 90, CALC_DEGREES, { 1, 0 }, { 0, 0 }, 1, { 6.12323399573677e-17, 0 }, { 1, 0 } },
			{ 1, 1, 180, CALC_DEGREES, { 2, 0 }, { 1, 0 }, 1, { 0, 0 }, { 1, 0 } },
			{ 0, 0, 30, CALC_DEGREES, { 3, 0 }, { 0, 3 }, 2, { 2.59807621135332, -1.5 },
			    { 1.5, 2.59807621135332 } },
		};
		double xo[2], yo[2];

		for (i = 0; i < COUNT(r); i++) {
			size_t k;

			CHECK_ST(calc_rotate2d(r[i].cx, r[i].cy, r[i].ang, r[i].u,
			    r[i].xi, r[i].yi, r[i].n, xo, yo), CALC_OK);
			for (k = 0; k < r[i].n; k++) {
				CHECK_NEAR(xo[k], r[i].xo[k]);
				CHECK_NEAR(yo[k], r[i].yo[k]);
			}
		}
		/* in place, radians */
		{
			double xs[2] = { 1, 0 }, ys[2] = { 0, 1 };

			CHECK_ST(calc_rotate2d(0, 0, 3.141592653589793 / 2, CALC_RADIANS,
			    xs, ys, 2, xs, ys), CALC_OK);
			CHECK_NEAR(xs[0], 0);
			CHECK_NEAR(ys[0], 1);
			CHECK_NEAR(xs[1], -1);
			CHECK_NEAR(ys[1], 0);
			CHECK_ST(calc_rotate2d(0, 0, 1, CALC_RADIANS, xs, ys, 2, ys, xs), CALC_E_ALIAS);
			CHECK_ST(calc_rotate2d(0, 0, 1, CALC_RADIANS, xs, ys, 2, xs, xs), CALC_E_ALIAS);
		}
		{
			static const int32_t xi[] = { 10, 0, -10 }, yi[] = { 0, 10, 0 };
			int32_t xr[3], yr[3];

			CHECK_ST(calc_rotate2d_i32(0, 0, 45, CALC_DEGREES, xi, yi, 3, xr, yr), CALC_OK);
			CHECK(xr[0] == 7 && yr[0] == 7);
			CHECK(xr[1] == -7 && yr[1] == 7);
			CHECK(xr[2] == -7 && yr[2] == -7);
			CHECK_ST(calc_rotate2d_i32(1e10, 0, 90, CALC_DEGREES, xi, yi, 3, xr, yr), CALC_E_RANGE);
		}
	}
}

/* --- 5.6 matrices -------------------------------------------------------- */

static void
test_matrices(void)
{
	double out[16], work[16], det;
	size_t i;

	{
		static const double a[] = { 1, 2, 3, 4, 5, 6 };
		static const double b[] = { 1, 2, 3, 4, 5, 6 };
		static const double r1[] = { 1, 4, 2, 5, 3, 6 };
		static const double r2[] = { 1, 3, 5, 2, 4, 6 };
		static const double s[] = { 7 };

		CHECK_ST(calc_mat_transpose(a, 2, 3, out), CALC_OK);
		for (i = 0; i < 6; i++)
			CHECK_NEAR(out[i], r1[i]);
		CHECK_ST(calc_mat_transpose(b, 3, 2, out), CALC_OK);
		for (i = 0; i < 6; i++)
			CHECK_NEAR(out[i], r2[i]);
		CHECK_ST(calc_mat_transpose(s, 1, 1, out), CALC_OK);
		CHECK_NEAR(out[0], 7);
		/* in place: square only */
		{
			double sq[4] = { 1, 2, 3, 4 };

			CHECK_ST(calc_mat_transpose(sq, 2, 2, sq), CALC_OK);
			CHECK(sq[0] == 1 && sq[1] == 3 && sq[2] == 2 && sq[3] == 4);
			CHECK_ST(calc_mat_transpose(r1, 2, 3, (double *)(uintptr_t)r1), CALC_E_ALIAS);
		}
	}
	{
		static const double a[] = { 1, 2, 3, 4, 5, 6 };
		static const double b[] = { 7, 8, 9, 10, 11, 12 };
		static const double i2[] = { 1, 0, 0, 1 };
		static const double m5[] = { 5, 6, 7, 8 };
		static const double r1[] = { 2, 3 }, r2[] = { 4, 5 };
		static const double s1[] = { 1, 2, 3, 4 }, s2[] = { 0, 1, 1, 0 };

		CHECK_ST(calc_mat_mul(a, 2, 3, b, 2, out), CALC_OK);
		CHECK_NEAR(out[0], 58);
		CHECK_NEAR(out[1], 64);
		CHECK_NEAR(out[2], 139);
		CHECK_NEAR(out[3], 154);
		CHECK_ST(calc_mat_mul(i2, 2, 2, m5, 2, out), CALC_OK);
		for (i = 0; i < 4; i++)
			CHECK_NEAR(out[i], m5[i]);
		CHECK_ST(calc_mat_mul(r1, 1, 2, r2, 1, out), CALC_OK);
		CHECK_NEAR(out[0], 23);
		CHECK_ST(calc_mat_mul(s1, 2, 2, s2, 2, out), CALC_OK);
		CHECK_NEAR(out[0], 2);
		CHECK_NEAR(out[1], 1);
		CHECK_NEAR(out[2], 4);
		CHECK_NEAR(out[3], 3);
		/* inner dimensions differ: the 2x3 by 2x3 case is not expressible
		 * with one n, so it appears as a zero inner size and as aliasing */
		CHECK_ST(calc_mat_mul(a, 2, 0, b, 3, out), CALC_E_SIZE);
		{
			double mm[4] = { 1, 2, 3, 4 };

			CHECK_ST(calc_mat_mul(mm, 2, 2, s2, 2, mm), CALC_E_ALIAS);
			CHECK_ST(calc_mat_mul(s2, 2, 2, mm, 2, mm), CALC_E_ALIAS);
		}
	}
	{
		static const double m1[] = { 2, 0, 0, 3 };
		static const double m2[] = { 1, 2, 3, 4 };
		static const double m3[] = { 6, 1, 1, 4, -2, 5, 2, 8, 7 };
		static const double m4[] = { 4, -2, 1, 3, 3, 6, -4, 2, 2, 1, 8, -5, 1, -3, 2, 9 };
		static const double m5[] = { 1, 2, 2, 4 };
		static const double m6[] = { 0, 1, 1, 0 };
		static const double m7[] = { 5 };
		static const struct { const double *m; size_t n; double det; } d[] = {
			{ m1, 2, 6 }, { m2, 2, -2 }, { m3, 3, -306 }, { m4, 4, 2180 },
			{ m5, 2, 0 }, { m6, 2, -1 }, { m7, 1, 5 },
		};

		for (i = 0; i < COUNT(d); i++) {
			CHECK_ST(calc_mat_det(d[i].m, d[i].n, work, &det), CALC_OK);
			CHECK_NEAR(det, d[i].det);
		}
		CHECK_ST(calc_mat_det(m5, 2, work, &det), CALC_OK);
		CHECK(det == 0.0);
		/* in place on the work matrix */
		memcpy(work, m4, sizeof m4);
		CHECK_ST(calc_mat_det(work, 4, work, &det), CALC_OK);
		CHECK_NEAR(det, 2180);
		CHECK_ST(calc_mat_det(m4, CALC_MAT_MAX + 1, work, &det), CALC_E_SIZE);
		CHECK_ST(calc_mat_det(m4, 0, work, &det), CALC_E_SIZE);
		CHECK_ST(calc_mat_det(m4, 4, (double *)(uintptr_t)&m4[1], &det), CALC_E_ALIAS);
	}
	{
		static const double i1[] = { 4, 7, 2, 6 };
		static const double i2[] = { 1, 2, 3, 0, 1, 4, 5, 6, 0 };
		static const double i3[] = { 2, -1, 0, -1, 2, -1, 0, -1, 2 };
		static const double i4[] = { 2, 0, 0, 0, 4, 0, 0, 0, 8 };
		static const double i5[] = { 0, 1, 1, 0 };
		static const double w1[] = { 0.6, -0.7, -0.2, 0.4 };
		static const double w2[] = { -24, 18, 5, 20, -15, -4, -5, 4, 1 };
		static const double w3[] = { 0.75, 0.5, 0.25, 0.5, 1, 0.5, 0.25, 0.5, 0.75 };
		static const double w4[] = { 0.5, 0, 0, 0, 0.25, 0, 0, 0, 0.125 };
		static const double w5[] = { 0, 1, 1, 0 };
		static const double sing[] = { 1, 2, 2, 4 };
		static const struct { const double *m, *w; size_t n; } v[] = {
			{ i1, w1, 2 }, { i2, w2, 3 }, { i3, w3, 3 }, { i4, w4, 3 }, { i5, w5, 2 },
		};
		size_t k;

		for (i = 0; i < COUNT(v); i++) {
			CHECK_ST(calc_mat_inverse(v[i].m, v[i].n, work, out), CALC_OK);
			for (k = 0; k < v[i].n * v[i].n; k++)
				CHECK_NEAR(out[k], v[i].w[k]);
		}
		/* a singular matrix leaves the output untouched */
		for (k = 0; k < 4; k++)
			out[k] = 123.0;
		CHECK_ST(calc_mat_inverse(sing, 2, work, out), CALC_E_SINGULAR);
		for (k = 0; k < 4; k++)
			CHECK(out[k] == 123.0);
		{
			static const double zero[] = { 0, 0, 0, 0 };
			static const double nearly[] = { 1, 2, 2, 4.000000000000001 };

			CHECK_ST(calc_mat_inverse(zero, 2, work, out), CALC_E_SINGULAR);
			CHECK_ST(calc_mat_inverse(nearly, 2, work, out), CALC_E_SINGULAR);
		}
		CHECK_ST(calc_mat_inverse(i1, 0, work, out), CALC_E_SIZE);
		CHECK_ST(calc_mat_inverse(i1, CALC_MAT_MAX + 1, work, out), CALC_E_SIZE);
		CHECK_ST(calc_mat_inverse(i1, 2, work, (double *)(uintptr_t)i1), CALC_E_ALIAS);
		CHECK_ST(calc_mat_inverse(i1, 2, out, out), CALC_E_ALIAS);
		/* extra: A * inverse(A) is the identity for a 6 by 6 Hilbert-like matrix */
		{
			double h[36], inv[36], prod[36], w6[36];
			size_t r, c;

			for (r = 0; r < 6; r++)
				for (c = 0; c < 6; c++)
					h[r * 6 + c] = 1.0 / (double)(r + c + 1) + (r == c ? 1.0 : 0.0);
			CHECK_ST(calc_mat_inverse(h, 6, w6, inv), CALC_OK);
			CHECK_ST(calc_mat_mul(h, 6, 6, inv, 6, prod), CALC_OK);
			for (r = 0; r < 6; r++)
				for (c = 0; c < 6; c++)
					CHECK_TOL(prod[r * 6 + c], r == c ? 1.0 : 0.0, 1e-13);
		}
	}
	{
		static const double a1[] = { 2, 1, -1, -3, -1, 2, -2, 1, 2 };
		static const double b1[] = { 8, -11, -3 };
		static const double a2[] = { 4, -2, 1, 3, 3, 6, -4, 2, 2, 1, 8, -5, 1, -3, 2, 9 };
		static const double b2[] = { 1, 2, 3, 4 };
		static const double x2[] = { 0.00917431192660551, 0.571100917431193,
		    0.612385321100917, 0.497706422018349 };
		static const double a3[] = { 3, 0, 0, 5 };
		static const double b3[] = { 6, 10 };
		static const double sing[] = { 1, 2, 2, 4 };
		static const double bs[] = { 1, 2 };
		static const double bm[] = { 8, 1, -11, 0, -3, 2 };
		double x[8];

		CHECK_ST(calc_mat_solve(a1, 3, b1, 1, work, x), CALC_OK);
		CHECK_NEAR(x[0], 2);
		CHECK_NEAR(x[1], 3);
		CHECK_NEAR(x[2], -1);
		CHECK_ST(calc_mat_solve(a2, 4, b2, 1, work, x), CALC_OK);
		for (i = 0; i < 4; i++)
			CHECK_NEAR(x[i], x2[i]);
		CHECK_ST(calc_mat_solve(a3, 2, b3, 1, work, x), CALC_OK);
		CHECK_NEAR(x[0], 2);
		CHECK_NEAR(x[1], 2);
		CHECK_ST(calc_mat_solve(sing, 2, bs, 1, work, x), CALC_E_SINGULAR);
		/* extra: two right-hand sides at once */
		CHECK_ST(calc_mat_solve(a1, 3, bm, 2, work, x), CALC_OK);
		CHECK_NEAR(x[0], 2);
		CHECK_NEAR(x[2], 3);
		CHECK_NEAR(x[4], -1);
		CHECK_ST(calc_mat_solve(a1, 3, bm, 0, work, x), CALC_E_SIZE);
		CHECK_ST(calc_mat_solve(a1, 3, b1, 1, work, work), CALC_E_ALIAS);
	}
	/*
	 * extra: conditioning. The 1-norm condition numbers below are exact
	 * values from rational arithmetic; the estimate is a lower bound that
	 * must stay within a factor of three. The refusal threshold is
	 * 1 / (n * DBL_EPSILON): 4.5e14 at n = 10 and 3.8e14 at n = 12.
	 */
	{
		static const struct { size_t n; double cond; } hilbert[] = {
			{ 4, 28375.0 }, { 6, 29070279.0 }, { 8, 33872791095.0 },
			{ 10, 35357439251992.0 },
		};
		double h[144], w[144], inv[144], x[12], b[12], cond;
		size_t n, r, c, k;

		for (k = 0; k < COUNT(hilbert); k++) {
			n = hilbert[k].n;
			for (r = 0; r < n; r++)
				for (c = 0; c < n; c++)
					h[r * n + c] = 1.0 / (double)(r + c + 1);
			CHECK_ST(calc_mat_cond(h, n, w, &cond), CALC_OK);
			CHECK(cond >= hilbert[k].cond / 3.0 && cond <= hilbert[k].cond * 1.0001);
			CHECK_ST(calc_mat_inverse(h, n, w, inv), CALC_OK);
		}
		/* Hilbert 12 (cond 4.1e16) and the nearly parallel pair (cond 4e15) are refused */
		for (r = 0; r < 12; r++)
			for (c = 0; c < 12; c++)
				h[r * 12 + c] = 1.0 / (double)(r + c + 1);
		for (k = 0; k < 144; k++)
			inv[k] = 123.0;
		for (k = 0; k < 12; k++)
			b[k] = 1.0;
		CHECK_ST(calc_mat_cond(h, 12, w, &cond), CALC_OK);
		CHECK(cond > 1e16);
		CHECK_ST(calc_mat_inverse(h, 12, w, inv), CALC_E_ILLCOND);
		for (k = 0; k < 144; k++)
			CHECK(inv[k] == 123.0);
		x[0] = 55.0;
		CHECK_ST(calc_mat_solve(h, 12, b, 1, w, x), CALC_E_ILLCOND);
		CHECK(x[0] == 55.0);
		{
			static const double par[] = { 1, 1, 1, 1 + 1e-15 };
			double p2[4], o2[4];

			CHECK_ST(calc_mat_inverse(par, 2, p2, o2), CALC_E_ILLCOND);
			CHECK_ST(calc_mat_solve(par, 2, b, 1, p2, o2), CALC_E_ILLCOND);
			CHECK_ST(calc_mat_cond(par, 2, p2, &cond), CALC_OK);
			CHECK(cond > 1e15 && cond < 5e15);
		}
		/* the condition number is a property of the matrix, not of its scale */
		{
			static const double base[] = { 2, 1, 1, 3 };
			double p2[4], o2[4], c1, c2;
			size_t si;

			CHECK_ST(calc_mat_cond(base, 2, p2, &c1), CALC_OK);
			CHECK_NEAR(c1, 3.2);	/* |A|_1 = 4, |A^-1|_1 = 4 / 5 */
			for (si = 0; si < 4; si++) {
				double sc = si < 2 ? 1e-150 : 1e150, m[4];

				for (k = 0; k < 4; k++)
					m[k] = base[k] * sc;
				CHECK_ST(calc_mat_inverse(m, 2, p2, o2), CALC_OK);
				CHECK_ST(calc_mat_cond(m, 2, p2, &c2), CALC_OK);
				CHECK_TOL(c2, c1, 1e-10);
			}
		}
		/* argument errors */
		CHECK_ST(calc_mat_cond(h, 12, h, &cond), CALC_E_ALIAS);
		CHECK_ST(calc_mat_cond(h, 0, w, &cond), CALC_E_SIZE);
		CHECK_ST(calc_mat_cond(h, 2, w, NULL), CALC_E_ARG);
	}

	/* extra: a NaN or infinite entry is a domain error for det, inverse, solve and cond */
	{
		static const double nan2[] = { 1, NAN, 0, 1 };
		static const double inf2[] = { 1, 0, INFINITY, 1 };
		static const double ninf2[] = { -INFINITY, 0, 0, 1 };
		static const double ok2[] = { 4, 1, 1, 3 };
		static const double *const bad[] = { nan2, inf2, ninf2 };
		double w2[4], o2[4], d2, c2, b2[2] = { 1, 1 };
		size_t k;

		for (k = 0; k < COUNT(bad); k++) {
			o2[0] = d2 = c2 = 9.0;
			CHECK_ST(calc_mat_det(bad[k], 2, w2, &d2), CALC_E_DOMAIN);
			CHECK_ST(calc_mat_inverse(bad[k], 2, w2, o2), CALC_E_DOMAIN);
			CHECK_ST(calc_mat_solve(bad[k], 2, b2, 1, w2, o2), CALC_E_DOMAIN);
			CHECK_ST(calc_mat_cond(bad[k], 2, w2, &c2), CALC_E_DOMAIN);
			CHECK(o2[0] == 9.0 && d2 == 9.0 && c2 == 9.0);
		}
		CHECK_ST(calc_mat_det(ok2, 2, w2, &d2), CALC_OK);
		CHECK_NEAR(d2, 11);
	}

}

/* --- 5.7 complex --------------------------------------------------------- */

enum { cfn_conj = 0, cfn_exp = CALC_C_EXP, cfn_log = CALC_C_LOG, cfn_sqrt = CALC_C_SQRT,
    cfn_sin = CALC_C_SIN, cfn_cos = CALC_C_COS, cfn_tan = CALC_C_TAN,
    cfn_asin = CALC_C_ASIN, cfn_acos = CALC_C_ACOS, cfn_atan = CALC_C_ATAN,
    cfn_sinh = CALC_C_SINH, cfn_cosh = CALC_C_COSH, cfn_tanh = CALC_C_TANH,
    cfn_asinh = CALC_C_ASINH, cfn_acosh = CALC_C_ACOSH, cfn_atanh = CALC_C_ATANH };

enum { op_add, op_sub, op_mul, op_div, op_pow };

static const struct { int fn; double zr, zi, er, ei; } cunary[] = {
#define CV_UNARY(n, zr, zi, er, ei) { cfn_##n, zr, zi, er, ei },
#define CV_BINARY(n, ar, ai, br, bi, er, ei)
#define CV_SCALAR(n, zr, zi, e)
#define CV_POLE(n, zr, zi, er, ei)
#include "complex_vectors.h"
#undef CV_UNARY
#undef CV_BINARY
#undef CV_SCALAR
#undef CV_POLE
};

static const struct { int op; double ar, ai, br, bi, er, ei; } cbinary[] = {
#define CV_UNARY(n, zr, zi, er, ei)
#define CV_BINARY(n, ar, ai, br, bi, er, ei) { op_##n, ar, ai, br, bi, er, ei },
#define CV_SCALAR(n, zr, zi, e)
#define CV_POLE(n, zr, zi, er, ei)
#include "complex_vectors.h"
#undef CV_UNARY
#undef CV_BINARY
#undef CV_SCALAR
#undef CV_POLE
};

enum { sc_abs, sc_arg, sc_phase };

static const struct { int kind; double zr, zi, e; } cscalar[] = {
#define CV_UNARY(n, zr, zi, er, ei)
#define CV_BINARY(n, ar, ai, br, bi, er, ei)
#define CV_SCALAR(n, zr, zi, e) { sc_##n, zr, zi, e },
#define CV_POLE(n, zr, zi, er, ei)
#include "complex_vectors.h"
#undef CV_UNARY
#undef CV_BINARY
#undef CV_SCALAR
#undef CV_POLE
};

static void
check_cplx(calc_cplx got, double er, double ei, double tol, int line)
{
	checks += 2;
	if (!near(got.re, er, tol) || !near(got.im, ei, tol)) {
		failures++;
		printf("FAIL %s:%d: (%.17g, %.17g), expected (%.17g, %.17g)\n",
		    __FILE__, line, got.re, got.im, er, ei);
	}
}

#define CHECK_CPLX(got, er, ei) check_cplx((got), (er), (ei), TOL, __LINE__)

static void
test_complex(void)
{
	calc_cplx z, r;
	size_t i;

	for (i = 0; i < COUNT(cunary); i++) {
		z = calc_c_make(cunary[i].zr, cunary[i].zi);
		if (cunary[i].fn == cfn_conj) {
			CHECK_CPLX(calc_c_conj(z), cunary[i].er, cunary[i].ei);
			continue;
		}
		CHECK_ST(calc_c_func((calc_cfn)cunary[i].fn, z, &r), CALC_OK);
		CHECK_CPLX(r, cunary[i].er, cunary[i].ei);
	}
	for (i = 0; i < COUNT(cbinary); i++) {
		calc_cplx a = calc_c_make(cbinary[i].ar, cbinary[i].ai);
		calc_cplx b = calc_c_make(cbinary[i].br, cbinary[i].bi);

		switch (cbinary[i].op) {
		case op_add:
			CHECK_ST(calc_c_add(a, b, &r), CALC_OK);
			break;
		case op_sub:
			CHECK_ST(calc_c_sub(a, b, &r), CALC_OK);
			break;
		case op_mul:
			CHECK_ST(calc_c_mul(a, b, &r), CALC_OK);
			break;
		case op_div:
			CHECK_ST(calc_c_div(a, b, &r), CALC_OK);
			break;
		default:
			CHECK_ST(calc_c_pow(a, b, &r), CALC_OK);
			break;
		}
		CHECK_CPLX(r, cbinary[i].er, cbinary[i].ei);
	}
	for (i = 0; i < COUNT(cscalar); i++) {
		z = calc_c_make(cscalar[i].zr, cscalar[i].zi);
		switch (cscalar[i].kind) {
		case sc_abs:
			CHECK_NEAR(calc_c_abs(z), cscalar[i].e);
			break;
		case sc_arg:
			CHECK_NEAR(calc_c_arg(z), cscalar[i].e);
			CHECK_NEAR(calc_c_phase(z, CALC_RADIANS), cscalar[i].e);
			break;
		default:
			CHECK_NEAR(calc_c_phase(z, CALC_DEGREES), cscalar[i].e);
			break;
		}
	}
	CHECK(COUNT(cunary) == 48 && COUNT(cbinary) == 15 && COUNT(cscalar) == 9);

	/* real and imaginary parts, construction, polar form, projection */
	CHECK_CPLX(calc_c_make(2.5, -1.5), 2.5, -1.5);
	CHECK_CPLX(calc_c_make(0, 0), 0, 0);
	CHECK_CPLX(calc_c_make(-7, 1e-3), -7, 0.001);
	CHECK_CPLX(calc_c_polar(2, 90, CALC_DEGREES), 1.22464679914735e-16, 2);
	CHECK_CPLX(calc_c_polar(1, 1.0471975511965976, CALC_RADIANS), 0.5, 0.866025403784439);
	CHECK_CPLX(calc_c_polar(3, 180, CALC_DEGREES), -3, 3.67394039744206e-16);
	CHECK_CPLX(calc_c_proj(calc_c_make(1, 2)), 1, 2);
	CHECK_CPLX(calc_c_proj(calc_c_make(-0.5, 0.25)), -0.5, 0.25);
	r = calc_c_proj(calc_c_make(INFINITY, -3));
	CHECK(r.re == INFINITY && r.im == 0.0 && signbit(r.im));
	r = calc_c_proj(calc_c_make(2, INFINITY));
	CHECK(r.re == INFINITY && r.im == 0.0 && !signbit(r.im));

	/* division by zero, power special cases, range */
	CHECK_ST(calc_c_div(calc_c_make(1, 2), calc_c_make(0, 0), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_pow(calc_c_make(0, 0), calc_c_make(0, 0), &r), CALC_OK);
	CHECK_CPLX(r, 1, 0);
	CHECK_ST(calc_c_pow(calc_c_make(0, 0), calc_c_make(2, 5), &r), CALC_OK);
	CHECK_CPLX(r, 0, 0);
	CHECK_ST(calc_c_pow(calc_c_make(0, 0), calc_c_make(-1, 0), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_pow(calc_c_make(0, 0), calc_c_make(0, 1), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_mul(calc_c_make(1e200, 0), calc_c_make(1e200, 0), &r), CALC_E_RANGE);
	CHECK_ST(calc_c_div(calc_c_make(1e300, 1e300), calc_c_make(1e300, 1e300), &r), CALC_OK);
	CHECK_CPLX(r, 1, 0);
	CHECK_ST(calc_c_func((calc_cfn)99, z, &r), CALC_E_ARG);

	/* extra: poles and overflow */
	CHECK_ST(calc_c_func(CALC_C_LOG, calc_c_make(0, 0), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_func(CALC_C_ATANH, calc_c_make(1, 0), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_func(CALC_C_ATANH, calc_c_make(-1, 0), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_func(CALC_C_ATAN, calc_c_make(0, 1), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_func(CALC_C_ATAN, calc_c_make(0, -1), &r), CALC_E_DOMAIN);
	CHECK_ST(calc_c_func(CALC_C_EXP, calc_c_make(1000, 0), &r), CALC_E_RANGE);
	CHECK_ST(calc_c_func(CALC_C_COSH, calc_c_make(1000, 0), &r), CALC_E_RANGE);
	CHECK_ST(calc_c_func(CALC_C_SIN, calc_c_make(0, 800), &r), CALC_E_RANGE);

	/* extra: branch cuts and signed zeros (C99 Annex G) */
	CHECK_ST(calc_c_func(CALC_C_SQRT, calc_c_make(-4, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0, 2);
	CHECK_ST(calc_c_func(CALC_C_SQRT, calc_c_make(-4, -0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0, -2);
	CHECK_ST(calc_c_func(CALC_C_LOG, calc_c_make(-1, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0, 3.141592653589793);
	CHECK_ST(calc_c_func(CALC_C_LOG, calc_c_make(-1, -0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0, -3.141592653589793);
	CHECK_ST(calc_c_func(CALC_C_ASIN, calc_c_make(2, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 1.5707963267948966, 1.3169578969248166);
	CHECK_ST(calc_c_func(CALC_C_ASIN, calc_c_make(2, -0.0), &r), CALC_OK);
	CHECK_CPLX(r, 1.5707963267948966, -1.3169578969248166);
	CHECK_ST(calc_c_func(CALC_C_ACOS, calc_c_make(-2, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 3.141592653589793, -1.3169578969248166);
	CHECK_ST(calc_c_func(CALC_C_ACOSH, calc_c_make(-2, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 1.3169578969248166, 3.141592653589793);
	CHECK_ST(calc_c_func(CALC_C_ACOSH, calc_c_make(0.5, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0, 1.0471975511965979);
	CHECK_ST(calc_c_func(CALC_C_ATANH, calc_c_make(2, 0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0.5493061443340548, 1.5707963267948966);
	CHECK_ST(calc_c_func(CALC_C_ATANH, calc_c_make(2, -0.0), &r), CALC_OK);
	CHECK_CPLX(r, 0.5493061443340548, -1.5707963267948966);
	CHECK_ST(calc_c_func(CALC_C_ASINH, calc_c_make(0, 2), &r), CALC_OK);
	CHECK_CPLX(r, 1.3169578969248166, 1.5707963267948966);
	CHECK_ST(calc_c_func(CALC_C_ATAN, calc_c_make(0, 2), &r), CALC_OK);
	CHECK_CPLX(r, 1.5707963267948966, 0.5493061443340548);

	/* extra: accuracy next to the real axis and at large magnitude */
	CHECK_ST(calc_c_func(CALC_C_ASIN, calc_c_make(0.5, 1e-10), &r), CALC_OK);
	CHECK_TOL(r.im / 1.1547005383792517e-10, 1.0, 1e-9);
	CHECK_ST(calc_c_func(CALC_C_ACOS, calc_c_make(1, 1e-10), &r), CALC_OK);
	CHECK_TOL(r.re / 9.999999999916667e-06, 1.0, 1e-9);
	CHECK_TOL(r.im / -1.0000000000083335e-05, 1.0, 1e-9);
	CHECK_ST(calc_c_func(CALC_C_ASIN, calc_c_make(1e200, 1e200), &r), CALC_OK);
	CHECK_TOL(r.re, 0.7853981633974483, 1e-12);
	CHECK_TOL(r.im / 461.55673936964905, 1.0, 1e-12);
	CHECK_ST(calc_c_func(CALC_C_SQRT, calc_c_make(1e308, 1e308), &r), CALC_OK);
	CHECK_TOL(r.re / 1.0986841134678099e154, 1.0, 1e-12);
	CHECK_TOL(r.im / 4.5508986056222734e153, 1.0, 1e-12);
	CHECK_ST(calc_c_func(CALC_C_ATANH, calc_c_make(1e200, 0.0), &r), CALC_OK);
	CHECK_TOL(r.im, 1.5707963267948966, 1e-12);
	CHECK_ST(calc_c_func(CALC_C_TAN, calc_c_make(1.5707963267948966, 0), &r), CALC_OK);
	CHECK_TOL(r.re / 1.633123935319537e16, 1.0, 1e-9);
	CHECK_ST(calc_c_func(CALC_C_TAN, calc_c_make(0.5, 30), &r), CALC_OK);
	CHECK_TOL(r.im, 1.0, 1e-12);

	/* extra: inverse functions undo the forward ones away from the cuts */
	for (i = 0; i < 20; i++) {
		static const calc_cfn fwd[] = { CALC_C_SIN, CALC_C_COS, CALC_C_TAN, CALC_C_SINH,
		    CALC_C_COSH, CALC_C_TANH };
		static const calc_cfn inv[] = { CALC_C_ASIN, CALC_C_ACOS, CALC_C_ATAN, CALC_C_ASINH,
		    CALC_C_ACOSH, CALC_C_ATANH };
		size_t k;
		calc_cplx w, back;

		z = calc_c_make(0.3 + 0.17 * (double)i, 0.9 - 0.11 * (double)i);
		for (k = 0; k < COUNT(fwd); k++) {
			CHECK_ST(calc_c_func(inv[k], z, &w), CALC_OK);
			CHECK_ST(calc_c_func(fwd[k], w, &back), CALC_OK);
			CHECK_TOL(back.re, z.re, 1e-11);
			CHECK_TOL(back.im, z.im, 1e-11);
		}
		CHECK_ST(calc_c_func(CALC_C_LOG, z, &w), CALC_OK);
		CHECK_ST(calc_c_func(CALC_C_EXP, w, &back), CALC_OK);
		CHECK_TOL(back.re, z.re, 1e-12);
		CHECK_TOL(back.im, z.im, 1e-12);
		CHECK_ST(calc_c_func(CALC_C_SQRT, z, &w), CALC_OK);
		CHECK_ST(calc_c_mul(w, w, &back), CALC_OK);
		CHECK_TOL(back.re, z.re, 1e-12);
		CHECK_TOL(back.im, z.im, 1e-12);
	}
}

/* --- 5.8 quaternions ----------------------------------------------------- */

static void
check_quat(calc_quat q, double w, double x, double y, double z, int line)
{
	checks += 4;
	if (!near(q.w, w, TOL) || !near(q.x, x, TOL) || !near(q.y, y, TOL) || !near(q.z, z, TOL)) {
		failures++;
		printf("FAIL %s:%d: [%.17g, %.17g, %.17g, %.17g], expected [%.17g, %.17g, %.17g, %.17g]\n",
		    __FILE__, line, q.w, q.x, q.y, q.z, w, x, y, z);
	}
}

#define CHECK_QUAT(q, w, x, y, z) check_quat((q), (w), (x), (y), (z), __LINE__)

static calc_quat
Q(double w, double x, double y, double z)
{
	calc_quat q;

	q.w = w;
	q.x = x;
	q.y = y;
	q.z = z;
	return q;
}

static void
test_quaternions(void)
{
	calc_quat q, inv;
	double mag;

	CHECK_QUAT(calc_quat_mul(Q(0, 1, 0, 0), Q(0, 0, 1, 0)), 0, 0, 0, 1);
	CHECK_QUAT(calc_quat_mul(Q(1, 0, 0, 0), Q(0.5, 0.5, 0.5, 0.5)), 0.5, 0.5, 0.5, 0.5);
	CHECK_QUAT(calc_quat_mul(Q(0.5, 0.5, 0.5, 0.5), Q(0.5, -0.5, 0.5, -0.5)), 0.5, -0.5, 0.5, 0.5);
	CHECK_QUAT(calc_quat_mul(Q(1, 2, 3, 4), Q(5, 6, 7, 8)), -60, 12, 30, 24);
	CHECK_QUAT(calc_quat_conj(Q(1, 2, 3, 4)), 1, -2, -3, -4);
	CHECK_QUAT(calc_quat_conj(Q(0.5, 0.5, -0.5, 0.5)), 0.5, -0.5, 0.5, -0.5);

	/* extra: the true inverse divides by the squared norm */
	CHECK_ST(calc_quat_inverse(Q(1, 2, 3, 4), &inv), CALC_OK);
	CHECK_QUAT(inv, 1.0 / 30, -2.0 / 30, -3.0 / 30, -4.0 / 30);
	CHECK_QUAT(calc_quat_mul(Q(1, 2, 3, 4), inv), 1, 0, 0, 0);
	CHECK_ST(calc_quat_inverse(Q(0, 0, 0, 0), &inv), CALC_E_DOMAIN);

	/*
	 * extra: the norm is formed after scaling by the largest component, so
	 * a tiny quaternion does not underflow to a domain error and a huge one
	 * does not overflow the squared norm to infinity (inverse 0). The
	 * inverse of (3, 4, 0, 12) * k is (3, -4, 0, -12) / (169 * k).
	 */
	{
		static const double scale[] = { 1e-200, 1e-170, 1e-100, 1e100, 1e170, 1e200 };
		size_t si;

		for (si = 0; si < COUNT(scale); si++) {
			double k = scale[si];

			CHECK_ST(calc_quat_inverse(Q(3 * k, 4 * k, 0, 12 * k), &inv), CALC_OK);
			CHECK_TOL(inv.w * k, 3.0 / 169, TOL);
			CHECK_TOL(inv.x * k, -4.0 / 169, TOL);
			CHECK(inv.y == 0.0);
			CHECK_TOL(inv.z * k, -12.0 / 169, TOL);
			CHECK_QUAT(calc_quat_mul(Q(3 * k, 4 * k, 0, 12 * k), inv), 1, 0, 0, 0);
		}
		CHECK_ST(calc_quat_inverse(Q(1e-200, 0, 0, 0), &inv), CALC_OK);
		CHECK_TOL(inv.w * 1e-200, 1.0, TOL);
		CHECK_ST(calc_quat_inverse(Q(1e200, 0, 0, 0), &inv), CALC_OK);
		CHECK_TOL(inv.w * 1e200, 1.0, TOL);
		CHECK_ST(calc_quat_inverse(Q(0, 0, -1e200, 0), &inv), CALC_OK);
		CHECK_TOL(inv.y * 1e200, 1.0, TOL);
		/* non-finite components and an unrepresentable inverse leave out untouched */
		inv = Q(7, 7, 7, 7);
		CHECK_ST(calc_quat_inverse(Q(NAN, 1, 0, 0), &inv), CALC_E_DOMAIN);
		CHECK_ST(calc_quat_inverse(Q(1, 0, INFINITY, 0), &inv), CALC_E_DOMAIN);
		CHECK_ST(calc_quat_inverse(Q(1e-310, 0, 0, 0), &inv), CALC_E_RANGE);
		CHECK(inv.w == 7.0 && inv.x == 7.0 && inv.y == 7.0 && inv.z == 7.0);
		CHECK_ST(calc_quat_inverse(Q(1, 0, 0, 0), NULL), CALC_E_ARG);
	}

	CHECK_ST(calc_quat_from_vector(3, 4, 12, &q, &mag), CALC_OK);
	CHECK_QUAT(q, 0, 0.230769230769231, 0.307692307692308, 0.923076923076923);
	CHECK_NEAR(mag, 13);
	CHECK_ST(calc_quat_from_vector(0, 0, 2, &q, &mag), CALC_OK);
	CHECK_QUAT(q, 0, 0, 0, 1);
	CHECK_NEAR(mag, 2);
	CHECK_ST(calc_quat_from_vector(1, -1, 1, &q, &mag), CALC_OK);
	CHECK_QUAT(q, 0, 0.577350269189626, -0.577350269189626, 0.577350269189626);
	CHECK_NEAR(mag, 1.73205080756888);
	CHECK_ST(calc_quat_from_vector(0, 0, 0, &q, &mag), CALC_E_DOMAIN);

	CHECK_ST(calc_quat_from_axis_angle(0, 0, 1, 90, CALC_DEGREES, &q), CALC_OK);
	CHECK_QUAT(q, 0.707106781186548, 0, 0, 0.707106781186547);
	CHECK_ST(calc_quat_from_axis_angle(0, 0, 2, 90, CALC_DEGREES, &q), CALC_OK);
	CHECK_QUAT(q, 0.707106781186548, 0, 0, 0.707106781186547);
	CHECK_ST(calc_quat_from_axis_angle(1, 1, 1, 120, CALC_DEGREES, &q), CALC_OK);
	CHECK_QUAT(q, 0.5, 0.5, 0.5, 0.5);
	CHECK_ST(calc_quat_from_axis_angle(1, 0, 0, 3.14159265358979, CALC_RADIANS, &q), CALC_OK);
	CHECK_QUAT(q, 6.12323399573677e-17, 1, 0, 0);
	CHECK_ST(calc_quat_from_axis_angle(0, 0, 0, 1, CALC_RADIANS, &q), CALC_E_DOMAIN);

	CHECK_QUAT(calc_quat_from_euler(0, 0, 0, CALC_RADIANS), 1, 0, 0, 0);
	CHECK_QUAT(calc_quat_from_euler(0.3, 0.5, 0.7, CALC_RADIANS),
	    0.887272187679753, 0.279443894078474, -0.052132410889548, 0.363237369728236);
	CHECK_QUAT(calc_quat_from_euler(90, 0, 0, CALC_DEGREES),
	    0.707106781186548, 0, -0.707106781186547, 0);
	CHECK_QUAT(calc_quat_from_euler(0, 30, 0, CALC_DEGREES), 0.965925826289068, 0.258819045102521, 0, 0);
	CHECK_QUAT(calc_quat_from_euler(0, 0, 45, CALC_DEGREES), 0.923879532511287, 0, 0, 0.38268343236509);

	CHECK_QUAT(calc_quat_rotate(Q(0.707106781186548, 0, 0, 0.707106781186547), Q(0, 1, 0, 0)),
	    0, 2.22044604925031e-16, 1, 0);
	CHECK_QUAT(calc_quat_rotate(Q(6.12323399573677e-17, 1, 0, 0), Q(0, 0, 1, 0)),
	    0, 0, -1, 1.22464679914735e-16);
	CHECK_QUAT(calc_quat_rotate(Q(0.707106781186548, 0, 0.707106781186547, 0), Q(0, 0, 0, 1)),
	    0, 1, 0, 2.22044604925031e-16);
	CHECK_QUAT(calc_quat_rotate(Q(0.5, 0.5, 0.5, 0.5), Q(0, 1, 0, 0)),
	    0, 1.11022302462516e-16, 1, -1.11022302462516e-16);
}

/* --- 5.9 CRC ------------------------------------------------------------- */

static const uint8_t digits[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

static uint64_t
crc_of(const calc_crc_spec *spec, const uint8_t *d, size_t n)
{
	uint64_t v = ~(uint64_t)0;

	if (calc_crc(spec, d, n, &v) != CALC_OK)
		return ~(uint64_t)0;
	return v;
}

#define SPEC(w, p, i, x, ri, ro) { w, p, i, x, ri, ro }

static void
test_crc(void)
{
	static const struct {
		const char *name;
		calc_crc_spec s;
		uint64_t check;
	} cat[] = {
		{ "CRC-8", SPEC(8, 0x7, 0x0, 0x0, 0, 0), 0xF4 },
		{ "CRC-8/MAXIM", SPEC(8, 0x31, 0x0, 0x0, 1, 1), 0xA1 },
		{ "CRC-8/DARC", SPEC(8, 0x39, 0x0, 0x0, 1, 1), 0x15 },
		{ "CRC-8/SAE-J1850", SPEC(8, 0x1D, 0xFF, 0xFF, 0, 0), 0x4B },
		{ "CRC-8/BLUETOOTH", SPEC(8, 0xA7, 0x0, 0x0, 1, 1), 0x26 },
		{ "CRC-12/DECT", SPEC(12, 0x80F, 0x0, 0x0, 0, 0), 0xF5B },
		{ "CRC-12/UMTS", SPEC(12, 0x80F, 0x0, 0x0, 0, 1), 0xDAF },
		{ "CRC-12/GSM", SPEC(12, 0xD31, 0x0, 0xFFF, 0, 0), 0xB34 },
		{ "CRC-12/CDMA2000", SPEC(12, 0xF13, 0xFFF, 0x0, 0, 0), 0xD4D },
		{ "CRC-16/ARC", SPEC(16, 0x8005, 0x0, 0x0, 1, 1), 0xBB3D },
		{ "CRC-16/MODBUS", SPEC(16, 0x8005, 0xFFFF, 0x0, 1, 1), 0x4B37 },
		{ "CRC-16/CCITT-FALSE", SPEC(16, 0x1021, 0xFFFF, 0x0, 0, 0), 0x29B1 },
		{ "CRC-16/XMODEM", SPEC(16, 0x1021, 0x0, 0x0, 0, 0), 0x31C3 },
		{ "CRC-16/KERMIT", SPEC(16, 0x1021, 0x0, 0x0, 1, 1), 0x2189 },
		{ "CRC-16/DNP", SPEC(16, 0x3D65, 0x0, 0xFFFF, 1, 1), 0xEA82 },
		{ "CRC-32/ISO-HDLC", SPEC(32, 0x4C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, 1, 1), 0xCBF43926 },
		{ "CRC-32/BZIP2", SPEC(32, 0x4C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0), 0xFC891918 },
		{ "CRC-32/MPEG-2", SPEC(32, 0x4C11DB7, 0xFFFFFFFF, 0x0, 0, 0), 0x0376E6E7 },
		{ "CRC-32C", SPEC(32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, 1, 1), 0xE3069283 },
		{ "CRC-32Q", SPEC(32, 0x814141AB, 0x0, 0x0, 0, 0), 0x3010BF7F },
		{ "CRC-64/ECMA-182", SPEC(64, 0x42F0E1EBA9EA3693ULL, 0x0, 0x0, 0, 0), 0x6C40DF5F0B497347ULL },
		/* extra: widths below 8 and the 64-bit reflected form */
		{ "CRC-3/GSM", SPEC(3, 0x3, 0x0, 0x7, 0, 0), 0x4 },
		{ "CRC-5/USB", SPEC(5, 0x05, 0x1F, 0x1F, 1, 1), 0x19 },
		{ "CRC-7/MMC", SPEC(7, 0x09, 0x0, 0x0, 0, 0), 0x75 },
		{ "CRC-64/XZ", SPEC(64, 0x42F0E1EBA9EA3693ULL, ~(uint64_t)0, ~(uint64_t)0, 1, 1),
		    0x995DC9BBDF1939FAULL },
	};
	static const uint8_t m0[] = { 0, 1, 2, 3 };
	static const uint8_t mfox[] = "The quick brown fox jumps over the lazy dog";
	static const uint8_t mff[] = { 0xFF };
	static const struct {
		const uint8_t *d;
		size_t n;
		uint64_t v[8];
	} msg[] = {
		{ NULL, 0, { 0x00, 0x00, 0x000, 0x000, 0x0000, 0xFFFF, 0x00000000, 0x00000000 } },
		{ m0, 4, { 0x48, 0xD8, 0x851, 0x8A1, 0xA110, 0xE5F1, 0x8BB98613, 0xD9331AA3 } },
		{ mfox, sizeof mfox - 1, { 0xC1, 0x16, 0x515, 0xA8A, 0xFCDF, 0x8FDD, 0x414FA339, 0x22620404 } },
		{ mff, 1, { 0xF3, 0x35, 0x606, 0x606, 0x4040, 0xFF00, 0xFF000000, 0xFF000000 } },
	};
	const calc_crc_spec *eight[8];
	static const struct { calc_crc_spec s; uint64_t v[3]; } defs[] = {
		/* the command defaults: CRC8, CRC12, CRC16, CRC32 over three messages */
		{ SPEC(8, 0x07, 0, 0, 0, 0), { 0xF4, 0x48, 0xF3 } },
		{ SPEC(12, 0x80D, 0, 0, 0, 0), { 0xEFB, 0x005, 0x752 } },
		{ SPEC(16, 0x1021, 0, 0, 0, 0), { 0x31C3, 0x6131, 0x1EF0 } },
		{ SPEC(32, 0x4C11DB7, 0, 0, 0, 0), { 0x89A1897F, 0xAC691451, 0xB1F740B4 } },
	};
	uint64_t v;
	size_t i, k;

	for (i = 0; i < COUNT(cat); i++) {
		checks++;
		v = crc_of(&cat[i].s, digits, sizeof digits);
		if (v != cat[i].check) {
			failures++;
			printf("FAIL %s:%d: %s check 0x%llx, expected 0x%llx\n", __FILE__, __LINE__,
			    cat[i].name, (unsigned long long)v, (unsigned long long)cat[i].check);
		}
	}
	/* the presets of the library equal their catalog rows */
	CHECK_U64(crc_of(&calc_crc8, digits, 9), 0xF4);
	CHECK_U64(crc_of(&calc_crc8_maxim, digits, 9), 0xA1);
	CHECK_U64(crc_of(&calc_crc12_dect, digits, 9), 0xF5B);
	CHECK_U64(crc_of(&calc_crc12_umts, digits, 9), 0xDAF);
	CHECK_U64(crc_of(&calc_crc16_arc, digits, 9), 0xBB3D);
	CHECK_U64(crc_of(&calc_crc16_ccitt_false, digits, 9), 0x29B1);
	CHECK_U64(crc_of(&calc_crc16_xmodem, digits, 9), 0x31C3);
	CHECK_U64(crc_of(&calc_crc32, digits, 9), 0xCBF43926);
	CHECK_U64(crc_of(&calc_crc32c, digits, 9), 0xE3069283);
	CHECK_U64(crc_of(&calc_crc32_bzip2, digits, 9), 0xFC891918);
	CHECK_U64(crc_of(&calc_crc64_ecma, digits, 9), 0x6C40DF5F0B497347ULL);

	eight[0] = &calc_crc8;
	eight[1] = &calc_crc8_maxim;
	eight[2] = &calc_crc12_dect;
	eight[3] = &calc_crc12_umts;
	eight[4] = &calc_crc16_arc;
	eight[5] = &calc_crc16_ccitt_false;
	eight[6] = &calc_crc32;
	eight[7] = &calc_crc32c;
	for (i = 0; i < COUNT(msg); i++)
		for (k = 0; k < 8; k++)
			CHECK_U64(crc_of(eight[k], msg[i].d, msg[i].n), msg[i].v[k]);
	for (i = 0; i < COUNT(defs); i++) {
		CHECK_U64(crc_of(&defs[i].s, digits, 9), defs[i].v[0]);
		CHECK_U64(crc_of(&defs[i].s, m0, 4), defs[i].v[1]);
		CHECK_U64(crc_of(&defs[i].s, mff, 1), defs[i].v[2]);
	}
	{
		/* final XOR applies after output reflection for every width */
		static const calc_crc_spec d1 = SPEC(8, 0x7, 0x0, 0x55, 0, 1);
		static const calc_crc_spec d2 = SPEC(32, 0x4C11DB7, 0xFFFFFFFF, 0x12345678, 0, 1);
		static const calc_crc_spec d3 = SPEC(8, 0x7, 0x0, 0xFF, 0, 1);

		CHECK_U64(crc_of(&d1, digits, 9), 0x7A);
		CHECK_U64(crc_of(&d2, digits, 9), 0xF55338B8);
		CHECK_U64(crc_of(&d3, digits, 9), 0xD0);
		/* reflect-in and reflect-out are independent flags */
		{
			static const calc_crc_spec ri = SPEC(16, 0x1021, 0, 0, 1, 0);
			static const calc_crc_spec ro = SPEC(16, 0x1021, 0, 0, 0, 1);

			CHECK_U64(crc_of(&ri, digits, 9), 0x9184);
			CHECK_U64(crc_of(&ro, digits, 9), 0xC38C);
			/* KERMIT is the two flags together */
			CHECK(crc_of(&ri, digits, 9) != crc_of(&ro, digits, 9));
		}
	}
	/* the 12-bit register is masked */
	CHECK(crc_of(&calc_crc12_dect, digits, 9) <= 0xFFF);
	CHECK_U64(crc_of(&calc_crc12_dect, digits, 9), 0xF5B);

	/* errors */
	{
		calc_crc_spec bad = calc_crc8;

		CHECK_ST(calc_crc(&bad, digits, 9, &v), CALC_OK);
		bad.width = 0;
		CHECK_ST(calc_crc(&bad, digits, 9, &v), CALC_E_ARG);
		bad.width = 65;
		CHECK_ST(calc_crc(&bad, digits, 9, &v), CALC_E_ARG);
		bad = calc_crc8;
		bad.poly = 0x107;
		CHECK_ST(calc_crc(&bad, digits, 9, &v), CALC_E_ARG);
		bad = calc_crc8;
		bad.refin = 2;
		CHECK_ST(calc_crc(&bad, digits, 9, &v), CALC_E_ARG);
		CHECK_ST(calc_crc(NULL, digits, 9, &v), CALC_E_ARG);
		CHECK_ST(calc_crc(&calc_crc8, NULL, 9, &v), CALC_E_ARG);
		CHECK_ST(calc_crc(&calc_crc8, NULL, 0, &v), CALC_OK);
	}
}

/* --- 5.10 random numbers ------------------------------------------------- */

static void
test_random(void)
{
	static calc_rng rng;
	uint32_t w[3];
	double s, s2, u, z;
	unsigned long within;

	calc_rng_seed(&rng, 5489);
	w[0] = calc_rng_u32(&rng);
	w[1] = calc_rng_u32(&rng);
	w[2] = calc_rng_u32(&rng);
	CHECK(w[0] == 3499211612u && w[1] == 581869302u && w[2] == 3890346734u);
	{
		uint32_t last = 0;
		unsigned long i;

		calc_rng_seed(&rng, 5489);
		for (i = 0; i < 10000; i++)
			last = calc_rng_u32(&rng);
		CHECK(last == 4123659995u);
	}
	calc_rng_seed(&rng, 1);
	CHECK(calc_rng_u32(&rng) == 1791095845u);
	CHECK(calc_rng_u32(&rng) == 4282876139u);
	CHECK(calc_rng_u32(&rng) == 3093770124u);
	calc_rng_seed(&rng, 0);
	CHECK(calc_rng_u32(&rng) == 2357136044u);
	CHECK(calc_rng_u32(&rng) == 2546248239u);
	CHECK(calc_rng_u32(&rng) == 3071714933u);

	/* unit mapping: w / 2^32, half open */
	calc_rng_seed(&rng, 5489);
	CHECK_TOL(calc_rng_unit(&rng), 0.814723691903055, 1e-15);
	CHECK(3499211612u / 4294967296.0 == 0.81472369190305471);
	CHECK(0u / 4294967296.0 == 0.0);
	CHECK_TOL(4294967295u / 4294967296.0, 0.999999999767169, 1e-15);
	CHECK(4294967295u / 4294967296.0 < 1.0);

	/* reseeding restarts the stream, and two states do not interfere */
	{
		static calc_rng a, b;
		uint32_t x1, x2;

		calc_rng_seed(&a, 77);
		calc_rng_seed(&b, 77);
		x1 = calc_rng_u32(&a);
		(void)calc_rng_u32(&b);
		(void)calc_rng_u32(&b);
		calc_rng_seed(&b, 77);
		x2 = calc_rng_u32(&b);
		CHECK(x1 == x2);
		calc_rng_seed(&a, 78);
		CHECK(calc_rng_u32(&a) != x1);
	}

	/* statistical acceptance, seed 20260101, 200000 draws */
	{
		unsigned long i;
		int ok = 1;

		calc_rng_seed(&rng, 20260101);
		s = 0.0;
		for (i = 0; i < 200000; i++) {
			u = calc_rng_unit(&rng);
			if (!(u >= 0.0 && u < 1.0))
				ok = 0;
			s += u;
		}
		CHECK(ok);
		CHECK(fabs(s / 200000.0 - 0.5) < 0.004);
		ok = 1;
		s = 0.0;
		for (i = 0; i < 200000; i++) {
			u = calc_rng_unit53(&rng);
			if (!(u >= 0.0 && u < 1.0))
				ok = 0;
			s += u;
		}
		CHECK(ok);
		CHECK(fabs(s / 200000.0 - 0.5) < 0.004);

		calc_rng_seed(&rng, 20260101);
		s = s2 = 0.0;
		within = 0;
		for (i = 0; i < 200000; i++) {
			z = calc_rng_normal(&rng, 0.0, 1.0);
			s += z;
			s2 += z * z;
			if (fabs(z) < 1.0)
				within++;
		}
		{
			double mean = s / 200000.0;
			double sd = sqrt((s2 - 200000.0 * mean * mean) / 199999.0);
			double frac = (double)within / 200000.0;

			CHECK(fabs(mean) < 0.012);
			CHECK(fabs(sd - 1.0) < 0.012);
			CHECK(frac > 0.67 && frac < 0.695);
		}
		/* mean and standard deviation parameters */
		calc_rng_seed(&rng, 5);
		s = s2 = 0.0;
		for (i = 0; i < 100000; i++) {
			z = calc_rng_normal(&rng, 10.0, 3.0);
			s += z;
			s2 += z * z;
		}
		CHECK(fabs(s / 100000.0 - 10.0) < 0.05);
		CHECK(fabs(sqrt(s2 / 100000.0 - (s / 100000.0) * (s / 100000.0)) - 3.0) < 0.05);
	}
}

/* --- 5.11 FFT ------------------------------------------------------------ */

static void
real_to_cplx(calc_cplx *x, const double *r, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		x[i].re = r[i];
		x[i].im = 0.0;
	}
}

static void
check_fft(const double *sig, size_t n, const double *xre, const double *xim,
    const double *mag, const double *ph, int line)
{
	calc_cplx x[8], tw[4];
	double m[8], p[8], peak = 0.0;
	size_t i;

	for (i = 0; i < n; i++)
		if (mag[i] > peak)
			peak = mag[i];
	if (peak < 1.0)
		peak = 1.0;
	real_to_cplx(x, sig, n);
	checks++;
	if (calc_fft(x, n, 0, NULL) != CALC_OK) {
		failures++;
		printf("FAIL %s:%d: calc_fft failed\n", __FILE__, line);
		return;
	}
	for (i = 0; i < n; i++) {
		checks += 2;
		if (fabs(x[i].re - xre[i]) > 1e-9 * peak || fabs(x[i].im - xim[i]) > 1e-9 * peak) {
			failures++;
			printf("FAIL %s:%d: bin %zu = (%.17g, %.17g), expected (%.17g, %.17g)\n",
			    __FILE__, line, i, x[i].re, x[i].im, xre[i], xim[i]);
		}
	}
	CHECK_ST(calc_fft_magnitude(x, n, m), CALC_OK);
	CHECK_ST(calc_fft_phase(x, n, p), CALC_OK);
	for (i = 0; i < n; i++) {
		CHECK(fabs(m[i] - mag[i]) <= 1e-9 * peak);
		if (mag[i] > 1e-9)
			CHECK(fabs(p[i] - ph[i]) <= 1e-9);
	}
	/* the same transform through a precomputed table */
	real_to_cplx(x, sig, n);
	if (n >= 2) {
		CHECK_ST(calc_fft_twiddles(tw, n), CALC_OK);
		CHECK_ST(calc_fft(x, n, 0, tw), CALC_OK);
	} else {
		CHECK_ST(calc_fft(x, n, 0, NULL), CALC_OK);
	}
	for (i = 0; i < n; i++)
		CHECK(fabs(x[i].re - xre[i]) <= 1e-9 * peak && fabs(x[i].im - xim[i]) <= 1e-9 * peak);
}

static void
test_fft(void)
{
	static const double s1[] = { 1, 2, 3, 4 };
	static const double s1re[] = { 10, -2, -2, -2 }, s1im[] = { 0, 2, 0, -2 };
	static const double s1m[] = { 10, 2.82842712474619, 2, 2.82842712474619 };
	static const double s1p[] = { 0, 2.35619449019234, 3.14159265358979, -2.35619449019234 };
	static const double s2[] = { 1, 0, 0, 0, 0, 0, 0, 0 };
	static const double s2re[] = { 1, 1, 1, 1, 1, 1, 1, 1 }, s2im[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static const double s2p[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static const double s3[] = { 1, 0.707106781186548, 6.12323399573677e-17, -0.707106781186547,
	    -1, -0.707106781186548, -1.83697019872103e-16, 0.707106781186547 };
	static const double s3re[] = { 0, 4, 0, 0, 0, 0, 0, 4 };
	static const double s3im[] = { 0, -5.58947851566226e-16, 0, 0, 0, 0, 0, 5.58947851566226e-16 };
	static const double s3m[] = { 0, 4, 0, 0, 0, 0, 0, 4 };
	static const double s3p[] = { 0, -1.39736962891556e-16, 0, 0, 0, 0, 0, 1.39736962891556e-16 };
	static const double s4[] = { 1, -1, 1, -1 };
	static const double s4re[] = { 0, 0, 4, 0 }, s4im[] = { 0, 0, 0, 0 };
	static const double s4m[] = { 0, 0, 4, 0 }, s4p[] = { 0, 0, 0, 0 };
	static const double s5[] = { 5 };
	static const double s5re[] = { 5 }, s5im[] = { 0 }, s5m[] = { 5 }, s5p[] = { 0 };
	static const double inv1[] = { 0.5, -1, 2.5, 3, 0, -2, 1, 4 };
	calc_cplx x[8];
	size_t i;

	check_fft(s1, 4, s1re, s1im, s1m, s1p, __LINE__);
	check_fft(s2, 8, s2re, s2im, s2re, s2p, __LINE__);
	check_fft(s3, 8, s3re, s3im, s3m, s3p, __LINE__);
	check_fft(s4, 4, s4re, s4im, s4m, s4p, __LINE__);
	check_fft(s5, 1, s5re, s5im, s5m, s5p, __LINE__);

	/* inverse: forward then inverse restores the signal */
	real_to_cplx(x, inv1, 8);
	CHECK_ST(calc_fft(x, 8, 0, NULL), CALC_OK);
	CHECK_ST(calc_fft(x, 8, 1, NULL), CALC_OK);
	for (i = 0; i < 8; i++) {
		CHECK(fabs(x[i].re - inv1[i]) <= 1e-9 * 4.0);
		CHECK(fabs(x[i].im) <= 1e-9 * 4.0);
	}
	x[0].re = 10; x[0].im = 0;
	x[1].re = -2; x[1].im = 2;
	x[2].re = -2; x[2].im = 0;
	x[3].re = -2; x[3].im = -2;
	CHECK_ST(calc_fft(x, 4, 1, NULL), CALC_OK);
	for (i = 0; i < 4; i++) {
		CHECK(fabs(x[i].re - s1[i]) <= 1e-9 * 4.0);
		CHECK(fabs(x[i].im) <= 1e-9 * 4.0);
	}

	/* sizes */
	CHECK_ST(calc_fft(x, 6, 0, NULL), CALC_E_SIZE);
	CHECK_ST(calc_fft(x, 0, 0, NULL), CALC_E_SIZE);
	CHECK_ST(calc_fft(x, 3, 0, NULL), CALC_E_SIZE);
	CHECK_ST(calc_fft(x, 2 * CALC_FFT_MAX, 0, NULL), CALC_E_RANGE);
	CHECK_ST(calc_fft_twiddles(x, 6), CALC_E_SIZE);
	CHECK_ST(calc_fft_magnitude(x, 0, NULL), CALC_E_ARG);

	/*
	 * extra: against a direct DFT at every power-of-two size from 4 to
	 * CALC_FFT_MAX, with and without the twiddle table. The reference table
	 * W[m] = exp(-2 pi i m / n) comes from the C library, and the sum
	 * indexes it with (k * j) mod n, which is exact.
	 *
	 * Tolerance. A radix-2 FFT of x satisfies, for the 2-norm of the error
	 * vector, |yhat - y| <= log2(n) * eta * |y| with |y| = sqrt(n) * |x|
	 * (Parseval) and eta = mu + gamma_4 (sqrt 2 + mu) about 4 * DBL_EPSILON
	 * when the twiddle error mu is one DBL_EPSILON (N. J. Higham, Accuracy
	 * and Stability of Numerical Algorithms, 2nd ed., section 24.1). The
	 * error of one bin cannot exceed that norm, so each bin is held to
	 * 4 * DBL_EPSILON * log2(n) * sqrt(n) * |x|_2. The reference sum adds
	 * about DBL_EPSILON * |x|_1 (one rounding per term, about 1.1e-13
	 * at n = 1024, against a bound of 5e-12 there). The measured error sits
	 * near 2 percent of the bound, and a twiddle table wrong in the tenth
	 * digit (a relative 1e-10) exceeds it by three to four orders of magnitude.
	 */
	{
		static calc_cplx big[CALC_FFT_MAX], tw[CALC_FFT_MAX / 2];
		static double sig[CALC_FFT_MAX], wr[CALC_FFT_MAX], wi[CALC_FFT_MAX];
		static calc_rng rng;
		size_t n, k, j, m, path;
		unsigned lg;
		double l2, bound, er, ei;

		for (n = 4; n <= CALC_FFT_MAX; n *= 2) {
			for (lg = 0; ((size_t)1 << lg) < n; lg++)
				;
			for (m = 0; m < n; m++) {
				wr[m] = cos(2.0 * 3.14159265358979323846 * (double)m / (double)n);
				wi[m] = -sin(2.0 * 3.14159265358979323846 * (double)m / (double)n);
			}
			for (path = 0; path < 2; path++) {
				calc_rng_seed(&rng, 99);
				l2 = 0.0;
				for (j = 0; j < n; j++) {
					sig[j] = 2.0 * calc_rng_unit(&rng) - 1.0;
					big[j].re = sig[j];
					big[j].im = 0.0;
					l2 += sig[j] * sig[j];
				}
				bound = 4.0 * DBL_EPSILON * (double)lg * sqrt((double)n) * sqrt(l2);
				if (path == 1) {
					CHECK_ST(calc_fft_twiddles(tw, n), CALC_OK);
					CHECK_ST(calc_fft(big, n, 0, tw), CALC_OK);
				} else {
					CHECK_ST(calc_fft(big, n, 0, NULL), CALC_OK);
				}
				for (k = 0; k < n; k++) {
					er = ei = 0.0;
					for (j = 0; j < n; j++) {
						er += sig[j] * wr[(k * j) % n];
						ei += sig[j] * wi[(k * j) % n];
					}
					CHECK(fabs(big[k].re - er) <= bound);
					CHECK(fabs(big[k].im - ei) <= bound);
				}
			}
		}
	}
}

/* --- 5.12 interpolation, scaling, element-wise --------------------------- */

static void
test_lists(void)
{
	double out[8], lo, hi;
	size_t i;

	CHECK_NEAR(calc_lerp(0, 10, 0.25), 2.5);
	CHECK_NEAR(calc_lerp(10, 0, 1.5), -5);
	CHECK_NEAR(calc_lerp(2, 4, 0), 2);
	CHECK_NEAR(calc_lerp(1, 3, 0.5), 2);

	{
		static const double a[] = { 1, 2, 3 }, b[] = { 0, 5, 10 }, c[] = { -4, 0, 4, 8 };
		static const double z[] = { 3, 3, 3 };

		CHECK_ST(calc_rescale(a, 3, 7, 3, out, &lo, &hi), CALC_OK);
		CHECK_NEAR(out[0], 7);
		CHECK_NEAR(out[1], 5);
		CHECK_NEAR(out[2], 3);
		CHECK(lo == 1 && hi == 3);
		CHECK_ST(calc_rescale(b, 3, 0, 1, out, &lo, &hi), CALC_OK);
		CHECK_NEAR(out[0], 0);
		CHECK_NEAR(out[1], 0.5);
		CHECK_NEAR(out[2], 1);
		CHECK(lo == 0 && hi == 10);
		CHECK_ST(calc_rescale(c, 4, -1, 1, out, NULL, NULL), CALC_OK);
		CHECK_NEAR(out[0], -1);
		CHECK_NEAR(out[1], -0.333333333333333);
		CHECK_NEAR(out[2], 0.333333333333333);
		CHECK_NEAR(out[3], 1);
		CHECK_ST(calc_rescale(z, 3, 0, 1, out, &lo, &hi), CALC_E_DOMAIN);
		CHECK_ST(calc_rescale(z, 0, 0, 1, out, &lo, &hi), CALC_E_SIZE);
		{
			double inplace[3] = { 1, 2, 3 };

			CHECK_ST(calc_rescale(inplace, 3, 0, 10, inplace, NULL, NULL), CALC_OK);
			CHECK_NEAR(inplace[1], 5);
			CHECK_ST(calc_rescale(inplace, 2, 0, 1, inplace + 1, NULL, NULL), CALC_E_ALIAS);
		}
	}
	{
		/* extra: table interpolation */
		static const double xs[] = { 0, 1, 3, 6 }, ys[] = { 0, 10, 14, 14 };
		static const double bad[] = { 0, 1, 1, 2 };
		double r;

		CHECK_ST(calc_interp_table(xs, ys, 4, 0.5, &r), CALC_OK);
		CHECK_NEAR(r, 5);
		CHECK_ST(calc_interp_table(xs, ys, 4, 2, &r), CALC_OK);
		CHECK_NEAR(r, 12);
		CHECK_ST(calc_interp_table(xs, ys, 4, 4.5, &r), CALC_OK);
		CHECK_NEAR(r, 14);
		CHECK_ST(calc_interp_table(xs, ys, 4, 1, &r), CALC_OK);
		CHECK(r == 10.0);
		CHECK_ST(calc_interp_table(xs, ys, 4, 0, &r), CALC_OK);
		CHECK(r == 0.0);
		CHECK_ST(calc_interp_table(xs, ys, 4, 6, &r), CALC_OK);
		CHECK(r == 14.0);
		CHECK_ST(calc_interp_table(xs, ys, 4, -0.1, &r), CALC_E_DOMAIN);
		CHECK_ST(calc_interp_table(xs, ys, 4, 6.1, &r), CALC_E_DOMAIN);
		CHECK_ST(calc_interp_table(xs, ys, 4, NAN, &r), CALC_E_DOMAIN);
		CHECK_ST(calc_interp_table(bad, ys, 4, 0.5, &r), CALC_E_ARG);
		CHECK_ST(calc_interp_table(xs, ys, 1, 0, &r), CALC_E_SIZE);
	}
	{
		static const double a[] = { 1, 2, 3 }, p[] = { 4, 9 }, q[] = { 2, -3 };
		static const double s2[] = { 4, -6, 0.5 }, s3[] = { 10 }, e8[] = { 8 };
		static const double neg[] = { -4 };
		static const double zero[] = { 0 };
		double r[3];

		CHECK_ST(calc_scale(a, 3, 2, r), CALC_OK);
		CHECK_NEAR(r[0], 2);
		CHECK_NEAR(r[1], 4);
		CHECK_NEAR(r[2], 6);
		CHECK_ST(calc_scale(s2, 3, -0.5, r), CALC_OK);
		CHECK_NEAR(r[0], -2);
		CHECK_NEAR(r[1], 3);
		CHECK_NEAR(r[2], -0.25);
		CHECK_ST(calc_scale(s3, 1, 0, r), CALC_OK);
		CHECK_NEAR(r[0], 0);
		CHECK_ST(calc_power(a, 3, 2, r), CALC_OK);
		CHECK_NEAR(r[0], 1);
		CHECK_NEAR(r[1], 4);
		CHECK_NEAR(r[2], 9);
		CHECK_ST(calc_power(p, 2, 0.5, r), CALC_OK);
		CHECK_NEAR(r[0], 2);
		CHECK_NEAR(r[1], 3);
		CHECK_ST(calc_power(q, 2, 3, r), CALC_OK);
		CHECK_NEAR(r[0], 8);
		CHECK_NEAR(r[1], -27);
		CHECK_ST(calc_power(e8, 1, -0.333333333333333, r), CALC_OK);
		CHECK_NEAR(r[0], 0.5);
		CHECK_ST(calc_power(neg, 1, 0.5, r), CALC_E_DOMAIN);
		CHECK_ST(calc_power(zero, 1, -1, r), CALC_E_DOMAIN);
		CHECK_ST(calc_power(zero, 1, 0, r), CALC_OK);
		CHECK_NEAR(r[0], 1);
		CHECK_ST(calc_power(neg, 1, -2, r), CALC_OK);
		CHECK_NEAR(r[0], 0.0625);
		CHECK_ST(calc_power(a, 3, 1, r), CALC_OK);
		CHECK_NEAR(r[2], 3);
		/* integer storage rounds only when storing: 0.5 stays 0.5 until the end */
		{
			static const int64_t iv[] = { 3, 4 };
			static const double fv[] = { 3.0, 5.0 };
			int64_t ir[2];

			CHECK_ST(calc_scale_i64(iv, 2, 0.5, ir), CALC_OK);
			CHECK(ir[0] == 2 && ir[1] == 2);
			CHECK_ST(calc_scale_to_i64(fv, 2, 0.5, ir), CALC_OK);
			CHECK(ir[0] == 2 && ir[1] == 3);
			CHECK_ST(calc_scale_to_i64(fv, 2, -0.5, ir), CALC_OK);
			CHECK(ir[0] == -2 && ir[1] == -3);
			CHECK_ST(calc_scale_to_i64(fv, 2, 1e19, ir), CALC_E_RANGE);
			CHECK_ST(calc_scale_i64(iv, 2, 1.0, ir), CALC_OK);
			CHECK(ir[0] == 3 && ir[1] == 4);
		}
	}
	{
		static const int64_t one[] = { 1 }, five[] = { 5 }, m16[] = { -16 }, k1000[] = { 1000 };
		static const int64_t m1[] = { -1 }, c256[] = { 256 };
		int64_t r[2];

		CHECK_ST(calc_i64_shift(one, 1, 3, 0, r), CALC_OK);
		CHECK(r[0] == 8);
		CHECK_ST(calc_i64_shift(five, 1, 62, 0, r), CALC_OK);
		CHECK(r[0] == 4611686018427387904LL);
		CHECK_ST(calc_i64_shift(one, 1, 63, 0, r), CALC_OK);
		CHECK(r[0] == INT64_MIN);
		CHECK_ST(calc_i64_shift(m16, 1, -2, 0, r), CALC_OK);
		CHECK(r[0] == -4);
		CHECK_ST(calc_i64_shift(k1000, 1, -3, 0, r), CALC_OK);
		CHECK(r[0] == 125);
		CHECK_ST(calc_i64_shift(m1, 1, -63, 0, r), CALC_OK);
		CHECK(r[0] == -1);
		CHECK_ST(calc_i64_shift(m16, 1, -2, 1, r), CALC_OK);
		CHECK(r[0] == 4611686018427387900LL);
		CHECK_ST(calc_i64_shift(m1, 1, -1, 1, r), CALC_OK);
		CHECK(r[0] == INT64_MAX);
		CHECK_ST(calc_i64_shift(c256, 1, -4, 1, r), CALC_OK);
		CHECK(r[0] == 16);
		CHECK_ST(calc_i64_shift(m16, 1, 0, 0, r), CALC_OK);
		CHECK(r[0] == -16);
		CHECK_ST(calc_i64_shift(one, 1, 64, 0, r), CALC_E_ARG);
		CHECK_ST(calc_i64_shift(one, 1, -64, 0, r), CALC_E_ARG);
	}
	{
		static const double a1[] = { 1, 2 }, b1[] = { 3, 4 };
		static const double a2[] = { 10, 5 }, b2[] = { 3, 7 };
		static const double a3[] = { 2, 3 }, b3[] = { 4, -5 };
		static const double a4[] = { 9, 1 }, b4[] = { 3, 4 }, b0[] = { 3, 0 };
		static const int64_t i1[] = { 12, 10 }, i2[] = { 10, 6 };
		static const int64_t d1[] = { 7, -7 }, d2[] = { 2, 2 }, dz[] = { 2, 0 };
		static const int64_t w1[] = { INT64_MAX, INT64_MIN }, w2[] = { 1, -1 };
		static const int64_t mm[] = { INT64_MIN }, mn[] = { -1 };
		double r[2];
		int64_t q[2];

		CHECK_ST(calc_vec_add(a1, b1, 2, r), CALC_OK);
		CHECK_NEAR(r[0], 4);
		CHECK_NEAR(r[1], 6);
		CHECK_ST(calc_vec_sub(a2, b2, 2, r), CALC_OK);
		CHECK_NEAR(r[0], 7);
		CHECK_NEAR(r[1], -2);
		CHECK_ST(calc_vec_mul(a3, b3, 2, r), CALC_OK);
		CHECK_NEAR(r[0], 8);
		CHECK_NEAR(r[1], -15);
		CHECK_ST(calc_vec_div(a4, b4, 2, r), CALC_OK);
		CHECK_NEAR(r[0], 3);
		CHECK_NEAR(r[1], 0.25);
		CHECK_ST(calc_vec_div(a4, b0, 2, r), CALC_E_DOMAIN);
		CHECK_ST(calc_i64_and(i1, i2, 2, q), CALC_OK);
		CHECK(q[0] == 8 && q[1] == 2);
		CHECK_ST(calc_i64_or(i1, i2, 2, q), CALC_OK);
		CHECK(q[0] == 14 && q[1] == 14);
		CHECK_ST(calc_i64_xor(i1, i2, 2, q), CALC_OK);
		CHECK(q[0] == 6 && q[1] == 12);
		CHECK_ST(calc_i64_div(d1, d2, 2, q), CALC_OK);
		CHECK(q[0] == 3 && q[1] == -3);
		CHECK_ST(calc_i64_div(d1, dz, 2, q), CALC_E_DOMAIN);
		CHECK_ST(calc_i64_div(mm, mn, 1, q), CALC_OK);
		CHECK(q[0] == INT64_MIN);
		CHECK_ST(calc_i64_add(w1, w2, 2, q), CALC_OK);
		CHECK(q[0] == INT64_MIN && q[1] == INT64_MAX);
		CHECK_ST(calc_i64_sub(w1, w2, 2, q), CALC_OK);
		CHECK(q[0] == INT64_MAX - 1 && q[1] == INT64_MIN + 1);
		CHECK_ST(calc_i64_mul(w1, w1, 2, q), CALC_OK);
		CHECK(q[0] == 1 && q[1] == 0);
		/* operands of unequal length surface as a zero length here, and as aliasing */
		CHECK_ST(calc_vec_add(a1, b1, 0, r), CALC_E_SIZE);
		{
			double buf[4] = { 1, 2, 3, 4 };

			CHECK_ST(calc_vec_add(buf, buf + 1, 2, buf), CALC_E_ALIAS);
			CHECK_ST(calc_vec_add(buf, buf + 2, 2, buf), CALC_OK);
			CHECK_NEAR(buf[0], 4);
			CHECK_NEAR(buf[1], 6);
		}
		{
			static const double x[] = { 0, 10 }, y[] = { 10, 0 };

			CHECK_ST(calc_blend(x, y, 2, 0.25, r), CALC_OK);
			CHECK_NEAR(r[0], 2.5);
			CHECK_NEAR(r[1], 7.5);
			CHECK_ST(calc_blend(x, y, 2, 1.5, r), CALC_OK);
			CHECK_NEAR(r[0], 15);
			CHECK_NEAR(r[1], -5);
			CHECK_ST(calc_blend(x, y, 2, 0.5, (double *)(uintptr_t)x), CALC_E_ALIAS);
		}
	}
	(void)i;
}

/* --- 5.13 base64 --------------------------------------------------------- */

static void
test_base64(void)
{
	static const struct { const char *hex; size_t n; const char *enc; } v[] = {
		{ "", 0, "" }, { "66", 1, "Zg==" }, { "666f", 2, "Zm8=" },
		{ "666f6f", 3, "Zm9v" }, { "666f6f62", 4, "Zm9vYg==" },
		{ "666f6f6261", 5, "Zm9vYmE=" }, { "666f6f626172", 6, "Zm9vYmFy" },
		{ "000102", 3, "AAEC" }, { "fffefdfc", 4, "//79/A==" },
	};
	uint8_t bytes[8], back[8];
	char text[16];
	size_t i, k, w;

	for (i = 0; i < COUNT(v); i++) {
		for (k = 0; k < v[i].n; k++) {
			unsigned b;

			sscanf(v[i].hex + 2 * k, "%2x", &b);
			bytes[k] = (uint8_t)b;
		}
		memset(text, '#', sizeof text);
		CHECK_ST(calc_b64_encode(bytes, v[i].n, text, sizeof text, &w), CALC_OK);
		CHECK(w == strlen(v[i].enc));
		CHECK(memcmp(text, v[i].enc, w) == 0);
		CHECK(text[w] == '#');
		CHECK_ST(calc_b64_decode(v[i].enc, strlen(v[i].enc), back, sizeof back, &w), CALC_OK);
		CHECK(w == v[i].n);
		CHECK(memcmp(back, bytes, v[i].n) == 0);
		/* exact capacity is enough, one byte less is not */
		if (v[i].n != 0) {
			CHECK_ST(calc_b64_encode(bytes, v[i].n, text, strlen(v[i].enc), &w), CALC_OK);
			CHECK_ST(calc_b64_encode(bytes, v[i].n, text, strlen(v[i].enc) - 1, &w), CALC_E_SIZE);
			CHECK_ST(calc_b64_decode(v[i].enc, strlen(v[i].enc), back, v[i].n, &w), CALC_OK);
			CHECK_ST(calc_b64_decode(v[i].enc, strlen(v[i].enc), back, v[i].n - 1, &w), CALC_E_SIZE);
		}
	}
	CHECK_ST(calc_b64_decode("Zm9v!A==", 8, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("Zm9", 3, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("Zm9vY", 5, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("Zg=A", 4, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("=Zg=", 4, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("Zg===", 5, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("Zg==Zg==", 8, back, sizeof back, &w), CALC_E_INVALID);
	CHECK_ST(calc_b64_decode("Zm9v-_8=", 8, back, sizeof back, &w), CALC_E_INVALID);
	/* whitespace is skipped */
	CHECK_ST(calc_b64_decode("Zm9v\r\nYmFy\n", 11, back, sizeof back, &w), CALC_OK);
	CHECK(w == 6 && memcmp(back, "foobar", 6) == 0);
	/* nothing is written on an error */
	memset(back, 0xAA, sizeof back);
	CHECK_ST(calc_b64_decode("Zm9vYg=!", 8, back, sizeof back, &w), CALC_E_INVALID);
	CHECK(back[0] == 0xAA && back[1] == 0xAA);
	/* a long input round trips */
	{
		static uint8_t big[300], big2[300];
		static char enc[404];
		size_t j;

		for (j = 0; j < sizeof big; j++)
			big[j] = (uint8_t)(j * 7 + 3);
		CHECK_ST(calc_b64_encode(big, sizeof big, enc, sizeof enc, &w), CALC_OK);
		CHECK(w == 400);
		CHECK_ST(calc_b64_decode(enc, w, big2, sizeof big2, &w), CALC_OK);
		CHECK(w == sizeof big && memcmp(big, big2, sizeof big) == 0);
	}
}

/* --- 5.14 PID ------------------------------------------------------------ */

static void
test_pid(void)
{
	static const struct {
		double kp, ki, kd, tau, olo, ohi, ilo, ihi, T, sp;
		double m[5];
		size_t n;
		double want[5];
	} v[] = {
		{ 2, 0.5, 0.1, 0.02, -10, 10, -5, 5, 0.1, 1, { 0, 0.2, 0.5, 0.9, 1 }, 5,
		    { 2.025, 1.38428571428571, 0.796377551020408, -0.122733236151604, 0.165814244064973 } },
		{ 100, 10, 0, 0.05, -10, 10, -2, 2, 0.01, 1, { 0, 0, 0.1, 0, 0 }, 3, { 10, 10, 10, 0, 0 } },
		{ 0, 1, 0, 0.05, -1, 1, -0.25, 0.25, 0.5, 2, { 0, 0, 0, 0, 0 }, 4, { 0.25, 0.25, 0.25, 0.25, 0 } },
	};
	calc_pid c;
	size_t i, k;

	for (i = 0; i < COUNT(v); i++) {
		CHECK_ST(calc_pid_init(&c, v[i].kp, v[i].ki, v[i].kd, v[i].tau, v[i].T,
		    v[i].olo, v[i].ohi, v[i].ilo, v[i].ihi), CALC_OK);
		for (k = 0; k < v[i].n; k++)
			CHECK_NEAR(calc_pid_update(&c, v[i].sp, v[i].m[k]), v[i].want[k]);
	}
	/* extra: parameter validation */
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, 0.1, 0.0005, -1, 1, -1, 1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, 0.1, 0.001, -1, 1, -1, 1), CALC_OK);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, 0.1, 0.1, 1, -1, -1, 1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, 0.1, 0.1, -1, 1, 1, -1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, -0.1, 0.1, -1, 1, -1, 1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, 0.0, 0.1, -1, 1, -1, 1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, -0.0, 0.1, -1, 1, -1, 1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(&c, 1, 0, 0, 1e-300, 0.1, -1, 1, -1, 1), CALC_OK);
	CHECK_ST(calc_pid_init(&c, NAN, 0, 0, 0.1, 0.1, -1, 1, -1, 1), CALC_E_ARG);
	CHECK_ST(calc_pid_init(NULL, 1, 0, 0, 0.1, 0.1, -1, 1, -1, 1), CALC_E_ARG);
}

/*
 * Properties of the derivative term that need no stored vector. With Kp = Ki
 * = 0 the output is the derivative term d[n] = (-2 Kd (m[n] - m[n-1]) +
 * (2 tau - T) d[n-1]) / (2 tau + T), whose pole (2 tau - T) / (2 tau + T)
 * is positive for tau >= T / 2.
 */
static void
test_pid_properties(void)
{
	static const struct { double kd, tau, T, r; } ramp[] = {
		{ 2.5, 0.3, 0.1, 4 },		/* pole 5/7 */
		{ 1, 0.02, 0.1, -3 },		/* pole -3/7: rings while it decays */
		{ 0.5, 0.05, 0.1, 10 },		/* pole 0 */
		{ 4, 1, 0.01, 0.25 },		/* pole near 1 */
	};
	calc_pid c;
	double prev, cur;
	size_t i, k;

	for (i = 0; i < COUNT(ramp); i++) {
		CHECK_ST(calc_pid_init(&c, 0, 0, ramp[i].kd, ramp[i].tau, ramp[i].T,
		    -1000, 1000, -1, 1), CALC_OK);
		cur = 0;
		for (k = 1; k <= 3000; k++)
			cur = calc_pid_update(&c, 0, (double)k * ramp[i].T * ramp[i].r);
		/* a ramp of slope r settles at -Kd r, whatever T and tau are */
		CHECK_TOL(cur, -ramp[i].kd * ramp[i].r, 1e-9);
	}

	/* a held step: the derivative term decays geometrically with a positive ratio */
	for (i = 0; i < 3; i++) {
		static const double tau[] = { 0.3, 0.05, 2.0 };

		CHECK_ST(calc_pid_init(&c, 0, 0, 1, tau[i], 0.1, -100, 100, -1, 1), CALC_OK);
		prev = calc_pid_update(&c, 0, 1);
		CHECK_NEAR(prev, -2.0 / (2.0 * tau[i] + 0.1));
		for (k = 0; k < 20; k++) {
			cur = calc_pid_update(&c, 0, 1);
			CHECK(cur <= 0.0);
			CHECK(cur == 0.0 || cur > prev);	/* negative and shrinking in magnitude */
			CHECK_NEAR(cur, prev * (2.0 * tau[i] - 0.1) / (2.0 * tau[i] + 0.1));
			prev = cur;
			if (prev == 0.0)
				break;
		}
	}
}


/* --- accuracy sweeps against the host C library --------------------------- */

static double worst_sincos, worst_hyper, worst_log10, worst_inv, worst_atan3;

static double
rel_err(double got, long double want)
{
	long double d = (long double)got - want;
	long double m = want < 0 ? -want : want;

	if (d < 0)
		d = -d;
	if (m < 1.0L)
		m = 1.0L;
	return (double)(d / m);
}

static void
test_accuracy(void)
{
	static calc_rng rng;
	double x, out, e;
	long double xl;
	calc_cplx z;
	int i;

	calc_rng_seed(&rng, 4242);
	for (i = 0; i < 20000; i++) {
		x = (2.0 * calc_rng_unit(&rng) - 1.0) * (i < 10000 ? 7.0 : 100000.0);
		z = calc_c_polar(1.0, x, CALC_RADIANS);
		xl = x;
		e = rel_err(z.re, cosl(xl));
		if (e > worst_sincos)
			worst_sincos = e;
		e = rel_err(z.im, sinl(xl));
		if (e > worst_sincos)
			worst_sincos = e;
	}
	CHECK(worst_sincos <= 1e-15);

	for (i = 0; i < 20000; i++) {
		x = (2.0 * calc_rng_unit(&rng) - 1.0) * (i < 10000 ? 1.0 : 30.0);
		xl = x;
		if (calc_sinh(x, &out) == CALC_OK && (e = rel_err(out, sinhl(xl))) > worst_hyper)
			worst_hyper = e;
		if (calc_cosh(x, &out) == CALC_OK && (e = rel_err(out, coshl(xl))) > worst_hyper)
			worst_hyper = e;
		if (calc_tanh(x, &out) == CALC_OK && (e = rel_err(out, tanhl(xl))) > worst_hyper)
			worst_hyper = e;
	}
	CHECK(worst_hyper <= 1e-13);

	for (i = 0; i < 20000; i++) {
		x = exp(40.0 * calc_rng_unit(&rng) - 20.0);
		xl = x;
		if (calc_log10(x, &out) == CALC_OK && (e = rel_err(out, log10l(xl))) > worst_log10)
			worst_log10 = e;
	}
	CHECK(worst_log10 <= 1e-13);

	for (i = 0; i < 20000; i++) {
		x = (2.0 * calc_rng_unit(&rng) - 1.0) * 30.0;
		xl = x;
		e = rel_err(calc_asinh(x), asinhl(xl));
		if (e > worst_inv)
			worst_inv = e;
		x = 1.0 + 30.0 * calc_rng_unit(&rng);
		xl = x;
		e = rel_err(calc_acosh(x), acoshl(xl));
		if (e > worst_inv)
			worst_inv = e;
		x = 2.0 * calc_rng_unit(&rng) - 1.0;
		xl = x;
		e = rel_err(calc_atanh(x), atanhl(xl));
		if (e > worst_inv)
			worst_inv = e;
		x = (2.0 * calc_rng_unit(&rng) - 1.0) * 10.0;
		e = (double)((long double)calc_atan3(x, 1.0, CALC_RADIANS) -
		    (atan2l((long double)x, 1.0L) < 0 ? atan2l((long double)x, 1.0L) + 6.283185307179586476925L
		    : atan2l((long double)x, 1.0L)));
		if (e < 0)
			e = -e;
		if (e > worst_atan3)
			worst_atan3 = e;
	}
	CHECK(worst_inv <= 1e-13);
	CHECK(worst_atan3 <= 1e-14);
}

/*
 * Compare the elementary complex functions with the host's <complex.h>.
 * The ARM run defines CALC_TEST_NO_HOST_COMPLEX: newlib's catan, casinh,
 * cacosh and catanh do not follow C99 Annex G on the branch cuts and at
 * signed zeros, so they are no oracle there. The Annex G table in
 * complex_vectors.h covers those points on every variant.
 */
#ifndef CALC_TEST_NO_HOST_COMPLEX
static void
test_complex_grid(void)
{
	static const double axis[] = { -2.5, -1.0, -0.4, -0.0, 0.0, 0.4, 1.0, 2.5 };
	static const struct { calc_cfn fn; double complex (*ref)(double complex); } f[] = {
		{ CALC_C_EXP, cexp }, { CALC_C_LOG, clog }, { CALC_C_SQRT, csqrt },
		{ CALC_C_SIN, csin }, { CALC_C_COS, ccos }, { CALC_C_TAN, ctan },
		{ CALC_C_ASIN, casin }, { CALC_C_ACOS, cacos }, { CALC_C_ATAN, catan },
		{ CALC_C_SINH, csinh }, { CALC_C_COSH, ccosh }, { CALC_C_TANH, ctanh },
		{ CALC_C_ASINH, casinh }, { CALC_C_ACOSH, cacosh }, { CALC_C_ATANH, catanh },
	};
	size_t k, i, j;
	calc_cplx z, r;
	double complex w, ref;
	calc_status st;
	unsigned long compared = 0;

	for (k = 0; k < COUNT(f); k++) {
		for (i = 0; i < COUNT(axis); i++) {
			for (j = 0; j < COUNT(axis); j++) {
				z = calc_c_make(axis[i], axis[j]);
				/* axis[i] + axis[j] * I would lose a -0 imaginary part; the
				 * pair of doubles is the layout of double complex. */
				{
					double parts[2];

					parts[0] = axis[i];
					parts[1] = axis[j];
					memcpy(&w, parts, sizeof w);
				}
				ref = f[k].ref(w);
				st = calc_c_func(f[k].fn, z, &r);
				checks++;
				if (st != CALC_OK) {
					if (isfinite(creal(ref)) && isfinite(cimag(ref))) {
						failures++;
						printf("FAIL %s:%d: fn %d at (%g, %g) status %d, host (%g, %g)\n",
						    __FILE__, __LINE__, (int)f[k].fn, axis[i], axis[j],
						    (int)st, creal(ref), cimag(ref));
					}
					continue;
				}
				compared++;
				checks += 2;
				if (!near(r.re, creal(ref), 1e-12) || !near(r.im, cimag(ref), 1e-12)) {
					failures++;
					printf("FAIL %s:%d: fn %d at (%g, %g) = (%.17g, %.17g), host (%.17g, %.17g)\n",
					    __FILE__, __LINE__, (int)f[k].fn, axis[i], axis[j], r.re, r.im,
					    creal(ref), cimag(ref));
				}
			}
		}
	}
	CHECK(compared > 800);
}
#endif

/* --- NULL required pointers ---------------------------------------------- */

/*
 * Every status-returning entry point, called once per required pointer with
 * that pointer NULL and the rest valid, must return CALC_E_ARG. A fault is a
 * failure that the run reports and survives, so a deleted guard shows up as a
 * FAIL line and not as a crash. The ARM run defines CALC_TEST_NO_SIGNALS: the
 * bare-metal C library has no sigaction, and a fault there ends the run.
 */
#ifndef CALC_TEST_NO_SIGNALS
static sigjmp_buf null_env;
static volatile sig_atomic_t null_armed;

static void
null_fault(int sig)
{
	if (null_armed)
		siglongjmp(null_env, 1);
	signal(sig, SIG_DFL);
	raise(sig);
}

/* Installed with sigaction: signal() may reset the handler on delivery under POSIX feature macros. */
static void
catch_faults(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = null_fault;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
}

#define NULL_ARG(call) do {							\
	int st_ = -1;								\
	checks++;								\
	null_armed = 1;								\
	if (sigsetjmp(null_env, 1) == 0) {					\
		st_ = (int)(call);						\
		null_armed = 0;							\
		if (st_ != (int)CALC_E_ARG) {					\
			failures++;						\
			printf("FAIL %s:%d: %s returned %d, expected CALC_E_ARG\n",	\
			    __FILE__, __LINE__, #call, st_);			\
		}								\
	} else {								\
		null_armed = 0;							\
		failures++;							\
		printf("FAIL %s:%d: %s faulted on a NULL required pointer\n",	\
		    __FILE__, __LINE__, #call);					\
	}									\
} while (0)
#else
#define NULL_ARG(call) CHECK_ST(call, CALC_E_ARG)
#endif

static void
test_null_arguments(void)
{
	static const double da[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	static const double db[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
	static const double mat[4] = { 2, 1, 1, 3 };
	static const int64_t ia[4] = { 1, 2, 3, 4 };
	static const int32_t ja[4] = { 1, 2, 3, 4 };
	static const uint8_t bytes[3] = { 1, 2, 3 };
	double o1[8], o2[8], w[4], c1;
	size_t sz, df;
	long li;
	int64_t io[4];
	int32_t jx[4], jy[4];
	calc_cplx x[8];
	calc_quat q, qi = { 1, 0, 0, 0 };
	uint64_t crc;
	char text[8];
	uint8_t raw[8];
	calc_cplx one = { 1, 2 }, two = { 3, 4 };

#ifndef CALC_TEST_NO_SIGNALS
	catch_faults();
#endif
	memset(x, 0, sizeof x);

	/* scalar */
	NULL_ARG(calc_sinh(1, NULL));
	NULL_ARG(calc_cosh(1, NULL));
	NULL_ARG(calc_tanh(1, NULL));
	NULL_ARG(calc_log10(2, NULL));
	/* statistics */
	NULL_ARG(calc_sum(NULL, 4, o1));
	NULL_ARG(calc_sum(da, 4, NULL));
	NULL_ARG(calc_mean(NULL, 4, o1));
	NULL_ARG(calc_mean(da, 4, NULL));
	NULL_ARG(calc_median(NULL, 4, o2, o1));
	NULL_ARG(calc_median(da, 4, NULL, o1));
	NULL_ARG(calc_median(da, 4, o2, NULL));
	NULL_ARG(calc_variance(NULL, 4, 0, o1));
	NULL_ARG(calc_variance(da, 4, 0, NULL));
	NULL_ARG(calc_stddev(NULL, 4, 0, o1));
	NULL_ARG(calc_stddev(da, 4, 0, NULL));
	NULL_ARG(calc_stddev(da, 4, 1, NULL));
	NULL_ARG(calc_min(NULL, 4, o1, &sz));
	NULL_ARG(calc_min(da, 4, NULL, &sz));
	NULL_ARG(calc_max(NULL, 4, o1, &sz));
	NULL_ARG(calc_max(da, 4, NULL, &sz));
	NULL_ARG(calc_correl(NULL, db, 4, o1));
	NULL_ARG(calc_correl(da, NULL, 4, o1));
	NULL_ARG(calc_correl(da, db, 4, NULL));
	NULL_ARG(calc_chi_square(NULL, 2, 2, o1, &df));
	NULL_ARG(calc_chi_square(da, 2, 2, NULL, &df));
	NULL_ARG(calc_chi_square(da, 2, 2, o1, NULL));
	NULL_ARG(calc_chi_square_p(1.0, 1, NULL));
	NULL_ARG(calc_crossing(NULL, 8, 0.5, 1, &li));
	NULL_ARG(calc_crossing(da, 8, 0.5, 1, NULL));
	/* vectors */
	NULL_ARG(calc_dot(NULL, db, 4, o1));
	NULL_ARG(calc_dot(da, NULL, 4, o1));
	NULL_ARG(calc_dot(da, db, 4, NULL));
	NULL_ARG(calc_magnitude(NULL, 4, o1));
	NULL_ARG(calc_magnitude(da, 4, NULL));
	NULL_ARG(calc_normalize(NULL, 4, o1));
	NULL_ARG(calc_normalize(da, 4, NULL));
	NULL_ARG(calc_cross3(NULL, db, o1));
	NULL_ARG(calc_cross3(da, NULL, o1));
	NULL_ARG(calc_cross3(da, db, NULL));
	NULL_ARG(calc_rotate2d(0, 0, 1, CALC_RADIANS, NULL, db, 3, o1, o2));
	NULL_ARG(calc_rotate2d(0, 0, 1, CALC_RADIANS, da, NULL, 3, o1, o2));
	NULL_ARG(calc_rotate2d(0, 0, 1, CALC_RADIANS, da, db, 3, NULL, o2));
	NULL_ARG(calc_rotate2d(0, 0, 1, CALC_RADIANS, da, db, 3, o1, NULL));
	NULL_ARG(calc_rotate2d_i32(0, 0, 1, CALC_RADIANS, NULL, ja, 3, jx, jy));
	NULL_ARG(calc_rotate2d_i32(0, 0, 1, CALC_RADIANS, ja, NULL, 3, jx, jy));
	NULL_ARG(calc_rotate2d_i32(0, 0, 1, CALC_RADIANS, ja, ja, 3, NULL, jy));
	NULL_ARG(calc_rotate2d_i32(0, 0, 1, CALC_RADIANS, ja, ja, 3, jx, NULL));
	/* matrices */
	NULL_ARG(calc_mat_mul(NULL, 2, 2, mat, 2, o1));
	NULL_ARG(calc_mat_mul(mat, 2, 2, NULL, 2, o1));
	NULL_ARG(calc_mat_mul(mat, 2, 2, mat, 2, NULL));
	NULL_ARG(calc_mat_vec(NULL, 2, 2, da, o1));
	NULL_ARG(calc_mat_vec(mat, 2, 2, NULL, o1));
	NULL_ARG(calc_mat_vec(mat, 2, 2, da, NULL));
	NULL_ARG(calc_mat_transpose(NULL, 2, 2, o1));
	NULL_ARG(calc_mat_transpose(mat, 2, 2, NULL));
	NULL_ARG(calc_mat_det(NULL, 2, w, o1));
	NULL_ARG(calc_mat_det(mat, 2, NULL, o1));
	NULL_ARG(calc_mat_det(mat, 2, w, NULL));
	NULL_ARG(calc_mat_inverse(NULL, 2, w, o1));
	NULL_ARG(calc_mat_inverse(mat, 2, NULL, o1));
	NULL_ARG(calc_mat_inverse(mat, 2, w, NULL));
	NULL_ARG(calc_mat_solve(NULL, 2, db, 1, w, o1));
	NULL_ARG(calc_mat_solve(mat, 2, NULL, 1, w, o1));
	NULL_ARG(calc_mat_solve(mat, 2, db, 1, NULL, o1));
	NULL_ARG(calc_mat_solve(mat, 2, db, 1, w, NULL));
	NULL_ARG(calc_mat_cond(NULL, 2, w, &c1));
	NULL_ARG(calc_mat_cond(mat, 2, NULL, &c1));
	NULL_ARG(calc_mat_cond(mat, 2, w, NULL));
	/* complex */
	NULL_ARG(calc_c_add(one, two, NULL));
	NULL_ARG(calc_c_sub(one, two, NULL));
	NULL_ARG(calc_c_mul(one, two, NULL));
	NULL_ARG(calc_c_div(one, two, NULL));
	NULL_ARG(calc_c_pow(one, two, NULL));
	NULL_ARG(calc_c_func(CALC_C_EXP, one, NULL));
	/* quaternions */
	NULL_ARG(calc_quat_inverse(qi, NULL));
	NULL_ARG(calc_quat_from_vector(1, 2, 3, NULL, o1));
	NULL_ARG(calc_quat_from_vector(1, 2, 3, &q, NULL));
	NULL_ARG(calc_quat_from_axis_angle(1, 0, 0, 1, CALC_RADIANS, NULL));
	/* FFT */
	NULL_ARG(calc_fft_twiddles(NULL, 8));
	NULL_ARG(calc_fft(NULL, 8, 0, NULL));
	NULL_ARG(calc_fft_magnitude(NULL, 8, o1));
	NULL_ARG(calc_fft_magnitude(x, 8, NULL));
	NULL_ARG(calc_fft_phase(NULL, 8, o1));
	NULL_ARG(calc_fft_phase(x, 8, NULL));
	/* interpolation and scaling */
	NULL_ARG(calc_rescale(NULL, 4, 0, 1, o1, NULL, NULL));
	NULL_ARG(calc_rescale(da, 4, 0, 1, NULL, NULL, NULL));
	NULL_ARG(calc_interp_table(NULL, da, 4, 2.5, o1));
	NULL_ARG(calc_interp_table(da, NULL, 4, 2.5, o1));
	NULL_ARG(calc_interp_table(da, da, 4, 2.5, NULL));
	/* element-wise lists */
	NULL_ARG(calc_vec_add(NULL, db, 4, o1));
	NULL_ARG(calc_vec_add(da, NULL, 4, o1));
	NULL_ARG(calc_vec_add(da, db, 4, NULL));
	NULL_ARG(calc_vec_sub(NULL, db, 4, o1));
	NULL_ARG(calc_vec_sub(da, NULL, 4, o1));
	NULL_ARG(calc_vec_sub(da, db, 4, NULL));
	NULL_ARG(calc_vec_mul(NULL, db, 4, o1));
	NULL_ARG(calc_vec_mul(da, NULL, 4, o1));
	NULL_ARG(calc_vec_mul(da, db, 4, NULL));
	NULL_ARG(calc_vec_div(NULL, db, 4, o1));
	NULL_ARG(calc_vec_div(da, NULL, 4, o1));
	NULL_ARG(calc_vec_div(da, db, 4, NULL));
	NULL_ARG(calc_i64_add(NULL, ia, 4, io));
	NULL_ARG(calc_i64_add(ia, NULL, 4, io));
	NULL_ARG(calc_i64_add(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_sub(NULL, ia, 4, io));
	NULL_ARG(calc_i64_sub(ia, NULL, 4, io));
	NULL_ARG(calc_i64_sub(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_mul(NULL, ia, 4, io));
	NULL_ARG(calc_i64_mul(ia, NULL, 4, io));
	NULL_ARG(calc_i64_mul(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_div(NULL, ia, 4, io));
	NULL_ARG(calc_i64_div(ia, NULL, 4, io));
	NULL_ARG(calc_i64_div(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_and(NULL, ia, 4, io));
	NULL_ARG(calc_i64_and(ia, NULL, 4, io));
	NULL_ARG(calc_i64_and(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_or(NULL, ia, 4, io));
	NULL_ARG(calc_i64_or(ia, NULL, 4, io));
	NULL_ARG(calc_i64_or(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_xor(NULL, ia, 4, io));
	NULL_ARG(calc_i64_xor(ia, NULL, 4, io));
	NULL_ARG(calc_i64_xor(ia, ia, 4, NULL));
	NULL_ARG(calc_i64_shift(NULL, 4, 1, 0, io));
	NULL_ARG(calc_i64_shift(ia, 4, 1, 0, NULL));
	NULL_ARG(calc_scale(NULL, 4, 2, o1));
	NULL_ARG(calc_scale(da, 4, 2, NULL));
	NULL_ARG(calc_scale_to_i64(NULL, 4, 2, io));
	NULL_ARG(calc_scale_to_i64(da, 4, 2, NULL));
	NULL_ARG(calc_scale_i64(NULL, 4, 2, io));
	NULL_ARG(calc_scale_i64(ia, 4, 2, NULL));
	NULL_ARG(calc_power(NULL, 4, 2, o1));
	NULL_ARG(calc_power(da, 4, 2, NULL));
	NULL_ARG(calc_blend(NULL, db, 4, 0.5, o1));
	NULL_ARG(calc_blend(da, NULL, 4, 0.5, o1));
	NULL_ARG(calc_blend(da, db, 4, 0.5, NULL));
	/* CRC, base64, PID */
	NULL_ARG(calc_crc(NULL, bytes, 3, &crc));
	NULL_ARG(calc_crc(&calc_crc8, NULL, 3, &crc));
	NULL_ARG(calc_crc(&calc_crc8, bytes, 3, NULL));
	NULL_ARG(calc_b64_encode(NULL, 3, text, sizeof text, &sz));
	NULL_ARG(calc_b64_encode(bytes, 3, NULL, sizeof text, &sz));
	NULL_ARG(calc_b64_encode(bytes, 3, text, sizeof text, NULL));
	NULL_ARG(calc_b64_decode(NULL, 4, raw, sizeof raw, &sz));
	NULL_ARG(calc_b64_decode("Zm9v", 4, NULL, sizeof raw, &sz));
	NULL_ARG(calc_b64_decode("Zm9v", 4, raw, sizeof raw, NULL));
	NULL_ARG(calc_pid_init(NULL, 1, 0, 0, 0.1, 0.1, -1, 1, -1, 1));
}

/* --- range: intermediates that leave the double range --------------------- */

/*
 * Scale-invariance family. A routine f with f(2^k x) = 2^(p k) f(x) must
 * return exactly that for k = -1000, -500, 500 and 1000 on inputs whose scaled
 * values stay normal (the ROM provider flushes subnormals, so no input here
 * needs one). The base inputs reach 2^23, so at k = 1000 the sums, squares or
 * products of the plain formula overflow while the exact result is normal.
 * p is 1 for mean, median, standard deviation, magnitude, chi-square and the
 * determinant with one row scaled, 2 for variance, 0 for correlation,
 * normalization, rescaling, table interpolation and the condition estimate.
 * The relation is exact, not toleranced: a power of two commutes with
 * rounding while no intermediate leaves the normal range.
 */
static const int kscale[] = { -1000, -500, 500, 1000 };

static void
scale_list(const double *in, double *out, size_t n, int k)
{
	size_t i;

	for (i = 0; i < n; i++)
		out[i] = ldexp(in[i], k);
}

static void
test_scale_statistics(void)
{
	static const double s1[] = { 8388608, 8388608, 1.5, -3, 0.000244140625 };
	static const double s2[] = { 0.000244140625, 1.5, 8388607, 8388608, 8388608, 8388608 };
	static const double s3[] = { 1, 2, 3, 4, 5.5, 100000 };
	static const double s4[] = { 0, 0, 0, 6144 };
	static const double s5[] = { 1, 2, 3, 4, 5.5, 100 };
	static const double cx[] = { 1, 2, 3, 4, 5, 6.5 };
	static const double cy[] = { 2, 4, 5, 4, 5, 9 };
	static const double t1[] = { 8388608, 4194304, 2097152, 6291456 };
	static const double t2[] = { 12, 5, 7, 9, 3, 1, 8388608, 2 };
	double a[8], b[8], scratch[8], out, base, base2, chi;
	size_t i, df;
	int k;

	/* independent values for the unscaled inputs */
	CHECK_ST(calc_mean(s1, 5, &base), CALC_OK);
	CHECK_NEAR(base / 3355442.900048828125, 1.0);
	CHECK_ST(calc_median(s2, 6, scratch, &out), CALC_OK);
	CHECK(out == 8388607.5);
	CHECK_ST(calc_variance(s4, 4, 0, &out), CALC_OK);
	CHECK(out == 7077888.0);
	CHECK_ST(calc_variance(s4, 4, 1, &out), CALC_OK);
	CHECK(out == 9437184.0);

	for (i = 0; i < COUNT(kscale); i++) {
		k = kscale[i];

		scale_list(s1, a, 5, k);
		CHECK_ST(calc_mean(s1, 5, &base), CALC_OK);
		CHECK_ST(calc_mean(a, 5, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, k));

		scale_list(s2, a, 6, k);
		CHECK_ST(calc_median(s2, 6, scratch, &base), CALC_OK);
		CHECK_ST(calc_median(a, 6, scratch, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, k));

		scale_list(s3, a, 6, k);
		CHECK_ST(calc_stddev(s3, 6, 1, &base), CALC_OK);
		CHECK_ST(calc_stddev(a, 6, 1, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, k));
		CHECK_ST(calc_stddev(s3, 6, 0, &base), CALC_OK);
		CHECK_ST(calc_stddev(a, 6, 0, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, k));
		scale_list(s4, a, 4, k);
		CHECK_ST(calc_stddev(s4, 4, 0, &base), CALC_OK);
		CHECK_ST(calc_stddev(a, 4, 0, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, k));

		scale_list(cx, a, 6, k);
		scale_list(cy, b, 6, -k);
		CHECK_ST(calc_correl(cx, cy, 6, &base), CALC_OK);
		CHECK_NEAR(base, 0.8839203772962249);
		CHECK_ST(calc_correl(a, cy, 6, &out), CALC_OK);
		CHECK_EXACT(out, base);
		CHECK_ST(calc_correl(cx, b, 6, &out), CALC_OK);
		CHECK_EXACT(out, base);
		CHECK_ST(calc_correl(a, b, 6, &out), CALC_OK);
		CHECK_EXACT(out, base);

		scale_list(t1, a, 4, k);
		CHECK_ST(calc_chi_square(t1, 2, 2, &base, &df), CALC_OK);
		CHECK_ST(calc_chi_square(a, 2, 2, &out, &df), CALC_OK);
		CHECK_EXACT(out, ldexp(base, k));
		scale_list(t2, a, 8, k);
		CHECK_ST(calc_chi_square(t2, 4, 2, &base2, &df), CALC_OK);
		CHECK_ST(calc_chi_square(a, 4, 2, &chi, &df), CALC_OK);
		CHECK_EXACT(chi, ldexp(base2, k));
	}

	/* variance scales by 2^(2k); the base inputs keep 2^(2k) var normal at k = +-500 */
	for (i = 1; i < 3; i++) {
		k = kscale[i];
		scale_list(s4, a, 4, k);
		CHECK_ST(calc_variance(s4, 4, 0, &base), CALC_OK);
		CHECK_ST(calc_variance(a, 4, 0, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, 2 * k));
		CHECK_ST(calc_variance(s4, 4, 1, &base), CALC_OK);
		CHECK_ST(calc_variance(a, 4, 1, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, 2 * k));
		scale_list(s5, a, 6, k);
		CHECK_ST(calc_variance(s5, 6, 1, &base), CALC_OK);
		CHECK_ST(calc_variance(a, 6, 1, &out), CALC_OK);
		CHECK_EXACT(out, ldexp(base, 2 * k));
	}
}

static void
test_scale_vectors(void)
{
	static const double v1[] = { 8388608, -8388608, 8388608, 4194304 };
	static const double v2[] = { 3, -4, 12, 0.001 };
	double a[4], base[4], out[4], m0, m1, h[3], hb[3];
	calc_quat q0, q1;
	size_t i, j;
	int k;

	CHECK_ST(calc_normalize(v1, 4, base), CALC_OK);
	CHECK_NEAR(base[0], 0.5547001962252291);
	CHECK_NEAR(base[3], 0.2773500981126146);
	for (i = 0; i < COUNT(kscale); i++) {
		k = kscale[i];
		scale_list(v1, a, 4, k);
		CHECK_ST(calc_normalize(a, 4, out), CALC_OK);
		for (j = 0; j < 4; j++)
			CHECK_EXACT(out[j], base[j]);
		/* in place, and the quaternion constructors built on the same direction */
		CHECK_ST(calc_normalize(a, 4, a), CALC_OK);
		for (j = 0; j < 4; j++)
			CHECK_EXACT(a[j], base[j]);

		scale_list(v2, a, 4, k);
		CHECK_ST(calc_normalize(v2, 4, base), CALC_OK);
		CHECK_ST(calc_normalize(a, 4, out), CALC_OK);
		for (j = 0; j < 4; j++)
			CHECK_EXACT(out[j], base[j]);
		CHECK_ST(calc_magnitude(v2, 4, &m0), CALC_OK);
		CHECK_ST(calc_magnitude(a, 4, &m1), CALC_OK);
		CHECK_EXACT(m1, ldexp(m0, k));
		CHECK_ST(calc_normalize(v1, 4, base), CALC_OK);

		scale_list(v2, h, 3, k);
		CHECK_ST(calc_quat_from_vector(v2[0], v2[1], v2[2], &q0, &m0), CALC_OK);
		CHECK_ST(calc_quat_from_vector(h[0], h[1], h[2], &q1, &m1), CALC_OK);
		CHECK(q1.w == 0.0);
		CHECK_EXACT(q1.x, q0.x);
		CHECK_EXACT(q1.y, q0.y);
		CHECK_EXACT(q1.z, q0.z);
		CHECK_EXACT(m1, ldexp(m0, k));
		CHECK_ST(calc_quat_from_axis_angle(v2[0], v2[1], v2[2], 1.0, CALC_RADIANS, &q0), CALC_OK);
		CHECK_ST(calc_quat_from_axis_angle(h[0], h[1], h[2], 1.0, CALC_RADIANS, &q1), CALC_OK);
		CHECK_EXACT(q1.w, q0.w);
		CHECK_EXACT(q1.x, q0.x);
		CHECK_EXACT(q1.y, q0.y);
		CHECK_EXACT(q1.z, q0.z);
	}
	(void)hb;

	/* a vector whose norm exceeds DBL_MAX: exact direction, infinite length */
	{
		static const double big3[] = { DBL_MAX, DBL_MAX, DBL_MAX };
		static const double neg3[] = { -DBL_MAX, 0, DBL_MAX };
		double u[3];

		CHECK_ST(calc_normalize(big3, 3, u), CALC_OK);
		CHECK_TOL(u[0], 0.5773502691896258, 1e-15);
		CHECK_EXACT(u[1], u[0]);
		CHECK_EXACT(u[2], u[0]);
		CHECK_ST(calc_normalize(neg3, 3, u), CALC_OK);
		CHECK_TOL(u[0], -0.7071067811865476, 1e-15);
		CHECK(u[1] == 0.0);
		CHECK_TOL(u[2], 0.7071067811865476, 1e-15);
		CHECK_ST(calc_magnitude(big3, 3, &m0), CALC_OK);
		CHECK(m0 > DBL_MAX);
		CHECK_ST(calc_quat_from_vector(DBL_MAX, DBL_MAX, DBL_MAX, &q0, &m0), CALC_OK);
		CHECK(q0.w == 0.0);
		CHECK_TOL(q0.x, 0.5773502691896258, 1e-15);
		CHECK_TOL(q0.y, 0.5773502691896258, 1e-15);
		CHECK_TOL(q0.z, 0.5773502691896258, 1e-15);
		CHECK(m0 > DBL_MAX);
		CHECK_ST(calc_quat_from_axis_angle(DBL_MAX, 0, 0, 90, CALC_DEGREES, &q0), CALC_OK);
		CHECK_TOL(q0.w, 0.7071067811865476, 1e-15);
		CHECK_TOL(q0.x, 0.7071067811865476, 1e-15);
		CHECK(q0.y == 0.0 && q0.z == 0.0);
		CHECK_ST(calc_quat_from_axis_angle(DBL_MAX, DBL_MAX, DBL_MAX, 180, CALC_DEGREES, &q0), CALC_OK);
		CHECK_TOL(q0.x, 0.5773502691896258, 1e-15);
		/* the zero vector and a NaN component keep their statuses */
		CHECK_ST(calc_normalize(h, 0, u), CALC_E_SIZE);
		h[0] = h[1] = h[2] = 0;
		CHECK_ST(calc_normalize(h, 3, u), CALC_E_DOMAIN);
		CHECK_ST(calc_quat_from_vector(0, 0, 0, &q0, &m0), CALC_E_DOMAIN);
		CHECK_ST(calc_quat_from_axis_angle(0, 0, 0, 1, CALC_RADIANS, &q0), CALC_E_DOMAIN);
		h[0] = NAN;
		h[1] = 1;
		CHECK_ST(calc_normalize(h, 3, u), CALC_OK);
		CHECK(u[0] != u[0]);
	}
}

static void
test_scale_matrices(void)
{
	static const double m2[] = { 4, 1, 2, 3 };
	static const double m3[] = { 4, -2, 1, 3, 6, -4, 2, 1, 8 };
	static const double tri[] = { 2, 3, 5, 0, 7, 11, 0, 0, 13 };
	double a[144], w[144], inv[144], base, got;
	size_t i, n, j;
	int k;

	/* DBL_MIN times the identity: the estimate is 1 / DBL_MIN and the vectors must not overflow */
	for (n = 2; n <= 12; n += 10) {
		for (j = 0; j < n * n; j++)
			a[j] = 0.0;
		for (j = 0; j < n; j++)
			a[j * n + j] = DBL_MIN;
		CHECK_ST(calc_mat_cond(a, n, w, &got), CALC_OK);
		CHECK_EXACT(got, 1.0);
		CHECK_ST(calc_mat_inverse(a, n, w, inv), CALC_OK);
		CHECK_EXACT(inv[0], 1.0 / DBL_MIN);
		CHECK_EXACT(inv[n + 1], 1.0 / DBL_MIN);
	}

	for (i = 0; i < COUNT(kscale); i++) {
		k = kscale[i];
		for (j = 0; j < 4; j++)
			a[j] = ldexp(m2[j], k);
		CHECK_ST(calc_mat_cond(m2, 2, w, &base), CALC_OK);
		CHECK_ST(calc_mat_cond(a, 2, w, &got), CALC_OK);
		CHECK_EXACT(got, base);
		for (j = 0; j < 9; j++)
			a[j] = ldexp(m3[j], k);
		CHECK_ST(calc_mat_cond(m3, 3, w, &base), CALC_OK);
		CHECK_ST(calc_mat_cond(a, 3, w, &got), CALC_OK);
		CHECK_EXACT(got, base);
		CHECK_ST(calc_mat_inverse(a, 3, w, inv), CALC_OK);

		/* the determinant with one row scaled by 2^k scales by 2^k */
		for (j = 0; j < 9; j++)
			a[j] = j < 3 ? ldexp(tri[j], k) : tri[j];
		CHECK_ST(calc_mat_det(a, 3, w, &got), CALC_OK);
		CHECK_EXACT(got, ldexp(182.0, k));
	}
}

/* Every ordering of a diagonal whose exact determinant is representable gives that value. */
static void
permute_det(double *d, size_t n, size_t at, double want)
{
	double a[16], w[16], got, tmp;
	size_t i, j;

	if (at == n) {
		for (i = 0; i < n * n; i++)
			a[i] = 0.0;
		for (i = 0; i < n; i++)
			a[i * n + i] = d[i];
		CHECK_ST(calc_mat_det(a, n, w, &got), CALC_OK);
		CHECK_EXACT(got, want);
		return;
	}
	for (j = at; j < n; j++) {
		tmp = d[at];
		d[at] = d[j];
		d[j] = tmp;
		permute_det(d, n, at + 1, want);
		tmp = d[at];
		d[at] = d[j];
		d[j] = tmp;
	}
}

static void
test_determinant_range(void)
{
	double d4[] = { 0x1p600, 0x1p600, 0x1p-600, 0x1p-600 };
	double d4m[] = { 1.5 * 0x1p700, 1.25 * 0x1p-700, 0x1p500, 0x1p-500 };
	double d4n[] = { -1.5 * 0x1p700, 1.25 * 0x1p-700, 0x1p500, 0x1p-500 };
	double a[144], w[144], got;
	size_t i, j;

	permute_det(d4, 4, 0, 1.0);
	permute_det(d4m, 4, 0, 1.875);
	permute_det(d4n, 4, 0, -1.875);
	/* twelve pivots: 2^600 six times and 2^-600 six times, in the worst order */
	for (j = 0; j < 144; j++)
		a[j] = 0.0;
	for (i = 0; i < 12; i++)
		a[i * 12 + i] = i < 6 ? 0x1p600 : 0x1p-600;
	CHECK_ST(calc_mat_det(a, 12, w, &got), CALC_OK);
	CHECK_EXACT(got, 1.0);
	for (i = 0; i < 12; i++)
		a[i * 12 + i] = i < 6 ? 0x1p-600 : 0x1p600;
	CHECK_ST(calc_mat_det(a, 12, w, &got), CALC_OK);
	CHECK_EXACT(got, 1.0);
	/* a row exchange: det [[0, 2^600], [2^-600, 0]] = -1 */
	a[0] = 0.0;
	a[1] = 0x1p600;
	a[2] = 0x1p-600;
	a[3] = 0.0;
	CHECK_ST(calc_mat_det(a, 2, w, &got), CALC_OK);
	CHECK_EXACT(got, -1.0);
	/* a determinant outside the double range is the infinity or the zero it rounds to */
	a[0] = 0x1p600;
	a[1] = 0.0;
	a[2] = 0.0;
	a[3] = 0x1p600;
	CHECK_ST(calc_mat_det(a, 2, w, &got), CALC_OK);
	CHECK(got > DBL_MAX);
	a[0] = -0x1p-600;
	a[3] = 0x1p-600;
	CHECK_ST(calc_mat_det(a, 2, w, &got), CALC_OK);
	CHECK_EXACT(got, -0.0);
	/* a zero pivot still gives exactly 0, and the plain product is unchanged */
	a[0] = 3.0;
	a[1] = 1.0;
	a[2] = 6.0;
	a[3] = 2.0;
	CHECK_ST(calc_mat_det(a, 2, w, &got), CALC_OK);
	CHECK(got == 0.0);
	a[0] = 3.0;
	a[3] = 5.0;
	a[1] = 0.0;
	a[2] = 0.0;
	CHECK_ST(calc_mat_det(a, 2, w, &got), CALC_OK);
	CHECK_EXACT(got, 15.0);
}

static void
test_scale_lists(void)
{
	static const double r1[] = { -8388608, 0, 4194304, 8388608 };
	static const double xs0[] = { -8388608, 8388608 };
	static const double xq[] = { -8388608, -4194304, 0, 1000.5, 8388608 };
	static const double ys0[] = { 0, 1 };
	double a[8], base[8], out[8], xs[2], r0, r;
	size_t i, j;
	int k;

	CHECK_ST(calc_rescale(r1, 4, -1, 1, base, NULL, NULL), CALC_OK);
	CHECK(base[0] == -1.0 && base[1] == 0.0 && base[2] == 0.5 && base[3] == 1.0);
	for (i = 0; i < COUNT(kscale); i++) {
		k = kscale[i];
		scale_list(r1, a, 4, k);
		CHECK_ST(calc_rescale(a, 4, -1, 1, out, &r0, &r), CALC_OK);
		for (j = 0; j < 4; j++)
			CHECK_EXACT(out[j], base[j]);
		CHECK_EXACT(r0, ldexp(-8388608.0, k));
		CHECK_EXACT(r, ldexp(8388608.0, k));

		xs[0] = ldexp(xs0[0], k);
		xs[1] = ldexp(xs0[1], k);
		for (j = 0; j < COUNT(xq); j++) {
			CHECK_ST(calc_interp_table(xs0, ys0, 2, xq[j], &r0), CALC_OK);
			CHECK_ST(calc_interp_table(xs, ys0, 2, ldexp(xq[j], k), &r), CALC_OK);
			CHECK_EXACT(r, r0);
		}
	}
	/* input ranges beyond DBL_MAX */
	{
		static const double wide[] = { -1e308, 0, 1e308 };
		static const double xw[] = { -1e308, 1e308 };

		CHECK_ST(calc_rescale(wide, 3, 0, 1, out, &r0, &r), CALC_OK);
		CHECK(out[0] == 0.0 && out[1] == 0.5 && out[2] == 1.0);
		CHECK(r0 == -1e308 && r == 1e308);
		CHECK_ST(calc_rescale(wide, 3, 10, -10, out, NULL, NULL), CALC_OK);
		CHECK(out[0] == 10.0 && out[1] == 0.0 && out[2] == -10.0);
		CHECK_ST(calc_interp_table(xw, ys0, 2, 0.0, &r), CALC_OK);
		CHECK(r == 0.5);
		CHECK_ST(calc_interp_table(xw, ys0, 2, 5e307, &r), CALC_OK);
		CHECK_NEAR(r, 0.75);
		CHECK_ST(calc_interp_table(xw, ys0, 2, -1e308, &r), CALC_OK);
		CHECK(r == 0.0);
	}
}

static void
test_statistics_range(void)
{
	static const double two[] = { 1e308, 1e308 };
	static const double three[] = { 1e308, 1e308, -1e308 };
	static const double pair[] = { 1.7e308, 1.6e308 };
	static const double opp[] = { -1.7e308, 1.7e308 };
	static const double lone[] = { 0, 0, 0, 2e154 };
	static const double sym[] = { -1e200, 1e200 };
	static const double rev[] = { 1e200, -1e200 };
	static const double tiny[] = { -1e-200, 1e-200 };
	static const double table[] = { 1, 2, 3, 4 };
	static const double huge_table[] = { 1e300, 2e300, 3e300, 4e300 };
	static const double full[] = { 1e308, 1e308, 1e308, 1e308 };
	static const double small_table[] = { 1e-160, 2e-160, 3e-160, 4e-160 };
	static const double nanl[] = { 1, NAN, 3 };
	static const double infl[] = { 1e308, INFINITY };
	double scratch[4], out, ref;
	size_t df;

	/* sums and averages whose plain forms overflow */
	CHECK_ST(calc_mean(two, 2, &out), CALC_OK);
	CHECK_NEAR(out / 1e308, 1.0);
	CHECK_ST(calc_mean(three, 3, &out), CALC_OK);
	CHECK_NEAR(out / (1e308 / 3.0), 1.0);
	CHECK_ST(calc_median(two, 2, scratch, &out), CALC_OK);
	CHECK_NEAR(out / 1e308, 1.0);
	CHECK_ST(calc_median(pair, 2, scratch, &out), CALC_OK);
	CHECK_NEAR(out / 1.65e308, 1.0);
	CHECK_ST(calc_median(opp, 2, scratch, &out), CALC_OK);
	CHECK(out == 0.0);
	/* non-finite input keeps the plain result */
	CHECK_ST(calc_mean(nanl, 3, &out), CALC_OK);
	CHECK(out != out);
	CHECK_ST(calc_mean(infl, 2, &out), CALC_OK);
	CHECK(out == INFINITY);

	/* squares that overflow although the variance or deviation does not */
	CHECK_ST(calc_variance(lone, 4, 0, &out), CALC_OK);
	CHECK_NEAR(out / 7.5e307, 1.0);
	CHECK_ST(calc_variance(two, 2, 0, &out), CALC_OK);	/* a constant list: the mean overflows */
	CHECK(out == 0.0);
	CHECK_ST(calc_stddev(two, 2, 1, &out), CALC_OK);
	CHECK(out == 0.0);
	CHECK_ST(calc_stddev(sym, 2, 0, &out), CALC_OK);
	CHECK_NEAR(out / 1e200, 1.0);
	CHECK_ST(calc_stddev(sym, 2, 1, &out), CALC_OK);
	CHECK_NEAR(out / 1.4142135623730951e200, 1.0);
	CHECK_ST(calc_variance(sym, 2, 0, &out), CALC_OK);	/* 1e400 is outside the range */
	CHECK(out == INFINITY);
	CHECK_ST(calc_stddev(tiny, 2, 0, &out), CALC_OK);	/* squares underflow to 0 */
	CHECK_NEAR(out / 1e-200, 1.0);
	CHECK_ST(calc_stddev(table, 4, 0, &out), CALC_OK);
	CHECK_NEAR(out, 1.118033988749895);
	CHECK_ST(calc_stddev(nanl, 3, 0, &out), CALC_OK);
	CHECK(out != out);

	/* correlation is scale-free: squares overflow or underflow, r does not change */
	CHECK_ST(calc_correl(sym, sym, 2, &out), CALC_OK);
	CHECK_NEAR(out, 1.0);
	CHECK_ST(calc_correl(sym, rev, 2, &out), CALC_OK);
	CHECK_NEAR(out, -1.0);
	CHECK_ST(calc_correl(tiny, tiny, 2, &out), CALC_OK);
	CHECK_NEAR(out, 1.0);
	CHECK_ST(calc_correl(sym, tiny, 2, &out), CALC_OK);
	CHECK_NEAR(out, 1.0);
	CHECK_ST(calc_correl(nanl, nanl, 3, &out), CALC_OK);
	CHECK(out != out);

	/* chi-square: totals and products of totals */
	CHECK_ST(calc_chi_square(table, 2, 2, &ref, &df), CALC_OK);
	CHECK_NEAR(ref, 0.0793650793650794);
	CHECK(df == 1);
	CHECK_ST(calc_chi_square(huge_table, 2, 2, &out, &df), CALC_OK);
	CHECK_NEAR(out / (0.0793650793650794e300), 1.0);
	CHECK_ST(calc_chi_square(full, 2, 2, &out, &df), CALC_OK);	/* the grand total exceeds DBL_MAX */
	CHECK(out == 0.0);
	/* counts near 1e-160: the product of two totals is subnormal, the ratio form is not */
	CHECK_ST(calc_chi_square(small_table, 2, 2, &out, &df), CALC_OK);
	CHECK_TOL(out / 7.93650793650794e-162, 1.0, 1e-12);
}

static void
test_power_range(void)
{
	static const double bases[] = { -1.0, -2.0, -0.5, 2.0, 0.5, 1.0, 0.0 };
	static const struct { double e; int odd; } ex[] = {
		{ 2147483649.0, 1 },		/* 2^31 + 1 */
		{ 9007199254740992.0, 0 },	/* 2^53 */
		{ 4503599627370497.0, 1 },	/* 2^52 + 1 */
		{ 1e300, 0 },
		{ -2147483649.0, 1 },
		{ -1e300, 0 },
	};
	double out, x, e, want;
	size_t i, j;
	int odd;

	for (i = 0; i < COUNT(bases); i++) {
		for (j = 0; j < COUNT(ex); j++) {
			x = bases[i];
			e = ex[j].e;
			if (x == 0.0 && e < 0.0) {
				CHECK_ST(calc_power(&x, 1, e, &out), CALC_E_DOMAIN);
				continue;
			}
			CHECK_ST(calc_power(&x, 1, e, &out), CALC_OK);
			odd = ex[j].odd && x < 0.0;
			if (fabs(x) == 1.0)
				want = odd ? -1.0 : 1.0;
			else if (x == 0.0)
				want = 0.0;
			else if ((fabs(x) > 1.0) == (e > 0.0))
				want = odd ? -INFINITY : INFINITY;
			else
				want = odd ? -0.0 : 0.0;
			CHECK_EXACT(out, want);
		}
	}
	/* the integer test no longer stops at 1e9, and pow_int still serves up to 2^31 */
	{
		x = -1.0;

		CHECK_ST(calc_power(&x, 1, 1000000001.0, &out), CALC_OK);
		CHECK_EXACT(out, -1.0);
		CHECK_ST(calc_power(&x, 1, 2147483647.0, &out), CALC_OK);
		CHECK_EXACT(out, -1.0);
		CHECK_ST(calc_power(&x, 1, 2147483648.0, &out), CALC_OK);
		CHECK_EXACT(out, 1.0);
		x = -2.0;
		CHECK_ST(calc_power(&x, 1, 2147483648.0, &out), CALC_OK);
		CHECK_EXACT(out, INFINITY);
		x = -0.0;
		CHECK_ST(calc_power(&x, 1, 2147483649.0, &out), CALC_OK);
		CHECK_EXACT(out, -0.0);
		CHECK_ST(calc_power(&x, 1, 1e300, &out), CALC_OK);
		CHECK_EXACT(out, 0.0);
		/* a non-integer exponent with a negative base is still a domain error */
		x = -1.0;
		CHECK_ST(calc_power(&x, 1, 1e300, &out), CALC_OK);	/* every such double is an integer */
		CHECK_EXACT(out, 1.0);
		CHECK_ST(calc_power(&x, 1, 0.5, &out), CALC_E_DOMAIN);
		CHECK_ST(calc_power(&x, 1, INFINITY, &out), CALC_E_DOMAIN);
		CHECK_ST(calc_power(&x, 1, NAN, &out), CALC_E_DOMAIN);
	}
}


/*
 * Approach to the poles of atanh (+-1) and atan (+-i) from either side, with
 * offsets of 1e-10, 1e-153 and 1e-160. The expected values come from
 * gen_complex_vectors.py, which evaluates them at 60 digits with mpmath.
 */
static const struct { int fn; double zr, zi, er, ei; } cpole[] = {
#define CV_UNARY(n, zr, zi, er, ei)
#define CV_BINARY(n, ar, ai, br, bi, er, ei)
#define CV_SCALAR(n, zr, zi, e)
#define CV_POLE(n, zr, zi, er, ei) { cfn_##n, zr, zi, er, ei },
#include "complex_vectors.h"
#undef CV_UNARY
#undef CV_BINARY
#undef CV_SCALAR
#undef CV_POLE
};

static void
test_complex_poles(void)
{
	calc_cplx r;
	size_t i;

	for (i = 0; i < COUNT(cpole); i++) {
		CHECK_ST(calc_c_func((calc_cfn)cpole[i].fn, calc_c_make(cpole[i].zr, cpole[i].zi), &r),
		    CALC_OK);
		check_cplx(r, cpole[i].er, cpole[i].ei, TOL, __LINE__);
	}
	CHECK(COUNT(cpole) == 28);

	/*
	 * calc__log1p is private to the library. The atanh and atan cores hand it
	 * 4x/d only for d >= 2^-1000, so no public entry point reaches its
	 * large-argument branch; it is pinned here directly. The plain product
	 * log(1 + x) * x / x overflows from x about 2.5e305.
	 */
	CHECK_TOL(calc__log1p(1e306), 704.591038456178, 1e-14);
	CHECK_TOL(calc__log1p(DBL_MAX), 709.782712893384, 1e-14);
	CHECK_TOL(calc__log1p(0x1p60), 41.58883083359672, 1e-14);
	CHECK_TOL(calc__log1p(0x1p53), 36.7368005696771, 1e-14);
}

int
main(void)
{
	test_atan3();
	test_scalar();
	test_statistics();
	test_chi_square();
	test_crossing();
	test_vectors();
	test_matrices();
	test_complex();
	test_quaternions();
	test_crc();
	test_random();
	test_fft();
	test_lists();
	test_base64();
	test_pid();
	test_pid_properties();
	test_null_arguments();
	test_scale_statistics();
	test_scale_vectors();
	test_scale_matrices();
	test_determinant_range();
	test_scale_lists();
	test_statistics_range();
	test_power_range();
	test_complex_poles();
	test_accuracy();
#ifdef CALC_TEST_NO_HOST_COMPLEX
	printf("INFO host complex grid skipped: C library lacks Annex G branch cuts\n");
#else
	test_complex_grid();
#endif
	if (failures != 0) {
		printf("calc_test: %lu of %lu checks failed\n", failures, checks);
		return 1;
	}
	printf("INFO accuracy: sincos abs %.2g, hyperbolic rel %.2g, log10 rel %.2g, inverse hyperbolic rel %.2g, atan3 abs %.2g\n",
	    worst_sincos, worst_hyper, worst_log10, worst_inv, worst_atan3);
	printf("PASS calc_test: %lu checks\n", checks);
	return 0;
}
