/*
 * Quaternions as w + xi + yj + zk. The magnitude tag of the original
 * representation is returned separately where it matters.
 */

#include <math.h>

#include "calc_internal.h"

static calc_quat
qmk(double w, double x, double y, double z)
{
	calc_quat q;

	q.w = w;
	q.x = x;
	q.y = y;
	q.z = z;
	return q;
}

calc_quat
calc_quat_mul(calc_quat a, calc_quat b)
{
	return qmk(a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
	    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
}

calc_quat
calc_quat_conj(calc_quat q)
{
	return qmk(q.w, -q.x, -q.y, -q.z);
}

/*
 * The inverse is conj(q) / |q|^2. The components are divided by the largest
 * magnitude s first, so the squared norm of the scaled quaternion lies in
 * [1, 4] and neither underflows (tiny q) nor overflows (huge q); the result
 * is conj(q / s) / |q / s|^2 / s, divided in two steps because |q / s|^2 * s
 * can overflow. A result that leaves the double range
 * (q with |q| below 1 / DBL_MAX) is CALC_E_RANGE.
 */
calc_status
calc_quat_inverse(calc_quat q, calc_quat *out)
{
	double s, w, x, y, z, n2;
	calc_quat r;

	if (out == NULL)
		return CALC_E_ARG;
	if (!calc__isfinite(q.w) || !calc__isfinite(q.x) ||
	    !calc__isfinite(q.y) || !calc__isfinite(q.z))
		return CALC_E_DOMAIN;
	s = calc__fabs(q.w);
	if (calc__fabs(q.x) > s)
		s = calc__fabs(q.x);
	if (calc__fabs(q.y) > s)
		s = calc__fabs(q.y);
	if (calc__fabs(q.z) > s)
		s = calc__fabs(q.z);
	if (s == 0.0)
		return CALC_E_DOMAIN;
	w = q.w / s;
	x = q.x / s;
	y = q.y / s;
	z = q.z / s;
	n2 = w * w + x * x + y * y + z * z;
	r = qmk(w / n2 / s, -x / n2 / s, -y / n2 / s, -z / n2 / s);
	if (!calc__isfinite(r.w) || !calc__isfinite(r.x) ||
	    !calc__isfinite(r.y) || !calc__isfinite(r.z))
		return CALC_E_RANGE;
	*out = r;
	return CALC_OK;
}

calc_status
calc_quat_from_vector(double x, double y, double z, calc_quat *q, double *magnitude)
{
	double v[3], u[3], m;

	if (q == NULL || magnitude == NULL)
		return CALC_E_ARG;
	v[0] = x;
	v[1] = y;
	v[2] = z;
	/* The scaled norm of calc_magnitude; the direction survives a norm above DBL_MAX. */
	if (calc__direction(v, 3, u, &m) != CALC_OK)
		return CALC_E_DOMAIN;
	*q = qmk(0.0, u[0], u[1], u[2]);
	*magnitude = m;
	return CALC_OK;
}

calc_status
calc_quat_from_axis_angle(double ax, double ay, double az, double angle,
    calc_angle_unit u, calc_quat *q)
{
	double v[3], d[3], h, s, c;

	if (q == NULL)
		return CALC_E_ARG;
	v[0] = ax;
	v[1] = ay;
	v[2] = az;
	if (calc__direction(v, 3, d, NULL) != CALC_OK)
		return CALC_E_DOMAIN;
	h = 0.5 * calc_angle_to_rad(u, angle);
	calc__sincos(h, &s, &c);
	*q = qmk(c, d[0] * s, d[1] * s, d[2] * s);
	return CALC_OK;
}

calc_quat
calc_quat_from_euler(double yaw, double pitch, double roll, calc_angle_unit u)
{
	double sp, cp, sy, cy, sr, cr;
	calc_quat qx, qy, qz;

	calc__sincos(0.5 * calc_angle_to_rad(u, pitch), &sp, &cp);
	calc__sincos(0.5 * calc_angle_to_rad(u, -yaw), &sy, &cy);
	calc__sincos(0.5 * calc_angle_to_rad(u, roll), &sr, &cr);
	qx = qmk(cp, sp, 0.0, 0.0);
	qy = qmk(cy, 0.0, sy, 0.0);
	qz = qmk(cr, 0.0, 0.0, sr);
	return calc_quat_mul(calc_quat_mul(qz, qy), qx);
}

calc_quat
calc_quat_rotate(calc_quat r, calc_quat v)
{
	return calc_quat_mul(calc_quat_mul(r, v), calc_quat_conj(r));
}
