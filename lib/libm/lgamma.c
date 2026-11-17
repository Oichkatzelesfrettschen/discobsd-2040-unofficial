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

#include <errno.h>
#include <math.h>

/*
 * C17 7.12.8.3: the natural logarithm of |gamma(x)|.  Where gamma(x) is
 * representable with room to spare the result is log(|tgamma(x)|), whose
 * absolute error is the relative error of tgamma().  Above that, Stirling's
 * series
 *
 *	lgamma(x) = (x - 1/2)*log(x) - x + log(2*pi)/2
 *	    + 1/(12x) - 1/(360x**3) + 1/(1260x**5) - 1/(1680x**7)
 *
 * is used; its first omitted term, 1/(1188x**9), is below 1e-17 at the
 * switch point.  Far below zero the reflection
 * gamma(x)*gamma(1 - x) = pi/sin(pi*x) applies, with the sine taken of x
 * reduced modulo 2 so that it keeps its accuracy.  Zero and the negative
 * integers are poles.
 */
double
lgamma(double x)
{
	static const double pi = 3.14159265358979323846;
	double w;

	if (x != x)
		return x;
	if (x - x != 0.0)
		return HUGE_VAL;
	if (x <= 0.0 && floor(x) == x) {
		errno = ERANGE;
		return HUGE_VAL;
	}
	if (x < -30.0)
		return log(pi / fabs(sin(pi * fmod(x, 2.0)))) -
		    lgamma(1.0 - x);
	/*
	 * Below 2**-60 gamma(x) is 1/x - 0.5772... to the last bit of the
	 * logarithm, and 1/x itself overflows for the smallest subnormals
	 * while log(1/x) does not.
	 */
	if (fabs(x) < 0x1p-60)
		return -log(fabs(x));
	if (x < 30.0)
		return log(fabs(tgamma(x)));
	w = 1.0 / (x * x);
	w = (x - 0.5) * log(x) - x + 0.91893853320467274178 +
	    (((-1.0 / 1680.0 * w + 1.0 / 1260.0) * w - 1.0 / 360.0) * w +
	    1.0 / 12.0) / x;
	if (w - w != 0.0)
		errno = ERANGE;		/* overflow (7.12.1p4) */
	return w;
}
