/*
 * Floating point conversion for _doprnt, split from doprnt.c so that the
 * software double arithmetic it needs is linked only into programs that
 * print floats. See the note at __doprnt_cvt in doprnt.c.
 */

#include <string.h>

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


/*
 * %a and %A: the value as a hexadecimal fraction and a binary exponent,
 * C17 7.21.6.1p8. The 52 mantissa bits are read out of the double's
 * representation, so no arithmetic is done on the value and every digit
 * is exact; an omitted precision prints the digits the value has, with
 * trailing zeros trimmed, and a given precision rounds to nearest, ties
 * to even, with a carry into the leading digit renormalized as an
 * exponent step. A subnormal is normalized first so the leading digit is
 * always 1, which is one of the forms the standard allows. The sign is
 * reported through negp so a negative zero keeps its sign.
 */
static int
hexcvt (double number, int prec, int sharpflag, unsigned char *negp,
	unsigned char fmtch, unsigned char *out)
{
	unsigned long long bits, mant;
	unsigned char *p = out;
	int exp, e, ndig, i, shift;
	unsigned char lead;

	memcpy (&bits, &number, sizeof bits);
	if (bits >> 63)
		*negp = 1;
	exp = (int)((bits >> 52) & 0x7ff);
	mant = bits & ((1ULL << 52) - 1);

	if (exp == 0 && mant == 0) {
		lead = 0;
		e = 0;
	} else if (exp == 0) {
		lead = 1;
		e = -1022;
		while ((mant & (1ULL << 52)) == 0) {
			mant <<= 1;
			e--;
		}
		mant &= (1ULL << 52) - 1;
	} else {
		lead = 1;
		e = exp - 1023;
	}

	if (prec < 0) {
		/* every digit the value has, trailing zeros trimmed */
		ndig = 13;
		while (ndig > 0 && (mant & 0xf) == 0) {
			mant >>= 4;
			ndig--;
		}
	} else {
		ndig = prec > 13 ? 13 : prec;
		shift = 4 * (13 - ndig);
		if (shift > 0) {
			unsigned long long rem = mant & ((1ULL << shift) - 1);
			unsigned long long half = 1ULL << (shift - 1);

			mant >>= shift;
			/* ties go to even: the last kept digit, or the leading digit when none is kept */
			if (rem > half || (rem == half &&
			    ((ndig == 0 ? lead : mant) & 1)))
				mant++;
			if (ndig == 0 ? mant != 0 : (mant >> (4 * ndig)) != 0) {
				/* carry into the leading digit */
				mant = 0;
				if (++lead == 2) {
					lead = 1;
					e++;
				}
			}
		}
	}

	*p++ = '0';
	*p++ = fmtch == 'A' ? 'X' : 'x';
	*p++ = (unsigned char)('0' + lead);
	if (ndig > 0 || sharpflag)
		*p++ = '.';
	for (i = ndig - 1; i >= 0; i--) {
		unsigned char d = (unsigned char)((mant >> (4 * i)) & 0xf);

		*p++ = d > 9 ? (unsigned char)(d - 10 + (fmtch == 'A' ? 'A' : 'a'))
		    : (unsigned char)(d + '0');
	}
	*p++ = fmtch == 'A' ? 'P' : 'p';
	if (e < 0) {
		*p++ = '-';
		e = -e;
	} else
		*p++ = '+';
	{
		unsigned char ebuf[8], *t = ebuf + sizeof ebuf;

		do {
			*--t = (unsigned char)('0' + e % 10);
			e /= 10;
		} while (e);
		while (t < ebuf + sizeof ebuf)
			*p++ = *t++;
	}
	return (int)(p - out);
}

int
__doprnt_cvt (double number, int prec, int sharpflag, unsigned char *negp, unsigned char fmtch,
	unsigned char *startp, unsigned char *endp)
{
	unsigned char *p, *t;
	double fract;
	int dotrim, expcnt, gformat;
	double integer, tmp;

	if (fmtch == 'a' || fmtch == 'A')
		return hexcvt (number, prec, sharpflag, negp, fmtch, startp);

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
