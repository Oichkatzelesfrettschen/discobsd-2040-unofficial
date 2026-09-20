/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Floating point conversion for _doscan, split from doscan.c so that the
 * software double arithmetic strtod() needs is linked only into programs
 * that scan floats. See the note at __doscan_cvt in doscan.c: on rp2040
 * share/mk/sys.mk forces this member only for a program declaring
 * SCANF_FLOAT=yes, and doscan.c makes the directive a matching failure
 * while the weak reference stays unresolved.
 *
 * The subject sequence is not staged verbatim. Significant digits go into
 * a fixed buffer while a decimal exponent absorbs everything outside it,
 * so an input of any length converts to the nearest double without the
 * buffer growing with the input: an integral digit past the buffer raises
 * the exponent, a fractional digit inside it lowers the exponent, and a
 * fractional digit past it changes neither. Staging the whole sequence
 * instead is what makes a scanner's buffer a function of its input.
 *
 * strtod() in lib/libc/stdlib accepts a decimal significand and exponent
 * and nothing else, so the infinity, nan and hexadecimal forms named in
 * C17 7.21.6.2p12 are not matched here and end the directive.
 */

#include <stdio.h>
#include <stdlib.h>
#include <float.h>

/*
 * Digits handed to strtod(). Seventeen decimal digits distinguish every
 * double, so nineteen carries the rounding digits as well and no further
 * digit changes the result.
 */
#define MANT_DIGITS	19

/* The emitted text: sign, digits, 'e', its sign, three digits, NUL. */
#define TEXT_BYTES	(1 + MANT_DIGITS + 1 + 1 + 3 + 1)

_Static_assert (MANT_DIGITS >= DBL_DIG + 4,
    "staged digits must cover the round-trip precision of a double");
_Static_assert (TEXT_BYTES >= 1 + MANT_DIGITS + 2 + 3 + 1,
    "emitted text must hold the sign, digits and a signed three-digit exponent");

/*
 * The target strtod clamps the emitted magnitude again at 511, where any
 * nonzero staged significand underflows or overflows. This wider clamp keeps
 * the emitted exponent three digits wide without changing that verdict.
 */
#define EXP_LIMIT	999

_Static_assert (EXP_LIMIT > DBL_MAX_10_EXP - DBL_MIN_10_EXP,
    "the clamp must lie outside the decimal range of a double");

/* The verdicts and the modifier bit _doscan defines. */
#define CONV_INPUT	(-1)
#define CONV_FAIL	0
#define CONV_OK		1
#define F_LONG		0x02

/*
 * Stream position, held as doscan.c holds it: ungetc() must not run after
 * getc() has returned EOF, because _filbuf() leaves a _IOSTRG stream's
 * _cnt negative from then on.
 */
struct fstate {
	FILE *fp;
	int *nread;
	int eof;
};

/*
 * Read the next character of the field, or EOF once the width is spent or
 * input ended. Only input ending sets eof, which separates a complete
 * conversion cut short by its width from an input failure.
 */
static int
f_get (struct fstate *s, int *width)
{
	int c;

	if (*width <= 0 || s->eof)
		return EOF;
	c = getc (s->fp);
	if (c == EOF) {
		s->eof = 1;
		return EOF;
	}
	(*s->nread)++;
	(*width)--;
	return c;
}

static void
f_unget (struct fstate *s, int c, int *width)
{
	if (c != EOF && ungetc (c, s->fp) != EOF) {
		(*s->nread)--;
		(*width)++;
	}
}

static int
f_isspace (int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
	    c == '\f' || c == '\r';
}

int
__doscan_cvt (FILE *fp, int width, int flags, void *dst, int *nread)
{
	struct fstate s;
	char digits[MANT_DIGITS];
	char text[TEXT_BYTES];
	char expbuf[4];
	double value;
	long exp10, expval, e;
	int c, i, t, nd, nsig, negate, seen, expneg, expdigits;

	s.fp = fp;
	s.nread = nread;
	s.eof = 0;

	/* Leading white space is skipped without charging the field width. */
	for (;;) {
		c = getc (fp);
		if (c == EOF) {
			s.eof = 1;
			return CONV_INPUT;
		}
		(*nread)++;
		if (! f_isspace (c)) {
			if (ungetc (c, fp) != EOF)
				(*nread)--;
			break;
		}
	}

	nsig = 0;
	exp10 = 0;
	negate = 0;
	seen = 0;

	c = f_get (&s, &width);
	if (c == EOF)
		return s.eof ? CONV_INPUT : CONV_FAIL;
	if (c == '+' || c == '-') {
		negate = (c == '-');
		c = f_get (&s, &width);
		if (c == EOF)
			return CONV_FAIL;
	}

	/*
	 * Integral digits. A digit the buffer cannot hold still scales the
	 * value, so the exponent rises in its place.
	 */
	while (c >= '0' && c <= '9') {
		seen = 1;
		if (nsig == 0 && c == '0')
			;
		else if (nsig < MANT_DIGITS)
			digits[nsig++] = (char) c;
		else
			exp10++;
		c = f_get (&s, &width);
	}

	if (c == '.') {
		c = f_get (&s, &width);
		/*
		 * Fractional digits. A zero before the first significant
		 * digit shifts the point, a staged digit shifts it too,
		 * and a digit past the buffer does neither.
		 */
		while (c >= '0' && c <= '9') {
			seen = 1;
			if (nsig == 0 && c == '0') {
				exp10--;
			} else if (nsig < MANT_DIGITS) {
				digits[nsig++] = (char) c;
				exp10--;
			}
			c = f_get (&s, &width);
		}
	}

	if (! seen) {
		f_unget (&s, c, &width);
		return CONV_FAIL;
	}

	if (c == 'e' || c == 'E') {
		expneg = 0;
		expdigits = 0;
		expval = 0;
		c = f_get (&s, &width);
		if (c == '+' || c == '-') {
			expneg = (c == '-');
			c = f_get (&s, &width);
		}
		while (c >= '0' && c <= '9') {
			expdigits++;
			if (expval < EXP_LIMIT)
				expval = expval * 10 + (c - '0');
			c = f_get (&s, &width);
		}
		/*
		 * An exponent marker with no digits is outside the subject
		 * sequence, but a stream guarantees only one pushback, so
		 * the marker stays consumed while the significand still
		 * converts.
		 */
		if (expdigits > 0)
			exp10 += expneg ? -expval : expval;
	}

	f_unget (&s, c, &width);

	if (nsig == 0) {
		/* Every digit was a zero, so the value is a signed zero. */
		digits[nsig++] = '0';
		exp10 = 0;
	}

	if (exp10 > EXP_LIMIT)
		exp10 = EXP_LIMIT;
	else if (exp10 < -EXP_LIMIT)
		exp10 = -EXP_LIMIT;

	/* Emit "[-]<digits>e[-]<exponent>" for strtod(). */
	t = 0;
	if (negate)
		text[t++] = '-';
	for (i = 0; i < nsig; i++)
		text[t++] = digits[i];
	text[t++] = 'e';
	if (exp10 < 0)
		text[t++] = '-';
	e = exp10 < 0 ? -exp10 : exp10;
	nd = 0;
	do {
		expbuf[nd++] = (char) ('0' + (int) (e % 10));
		e /= 10;
	} while (e != 0 && nd < (int) sizeof expbuf);
	while (nd > 0)
		text[t++] = expbuf[--nd];
	text[t] = '\0';

	value = strtod (text, (char **) NULL);

	if (dst == NULL)
		return CONV_OK;
	if (flags & F_LONG)
		*(double *) dst = value;
	else
		*(float *) dst = (float) value;
	return CONV_OK;
}
