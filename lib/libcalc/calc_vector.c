/*
 * Vector operations: dot product, overflow-safe magnitude, normalization,
 * the 3-D cross product and 2-D point rotation.
 */

#include <math.h>

#include "calc_internal.h"

calc_status
calc_dot(const double *a, const double *b, size_t n, double *out)
{
	double s = 0.0;
	size_t i;

	if (a == NULL || b == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	for (i = 0; i < n; i++)
		s += a[i] * b[i];
	*out = s;
	return CALC_OK;
}

/*
 * Euclidean norm as the largest magnitude *m and the root of the sum of the
 * squares of the components divided by it, so a vector near 1e200 neither
 * overflows nor, near 1e-200, underflows to zero. The norm is *m * *root with
 * *root in [1, sqrt(n)]. Returns 0 when the norm is 0, infinite or NaN: *m
 * then holds it.
 */
static int
norm_parts(const double *a, size_t n, double *m, double *root)
{
	double mx = 0.0, s = 0.0, t;
	size_t i;

	for (i = 0; i < n; i++) {
		t = calc__fabs(a[i]);
		if (calc__isnan(t)) {
			*m = t;
			return 0;
		}
		if (t > mx)
			mx = t;
	}
	*m = mx;
	if (mx == 0.0 || calc__isinf(mx))
		return 0;
	for (i = 0; i < n; i++) {
		t = a[i] / mx;
		s += t * t;
	}
	*root = sqrt(s);
	return 1;
}

static double
norm(const double *a, size_t n)
{
	double m, root;

	return norm_parts(a, n, &m, &root) ? m * root : m;
}

/*
 * out = a / |a| and, when mag is not NULL, *mag = |a|. CALC_E_DOMAIN for the
 * zero vector, with out unwritten. When the norm m * root exceeds DBL_MAX,
 * *mag is the infinity the product gives, and both operands of the quotient
 * are scaled by 2^-32 (root <= sqrt(n) < 2^32, so the scaled norm is finite):
 * a power of two leaves the rounded quotient exactly as an unbounded
 * exponent range would give it. out may equal a.
 */
calc_status
calc__direction(const double *a, size_t n, double *out, double *mag)
{
	double m, root, len;
	size_t i;

	if (norm_parts(a, n, &m, &root)) {
		len = m * root;
		if (mag != NULL)
			*mag = len;
		if (!calc__isfinite(len)) {
			len = m * 0x1p-32 * root;
			for (i = 0; i < n; i++)
				out[i] = a[i] * 0x1p-32 / len;
			return CALC_OK;
		}
	} else {
		len = m;
		if (len == 0.0)
			return CALC_E_DOMAIN;
		if (mag != NULL)
			*mag = len;
	}
	for (i = 0; i < n; i++)
		out[i] = a[i] / len;
	return CALC_OK;
}

calc_status
calc_magnitude(const double *a, size_t n, double *out)
{
	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	*out = norm(a, n);
	return CALC_OK;
}

calc_status
calc_normalize(const double *a, size_t n, double *out)
{
	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__partial(a, out, n * sizeof(double)))
		return CALC_E_ALIAS;
	return calc__direction(a, n, out, NULL);
}

calc_status
calc_cross3(const double a[3], const double b[3], double out[3])
{
	double x, y, z;

	if (a == NULL || b == NULL || out == NULL)
		return CALC_E_ARG;
	x = a[1] * b[2] - a[2] * b[1];
	y = a[2] * b[0] - a[0] * b[2];
	z = a[0] * b[1] - a[1] * b[0];
	out[0] = x;
	out[1] = y;
	out[2] = z;
	return CALC_OK;
}

/*
 * Alias rules shared by the double and int32_t rotations: each output equals
 * its own input or is disjoint from it, and the crossed pairs never share
 * storage, so the per-point update reads both inputs before writing either
 * output.
 */
static calc_status
rotate_alias(const void *xin, const void *yin, const void *xout, const void *yout,
    size_t bytes)
{
	if (calc__overlap(xout, bytes, yout, bytes) ||
	    calc__partial(xout, xin, bytes) || calc__partial(yout, yin, bytes) ||
	    calc__overlap(xout, bytes, yin, bytes) ||
	    calc__overlap(yout, bytes, xin, bytes))
		return CALC_E_ALIAS;
	return CALC_OK;
}

calc_status
calc_rotate2d(double cx, double cy, double angle, calc_angle_unit u,
    const double *xin, const double *yin, size_t n, double *xout, double *yout)
{
	double s, c, dx, dy;
	size_t i;

	if (xin == NULL || yin == NULL || xout == NULL || yout == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (rotate_alias(xin, yin, xout, yout, n * sizeof(double)) != CALC_OK)
		return CALC_E_ALIAS;
	calc__sincos(calc_angle_to_rad(u, angle), &s, &c);
	for (i = 0; i < n; i++) {
		dx = xin[i] - cx;
		dy = yin[i] - cy;
		xout[i] = dx * c - dy * s + cx;
		yout[i] = dy * c + dx * s + cy;
	}
	return CALC_OK;
}

calc_status
calc_rotate2d_i32(double cx, double cy, double angle, calc_angle_unit u,
    const int32_t *xin, const int32_t *yin, size_t n, int32_t *xout, int32_t *yout)
{
	double s, c, dx, dy, nx, ny;
	size_t i;

	if (xin == NULL || yin == NULL || xout == NULL || yout == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (rotate_alias(xin, yin, xout, yout, n * sizeof(int32_t)) != CALC_OK)
		return CALC_E_ALIAS;
	calc__sincos(calc_angle_to_rad(u, angle), &s, &c);
	for (i = 0; i < n; i++) {
		dx = (double)xin[i] - cx;
		dy = (double)yin[i] - cy;
		nx = calc_round(dx * c - dy * s + cx);
		ny = calc_round(dy * c + dx * s + cy);
		if (!(nx >= -2147483648.0 && nx <= 2147483647.0 &&
		    ny >= -2147483648.0 && ny <= 2147483647.0))
			return CALC_E_RANGE;
		xout[i] = (int32_t)nx;
		yout[i] = (int32_t)ny;
	}
	return CALC_OK;
}
