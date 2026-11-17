#include <math.h>

#include "fpbits.h"

/*
 * C17 7.12.7.2: the absolute value, which F.10.4.2 makes the operand with
 * its sign bit cleared: fabs(-0.0) is +0.0 and the sign of a NaN goes too.
 * A comparison with zero cannot see the sign of a zero, so the bit is
 * cleared directly, as fabsf.c does for float.
 */
double
fabs(double x)
{
	return fp_double(fp_bits(x) & ~FP_SIGN_BIT);
}
