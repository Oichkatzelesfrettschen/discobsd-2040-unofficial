/*
 * asin(arg) and acos(arg) return the arcsin, arccos,
 * respectively of their arguments.
 *
 * Arctan is called after appropriate range reduction.
 */
#include <errno.h>
#include <math.h>


static double pio2	= 1.570796326794896619;

double
asin(arg)
        double arg;
{
	double sign, temp;

	if(arg != arg)
		return(arg);
	sign = 1.;
	if(arg <0){
		arg = -arg;
		sign = -1.;
	}

	if(arg > 1.){
		/* C17 7.12.4.2: |arg| > 1 is a domain error, F.10.1.2 a NaN. */
		errno = EDOM;
		return(NAN);
	}

	temp = sqrt(1. - arg*arg);
	if(arg > 0.7)
		temp = pio2 - atan(temp/arg);
	else
		temp = atan(arg/temp);

	return(sign*temp);
}

double
acos(arg)
        double arg;
{
	if(arg > 1. || arg < -1.){
		errno = EDOM;
		return(NAN);
	}

	/* asin() is odd, so pi/2 - asin(arg) holds on both sides of zero. */
	return(pio2 - asin(arg));
}
