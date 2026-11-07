/*
 * computes a^b.
 * uses log and exp
 */
#include <errno.h>
#include <math.h>

/*
 * Whether y is an odd integer: an integer whose half is not one. Every
 * double of magnitude 2^53 or more, and each infinity, is even under this
 * test, and a NaN is not an integer.
 */
static int
oddint(y)
	double y;
{
	double h;

	if(floor(y) != y)
		return(0);
	h = y * 0.5;
	return(floor(h) != h);
}

double
pow(arg1,arg2)
        double arg1, arg2;
{
	double temp;
	long l;

	if(arg1 <= 0.) {
		if(arg1 == 0.) {
			/*
			 * C17 F.10.4.4: pow(+-0, 0) is 1. A negative power of
			 * zero is a pole, a range error (7.12.1p4) that returns
			 * an infinity, and a positive power returns a zero; both
			 * take the sign of the zero when y is an odd integer and
			 * are positive otherwise. 1 / arg1 is an infinity of the
			 * zero's sign (F.3), which reads that sign.
			 */
			if(arg2 == 0.)
				return(1.);
			if(arg2 < 0.) {
				errno = ERANGE;
				temp = HUGE_VAL;
			} else
				temp = 0.;
			if(oddint(arg2) && 1. / arg1 < 0.)
				temp = -temp;
			return(temp);
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
