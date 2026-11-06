/*
 * The constants of the tree's math.h, compiled as strict C17 against the
 * tree headers so that a definition which is not a constant expression, or
 * needs a compiler extension the strict profile rejects, fails to build.
 * huge_val_contract_test.c reads them through these accessors because it is
 * compiled against the host headers.
 */
#include <errno.h>
#include <math.h>

static const double constant_huge_val = HUGE_VAL;
static const float constant_huge_valf = HUGE_VALF;
static const long double constant_huge_vall = HUGE_VALL;
static const float constant_infinity = INFINITY;

double
header_huge_val(void)
{
	return constant_huge_val;
}

float
header_huge_valf(void)
{
	return constant_huge_valf;
}

long double
header_huge_vall(void)
{
	return constant_huge_vall;
}

float
header_infinity(void)
{
	return constant_infinity;
}

/* C17 7.12p4: HUGE_VAL has type double, HUGE_VALF float, HUGE_VALL long double. */
int
header_types_match(void)
{
	return _Generic(HUGE_VAL, double: 1, default: 0) &&
	    _Generic(HUGE_VALF, float: 1, default: 0) &&
	    _Generic(HUGE_VALL, long double: 1, default: 0) &&
	    _Generic(INFINITY, float: 1, default: 0);
}

/* The tree's values, which a host errno.h need not share. */
int
header_edom(void)
{
	return EDOM;
}

int
header_erange(void)
{
	return ERANGE;
}
