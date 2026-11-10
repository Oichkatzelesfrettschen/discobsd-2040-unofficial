#include <inttypes.h>

/*
 * C17 7.8.2.2: the quotient truncates toward zero and the remainder takes
 * the numerator's sign.  The division is restoring shift-and-subtract over
 * the magnitudes, one quotient bit per step, because ARMv6-M has no divide
 * instruction and libc carries no 64-bit divide routine; every shift is by
 * a constant, which the compiler opens in line.  A zero denominator and
 * INTMAX_MIN / -1 are undefined, as they are for the / operator.
 */
imaxdiv_t
imaxdiv(intmax_t numerator, intmax_t denominator)
{
	imaxdiv_t result;
	uintmax_t dividend, divisor, quotient, remainder;
	int bit;

	dividend = numerator < 0 ? 0 - (uintmax_t)numerator :
	    (uintmax_t)numerator;
	divisor = denominator < 0 ? 0 - (uintmax_t)denominator :
	    (uintmax_t)denominator;
	quotient = 0;
	remainder = 0;
	for (bit = 0; bit < 64; bit++) {
		remainder = remainder << 1 | dividend >> 63;
		dividend <<= 1;
		quotient <<= 1;
		if (remainder >= divisor) {
			remainder -= divisor;
			quotient |= 1;
		}
	}
	result.quot = (numerator < 0) != (denominator < 0) ?
	    (intmax_t)(0 - quotient) : (intmax_t)quotient;
	result.rem = numerator < 0 ? (intmax_t)(0 - remainder) :
	    (intmax_t)remainder;
	return result;
}
