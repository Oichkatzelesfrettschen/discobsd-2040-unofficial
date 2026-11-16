/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef _MATH_H_
#define _MATH_H_

double  fabs(double), floor(double), ceil(double);
double  sqrt(double), hypot(double, double);
double  sin(double), cos(double), tan(double);
double  asin(double), acos(double), atan(double), atan2(double, double);
double  exp(double), log(double), log10(double), pow(double, double);
double  sinh(double), cosh(double), tanh(double);
double  j0(double), j1(double), jn(int, double);
double  y0(double), y1(double), yn(int, double);

/*
 * C17 7.12p3-4 and Annex F.9: HUGE_VAL is a positive double constant
 * expression, and under IEEE 754 arithmetic it is +infinity, so the
 * overflowing result of a function is distinguishable from the largest
 * finite value. HUGE_VALF and HUGE_VALL are the same value as float and
 * long double, and INFINITY is the float infinity. GCC and Clang fold the
 * builtins to constants; another compiler gets the product, which IEEE 754
 * arithmetic evaluates to infinity.
 */
#if defined(__GNUC__)
#define HUGE_VAL    (__builtin_huge_val())
#define HUGE_VALF   (__builtin_huge_valf())
#define HUGE_VALL   (__builtin_huge_vall())
#define INFINITY    (__builtin_inff())
#else
#define HUGE_VAL    (1.0e308 * 1.0e308)
#define HUGE_VALF   ((float)HUGE_VAL)
#define HUGE_VALL   ((long double)HUGE_VAL)
#define INFINITY    ((float)HUGE_VAL)
#endif

/*
 * The double functions behind the isnan() and isinf() macros below; the
 * parentheses keep the names from expanding as those macros.
 */
int isnanf(float x);
int (isnan)(double x);

int isinff(float x);
int (isinf)(double x);

float modff(float x, float *iptr);
double modf(double x, double *iptr);

float frexpf(float x, int *exp);
double frexp(double x, int *exp);

float ldexpf(float x, int exp);
double ldexp(double x, int exp);

double fmod(double x, double y);

#ifndef __SMALLER_C__
/*
 * C17 7.12.  Expressions evaluate in the type of their operands
 * (FLT_EVAL_METHOD is 0), so float_t and double_t are float and double.
 * Errors are reported through errno; the system has no floating-point
 * exception flags.  The members below are hidden from Smaller C, whose
 * language has neither float functions of this shape nor _Generic.
 */
typedef float	float_t;
typedef double	double_t;

#if defined(__GNUC__)
#define NAN		(__builtin_nanf(""))
#else
#define NAN		(0.0f / 0.0f)
#endif

#define FP_NAN		0
#define FP_INFINITE	1
#define FP_ZERO		2
#define FP_SUBNORMAL	3
#define FP_NORMAL	4

#define FP_ILOGB0	(-2147483647)
#define FP_ILOGBNAN	2147483647

#define MATH_ERRNO	1
#define MATH_ERREXCEPT	2
#define math_errhandling	MATH_ERRNO

/*
 * C17 7.12.3 and 7.12.14: classification and quiet comparison of an
 * argument of any real floating type.  The compiler expands them from the
 * operand's own format.  isnan() and isinf() keep calling the functions of
 * those names for a double, so existing callers compile to the same code.
 */
#define fpclassify(x)	__builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, \
			    FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x)	__builtin_isfinite(x)
#define isnormal(x)	__builtin_isnormal(x)
#define signbit(x)	__builtin_signbit(x)
#define isnan(x)	_Generic((x), float: isnanf, default: isnan)(x)
#define isinf(x)	_Generic((x), float: isinff, default: isinf)(x)
#define isgreater(x, y)		__builtin_isgreater((x), (y))
#define isgreaterequal(x, y)	__builtin_isgreaterequal((x), (y))
#define isless(x, y)		__builtin_isless((x), (y))
#define islessequal(x, y)	__builtin_islessequal((x), (y))
#define islessgreater(x, y)	__builtin_islessgreater((x), (y))
#define isunordered(x, y)	__builtin_isunordered((x), (y))

double  acosh(double);
double  asinh(double);
double  atanh(double);
double  exp2(double);
double  expm1(double);
double  log1p(double);
double  log2(double);
double  logb(double);
double  cbrt(double);
double  erf(double);
double  erfc(double);
double  lgamma(double);
double  tgamma(double);
double  nearbyint(double);
double  rint(double);
double  round(double);
double  trunc(double);
double  remainder(double, double), copysign(double, double);
double  nextafter(double, double);
double  fdim(double, double), fmax(double, double), fmin(double, double);
double  fma(double, double, double);
double  remquo(double, double, int *);
double  scalbn(double, int), scalbln(double, long);
double  nan(const char *);
int     ilogb(double);
long    lrint(double), lround(double);
long long llrint(double), llround(double);

float   acosf(float);
float   asinf(float);
float   atanf(float);
float   cosf(float);
float   sinf(float);
float   tanf(float);
float   acoshf(float);
float   asinhf(float);
float   atanhf(float);
float   coshf(float);
float   sinhf(float);
float   tanhf(float);
float   expf(float);
float   exp2f(float);
float   expm1f(float);
float   logf(float);
float   log10f(float);
float   log1pf(float);
float   log2f(float);
float   logbf(float);
float   cbrtf(float);
float   fabsf(float);
float   sqrtf(float);
float   erff(float);
float   erfcf(float);
float   lgammaf(float);
float   tgammaf(float);
float   ceilf(float);
float   floorf(float);
float   nearbyintf(float);
float   rintf(float);
float   roundf(float);
float   truncf(float);
float   atan2f(float, float);
float   powf(float, float);
float   hypotf(float, float);
float   fmodf(float, float);
float   remainderf(float, float);
float   copysignf(float, float);
float   nextafterf(float, float);
float   fdimf(float, float);
float   fmaxf(float, float);
float   fminf(float, float);
float   fmaf(float, float, float);
float   remquof(float, float, int *);
float   scalbnf(float, int), scalblnf(float, long);
float   nanf(const char *);
float   nexttowardf(float, long double);
int     ilogbf(float);
long    lrintf(float), lroundf(float);
long long llrintf(float), llroundf(float);

/*
 * long double has double's format on this ABI, so each long double
 * function is the double function under a second name and costs no code.
 * A translation unit whose long double is wider sees none of them.
 */
#if defined(__LDBL_MANT_DIG__) && __LDBL_MANT_DIG__ == __DBL_MANT_DIG__
#define __LDBL_IS(name)	__asm__(#name)
long double acosl(long double) __LDBL_IS(acos);
long double asinl(long double) __LDBL_IS(asin);
long double atanl(long double) __LDBL_IS(atan);
long double cosl(long double) __LDBL_IS(cos);
long double sinl(long double) __LDBL_IS(sin);
long double tanl(long double) __LDBL_IS(tan);
long double acoshl(long double) __LDBL_IS(acosh);
long double asinhl(long double) __LDBL_IS(asinh);
long double atanhl(long double) __LDBL_IS(atanh);
long double coshl(long double) __LDBL_IS(cosh);
long double sinhl(long double) __LDBL_IS(sinh);
long double tanhl(long double) __LDBL_IS(tanh);
long double expl(long double) __LDBL_IS(exp);
long double exp2l(long double) __LDBL_IS(exp2);
long double expm1l(long double) __LDBL_IS(expm1);
long double logl(long double) __LDBL_IS(log);
long double log10l(long double) __LDBL_IS(log10);
long double log1pl(long double) __LDBL_IS(log1p);
long double log2l(long double) __LDBL_IS(log2);
long double logbl(long double) __LDBL_IS(logb);
long double cbrtl(long double) __LDBL_IS(cbrt);
long double fabsl(long double) __LDBL_IS(fabs);
long double sqrtl(long double) __LDBL_IS(sqrt);
long double erfl(long double) __LDBL_IS(erf);
long double erfcl(long double) __LDBL_IS(erfc);
long double lgammal(long double) __LDBL_IS(lgamma);
long double tgammal(long double) __LDBL_IS(tgamma);
long double ceill(long double) __LDBL_IS(ceil);
long double floorl(long double) __LDBL_IS(floor);
long double nearbyintl(long double) __LDBL_IS(nearbyint);
long double rintl(long double) __LDBL_IS(rint);
long double roundl(long double) __LDBL_IS(round);
long double truncl(long double) __LDBL_IS(trunc);
long double atan2l(long double, long double) __LDBL_IS(atan2);
long double powl(long double, long double) __LDBL_IS(pow);
long double hypotl(long double, long double) __LDBL_IS(hypot);
long double fmodl(long double, long double) __LDBL_IS(fmod);
long double remainderl(long double, long double) __LDBL_IS(remainder);
long double copysignl(long double, long double) __LDBL_IS(copysign);
long double nextafterl(long double, long double) __LDBL_IS(nextafter);
long double fdiml(long double, long double) __LDBL_IS(fdim);
long double fmaxl(long double, long double) __LDBL_IS(fmax);
long double fminl(long double, long double) __LDBL_IS(fmin);
long double fmal(long double, long double, long double) __LDBL_IS(fma);
long double remquol(long double, long double, int *) __LDBL_IS(remquo);
long double scalbnl(long double, int) __LDBL_IS(scalbn);
long double scalblnl(long double, long) __LDBL_IS(scalbln);
long double frexpl(long double, int *) __LDBL_IS(frexp);
long double ldexpl(long double, int) __LDBL_IS(ldexp);
long double modfl(long double, long double *) __LDBL_IS(modf);
long double nanl(const char *) __LDBL_IS(nan);
long double nexttowardl(long double, long double) __LDBL_IS(nextafter);
double  nexttoward(double, long double) __LDBL_IS(nextafter);
int     ilogbl(long double) __LDBL_IS(ilogb);
long    lrintl(long double) __LDBL_IS(lrint);
long    lroundl(long double) __LDBL_IS(lround);
long long llrintl(long double) __LDBL_IS(llrint);
long long llroundl(long double) __LDBL_IS(llround);
#undef __LDBL_IS
#endif
#endif /* !__SMALLER_C__ */

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)

#define M_E             2.7182818284590452354   /* e */
#define M_LOG2E         1.4426950408889634074   /* log 2e */
#define M_LOG10E        0.43429448190325182765  /* log 10e */
#define M_LN2           0.69314718055994530942  /* log e2 */
#define M_LN10          2.30258509299404568402  /* log e10 */
#define M_PI            3.14159265358979323846  /* pi */
#define M_PI_2          1.57079632679489661923  /* pi/2 */
#define M_PI_4          0.78539816339744830962  /* pi/4 */
#define M_1_PI          0.31830988618379067154  /* 1/pi */
#define M_2_PI          0.63661977236758134308  /* 2/pi */
#define M_2_SQRTPI      1.12837916709551257390  /* 2/sqrt(pi) */
#define M_SQRT2         1.41421356237309504880  /* sqrt(2) */
#define M_SQRT1_2       0.70710678118654752440  /* 1/sqrt(2) */

#endif /* !_ANSI_SOURCE && !_POSIX_SOURCE */

#endif /* !_MATH_H_ */
