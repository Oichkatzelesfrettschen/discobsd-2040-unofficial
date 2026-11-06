/*
 * computes a^b.
 * uses log and exp
 */
#include <errno.h>
#include <math.h>


double
pow(arg1,arg2)
        double arg1, arg2;
{
	double temp;
	long l;

	if(arg1 <= 0.) {
		if(arg1 == 0.) {
			/*
			 * C17 F.10.4.4: pow(0, 0) is 1, and a negative power of
			 * zero is a pole, a range error (7.12.1p4) that returns
			 * +infinity.
			 */
			if(arg2 == 0.)
				return(1.);
			if(arg2 < 0.) {
				errno = ERANGE;
				return(HUGE_VAL);
			}
			return(0.);
		}
		l = arg2;
		if(l != arg2)
			goto domain;
		temp = exp(arg2 * log(-arg1));
		if(l & 1)
			temp = -temp;
		return(temp);
	}
	return(exp(arg2 * log(arg1)));

domain:
	errno = EDOM;
	return(0.);
}
