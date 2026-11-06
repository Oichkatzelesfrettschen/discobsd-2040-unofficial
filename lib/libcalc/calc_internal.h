/*
 * Private helpers shared by the libcalc objects. The calc__ prefix marks a
 * symbol that is not part of the public interface in mathematics.h.
 *
 * The C library of the target provides sin, cos, exp, log, atan2, sqrt and
 * floor. Its sin and cos lose absolute accuracy in proportion to the
 * argument, its acos is wrong for negative arguments, its atan2 returns a
 * quarter turn for (0, 0), and exp returns a finite HUGE_VAL past the
 * double range. The wrappers below confine libcalc to the calls whose
 * accuracy the tests measure and replace the rest.
 */

#ifndef CALC_INTERNAL_H
#define CALC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "mathematics.h"

#define CALC_PI		3.14159265358979323846
#define CALC_TWO_PI	6.28318530717958647693
#define CALC_HALF_PI	1.57079632679489661923
#define CALC_LN2	0.693147180559945309417
#define CALC_DEG_PER_RAD 57.29577951308232087680	/* 180 / pi */

#if defined(__GNUC__)
#define CALC_INF	__builtin_inf()
#define CALC_NAN	__builtin_nan("")
#else
#define CALC_INF	(1.0e308 * 10.0)
#define CALC_NAN	(CALC_INF - CALC_INF)
#endif

/* Largest argument for which exp is finite, and the smallest that is not 0. */
#define CALC_EXP_MAX	709.782712893384
#define CALC_EXP_MIN	(-745.2)

/* Sign bit of a double, including negative zero and NaN. */
static inline int
calc__signbit(double x)
{
	union { double d; uint64_t u; } v;

	v.d = x;
	return (int)(v.u >> 63);
}

static inline double
calc__fabs(double x)
{
	union { double d; uint64_t u; } v;

	v.d = x;
	v.u &= ~((uint64_t)1 << 63);
	return v.d;
}

static inline double
calc__copysign(double mag, double sgn)
{
	return calc__signbit(sgn) ? -calc__fabs(mag) : calc__fabs(mag);
}

static inline int
calc__isnan(double x)
{
	return x != x;
}

static inline int
calc__isfinite(double x)
{
	return x - x == 0.0;
}

static inline int
calc__isinf(double x)
{
	return !calc__isnan(x) && !calc__isfinite(x);
}

/*
 * Power-of-two scaling from the IEEE exponent field. A power of two is exact
 * on the host and on the ROM float provider, so a value scaled by one and
 * scaled back returns every mantissa bit while the intermediate stays inside
 * the normal range. The ROM flushes subnormals, so no helper forms a
 * subnormal factor.
 */

/* 2^e exactly, for e in [-1022, 1023]. */
static inline double
calc__pow2(int e)
{
	union { double d; uint64_t u; } v;

	v.u = (uint64_t)(e + 1023) << 52;
	return v.d;
}

/*
 * x * 2^e for e in [-2044, 2044]: two factors of 2^(e / 2) and 2^(e - e / 2),
 * each inside [2^-1022, 2^1022]. Exact when x and the result are normal and
 * no partial product leaves the normal range.
 */
static inline double
calc__ldexp(double x, int e)
{
	int h = e / 2;

	return x * calc__pow2(h) * calc__pow2(e - h);
}

/*
 * The finite non-zero x with its exponent replaced by 0: a value in [1, 2)
 * with the sign of x kept. *e receives the exponent, so x = result * 2^*e.
 */
static inline double
calc__frexp1(double x, int *e)
{
	union { double d; uint64_t u; } v;
	int shift = 0;

	v.d = x;
	if (((v.u >> 52) & 0x7ff) == 0) {
		v.d *= 0x1p54;
		shift = 54;
	}
	*e = (int)((v.u >> 52) & 0x7ff) - 1023 - shift;
	v.u = (v.u & ~((uint64_t)0x7ff << 52)) | ((uint64_t)1023 << 52);
	return v.d;
}

/* Non-zero when the byte ranges [a, a + abytes) and [b, b + bbytes) share a byte. */
static inline int
calc__overlap(const void *a, size_t abytes, const void *b, size_t bbytes)
{
	uintptr_t pa = (uintptr_t)a;
	uintptr_t pb = (uintptr_t)b;

	return abytes != 0 && bbytes != 0 && pa < pb + bbytes && pb < pa + abytes;
}

/*
 * Non-zero when a and b of n doubles share storage without being the same
 * array: the shifted overlap that corrupts an element-wise loop.
 */
static inline int
calc__partial(const void *a, const void *b, size_t bytes)
{
	return a != b && calc__overlap(a, bytes, b, bytes);
}

/*
 * out = a / |a| for n components, overflow-safe; *mag (may be NULL) receives
 * |a|, which is infinite when it exceeds DBL_MAX while out stays exact.
 * CALC_E_DOMAIN for the zero vector (out unwritten). out may equal a.
 */
calc_status calc__direction(const double *a, size_t n, double *out, double *mag);

double calc__exp(double x);
double calc__log(double x);
double calc__log1p(double x);
double calc__hypot(double a, double b);
double calc__atan2(double y, double x);
void calc__sincos(double x, double *s, double *c);

double calc__sinh(double x);
double calc__cosh(double x);
double calc__tanh(double x);

#endif
