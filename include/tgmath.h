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

#ifndef _TGMATH_H_
#define _TGMATH_H_

/*
 * C17 7.25: type-generic macros over <math.h>.  Each selects the float,
 * double or long double function from the type its real arguments have
 * after the usual arithmetic conversions; an integer argument selects the
 * double function (7.25p3).  __tg_real() gives an integer argument type
 * double and leaves a floating one alone, and the selection type is that
 * of the sum of those, which is what the conversions then yield.  <complex.h> is not
 * provided (__STDC_NO_COMPLEX__), so there are no complex selections.
 */
#include <math.h>

#define __tg_real(x)		_Generic((x), float: (x), double: (x), \
				    long double: (x), default: (double)(x))
#define __tg_select(type, name)	_Generic((type), float: name##f, \
				    long double: name##l, default: name)
#define __tg_1(name, x)		__tg_select(__tg_real(x), name)(x)
#define __tg_2(name, x, y)	\
	__tg_select(__tg_real(x) + __tg_real(y), name)((x), (y))
#define __tg_3(name, x, y, z)	\
	__tg_select(__tg_real(x) + __tg_real(y) + __tg_real(z), name)	\
	    ((x), (y), (z))
/* The second argument is not a floating operand and selects nothing. */
#define __tg_1n(name, x, n)	__tg_select(__tg_real(x), name)((x), (n))

#define acos(x)		__tg_1(acos, x)
#define asin(x)		__tg_1(asin, x)
#define atan(x)		__tg_1(atan, x)
#define acosh(x)		__tg_1(acosh, x)
#define asinh(x)		__tg_1(asinh, x)
#define atanh(x)		__tg_1(atanh, x)
#define cos(x)		__tg_1(cos, x)
#define sin(x)		__tg_1(sin, x)
#define tan(x)		__tg_1(tan, x)
#define cosh(x)		__tg_1(cosh, x)
#define sinh(x)		__tg_1(sinh, x)
#define tanh(x)		__tg_1(tanh, x)
#define exp(x)		__tg_1(exp, x)
#define log(x)		__tg_1(log, x)
#define log10(x)		__tg_1(log10, x)
#define sqrt(x)		__tg_1(sqrt, x)
#define fabs(x)		__tg_1(fabs, x)
#define exp2(x)		__tg_1(exp2, x)
#define expm1(x)		__tg_1(expm1, x)
#define log1p(x)		__tg_1(log1p, x)
#define log2(x)		__tg_1(log2, x)
#define logb(x)		__tg_1(logb, x)
#define cbrt(x)		__tg_1(cbrt, x)
#define erf(x)		__tg_1(erf, x)
#define erfc(x)		__tg_1(erfc, x)
#define lgamma(x)		__tg_1(lgamma, x)
#define tgamma(x)		__tg_1(tgamma, x)
#define ceil(x)		__tg_1(ceil, x)
#define floor(x)		__tg_1(floor, x)
#define nearbyint(x)		__tg_1(nearbyint, x)
#define rint(x)		__tg_1(rint, x)
#define round(x)		__tg_1(round, x)
#define trunc(x)		__tg_1(trunc, x)
#define atan2(x, y)	__tg_2(atan2, x, y)
#define pow(x, y)	__tg_2(pow, x, y)
#define hypot(x, y)	__tg_2(hypot, x, y)
#define fmod(x, y)	__tg_2(fmod, x, y)
#define remainder(x, y)	__tg_2(remainder, x, y)
#define copysign(x, y)	__tg_2(copysign, x, y)
#define nextafter(x, y)	__tg_2(nextafter, x, y)
#define fdim(x, y)	__tg_2(fdim, x, y)
#define fmax(x, y)	__tg_2(fmax, x, y)
#define fmin(x, y)	__tg_2(fmin, x, y)
#define fma(x, y, z)	__tg_3(fma, x, y, z)
#define remquo(x, y, q)	\
	__tg_select(__tg_real(x) + __tg_real(y), remquo)((x), (y), (q))
#define frexp(x, e)	__tg_1n(frexp, x, e)
#define ldexp(x, n)	__tg_1n(ldexp, x, n)
#define scalbn(x, n)	__tg_1n(scalbn, x, n)
#define scalbln(x, n)	__tg_1n(scalbln, x, n)
#define nexttoward(x, y)	_Generic(__tg_real(x), float: nexttowardf, \
			    long double: nexttowardl, default: nexttoward)((x), (y))
#define ilogb(x)	__tg_1(ilogb, x)
#define lrint(x)	__tg_1(lrint, x)
#define llrint(x)	__tg_1(llrint, x)
#define lround(x)	__tg_1(lround, x)
#define llround(x)	__tg_1(llround, x)

#endif /* !_TGMATH_H_ */
