#include <errno.h>
#include <float.h>
#include <math.h>

double
ldexp(fr, exp)
        double fr;
        int exp;
{
	int neg;
	int i;

	if (fr == 0)
		return 0;
	/* an infinity or a NaN scales to itself */
	if (fr != fr || fr - fr != 0)
		return fr;
	neg = 0;
	if (fr < 0) {
		fr = -fr;
		neg = 1;
	}
	fr = frexp(fr, &i);
	while (fr < 0.5) {
		fr = 2*fr;
		i = i-1;
	}
	exp = exp+i;
	if (exp > DBL_MAX_EXP) {
		errno = ERANGE;
		if (neg)
			return(-HUGE_VAL);
		else
			return(HUGE_VAL);
        }
	if (exp < DBL_MIN_EXP - 2)
		return(0);
	while (exp > 30) {
		fr = fr*(1L<<30);
		exp = exp-30;
	}
	while (exp < -30) {
		fr = fr/(1L<<30);
		exp = exp+30;
	}
	if (exp > 0)
		fr = fr*(1L<<exp);
	if (exp < 0)
		fr = fr/(1L<<-exp);
	if (neg)
            fr = -fr;
	return(fr);
}
