/*
 * Angle units, the full-turn arctangent, hyperbolic functions, the common
 * logarithm, the inverse hyperbolics and rounding.
 *
 * The hyperbolic functions are built on exp and a Taylor series for small
 * arguments, so they carry no dependence on the libm sinh, cosh and tanh.
 */

#include <math.h>

#include "calc_internal.h"

double
calc_angle_to_rad(calc_angle_unit u, double a)
{
	return u == CALC_DEGREES ? a / CALC_DEG_PER_RAD : a;
}

double
calc_angle_from_rad(calc_angle_unit u, double r)
{
	return u == CALC_DEGREES ? r * CALC_DEG_PER_RAD : r;
}

double
calc_atan3(double y, double x, calc_angle_unit u)
{
	double r;

	if (y == 0.0 && x == 0.0)
		return 0.0;
	r = calc__atan2(y, x);
	if (r < 0.0) {
		r += CALC_TWO_PI;
		if (r >= CALC_TWO_PI)
			r = 0.0;
	}
	r += 0.0;	/* turns -0 into +0 */
	return calc_angle_from_rad(u, r);
}

/* Odd Taylor series of sinh through x^17, used for |x| < 0.5. */
static double
sinh_series(double x)
{
	double x2 = x * x;

	return x + x * x2 * (1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (1.0 / 5040.0
	    + x2 * (1.0 / 362880.0 + x2 * (1.0 / 39916800.0
	    + x2 * (1.0 / 6227020800.0 + x2 * (1.0 / 1307674368000.0
	    + x2 * (1.0 / 355687428096000.0))))))));
}

/*
 * exp(|x|) * 0.5 without overflow until the result itself overflows: two
 * factors of exp(|x| / 2) keep each partial product finite.
 */
static double
half_exp_large(double ax)
{
	double h = calc__exp(0.5 * ax);

	return h * (0.5 * h);
}

double
calc__sinh(double x)
{
	double ax = calc__fabs(x);
	double e, v;

	if (calc__isnan(x))
		return x;
	if (ax < 0.5)
		return sinh_series(x);
	if (ax > 20.0) {
		v = half_exp_large(ax);
	} else {
		e = calc__exp(ax);
		v = 0.5 * (e - 1.0 / e);
	}
	return x < 0.0 ? -v : v;
}

double
calc__cosh(double x)
{
	double ax = calc__fabs(x);
	double e;

	if (calc__isnan(x))
		return x;
	if (ax > 20.0)
		return half_exp_large(ax);
	e = calc__exp(ax);
	return 0.5 * (e + 1.0 / e);
}

double
calc__tanh(double x)
{
	double ax = calc__fabs(x);
	double t, v;

	if (calc__isnan(x))
		return x;
	if (ax >= 20.0) {
		v = 1.0;
	} else if (ax < 0.5) {
		v = sinh_series(ax) / calc__cosh(ax);
	} else {
		t = calc__exp(2.0 * ax);
		v = (t - 1.0) / (t + 1.0);
	}
	return x < 0.0 ? -v : v;
}

calc_status
calc_sinh(double x, double *out)
{
	double v;

	if (out == NULL)
		return CALC_E_ARG;
	v = calc__sinh(x);
	if (calc__isfinite(x) && !calc__isfinite(v))
		return CALC_E_RANGE;
	*out = v;
	return CALC_OK;
}

calc_status
calc_cosh(double x, double *out)
{
	double v;

	if (out == NULL)
		return CALC_E_ARG;
	v = calc__cosh(x);
	if (calc__isfinite(x) && !calc__isfinite(v))
		return CALC_E_RANGE;
	*out = v;
	return CALC_OK;
}

calc_status
calc_tanh(double x, double *out)
{
	if (out == NULL)
		return CALC_E_ARG;
	*out = calc__tanh(x);
	return CALC_OK;
}

/* 10^k for |k| <= 22, where every power of ten is an exact double. */
static double
pow10_exact(int k)
{
	double p = 1.0;
	int i, n = k < 0 ? -k : k;

	for (i = 0; i < n; i++)
		p *= 10.0;
	return k < 0 ? 1.0 / p : p;
}

calc_status
calc_log10(double x, double *out)
{
	double v, nearest;
	int k;

	if (out == NULL)
		return CALC_E_ARG;
	if (calc__isnan(x)) {
		*out = x;
		return CALC_OK;
	}
	if (x <= 0.0)
		return CALC_E_DOMAIN;
	if (calc__isinf(x)) {
		*out = x;
		return CALC_OK;
	}
	v = calc__log(x) * 0.43429448190325182765;
	nearest = calc_round(v);
	if (calc__fabs(nearest) <= 22.0) {
		k = (int)nearest;
		if (x == pow10_exact(k))
			v = nearest;
	}
	*out = v;
	return CALC_OK;
}

double
calc_asinh(double x)
{
	double ax = calc__fabs(x);
	double v;

	if (calc__isnan(x) || calc__isinf(x))
		return x;
	if (ax > 1.0e8)
		v = calc__log(ax) + CALC_LN2;
	else
		v = calc__log1p(ax + ax * ax / (1.0 + sqrt(1.0 + ax * ax)));
	return x < 0.0 ? -v : v;
}

double
calc_acosh(double x)
{
	double t;

	if (calc__isnan(x) || x < 1.0)
		return CALC_NAN;
	if (calc__isinf(x))
		return x;
	if (x > 1.0e8)
		return calc__log(x) + CALC_LN2;
	t = x - 1.0;
	return calc__log1p(t + sqrt(t * (x + 1.0)));
}

double
calc_atanh(double x)
{
	double ax = calc__fabs(x);
	double v;

	if (calc__isnan(x) || ax > 1.0)
		return CALC_NAN;
	if (ax == 1.0)
		return x < 0.0 ? -CALC_INF : CALC_INF;
	v = 0.5 * calc__log1p(2.0 * ax / (1.0 - ax));
	return x < 0.0 ? -v : v;
}

double
calc_round(double x)
{
	double t, f;

	if (calc__isnan(x) || calc__fabs(x) >= 4503599627370496.0)
		return x;
	if (x < 0.0)
		return -calc_round(-x);
	t = floor(x);
	f = x - t;
	return f >= 0.5 ? t + 1.0 : t;
}
