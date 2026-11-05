/*
 * Elementary-function wrappers: range reduction, overflow handling and the
 * special cases that the target C library gets wrong. See calc_internal.h.
 */

#include <math.h>

#include "calc_internal.h"

double
calc__exp(double x)
{
	if (calc__isnan(x))
		return x;
	if (x > CALC_EXP_MAX)
		return CALC_INF;
	if (x < CALC_EXP_MIN)
		return 0.0;
	return exp(x);
}

double
calc__log(double x)
{
	if (calc__isnan(x) || x < 0.0)
		return CALC_NAN;
	if (x == 0.0)
		return -CALC_INF;
	if (calc__isinf(x))
		return x;
	return log(x);
}

/*
 * log(1 + x) without cancellation for small x: u = fl(1 + x) differs from
 * 1 by x(1 + e), and log(u) * x / (u - 1) cancels that error.
 */
double
calc__log1p(double x)
{
	double u;

	if (calc__isnan(x) || x < -1.0)
		return CALC_NAN;
	if (x == -1.0)
		return -CALC_INF;
	if (calc__isinf(x))
		return x;
	u = 1.0 + x;
	if (u == 1.0)
		return x;
	return calc__log(u) * x / (u - 1.0);
}

/* sqrt(a^2 + b^2) scaled by the larger magnitude, so it neither overflows nor underflows early. */
double
calc__hypot(double a, double b)
{
	double r, t;

	a = calc__fabs(a);
	b = calc__fabs(b);
	if (calc__isinf(a) || calc__isinf(b))
		return CALC_INF;
	if (calc__isnan(a) || calc__isnan(b))
		return CALC_NAN;
	if (a < b) {
		t = a;
		a = b;
		b = t;
	}
	if (a == 0.0)
		return 0.0;
	r = b / a;
	return a * sqrt(1.0 + r * r);
}

/*
 * atan2 with the C99 results on the x axis: +-0 for x >= +0 and +-pi for
 * x <= -0, the sign following y, including y = -0 and the origin.
 */
double
calc__atan2(double y, double x)
{
	if (y == 0.0 && !calc__isnan(x))
		return calc__copysign(calc__signbit(x) ? CALC_PI : 0.0, y);
	return atan2(y, x);
}

/*
 * sin and cos together. The argument is reduced by the nearest multiple of
 * pi / 2 using three pieces of pi / 2 (the first two carry 33 bits each, so
 * k * piece is exact for k < 2^20) and the remainder, in [-pi/4, pi/4], goes
 * through Taylor polynomials. The reduction holds below 1e6 radians; beyond
 * that the C library result is returned unchanged.
 */
void
calc__sincos(double x, double *s, double *c)
{
	static const double pio2_1 = 0x1.921fb54400000p+0;
	static const double pio2_2 = 0x1.0b4611a600000p-34;
	static const double pio2_3 = 0x1.3198a2e037073p-69;
	double r, r2, sn, cs, kd;
	long k;

	if (calc__isnan(x) || calc__isinf(x)) {
		*s = *c = CALC_NAN;
		return;
	}
	if (calc__fabs(x) >= 1.0e6) {
		*s = sin(x);
		*c = cos(x);
		return;
	}
	kd = floor(x * 0.63661977236758134308 + 0.5);
	k = (long)kd;
	r = ((x - kd * pio2_1) - kd * pio2_2) - kd * pio2_3;
	r2 = r * r;
	sn = r + r * r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0
	    + r2 * (1.0 / 362880.0 + r2 * (-1.0 / 39916800.0
	    + r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0
	    + r2 * (1.0 / 355687428096000.0))))))));
	cs = 1.0 + r2 * (-1.0 / 2.0 + r2 * (1.0 / 24.0 + r2 * (-1.0 / 720.0
	    + r2 * (1.0 / 40320.0 + r2 * (-1.0 / 3628800.0
	    + r2 * (1.0 / 479001600.0 + r2 * (-1.0 / 87178291200.0
	    + r2 * (1.0 / 20922789888000.0 + r2 * (-1.0 / 6402373705728000.0)))))))));
	switch (((k % 4) + 4) % 4) {
	case 0:
		*s = sn;
		*c = cs;
		break;
	case 1:
		*s = cs;
		*c = -sn;
		break;
	case 2:
		*s = -sn;
		*c = -cs;
		break;
	default:
		*s = -cs;
		*c = sn;
		break;
	}
}
