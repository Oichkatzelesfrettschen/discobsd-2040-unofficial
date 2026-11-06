/*
 * Complex arithmetic and elementary functions in double precision, written
 * against calc_cplx because the tree has no complex header. Principal values
 * and the branch cuts follow C99 Annex G. The inverse circular and
 * hyperbolic functions use half-sum formulas (r = |z + 1|, s = |z - 1|,
 * A = (r + s) / 2) with the small differences A - 1 and A - x formed
 * without cancellation, so they stay accurate near the real axis.
 */

#include <math.h>

#include "calc_internal.h"

calc_cplx
calc_c_make(double re, double im)
{
	calc_cplx z;

	z.re = re;
	z.im = im;
	return z;
}

static calc_cplx
cmk(double re, double im)
{
	return calc_c_make(re, im);
}

calc_cplx
calc_c_polar(double radius, double angle, calc_angle_unit u)
{
	double s, c;

	calc__sincos(calc_angle_to_rad(u, angle), &s, &c);
	return cmk(radius * c, radius * s);
}

double
calc_c_abs(calc_cplx z)
{
	return calc__hypot(z.re, z.im);
}

double
calc_c_arg(calc_cplx z)
{
	return calc__atan2(z.im, z.re);
}

double
calc_c_phase(calc_cplx z, calc_angle_unit u)
{
	return calc_angle_from_rad(u, calc_c_arg(z));
}

calc_cplx
calc_c_conj(calc_cplx z)
{
	return cmk(z.re, -z.im);
}

calc_cplx
calc_c_proj(calc_cplx z)
{
	if (calc__isinf(z.re) || calc__isinf(z.im))
		return cmk(CALC_INF, calc__copysign(0.0, z.im));
	return z;
}

static int
cfinite(calc_cplx z)
{
	return calc__isfinite(z.re) && calc__isfinite(z.im);
}

/* Store r unless finite operands overflowed to a non-finite result. */
static calc_status
cstore(calc_cplx in1, calc_cplx in2, calc_cplx r, calc_cplx *out)
{
	if (cfinite(in1) && cfinite(in2) && !cfinite(r))
		return CALC_E_RANGE;
	*out = r;
	return CALC_OK;
}

calc_status
calc_c_add(calc_cplx a, calc_cplx b, calc_cplx *out)
{
	if (out == NULL)
		return CALC_E_ARG;
	return cstore(a, b, cmk(a.re + b.re, a.im + b.im), out);
}

calc_status
calc_c_sub(calc_cplx a, calc_cplx b, calc_cplx *out)
{
	if (out == NULL)
		return CALC_E_ARG;
	return cstore(a, b, cmk(a.re - b.re, a.im - b.im), out);
}

calc_status
calc_c_mul(calc_cplx a, calc_cplx b, calc_cplx *out)
{
	if (out == NULL)
		return CALC_E_ARG;
	return cstore(a, b, cmk(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re), out);
}

/*
 * Smith's algorithm: divide through by the larger component of the divisor.
 * *den receives the denominator so the caller can see it leave the range.
 */
static calc_cplx
smith(calc_cplx a, calc_cplx b, double *den)
{
	double r;

	if (calc__fabs(b.re) >= calc__fabs(b.im)) {
		r = b.im / b.re;
		*den = b.re + b.im * r;
		return cmk((a.re + a.im * r) / *den, (a.im - a.re * r) / *den);
	}
	r = b.re / b.im;
	*den = b.re * r + b.im;
	return cmk((a.re * r + a.im) / *den, (a.im * r - a.re) / *den);
}

/* The exponent of the larger component of a finite non-zero z, else 0. */
static int
cexponent(calc_cplx z)
{
	double m = calc__fabs(z.re) >= calc__fabs(z.im) ? z.re : z.im;
	int e = 0;

	if (m != 0.0)
		(void)calc__frexp1(m, &e);
	return e;
}

/*
 * The denominator lies between the divisor's larger component and twice it,
 * and each numerator is a sum of the dividend's components, so either can
 * leave the double range while the quotient is a normal double: a divisor
 * near DBL_MAX or DBL_MIN, or a dividend near DBL_MAX. Then dividend and
 * divisor are each divided by 2^e, e the exponent of the larger component,
 * which is exact, and the quotient is multiplied by 2^(ea - eb).
 */
calc_status
calc_c_div(calc_cplx a, calc_cplx b, calc_cplx *out)
{
	double den;
	calc_cplx q;
	int ea, eb;

	if (out == NULL)
		return CALC_E_ARG;
	if (b.re == 0.0 && b.im == 0.0)
		return CALC_E_DOMAIN;
	q = smith(a, b, &den);
	if ((!calc__isfinite(den) || calc__fabs(den) < CALC_DBL_MIN ||
	    !calc__isfinite(q.re) || !calc__isfinite(q.im)) &&
	    calc__isfinite(a.re) && calc__isfinite(a.im) &&
	    calc__isfinite(b.re) && calc__isfinite(b.im)) {
		ea = cexponent(a);
		eb = cexponent(b);
		q = smith(cmk(calc__ldexp(a.re, -ea), calc__ldexp(a.im, -ea)),
		    cmk(calc__ldexp(b.re, -eb), calc__ldexp(b.im, -eb)), &den);
		/* calc__ldexp takes [-2044, 2044]; past it the quotient is out of range anyway */
		ea -= eb;
		if (ea > 2044)
			ea = 2044;
		if (ea < -2044)
			ea = -2044;
		q = cmk(calc__ldexp(q.re, ea), calc__ldexp(q.im, ea));
	}
	return cstore(a, b, q, out);
}

/* --- elementary functions ------------------------------------------------ */

static calc_cplx
cexp_(calc_cplx z)
{
	double s, c, e, h;

	calc__sincos(z.im, &s, &c);
	if (z.re > 700.0) {
		h = calc__exp(0.5 * z.re);
		return cmk(h * c * h, h * s * h);
	}
	e = calc__exp(z.re);
	return cmk(e * c, e * s);
}

static calc_cplx
clog_(calc_cplx z)
{
	double a = calc__fabs(z.re), b = calc__fabs(z.im), t, r;

	if (a < b) {
		t = a;
		a = b;
		b = t;
	}
	if (calc__isinf(a))
		return cmk(CALC_INF, calc__atan2(z.im, z.re));
	if (a == 0.0)
		return cmk(-CALC_INF, calc__atan2(z.im, z.re));
	r = b / a;
	return cmk(calc__log(a) + 0.5 * calc__log1p(r * r), calc__atan2(z.im, z.re));
}

static calc_cplx
csqrt_(calc_cplx z)
{
	double a = calc__fabs(z.re), b = calc__fabs(z.im);
	double m = a > b ? a : b;
	double t, scale = 1.0;

	if (z.re == 0.0 && z.im == 0.0)
		return cmk(0.0, z.im);
	if (calc__isinf(z.im))
		return cmk(CALC_INF, z.im);
	if (calc__isinf(z.re)) {
		if (z.re > 0.0)
			return cmk(z.re, calc__copysign(0.0, z.im));
		return cmk(calc__isnan(z.im) ? z.im : 0.0, calc__copysign(CALC_INF, z.im));
	}
	if (calc__isnan(m))
		return cmk(CALC_NAN, CALC_NAN);
	if (m > 1.0e307) {
		a *= 0.25;
		b *= 0.25;
		scale = 2.0;
	} else if (m < 1.0e-290) {
		a *= 0x1p100;
		b *= 0x1p100;
		scale = 0x1p-50;
	}
	t = sqrt(0.5 * (a + calc__hypot(a, b)));
	if (z.re >= 0.0)
		return cmk(scale * t, calc__copysign(scale * (b / (2.0 * t)), z.im));
	return cmk(scale * (b / (2.0 * t)), calc__copysign(scale * t, z.im));
}

/*
 * *ch = fc cosh(y) and *sh = fs sinh(y). Past |y| = 709 cosh and sinh
 * overflow while a factor below one can still bring the product into range,
 * so there both are e^|y| / 2, formed as (f / 2) e^(|y|/2) e^(|y|/2) with the
 * factor applied before the second exponential.
 */
static void
hyp_mul(double y, double fc, double fs, double *ch, double *sh)
{
	double ay = calc__fabs(y), e;

	if (ay < 709.0) {
		*ch = fc * calc__cosh(y);
		*sh = fs * calc__sinh(y);
		return;
	}
	e = calc__exp(0.5 * ay);
	*ch = (0.5 * fc * e) * e;
	*sh = (0.5 * fs * e) * e;
	if (y < 0.0)
		*sh = -*sh;
}

static calc_cplx
csin_(calc_cplx z)
{
	double s, c, re, im;

	calc__sincos(z.re, &s, &c);
	hyp_mul(z.im, s, c, &re, &im);
	return cmk(re, im);
}

static calc_cplx
ccos_(calc_cplx z)
{
	double s, c, re, im;

	calc__sincos(z.re, &s, &c);
	hyp_mul(z.im, c, s, &re, &im);
	return cmk(re, -im);
}

/*
 * tan(x + iy) = (t (1 - u^2) + i u (1 + t^2)) / (1 + t^2 u^2) with t = tan x
 * and u = tanh y; 1 - u^2 is written as 1 / cosh^2 y so that it does not
 * cancel for large y.
 */
static calc_cplx
ctan_(calc_cplx z)
{
	double s, c, t, u, inv, den;

	calc__sincos(z.re, &s, &c);
	t = s / c;
	u = calc__tanh(z.im);
	inv = 1.0 / calc__cosh(z.im);
	den = 1.0 + t * t * u * u;
	return cmk(t * inv * inv / den, u * (1.0 + t * t) / den);
}

static calc_cplx
csinh_(calc_cplx z)
{
	double s, c, re, im;

	calc__sincos(z.im, &s, &c);
	hyp_mul(z.re, s, c, &im, &re);
	return cmk(re, im);
}

static calc_cplx
ccosh_(calc_cplx z)
{
	double s, c, re, im;

	calc__sincos(z.im, &s, &c);
	hyp_mul(z.re, c, s, &re, &im);
	return cmk(re, im);
}

/* tanh z = -i tan(i z) */
static calc_cplx
ctanh_(calc_cplx z)
{
	calc_cplx w = ctan_(cmk(-z.im, z.re));

	return cmk(w.im, -w.re);
}

/* y2 / den, taken as 0 when the numerator is 0 (avoids 0 / 0 at x = 1). */
static double
div0(double num, double den)
{
	return num == 0.0 ? 0.0 : num / den;
}

/*
 * Shared core of asin and acos for x >= 0: *root = sqrt(A^2 - x^2) and
 * *lg = acosh(A) >= 0, with A = (|z+1| + |z-1|) / 2. Beyond 1e150 the
 * leading asymptotic terms replace the half-sum formulas.
 */
static void
asin_core(double x, double y, double *root, double *lg)
{
	double ay = calc__fabs(y), y2, r, s, a, am1, amx, apx;

	if (x > 1.0e150 || ay > 1.0e150) {
		/* log|z| from the larger component, so |z| past DBL_MAX stays finite */
		double big = x > ay ? x : ay, small = x > ay ? ay : x, q = small / big;

		*root = ay;
		*lg = CALC_LN2 + calc__log(big) + 0.5 * calc__log1p(q * q);
		return;
	}
	y2 = ay * ay;
	r = calc__hypot(x + 1.0, ay);
	s = calc__hypot(x - 1.0, ay);
	a = 0.5 * (r + s);
	if (a < 1.5)
		am1 = 0.5 * (div0(y2, r + (x + 1.0)) +
		    (x < 1.0 ? div0(y2, s + (1.0 - x)) : s + (x - 1.0)));
	else
		am1 = a - 1.0;
	amx = 0.5 * (div0(y2, r + (x + 1.0)) +
	    (x >= 1.0 ? div0(y2, s + (x - 1.0)) : s + (1.0 - x)));
	apx = a + x;
	*root = sqrt(amx * apx);
	*lg = calc__log1p(am1 + sqrt(am1 * (a + 1.0)));
}

static calc_cplx
casin_(double x, double y)
{
	double xa = calc__fabs(x), root, lg, re;

	asin_core(xa, y, &root, &lg);
	re = calc__atan2(xa, root);
	return cmk(calc__copysign(re, x), calc__copysign(lg, y));
}

static calc_cplx
cacos_(double x, double y)
{
	double xa = calc__fabs(x), root, lg, re;

	asin_core(xa, y, &root, &lg);
	re = calc__atan2(root, xa);
	if (calc__signbit(x))
		re = CALC_PI - re;
	return cmk(re, -calc__copysign(lg, y));
}

/*
 * atanh from its real and imaginary parts; the poles are screened by the
 * caller. The real part is odd in x, so it is formed for |x| and takes the
 * sign of x: 4x/d rounds to -1 at x = -1 and log1p(-1) is -infinity, where
 * the reflected argument gives the same value as the +1 side. Within 2^-500 of
 * a pole (d = |1 - x|^2 + y^2 below 2^-1000, where d would underflow and 4x/d
 * overflow) the real part is half the difference of two logarithms of exactly
 * representable moduli.
 */
static calc_cplx
catanh_(double x, double y)
{
	double m = calc__fabs(x) > calc__fabs(y) ? calc__fabs(x) : calc__fabs(y);
	double xs, ys, den, d, xa, re;

	if (m > 1.0e150) {
		xs = x / m;
		ys = y / m;
		den = m * (xs * xs + ys * ys);
		return cmk(xs / den, calc__copysign(CALC_HALF_PI, y));
	}
	xa = calc__fabs(x);
	d = (1.0 - xa) * (1.0 - xa) + y * y;
	if (d < 0x1p-1000)
		re = 0.5 * (calc__log(calc__hypot(1.0 + xa, y)) - calc__log(calc__hypot(1.0 - xa, y)));
	else
		re = 0.25 * calc__log1p(4.0 * xa / d);
	return cmk(calc__copysign(re, x),
	    0.5 * calc__atan2(2.0 * y, (1.0 - x) * (1.0 + x) - y * y));
}

calc_status
calc_c_func(calc_cfn fn, calc_cplx z, calc_cplx *out)
{
	calc_cplx r, w;

	if (out == NULL)
		return CALC_E_ARG;
	switch (fn) {
	case CALC_C_EXP:
		r = cexp_(z);
		break;
	case CALC_C_LOG:
		if (z.re == 0.0 && z.im == 0.0)
			return CALC_E_DOMAIN;
		r = clog_(z);
		break;
	case CALC_C_SQRT:
		r = csqrt_(z);
		break;
	case CALC_C_SIN:
		r = csin_(z);
		break;
	case CALC_C_COS:
		r = ccos_(z);
		break;
	case CALC_C_TAN:
		r = ctan_(z);
		break;
	case CALC_C_ASIN:
		r = casin_(z.re, z.im);
		break;
	case CALC_C_ACOS:
		r = cacos_(z.re, z.im);
		break;
	case CALC_C_ATAN:
		if (z.re == 0.0 && calc__fabs(z.im) == 1.0)
			return CALC_E_DOMAIN;
		w = catanh_(-z.im, z.re);
		r = cmk(w.im, -w.re);
		break;
	case CALC_C_SINH:
		r = csinh_(z);
		break;
	case CALC_C_COSH:
		r = ccosh_(z);
		break;
	case CALC_C_TANH:
		r = ctanh_(z);
		break;
	case CALC_C_ASINH:
		/* asinh z = -i asin(i z) */
		w = casin_(-z.im, z.re);
		r = cmk(w.im, -w.re);
		break;
	case CALC_C_ACOSH:
		/* acosh z = +-i acos z, the sign chosen so that Re >= 0 */
		w = cacos_(z.re, z.im);
		r = calc__signbit(z.im) ? cmk(w.im, -w.re) : cmk(-w.im, w.re);
		break;
	case CALC_C_ATANH:
		if (z.im == 0.0 && calc__fabs(z.re) == 1.0)
			return CALC_E_DOMAIN;
		r = catanh_(z.re, z.im);
		break;
	default:
		return CALC_E_ARG;
	}
	return cstore(z, z, r, out);
}

calc_status
calc_c_pow(calc_cplx a, calc_cplx b, calc_cplx *out)
{
	calc_cplx l, p;

	if (out == NULL)
		return CALC_E_ARG;
	if (a.re == 0.0 && a.im == 0.0) {
		if (b.re == 0.0 && b.im == 0.0) {
			*out = cmk(1.0, 0.0);
			return CALC_OK;
		}
		if (b.re > 0.0) {
			*out = cmk(0.0, 0.0);
			return CALC_OK;
		}
		return CALC_E_DOMAIN;
	}
	l = clog_(a);
	p = cmk(b.re * l.re - b.im * l.im, b.re * l.im + b.im * l.re);
	return cstore(a, b, cexp_(p), out);
}
