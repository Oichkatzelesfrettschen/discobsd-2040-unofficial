/*
 * Floating point conversion for _doprnt, split from doprnt.c so that the
 * software double arithmetic it needs is linked only into programs that
 * print floats. See the note at __doprnt_cvt in doprnt.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>

static unsigned char *
cvtround (double fract, int *exp, unsigned char *start, unsigned char *end, unsigned char ch,
	unsigned char *negp)
{
	double tmp;

	if (fract) {
		modf (fract * 10, &tmp);
	} else {
		tmp = ch - '0';
	}
	if (tmp > 4) {
		for (;; --end) {
			if (*end == '.') {
				--end;
			}
			if (++*end <= '9') {
				break;
			}
			*end = '0';
			if (end == start) {
				if (exp) {	/* e/E; increment exponent */
					*end = '1';
					++*exp;
				} else {	/* f; add extra digit */
					*--end = '1';
					--start;
				}
				break;
			}
		}
	} else if (*negp) {
		/*
		 * ``"%.3f", (double)-0.0004'' gives you a negative 0.
		 */
		for (;; --end) {
			if (*end == '.') {
				--end;
			}
			if (*end != '0') {
				break;
			}
			if (end == start) {
				*negp = 0;
			}
		}
	}
	return start;
}

static unsigned char *
exponent (unsigned char *p, int exp, unsigned char fmtch)
{
	unsigned char expbuf [8], *t;

	*p++ = fmtch;
	if (exp < 0) {
		exp = -exp;
		*p++ = '-';
	} else {
		*p++ = '+';
	}
	t = expbuf + sizeof(expbuf);
	if (exp > 9) {
		do {
			*--t = exp % 10 + '0';
		} while ((exp /= 10) > 9);
		*--t = exp + '0';
		for (; t < expbuf + sizeof(expbuf); *p++ = *t++)
			continue;
	} else {
		*p++ = '0';
		*p++ = exp + '0';
	}
	return p;
}

int
__doprnt_cvt (double number, int prec, int sharpflag, unsigned char *negp, unsigned char fmtch,
	unsigned char *startp, unsigned char *endp)
{
	unsigned char *p, *t;
	double fract;
	int dotrim, expcnt, gformat;
	double integer, tmp;

	expcnt = 0;
	dotrim = expcnt = gformat = 0;
	fract = modf (number, &integer);

	/*
	 * get an extra slot for rounding
	 */
	t = ++startp;

	/*
	 * get integer portion of number; put into the end of the buffer; the
	 * .01 is added for modf (356.0 / 10, &integer) returning .59999999...
	 */
	for (p = endp - 1; integer; ++expcnt) {
		tmp = modf (integer / 10, &integer);
		*p-- = (int) ((tmp + .01) * 10) + '0';
	}
	switch (fmtch) {
	case 'f':
		/* reverse integer into beginning of buffer */
		if (expcnt) {
			for (; ++p < endp; *t++ = *p);
		} else {
			*t++ = '0';
		}

		/*
		 * if precision required or alternate flag set, add in a
		 * decimal point.
		 */
		if (prec || sharpflag) {
			*t++ = '.';
		}

		/*
		 * if requires more precision and some fraction left
		 */
		if (fract) {
			if (prec) {
				do {
					fract = modf (fract * 10, &tmp);
					*t++ = (int)tmp + '0';
				} while (--prec && fract);
			}
			if (fract) {
				startp = cvtround (fract, 0, startp,
					t - 1, '0', negp);
			}
		}
		for (; prec--; *t++ = '0');
		break;
	case 'e':
	case 'E':
eformat:	if (expcnt) {
			*t++ = *++p;
			if (prec || sharpflag) {
				*t++ = '.';
			}

			/*
			 * if requires more precision and some integer left
			 */
			for (; prec && ++p < endp; --prec) {
				*t++ = *p;
			}

			/*
			 * if done precision and more of the integer component,
			 * round using it; adjust fract so we don't re-round
			 * later.
			 */
			if (! prec && ++p < endp) {
				fract = 0;
				startp = cvtround (0, &expcnt, startp,
					t - 1, *p, negp);
			}
			/*
			 * adjust expcnt for digit in front of decimal
			 */
			--expcnt;
		}
		/*
		 * until first fractional digit, decrement exponent
		 */
		else if (fract) {
			/*
			 * adjust expcnt for digit in front of decimal
			 */
			for (expcnt = -1;; --expcnt) {
				fract = modf (fract * 10, &tmp);
				if (tmp) {
					break;
				}
			}
			*t++ = (int)tmp + '0';
			if (prec || sharpflag) {
				*t++ = '.';
			}
		} else {
			*t++ = '0';
			if (prec || sharpflag) {
				*t++ = '.';
			}
		}
		/*
		 * if requires more precision and some fraction left
		 */
		if (fract) {
			if (prec) {
				do {
					fract = modf (fract * 10, &tmp);
					*t++ = (int)tmp + '0';
				} while (--prec && fract);
			}
			if (fract) {
				startp = cvtround (fract, &expcnt, startp,
					t - 1, '0', negp);
			}
		}
		/*
		 * if requires more precision
		 */
		for (; prec--; *t++ = '0');

		/*
		 * unless alternate flag, trim any g/G format trailing 0's
		 */
		if (gformat && ! sharpflag) {
			while (t > startp && *--t == '0');
			if (*t == '.') {
				--t;
			}
			++t;
		}
		t = exponent (t, expcnt, fmtch);
		break;
	case 'g':
	case 'G':
		/*
		 * a precision of 0 is treated as a precision of 1
		 */
		if (!prec) {
			++prec;
		}

		/*
		 * ``The style used depends on the value converted; style e
		 * will be used only if the exponent resulting from the
		 * conversion is less than -4 or greater than the precision.''
		 *	-- ANSI X3J11
		 */
		if (expcnt > prec || (! expcnt && fract && fract < .0001)) {
			/*
			 * g/G format counts "significant digits, not digits of
			 * precision; for the e/E format, this just causes an
			 * off-by-one problem, i.e. g/G considers the digit
			 * before the decimal point significant and e/E doesn't
			 * count it as precision.
			 */
			--prec;
			fmtch -= 2;		/* G->E, g->e */
			gformat = 1;
			goto eformat;
		}
		/*
		 * reverse integer into beginning of buffer,
		 * note, decrement precision
		 */
		if (expcnt) {
			for (; ++p < endp; *t++ = *p, --prec);
		} else {
			*t++ = '0';
		}
		/*
		 * if precision required or alternate flag set, add in a
		 * decimal point.  If no digits yet, add in leading 0.
		 */
		if (prec || sharpflag) {
			dotrim = 1;
			*t++ = '.';
		} else {
			dotrim = 0;
		}
		/*
		 * if requires more precision and some fraction left
		 */
		while (prec && fract) {
			fract = modf (fract * 10, &tmp);
			*t++ = (int)tmp + '0';
			prec--;
		}
		if (fract) {
			startp = cvtround (fract, 0, startp, t - 1, '0', negp);
		}
		/*
		 * alternate format, adds 0's for precision, else trim 0's
		 */
		if (sharpflag) {
			for (; prec--; *t++ = '0');
		} else if (dotrim) {
			while (t > startp && *--t == '0');
			if (*t != '.') {
				++t;
			}
		}
	}
	return t - startp;
}
