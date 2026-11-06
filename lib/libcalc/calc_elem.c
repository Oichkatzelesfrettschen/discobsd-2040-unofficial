/*
 * Element-wise operations on lists of doubles and 64-bit integers. An
 * output may equal an input exactly; any other overlap is rejected, because
 * a shifted overlap would read results already written.
 */

#include <math.h>

#include "calc_internal.h"

#define LIST_CHECK(a, b, out, n, elt) do {					\
	if ((a) == NULL || (b) == NULL || (out) == NULL)			\
		return CALC_E_ARG;						\
	if ((n) == 0)								\
		return CALC_E_SIZE;						\
	if (calc__partial((a), (out), (n) * sizeof(elt)) ||			\
	    calc__partial((b), (out), (n) * sizeof(elt)))			\
		return CALC_E_ALIAS;						\
} while (0)

calc_status
calc_vec_add(const double *a, const double *b, size_t n, double *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, double);
	for (i = 0; i < n; i++)
		out[i] = a[i] + b[i];
	return CALC_OK;
}

calc_status
calc_vec_sub(const double *a, const double *b, size_t n, double *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, double);
	for (i = 0; i < n; i++)
		out[i] = a[i] - b[i];
	return CALC_OK;
}

calc_status
calc_vec_mul(const double *a, const double *b, size_t n, double *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, double);
	for (i = 0; i < n; i++)
		out[i] = a[i] * b[i];
	return CALC_OK;
}

calc_status
calc_vec_div(const double *a, const double *b, size_t n, double *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, double);
	for (i = 0; i < n; i++)
		if (b[i] == 0.0)
			return CALC_E_DOMAIN;
	for (i = 0; i < n; i++)
		out[i] = a[i] / b[i];
	return CALC_OK;
}

/* Integer arithmetic runs on uint64_t, where wrap-around is defined. */
calc_status
calc_i64_add(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		out[i] = (int64_t)((uint64_t)a[i] + (uint64_t)b[i]);
	return CALC_OK;
}

calc_status
calc_i64_sub(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		out[i] = (int64_t)((uint64_t)a[i] - (uint64_t)b[i]);
	return CALC_OK;
}

calc_status
calc_i64_mul(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		out[i] = (int64_t)((uint64_t)a[i] * (uint64_t)b[i]);
	return CALC_OK;
}

calc_status
calc_i64_div(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		if (b[i] == 0)
			return CALC_E_DOMAIN;
	for (i = 0; i < n; i++) {
		if (b[i] == -1)
			out[i] = (int64_t)(0 - (uint64_t)a[i]);
		else
			out[i] = a[i] / b[i];
	}
	return CALC_OK;
}

calc_status
calc_i64_and(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		out[i] = a[i] & b[i];
	return CALC_OK;
}

calc_status
calc_i64_or(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		out[i] = a[i] | b[i];
	return CALC_OK;
}

calc_status
calc_i64_xor(const int64_t *a, const int64_t *b, size_t n, int64_t *out)
{
	size_t i;

	LIST_CHECK(a, b, out, n, int64_t);
	for (i = 0; i < n; i++)
		out[i] = a[i] ^ b[i];
	return CALC_OK;
}

calc_status
calc_i64_shift(const int64_t *a, size_t n, int amount, int logical, int64_t *out)
{
	uint64_t u;
	unsigned sh;
	size_t i;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (amount < -63 || amount > 63)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__partial(a, out, n * sizeof(int64_t)))
		return CALC_E_ALIAS;
	for (i = 0; i < n; i++) {
		u = (uint64_t)a[i];
		if (amount >= 0) {
			sh = (unsigned)amount;
			out[i] = (int64_t)(u << sh);
		} else {
			sh = (unsigned)(-amount);
			if (logical)
				out[i] = (int64_t)(u >> sh);
			else
				/* Sign-extending shift without relying on signed >>. */
				out[i] = (int64_t)((u >> sh) |
				    ((u >> 63) ? ~(~(uint64_t)0 >> sh) : 0));
		}
	}
	return CALC_OK;
}

calc_status
calc_scale(const double *in, size_t n, double s, double *out)
{
	size_t i;

	if (in == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__partial(in, out, n * sizeof(double)))
		return CALC_E_ALIAS;
	for (i = 0; i < n; i++)
		out[i] = in[i] * s;
	return CALC_OK;
}

/* Round half away from zero into int64_t; non-zero when v is out of range. */
static int
to_i64(double v, int64_t *r)
{
	double t = calc_round(v);

	/* 2^63 is exactly representable; the largest double below it is 2^63 - 1024. */
	if (!(t >= -9223372036854775808.0 && t < 9223372036854775808.0))
		return 1;
	*r = (int64_t)t;
	return 0;
}

calc_status
calc_scale_to_i64(const double *in, size_t n, double s, int64_t *out)
{
	size_t i;
	int64_t r;

	if (in == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__overlap(in, n * sizeof(double), out, n * sizeof(int64_t)))
		return CALC_E_ALIAS;
	for (i = 0; i < n; i++)
		if (to_i64(in[i] * s, &r))
			return CALC_E_RANGE;
	for (i = 0; i < n; i++)
		(void)to_i64(in[i] * s, &out[i]);
	return CALC_OK;
}

calc_status
calc_scale_i64(const int64_t *in, size_t n, double s, int64_t *out)
{
	size_t i;
	int64_t r;

	if (in == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__partial(in, out, n * sizeof(int64_t)))
		return CALC_E_ALIAS;
	for (i = 0; i < n; i++)
		if (to_i64((double)in[i] * s, &r))
			return CALC_E_RANGE;
	for (i = 0; i < n; i++)
		(void)to_i64((double)in[i] * s, &out[i]);
	return CALC_OK;
}

/* Largest |exponent| for which pow_int runs: at most 32 squarings. */
#define POW_INT_MAX	2147483648.0

static double
pow_mag(double base, double e)
{
	double r = 1.0;

	while (e >= 1.0) {
		if (e - 2.0 * floor(0.5 * e) >= 1.0)
			r *= base;
		base *= base;
		e = floor(0.5 * e);
	}
	return r;
}

/*
 * x^k for an integral exponent by repeated squaring; k may be negative. A
 * negative k takes the reciprocal of x^|k| while that power is a normal
 * double, which rounds once; otherwise x^|k| left the normal range, which a
 * flush-to-zero provider turns into 0 or which overflowed, and the power of
 * 1 / x is formed instead.
 */
static double
pow_int(double x, double k)
{
	double r = pow_mag(x, calc__fabs(k));

	if (k >= 0.0)
		return r;
	if (calc__isfinite(r) && calc__fabs(r) >= CALC_DBL_MIN)
		return 1.0 / r;
	return pow_mag(1.0 / x, calc__fabs(k));
}

calc_status
calc_power(const double *in, size_t n, double exponent, double *out)
{
	int integral, odd;
	size_t i;

	if (in == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__partial(in, out, n * sizeof(double)))
		return CALC_E_ALIAS;
	/*
	 * An exponent is integral when it is finite and equals its floor. Every
	 * finite double of magnitude 2^53 or more is an even integer; the parity
	 * of a smaller one is its low bit.
	 */
	integral = calc__isfinite(exponent) && exponent == floor(exponent);
	/* Beyond pow_int's range the sign of a negative base comes from the parity of the exponent. */
	odd = integral && calc__fabs(exponent) > POW_INT_MAX &&
	    exponent - 2.0 * floor(0.5 * exponent) >= 1.0;
	for (i = 0; i < n; i++) {
		if (in[i] < 0.0 && !integral)
			return CALC_E_DOMAIN;
		if (in[i] == 0.0 && exponent < 0.0)
			return CALC_E_DOMAIN;
	}
	for (i = 0; i < n; i++) {
		if (exponent == 1.0)
			out[i] = in[i];
		else if (integral && calc__fabs(exponent) <= POW_INT_MAX)
			out[i] = pow_int(in[i], exponent);
		else if (exponent == 0.5)
			out[i] = sqrt(in[i]);
		else if (in[i] == 0.0)
			out[i] = 0.0;
		else	/* a negative base here has a large integral exponent: |x| carries the logarithm */
			out[i] = calc__exp(exponent * calc__log(calc__fabs(in[i])));
		if (odd && calc__signbit(in[i]))
			out[i] = -out[i];
	}
	return CALC_OK;
}

calc_status
calc_blend(const double *in1, const double *in2, size_t n, double ratio, double *out)
{
	size_t i, bytes = n * sizeof(double);

	if (in1 == NULL || in2 == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__overlap(in1, bytes, out, bytes) || calc__overlap(in2, bytes, out, bytes))
		return CALC_E_ALIAS;
	for (i = 0; i < n; i++)
		out[i] = (in2[i] - in1[i]) * ratio + in1[i];
	return CALC_OK;
}
