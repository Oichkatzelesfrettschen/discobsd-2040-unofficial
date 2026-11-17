/*
 * double strtodx (char *string, char **endPtr, int radix)
 *	This procedure converts a floating-point number from an ASCII
 *	decimal representation to internal double-precision format.
 *
 * Original sources taken from 386bsd and modified for variable radix
 * by Serge Vakulenko, <vak@kiae.su>.
 *
 * Arguments:
 * string
 *      A decimal ASCII floating-point number, optionally preceded
 *      by white space. Must have form "-I.FE-X", where I is the integer
 *      part of the mantissa, F is the fractional part of the mantissa,
 *      and X is the exponent.  Either of the signs may be "+", "-", or
 *      omitted.  Either I or F may be omitted, or both.  The decimal point
 *      isn't necessary unless F is present. The "E" may actually be an "e",
 *      or "E", "S", "s", "F", "f", "D", "d", "L", "l".
 *      E and X may both be omitted (but not just one).
 *
 * endPtr
 *      If non-NULL, store terminating character's address here.
 *
 * radix
 *      Radix of floating point, one of 2, 8, 10, 16.
 *
 * The return value is the double-precision floating-point
 * representation of the characters in string.  If endPtr isn't
 * NULL, then *endPtr is filled in with the address of the
 * next character after the last one that was part of the
 * floating-point number.
 */

/*
 * The inherited note above describes the procedure this file once held.
 * strtod() now takes C17 7.22.1.3's whole subject sequence (decimal,
 * hexadecimal, inf and infinity, nan with an optional character sequence)
 * and rounds it once, to nearest with ties to even, through the converter
 * in strtofp_core.h.  A result that overflows, or that is a zero or
 * subnormal and inexact, sets ERANGE.
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "strtofp_core.h"

double
strtod(const char *string, char **end)
{
	union {
		double		value;
		uint64_t	bits;
	} result;
	struct input in = { string, 0 };
	size_t length;
	int range;

	result.bits = convert(&in, &length, STRTOFP_BINARY64, &range);
	if (range)
		errno = ERANGE;
	if (end != NULL)
		*end = (char *)string + length;
	return result.value;
}
