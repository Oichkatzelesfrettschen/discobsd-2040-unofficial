/*
 * exp returns the exponential function of its
 * floating-point argument.
 *
 * The coefficients are #1069 from Hart and Cheney. (22.35D)
 */
#include <float.h>
#include <errno.h>
#include <math.h>

static double	p0	= .2080384346694663001443843411e7;
static double	p1	= .3028697169744036299076048876e5;
static double	p2	= .6061485330061080841615584556e2;
static double	q0	= .6002720360238832528230907598e7;
static double	q1	= .3277251518082914423057964422e6;
static double	q2	= .1749287689093076403844945335e4;
static double	log2e	= 1.4426950408889634073599247;
static double	sqrt2	= 1.4142135623730950488016887;
/*
 * ln 2 as a head whose low 21 bits are zero and a tail, so that ent * ln2hi
 * is exact for any exponent this function reaches and the reduced argument
 * keeps full precision: reducing through arg * log2e alone leaves a
 * relative error of about 1e-13 in a result near 2^1000.
 */
static double	ln2hi	= 6.93147180369123816490e-01;
static double	ln2lo	= 1.90821492927058770002e-10;
/*
 * C17 7.12.6.1: the result overflows above maxf = ln(DBL_MAX) and rounds
 * to zero below minf, a little under ln(2^-1075), half the smallest
 * subnormal. Both guards also keep floor(arg * log2e) inside an int. The
 * tree ldexp flushes a result under 2^-1024 to zero, so an argument from
 * minf up to ln(2^-1024) returns zero from ldexp.
 */
static double	maxf	= 709.782712893384;
static double	minf	= -745.14;

double
exp(arg)
double arg;
{
	double fract;
	double temp1, temp2, xsq;
	int ent;

	if(arg == 0.)
		return(1.);
	if(arg < minf)
		return(0.);
	if(arg > maxf) {
		errno = ERANGE;
		return(HUGE_VAL);
	}
	ent = floor(arg*log2e);
	fract = ((arg - ent*ln2hi) - ent*ln2lo)*log2e - 0.5;
	xsq = fract*fract;
	temp1 = ((p2*xsq+p1)*xsq+p0)*fract;
	temp2 = ((1.0*xsq+q2)*xsq+q1)*xsq + q0;
	return(ldexp(sqrt2*(temp2+temp1)/(temp2-temp1), ent));
}
