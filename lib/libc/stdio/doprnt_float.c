/*
 * Floating point conversion for _doprnt, split from doprnt.c so that the
 * conversion code it needs is linked only into programs that print floats.
 * The decimal conversions read the double's bits and use integer
 * arithmetic alone, so they link no software floating point routine. See
 * the note at __doprnt_cvt in doprnt.c.
 */

#include <stddef.h>
#include <string.h>

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

/*
 * Exact decimal digits of a double. A double is mant * 2^e2 with mant
 * below 2^53, so its decimal expansion is finite and integer arithmetic
 * produces every digit: no floating point operation rounds, and a tie
 * between two candidate results is visible as a remainder of exactly one
 * half. The value is held as the integer V = mant * 2^(e2 + 16 * fl) in
 * 16-bit limbs, little end first, where fl is the fraction limb count, so
 * the binary point sits between limb fl - 1 and limb fl. The integer part
 * (limbs fl and up) converts by repeated short division by ten; each digit
 * of the fraction (limbs below fl) is the carry out of limb fl - 1 when
 * the fraction is multiplied by ten. No intermediate exceeds 32 bits.
 * Largest finite value: e2 = 971, 1024 bits, 64 limbs. Smallest
 * subnormal: e2 = -1074, a fraction of 1074 bits in 68 limbs.
 */
#define NLIMB	68

struct dstream {
	unsigned short v [NLIMB];
	unsigned char *idig;	/* integer digits, most significant first */
	int ni;			/* their count */
	int k;			/* next integer digit to hand out */
	int fl;			/* fraction limbs */
};

/*
 * Load the value and convert its integer part, storing the digits at the
 * end of the conversion buffer, below endp.
 */
static void
dinit (struct dstream *d, double number, unsigned char *endp)
{
	unsigned long long bits;
	unsigned long t, cur, rem;
	unsigned short m [4];
	unsigned char *p;
	int e, e2, sh, i, top;

	memcpy (&bits, &number, sizeof bits);
	e = (int) ((bits >> 52) & 0x7ff);
	m [0] = (unsigned short) bits;
	m [1] = (unsigned short) (bits >> 16);
	m [2] = (unsigned short) (bits >> 32);
	m [3] = (unsigned short) (((bits >> 48) & 0xf) | (e ? 0x10 : 0));
	e2 = (e ? e : 1) - 1075;

	memset (d->v, 0, sizeof d->v);
	d->fl = 0;
	sh = e2;
	if (e2 < 0) {
		d->fl = (15 - e2) >> 4;
		sh = 16 * d->fl + e2;
	}
	for (i = 0; i < 4; i++) {
		t = (unsigned long) m [i] << (sh & 15);
		d->v [(sh >> 4) + i] |= (unsigned short) t;
		d->v [(sh >> 4) + i + 1] |= (unsigned short) (t >> 16);
	}

	p = endp;
	for (top = NLIMB - 1; top >= d->fl && ! d->v [top]; --top)
		continue;
	while (top >= d->fl) {
		rem = 0;
		for (i = top; i >= d->fl; --i) {
			cur = (rem << 16) | d->v [i];
			d->v [i] = (unsigned short) (cur / 10);
			rem = cur % 10;
		}
		*--p = (unsigned char) ('0' + rem);
		while (top >= d->fl && ! d->v [top])
			--top;
	}
	d->idig = p;
	d->ni = (int) (endp - p);
	d->k = 0;
}

/*
 * The next digit of the integer part followed by the fraction, as a
 * character; zeros once the fraction is exhausted.
 */
static int
dnext (struct dstream *d)
{
	unsigned long carry, cur;
	int i;

	if (d->k < d->ni)
		return d->idig [d->k++];
	carry = 0;
	for (i = 0; i < d->fl; i++) {
		cur = (unsigned long) d->v [i] * 10 + carry;
		d->v [i] = (unsigned short) cur;
		carry = cur >> 16;
	}
	return (int) ('0' + carry);
}

/* Whether any digit after the last one handed out is not zero. */
static int
drest (const struct dstream *d)
{
	int i;

	for (i = d->k; i < d->ni; i++)
		if (d->idig [i] != '0')
			return 1;
	for (i = 0; i < d->fl; i++)
		if (d->v [i])
			return 1;
	return 0;
}

/*
 * Round the digits written so far to nearest, ties to even, the default
 * rounding direction of C17 F.3: the digit that follows decides, and when
 * it is a five the remainder and the parity of the last kept digit do.
 * last is the last kept digit; a carry out of the first digit returns 1.
 */
static int
dround (struct dstream *d, unsigned char *first, unsigned char *last)
{
	int next = dnext (d);

	if (next < '5' || (next == '5' && ! drest (d) && ! ((*last - '0') & 1)))
		return 0;
	for (; last >= first; --last) {
		if (*last == '.')
			continue;
		if (*last != '9') {
			++*last;
			return 0;
		}
		*last = '0';
	}
	return 1;
}

/*
 * The f, e and E conversions, correctly rounded for any precision the
 * buffer holds. The result starts at startp + 1, or at startp after an f
 * conversion rounds up into a new leading digit, and the return value is
 * its length. A precision that exceeds the buffer, about 320 digits after
 * the point under f and 320 significant digits under e, is cut to what fits
 * and the difference is returned in *extrap for the caller to print as
 * zeros after the digits (f) or before the exponent (e). The digits kept
 * are those of the exact value rounded at the cut, far past the
 * DECIMAL_DIG significant digits that C17 7.21.6.1p13 requires to be
 * correctly rounded, except for an f conversion of a value under 1e-307.
 */
static int
decimal_cvt (double number, int prec, int sharpflag, unsigned char fmtch,
	unsigned char *startp, unsigned char *endp, int *extrap)
{
	struct dstream d;
	unsigned char *first, *t;
	int ex, room, ndig;
	int c;

	dinit (&d, number, endp);
	first = t = startp + 1;
	if (fmtch == 'f' || fmtch == 'F') {
		/* ni digits, the point and prec digits, then a terminator */
		ndig = d.ni ? d.ni : 1;
		room = (int) (endp - first) - ndig - 1;
		*extrap = prec > room ? prec - room : 0;
		if (prec > room)
			prec = room;
		if (d.ni == 0)
			*t++ = '0';
		for (c = d.ni; c > 0; --c)
			*t++ = (unsigned char) dnext (&d);
		if (prec || sharpflag)
			*t++ = '.';
		while (prec-- > 0)
			*t++ = (unsigned char) dnext (&d);
		if (dround (&d, first, t[-1] == '.' ? t - 2 : t - 1))
			*--first = '1';
	} else {
		/* d, the point, prec digits, and e, a sign, three digits */
		room = (int) (endp - first) - 7;
		*extrap = prec > room ? prec - room : 0;
		if (prec > room)
			prec = room;
		ex = 0;
		if (d.ni)
			ex = d.ni - 1;
		if (d.ni == 0 && drest (&d)) {
			for (ex = -1; (c = dnext (&d)) == '0'; --ex)
				continue;
			*t++ = (unsigned char) c;
		} else
			*t++ = (unsigned char) dnext (&d);
		if (prec || sharpflag)
			*t++ = '.';
		while (prec-- > 0)
			*t++ = (unsigned char) dnext (&d);
		if (dround (&d, first, t[-1] == '.' ? t - 2 : t - 1)) {
			*first = '1';
			++ex;
		}
		t = exponent (t, ex, fmtch);
	}
	return (int) (t - first);
}

/*
 * %g and %G, C17 7.21.6.1p8. The precision P (zero counts as one) is the
 * number of significant digits. The value is converted in style e with
 * P - 1 fraction digits; with X the exponent of that result, rounding
 * carry included, style f with precision P - 1 - X is used when
 * -4 <= X < P and style e otherwise. Both styles show the digits of the
 * one style e conversion, so the choice of style cannot change a digit
 * and the rounding of that conversion is the rounding of this one.
 * Unless the # flag is given, trailing zeros of the fraction go, and the
 * point goes with them when no fraction digit is left.
 *
 * The result is rearranged in place: the digits move together, then the
 * point, the leading zeros or the exponent go around them. The zeros that
 * # requires beyond the digits decimal_cvt stored are not stored: they
 * are returned in *extrap for the caller to print after the string
 * (style f) or before the exponent (style e). Without #, they are
 * trailing zeros of the fraction and *extrap is zero.
 */
static int
general_cvt (double number, int prec, int sharpflag, unsigned char fmtch,
	unsigned char *startp, unsigned char *endp, int *extrap)
{
	unsigned char *s, *p, *t;
	int nd, ex, exneg, i, len, lost;

	if (prec < 1)
		prec = 1;
	len = decimal_cvt (number, prec - 1, 0, fmtch - 2, startp, endp, &lost);

	/* d[.ddd]e+XX: gather the digits, then read the exponent */
	s = startp + 1;
	nd = 1;
	for (p = s + 1; *p != 'e' && *p != 'E'; ++p)
		if (*p != '.')
			s [nd++] = *p;
	exneg = *++p == '-';
	for (ex = 0, ++p; p < s + len; ++p)
		ex = ex * 10 + (*p - '0');
	if (exneg)
		ex = -ex;

	/* trailing zeros of the fraction, unless # keeps them */
	if (! sharpflag)
		while (nd > 1 && s [nd - 1] == '0')
			--nd;

	*extrap = 0;
	if (ex < -4 || ex >= prec) {
		t = s + nd;
		if (nd > 1 || sharpflag) {
			memmove (s + 2, s + 1, (size_t) (nd - 1));
			s [1] = '.';
			++t;
		}
		if (sharpflag)
			*extrap = prec - nd;
		t = exponent (t, ex, fmtch - 2);
	} else if (ex < 0) {
		memmove (s + 1 - ex, s, (size_t) nd);
		s [0] = '0';
		s [1] = '.';
		for (i = 2; i < 1 - ex; ++i)
			s [i] = '0';
		t = s + 1 - ex + nd;
		if (sharpflag)
			*extrap = prec - nd;
	} else {
		/* ex + 1 integer digits, zero-filled past the digits generated */
		for (i = nd; i <= ex; ++i)
			s [i] = '0';
		if (nd > ex + 1) {
			memmove (s + ex + 2, s + ex + 1, (size_t) (nd - ex - 1));
			s [ex + 1] = '.';
			t = s + nd + 1;
		} else {
			t = s + ex + 1;
			if (sharpflag)
				*t++ = '.';
		}
		if (sharpflag)
			*extrap = prec - (nd > ex + 1 ? nd : ex + 1);
	}
	return (int) (t - s);
}

int
__doprnt_cvt (double number, int prec, int sharpflag, unsigned char *negp,
	unsigned char fmtch, unsigned char *startp, unsigned char *endp,
	int *extrap)
{
	if (fmtch == 'a' || fmtch == 'A')
		return hexcvt (number, prec, sharpflag, negp, fmtch, startp);
	if (fmtch == 'g' || fmtch == 'G')
		return general_cvt (number, prec, sharpflag, fmtch, startp,
			endp, extrap);
	return decimal_cvt (number, prec, sharpflag, fmtch, startp, endp,
		extrap);
}
