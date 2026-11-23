/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Strict C17 translation of the library headers added for the hosted
 * interface: <fenv.h>, <locale.h>, <tgmath.h>, <uchar.h>, <wchar.h>,
 * <wctype.h>, and the additions to <float.h>, <math.h>, <stdlib.h>,
 * <string.h> and <time.h>.
 *
 * Each function is bound to a pointer of the exact type the standard's
 * synopsis gives, which a declaration of any other type fails.  Type
 * results are measured with _Generic, which applies zero conversions, and
 * the <float.h> values are compared with the compiler's own description
 * of the target's formats.
 */
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

#define IS_TYPE(expression, type) \
	_Generic((expression), type: 1, default: 0)

/* C17 5.2.4.2.2: the values the compiler uses for the target. */
_Static_assert(FLT_EVAL_METHOD == __FLT_EVAL_METHOD__, "FLT_EVAL_METHOD");
_Static_assert(DECIMAL_DIG == __DECIMAL_DIG__, "DECIMAL_DIG");
_Static_assert(FLT_DECIMAL_DIG == __FLT_DECIMAL_DIG__, "FLT_DECIMAL_DIG");
_Static_assert(DBL_DECIMAL_DIG == __DBL_DECIMAL_DIG__, "DBL_DECIMAL_DIG");
_Static_assert(LDBL_DECIMAL_DIG == __LDBL_DECIMAL_DIG__, "LDBL_DECIMAL_DIG");
_Static_assert(FLT_HAS_SUBNORM == __FLT_HAS_DENORM__, "FLT_HAS_SUBNORM");
_Static_assert(DBL_HAS_SUBNORM == __DBL_HAS_DENORM__, "DBL_HAS_SUBNORM");

/*
 * A floating comparison is not an integer constant expression, so the
 * *_TRUE_MIN values are compared in code the compiler folds: a mismatch
 * leaves a call to c17_float_mismatch, which no object defines, and the
 * Makefile requires this object to import zero symbols.
 */
void c17_float_mismatch(void);
void c17_float_values(void);

void
c17_float_values(void)
{
	if (FLT_TRUE_MIN != __FLT_DENORM_MIN__ ||
	    DBL_TRUE_MIN != __DBL_DENORM_MIN__ ||
	    LDBL_TRUE_MIN != __LDBL_DENORM_MIN__ ||
	    !(FLT_TRUE_MIN < FLT_MIN) || !(DBL_TRUE_MIN < DBL_MIN))
		c17_float_mismatch();
}

/* C17 7.12p2 with FLT_EVAL_METHOD 0. */
_Static_assert(IS_TYPE((float_t)0, float), "float_t is float");
_Static_assert(IS_TYPE((double_t)0, double), "double_t is double");
_Static_assert(IS_TYPE(NAN, float), "NAN is a float");
_Static_assert(IS_TYPE(INFINITY, float), "INFINITY is a float");
_Static_assert(FP_ILOGB0 == -2147483647 && FP_ILOGBNAN == 2147483647,
    "FP_ILOGB0 and FP_ILOGBNAN are INT_MIN + 1 and INT_MAX");
_Static_assert((math_errhandling & MATH_ERRNO) != 0,
    "errors are reported through errno");
_Static_assert(FP_NAN != FP_INFINITE && FP_INFINITE != FP_ZERO &&
    FP_ZERO != FP_SUBNORMAL && FP_SUBNORMAL != FP_NORMAL &&
    FP_NORMAL != FP_NAN && FP_ZERO != FP_NAN && FP_SUBNORMAL != FP_NAN &&
    FP_NORMAL != FP_INFINITE && FP_NORMAL != FP_ZERO &&
    FP_SUBNORMAL != FP_INFINITE, "the classification values are distinct");

/*
 * C17 7.25p3: the generic type is that of the floating arguments after the
 * usual arithmetic conversions, with an integer argument taken as double.
 */
static float f;
static double d;
static long double ld;
static int i;
_Static_assert(IS_TYPE(sqrt(f), float), "sqrt(float) selects sqrtf");
_Static_assert(IS_TYPE(sqrt(d), double), "sqrt(double) selects sqrt");
_Static_assert(IS_TYPE(sqrt(ld), long double), "sqrt(long double)");
_Static_assert(IS_TYPE(sqrt(i), double), "an integer selects double");
_Static_assert(IS_TYPE(pow(f, f), float), "pow(float, float)");
_Static_assert(IS_TYPE(pow(f, d), double), "pow(float, double)");
_Static_assert(IS_TYPE(pow(f, ld), long double), "pow(float, long double)");
_Static_assert(IS_TYPE(pow(f, i), double), "pow(float, int) is double");
_Static_assert(IS_TYPE(fma(f, f, f), float), "fma(float, float, float)");
_Static_assert(IS_TYPE(fma(f, f, d), double), "fma widens to double");
_Static_assert(IS_TYPE(ldexp(f, i), float), "ldexp keeps the float");
_Static_assert(IS_TYPE(frexp(d, &i), double), "frexp(double, int *)");
_Static_assert(IS_TYPE(ilogb(f), int), "ilogb returns int");
_Static_assert(IS_TYPE(lround(f), long), "lround returns long");
_Static_assert(IS_TYPE(llrint(d), long long), "llrint returns long long");
_Static_assert(IS_TYPE(nexttoward(f, ld), float), "nexttoward(float, ...)");
_Static_assert(IS_TYPE(remquo(f, f, &i), float), "remquo(float, float)");
_Static_assert(IS_TYPE(fabs(f), float) && IS_TYPE(fmax(d, f), double),
    "fabs and fmax select by type");

/* <fenv.h>: the environment of an implementation without flags. */
_Static_assert(FE_ALL_EXCEPT == 0, "no exception is supported");
static const fenv_t *const default_environment = FE_DFL_ENV;

/* <locale.h>, <time.h> and <uchar.h> constants and types. */
_Static_assert(LC_ALL != LC_COLLATE && LC_CTYPE != LC_NUMERIC &&
    LC_MONETARY != LC_TIME, "the categories are distinct");
_Static_assert(TIME_UTC > 0, "TIME_UTC is a positive base");
_Static_assert(IS_TYPE(CLOCKS_PER_SEC, clock_t), "CLOCKS_PER_SEC is clock_t");
_Static_assert(CLOCKS_PER_SEC == 1000000, "XSI fixes CLOCKS_PER_SEC");
_Static_assert(IS_TYPE(u'x', char16_t), "u'' literals are char16_t");
_Static_assert(IS_TYPE(U'x', char32_t), "U'' literals are char32_t");
_Static_assert(IS_TYPE(L'x', wchar_t) || IS_TYPE(L'x', int),
    "wchar_t matches the literal");
_Static_assert(IS_TYPE(WEOF, wint_t), "WEOF is a wint_t");

/* Exact prototypes. */
int (*const p_fegetround)(void) = fegetround;
int (*const p_fesetround)(int) = fesetround;
int (*const p_feclearexcept)(int) = feclearexcept;
int (*const p_fegetenv)(fenv_t *) = fegetenv;
int (*const p_feupdateenv)(const fenv_t *) = feupdateenv;
char *(*const p_setlocale)(int, const char *) = setlocale;
struct lconv *(*const p_localeconv)(void) = localeconv;
int (*const p_strcoll)(const char *, const char *) = strcoll;
size_t (*const p_strxfrm)(char *restrict, const char *restrict, size_t) =
    strxfrm;
clock_t (*const p_clock)(void) = clock;
int (*const p_timespec_get)(struct timespec *, int) = timespec_get;
float (*const p_strtof)(const char *restrict, char **restrict) = strtof;
long double (*const p_strtold)(const char *restrict, char **restrict) =
    strtold;
size_t (*const p_mbrtowc)(wchar_t *restrict, const char *restrict, size_t,
    mbstate_t *restrict) = mbrtowc;
size_t (*const p_wcsrtombs)(char *restrict, const wchar_t **restrict, size_t,
    mbstate_t *restrict) = wcsrtombs;
wchar_t *(*const p_wcstok)(wchar_t *restrict, const wchar_t *restrict,
    wchar_t **restrict) = wcstok;
long double (*const p_wcstold)(const wchar_t *restrict, wchar_t **restrict) =
    wcstold;
int (*const p_iswctype)(wint_t, wctype_t) = iswctype;
wint_t (*const p_towctrans)(wint_t, wctrans_t) = towctrans;
size_t (*const p_mbrtoc16)(char16_t *restrict, const char *restrict, size_t,
    mbstate_t *restrict) = mbrtoc16;
size_t (*const p_c32rtomb)(char *restrict, char32_t, mbstate_t *restrict) =
    c32rtomb;
div_t (*const p_div)(int, int) = div;

/* <math.h> functions by their own names, macros suppressed. */
double (*const p_fma)(double, double, double) = (fma);
float (*const p_fmaf)(float, float, float) = (fmaf);
long double (*const p_fmal)(long double, long double, long double) = (fmal);
double (*const p_remquo)(double, double, int *) = (remquo);
double (*const p_nexttoward)(double, long double) = (nexttoward);
int (*const p_ilogb)(double) = (ilogb);
long long (*const p_llround)(double) = (llround);
float (*const p_scalbnf)(float, int) = (scalbnf);
double (*const p_scalbln)(double, long) = (scalbln);
double (*const p_nan)(const char *) = nan;
double (*const p_tgamma)(double) = (tgamma);
long double (*const p_cbrtl)(long double) = (cbrtl);

const void *c17_library_contract_anchor(void);

/* Keep the static objects referenced. */
const void *
c17_library_contract_anchor(void)
{
	return (f + d + ld + i) > 0 ? (const void *)default_environment : 0;
}
