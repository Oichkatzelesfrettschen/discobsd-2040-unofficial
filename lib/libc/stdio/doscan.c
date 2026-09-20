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
 * Formatted input conversion behind scanf, fscanf, sscanf and the curses
 * scanw, implementing the directives of C17 7.21.6.2.
 *
 * Integer conversion accumulates into an unsigned long as each digit
 * arrives, so no directive stages input text in an automatic buffer. The
 * V7 scanner this replaces staged digits in a 64-byte numbuf while an
 * absent field width defaulted to 30000, so an unwidthed conversion
 * overran the frame. sysctl.c reaches it from a shipped program with an
 * argument the caller writes: kern.hostid is CTLTYPE_LONG in sysctl.h,
 * and its `sysctl -w` path converts the value string through %ld.
 * Accumulation removes the staging buffer rather than enlarging it, and
 * saturating at the destination limit keeps an over-long digit run
 * defined where C17 7.21.6.2p10 leaves the unrepresentable result
 * undefined.
 *
 * The scanset of %[ lives in a 256-bit automatic bitmap. The V7 scanner
 * rewrote a file-scope 256-byte table per directive, which charged that
 * table to every process linking scanf and left one scanset shared
 * across calls; the bitmap costs 32 bytes of frame while a conversion
 * runs and nothing between calls.
 *
 * Floating conversion reaches doscan_float.c through a weak
 * __doscan_cvt, the arrangement doprnt.c uses for __doprnt_cvt:
 * share/mk/sys.mk links that member on rp2040 only for a program
 * declaring SCANF_FLOAT=yes, because the software double arithmetic it
 * carries is large against a 144 KB process window. An unresolved weak
 * reference makes the directive a matching failure, so the scan stops
 * instead of leaving the destination indeterminate while counting the
 * directive as converted, which is what the V7 scanner did whenever its
 * HAVE_FLOAT conditional went undefined.
 *
 * Conversions are 32-bit: this target's long, size_t, ptrdiff_t,
 * intmax_t and pointers are four bytes, so z, j and t select the
 * accumulator that l already selects. The ll and L modifiers select no
 * wider accumulator, matching _doprnt, whose value model is a single l
 * flag over unsigned long.
 */

#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

/* Conversion classes. */
#define CT_INT		0	/* d, i, o, u, x, X, p */
#define CT_CHAR		1	/* c */
#define CT_STRING	2	/* s */
#define CT_CCL		3	/* [ */
#define CT_FLOAT	4	/* a, e, f, g and their uppercase spellings */

/* Directive state carried from the modifiers to the store. */
#define F_SUPPRESS	0x01	/* * : convert, assign nothing */
#define F_LONG		0x02	/* l, ll, L, q, z, j, t */
#define F_SHORT		0x04	/* h */
#define F_CHAR		0x08	/* hh */
#define F_SIGNED	0x10	/* d, i : saturate against the signed limits */
#define F_POINTER	0x20	/* p */

/*
 * A scanset covers the 256 byte values in 32 bytes. getc() yields 0..255
 * or EOF, and every test runs on a value already proven not EOF, so the
 * index stays inside the bitmap.
 */
#define CCL_BYTES	32
#define CCL_SET(t, c)	((t)[(unsigned)(c) >> 3] |= \
			    (unsigned char)(1u << ((c) & 7)))
#define CCL_TEST(t, c)	((t)[(unsigned)(c) >> 3] & (1u << ((c) & 7)))

/*
 * An absent field width admits input until the conversion itself rejects
 * a character. INT_MAX stands in for that so one countdown serves both
 * the widthed and the unwidthed directive.
 */
#define WIDTH_NONE	INT_MAX

/*
 * Every conversion reports one of three verdicts. Assignment is not one
 * of them: a suppressed directive that converts reports CONV_OK, and
 * _doscan decides whether the directive reaches the returned count.
 */
#define CONV_INPUT	(-1)	/* input ended before the subject sequence */
#define CONV_FAIL	0	/* the input does not match the directive */
#define CONV_OK		1	/* the directive converted */

/*
 * Floating conversion, resolved only when doscan_float.o is linked. It
 * consumes the subject sequence from fp under width, adds the characters
 * it consumes to *nread, and stores a float, or a double when flags
 * carries F_LONG, unless dst is NULL. Its verdicts are those above.
 */
extern int __doscan_cvt (FILE *fp, int width, int flags, void *dst,
	int *nread) __attribute__((weak));

/*
 * The scanner's position. nread counts the characters consumed so %n can
 * report them, and eof records that getc() reached end of input, because
 * ungetc() must not run afterwards: _filbuf() leaves a _IOSTRG stream's
 * _cnt negative once it has returned EOF, so a byte pushed back there is
 * never read again.
 */
struct scanstate {
	FILE *fp;
	int nread;
	int eof;
};

static int
s_get (struct scanstate *s)
{
	int c;

	if (s->eof)
		return EOF;
	c = getc (s->fp);
	if (c == EOF) {
		s->eof = 1;
		return EOF;
	}
	s->nread++;
	return c;
}

/*
 * Read the next character of a field, or EOF once the width is spent or
 * input ended. The two are distinguished by s->eof, which only the
 * second sets; a width spent mid-number is a complete conversion, while
 * input ending before any digit is an input failure.
 */
static int
s_getw (struct scanstate *s, int *width)
{
	int c;

	if (*width <= 0)
		return EOF;
	c = s_get (s);
	if (c != EOF)
		(*width)--;
	return c;
}

/*
 * Return one character to the stream. ungetc() refuses a pushback at
 * offset zero of a string stream, where _ptr still equals _base; every
 * caller here unreads a character it has read, so _ptr has advanced and
 * the pushback lands.
 */
static void
s_unget (struct scanstate *s, int c)
{
	if (c != EOF && ungetc (c, s->fp) != EOF)
		s->nread--;
}

static int
s_isspace (int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
	    c == '\f' || c == '\r';
}

/*
 * Consume white space. A directive that skips leading space may find
 * none, so this reports nothing and leaves end of input for the
 * directive to discover.
 */
static void
s_skipspace (struct scanstate *s)
{
	int c;

	while ((c = s_get (s)) != EOF) {
		if (! s_isspace (c)) {
			s_unget (s, c);
			return;
		}
	}
}

/*
 * Value of c as a digit in the given base, or -1 when c is not one.
 */
static int
digitval (int c, int base)
{
	int v;

	if (c >= '0' && c <= '9')
		v = c - '0';
	else if (c >= 'a' && c <= 'z')
		v = c - 'a' + 10;
	else if (c >= 'A' && c <= 'Z')
		v = c - 'A' + 10;
	else
		return -1;
	return v < base ? v : -1;
}

/*
 * Build the scanset of a %[ directive and return the character after its
 * closing bracket. A ] in the first position is a member rather than the
 * terminator, and a - is a range only between two members, both as C17
 * 7.21.6.2p6 requires. An unterminated scanset returns NULL, which the
 * caller reports as a malformed directive.
 */
static const char *
scanset (const char *fmt, unsigned char *tab)
{
	int negate, c, last;
	int i;

	for (i = 0; i < CCL_BYTES; i++)
		tab[i] = 0;

	negate = 0;
	if (*fmt == '^') {
		negate = 1;
		fmt++;
	}
	if (*fmt == ']') {
		CCL_SET (tab, ']');
		fmt++;
	}

	last = -1;
	while ((c = (unsigned char) *fmt) != ']') {
		if (c == '\0')
			return NULL;
		fmt++;
		if (c == '-' && last >= 0 && *fmt != ']' &&
		    (unsigned char) *fmt >= last) {
			for (c = last; c <= (unsigned char) *fmt; c++)
				CCL_SET (tab, c);
			last = (unsigned char) *fmt;
			fmt++;
			continue;
		}
		CCL_SET (tab, c);
		last = c;
	}
	fmt++;

	if (negate) {
		for (i = 0; i < CCL_BYTES; i++)
			tab[i] = (unsigned char) ~tab[i];
	}
	return fmt;
}

/*
 * Return the largest magnitude the selected destination can represent.
 * The negative signed magnitude is one greater than its positive maximum;
 * expressing it as unsigned arithmetic avoids negating LONG_MIN.
 */
static unsigned long
intlimit (int flags, int negate)
{
	if (flags & F_POINTER)
		return ULONG_MAX;
	if (! (flags & F_SIGNED)) {
		if (flags & F_CHAR)
			return UCHAR_MAX;
		if (flags & F_SHORT)
			return USHRT_MAX;
		if (flags & F_LONG)
			return ULONG_MAX;
		return UINT_MAX;
	}
	if (flags & F_CHAR)
		return negate ? (unsigned long) SCHAR_MAX + 1UL : SCHAR_MAX;
	if (flags & F_SHORT)
		return negate ? (unsigned long) SHRT_MAX + 1UL : SHRT_MAX;
	if (flags & F_LONG)
		return negate ? (unsigned long) LONG_MAX + 1UL : LONG_MAX;
	return negate ? (unsigned long) INT_MAX + 1UL : INT_MAX;
}

/*
 * Store an accumulated value through the destination the modifiers
 * select. The accumulator is unsigned long throughout, so a negated
 * value reaches a signed destination with the bit pattern the widening
 * produced.
 */
static void
storeint (va_list *ap, int flags, unsigned long acc)
{
	if (flags & F_POINTER) {
		*va_arg (*ap, void **) = (void *) acc;
		return;
	}
	if (flags & F_CHAR) {
		if (flags & F_SIGNED)
			*va_arg (*ap, signed char *) = (signed char) acc;
		else
			*va_arg (*ap, unsigned char *) = (unsigned char) acc;
		return;
	}
	if (flags & F_SHORT) {
		if (flags & F_SIGNED)
			*va_arg (*ap, short *) = (short) acc;
		else
			*va_arg (*ap, unsigned short *) = (unsigned short) acc;
		return;
	}
	if (flags & F_LONG) {
		if (flags & F_SIGNED)
			*va_arg (*ap, long *) = (long) acc;
		else
			*va_arg (*ap, unsigned long *) = acc;
		return;
	}
	if (flags & F_SIGNED)
		*va_arg (*ap, int *) = (int) acc;
	else
		*va_arg (*ap, unsigned int *) = (unsigned int) acc;
}

/*
 * Convert one integer directive.
 *
 * The subject sequence is the longest input prefix that could begin a
 * number in the base, which costs one character of lookahead. A stream
 * guarantees a single pushback, so an input of 0x with no hexadecimal
 * digit after it converts the leading 0 and leaves the stream positioned
 * after the x rather than before it; that is the one place where the
 * consumed extent departs from the longest valid subject sequence.
 *
 * A base of 0 selects 16 after 0x, 8 after 0, and 10 otherwise, which is
 * what %i means.
 */
static int
convint (struct scanstate *s, va_list *ap, int width, int flags, int base)
{
	unsigned long acc, limit;
	int c, v, negate, ndigits, saturated;

	s_skipspace (s);

	acc = 0;
	negate = 0;
	ndigits = 0;
	saturated = 0;

	c = s_getw (s, &width);
	if (c == EOF)
		return s->eof ? CONV_INPUT : CONV_FAIL;

	if (c == '+' || c == '-') {
		negate = (c == '-');
		c = s_getw (s, &width);
		if (c == EOF)
			return CONV_FAIL;
	}
	limit = intlimit (flags, negate);

	if ((base == 0 || base == 16) && c == '0') {
		/*
		 * The leading zero is a digit in its own right, so a
		 * number that ends there still converts.
		 */
		ndigits = 1;
		base = (base == 0) ? 8 : 16;
		c = s_getw (s, &width);
		if (c == 'x' || c == 'X') {
			int d = s_getw (s, &width);

			if (d != EOF && digitval (d, 16) >= 0) {
				base = 16;
				ndigits = 0;
				c = d;
			} else {
				s_unget (s, d);
				goto store;
			}
		}
	} else if (base == 0) {
		base = 10;
	}

	while (c != EOF) {
		v = digitval (c, base);
		if (v < 0) {
			s_unget (s, c);
			break;
		}
		if (acc > (limit - (unsigned long) v) /
		    (unsigned long) base)
			saturated = 1;
		else
			acc = acc * (unsigned long) base + (unsigned long) v;
		ndigits++;
		c = s_getw (s, &width);
	}

	if (ndigits == 0)
		return CONV_FAIL;

store:
	if (saturated) {
		acc = limit;
		if (negate && (flags & F_SIGNED))
			acc = 0UL - acc;
	} else if (negate) {
		acc = 0UL - acc;
	}
	if (! (flags & F_SUPPRESS))
		storeint (ap, flags, acc);
	return CONV_OK;
}

/*
 * Convert %c: width characters, one by default, with no leading white
 * space skipped and no terminator appended.
 */
static int
convchar (struct scanstate *s, va_list *ap, int width, int flags)
{
	char *dst;
	int c, n;

	if (width == WIDTH_NONE)
		width = 1;
	dst = (flags & F_SUPPRESS) ? NULL : va_arg (*ap, char *);

	for (n = 0; n < width; n++) {
		c = s_get (s);
		if (c == EOF)
			return n == 0 ? CONV_INPUT : CONV_OK;
		if (dst)
			dst[n] = (char) c;
	}
	return CONV_OK;
}

/*
 * Convert %s when tab is NULL, otherwise %[. A %s skips leading white
 * space and stops at the next white space; a %[ skips nothing and stops
 * at the first character outside its scanset. Both terminate the
 * destination, and an empty run is a matching failure.
 */
static int
convstr (struct scanstate *s, va_list *ap, int width, int flags,
	const unsigned char *tab)
{
	char *dst;
	int c, n;

	if (tab == NULL)
		s_skipspace (s);

	dst = (flags & F_SUPPRESS) ? NULL : va_arg (*ap, char *);
	n = 0;
	while (n < width) {
		c = s_get (s);
		if (c == EOF)
			break;
		if (tab == NULL ? s_isspace (c) : ! CCL_TEST (tab, c)) {
			s_unget (s, c);
			break;
		}
		if (dst)
			dst[n] = (char) c;
		n++;
	}
	if (n == 0)
		return s->eof ? CONV_INPUT : CONV_FAIL;
	if (dst)
		dst[n] = '\0';
	return CONV_OK;
}

int
_doscan (FILE *iop, const char *fmt, va_list argp)
{
	struct scanstate s;
	unsigned char ccltab[CCL_BYTES];
	va_list ap;
	const char *next;
	int nassigned, c, got, width, flags, base, class, r;

	s.fp = iop;
	s.nread = 0;
	s.eof = 0;
	nassigned = 0;
	va_copy (ap, argp);

	for (;;) {
		c = (unsigned char) *fmt++;
		if (c == '\0')
			break;

		if (s_isspace (c)) {
			s_skipspace (&s);
			continue;
		}

		if (c != '%') {
			got = s_get (&s);
			if (got == EOF)
				goto input_failure;
			if (got != c) {
				s_unget (&s, got);
				goto matching_failure;
			}
			continue;
		}

		if (*fmt == '%') {
			fmt++;
			got = s_get (&s);
			if (got == EOF)
				goto input_failure;
			if (got != '%') {
				s_unget (&s, got);
				goto matching_failure;
			}
			continue;
		}

		flags = 0;
		if (*fmt == '*') {
			flags |= F_SUPPRESS;
			fmt++;
		}

		width = 0;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		if (width == 0)
			width = WIDTH_NONE;

		switch (*fmt) {
		case 'h':
			fmt++;
			if (*fmt == 'h') {
				flags |= F_CHAR;
				fmt++;
			} else {
				flags |= F_SHORT;
			}
			break;
		case 'l':
			fmt++;
			if (*fmt == 'l')
				fmt++;
			flags |= F_LONG;
			break;
		case 'L':
		case 'q':
		case 'z':
		case 'j':
		case 't':
			fmt++;
			flags |= F_LONG;
			break;
		default:
			break;
		}

		base = 10;
		class = CT_INT;
		c = (unsigned char) *fmt++;
		switch (c) {
		case 'D':
			flags |= F_LONG | F_SIGNED;
			break;
		case 'd':
			flags |= F_SIGNED;
			break;
		case 'i':
			flags |= F_SIGNED;
			base = 0;
			break;
		case 'o':
			base = 8;
			break;
		case 'O':
			base = 8;
			flags |= F_LONG;
			break;
		case 'u':
			break;
		case 'x':
		case 'X':
			base = 16;
			break;
		case 'p':
			base = 16;
			flags |= F_POINTER;
			break;
		case 'c':
			class = CT_CHAR;
			break;
		case 's':
			class = CT_STRING;
			break;
		case '[':
			class = CT_CCL;
			next = scanset (fmt, ccltab);
			if (next == NULL)
				goto malformed;
			fmt = next;
			break;
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
			class = CT_FLOAT;
			break;
		case 'n':
			/*
			 * C17 7.21.6.2p12: %n assigns the count of
			 * characters read so far and is not a conversion,
			 * so it neither reaches the returned count nor
			 * stores anything when suppressed.
			 */
			flags |= F_SIGNED;
			if (! (flags & F_SUPPRESS))
				storeint (&ap, flags,
				    (unsigned long) s.nread);
			continue;
		default:
			goto malformed;
		}

		switch (class) {
		case CT_INT:
			r = convint (&s, &ap, width, flags, base);
			break;
		case CT_CHAR:
			r = convchar (&s, &ap, width, flags);
			break;
		case CT_STRING:
			r = convstr (&s, &ap, width, flags, NULL);
			break;
		case CT_CCL:
			r = convstr (&s, &ap, width, flags, ccltab);
			break;
		default:
			if (__doscan_cvt == NULL)
				goto matching_failure;
			r = __doscan_cvt (s.fp, width, flags,
			    (flags & F_SUPPRESS) ? NULL :
			    va_arg (ap, void *), &s.nread);
			break;
		}

		if (r == CONV_INPUT)
			goto input_failure;
		if (r == CONV_FAIL)
			goto matching_failure;
		if (! (flags & F_SUPPRESS))
			nassigned++;
	}

	va_end (ap);
	return nassigned;

input_failure:
	/*
	 * C17 7.21.6.2p16: EOF is returned when input failure occurs
	 * before any conversion; a conversion that already succeeded
	 * makes the count the result instead.
	 */
	va_end (ap);
	return nassigned > 0 ? nassigned : EOF;

matching_failure:
	va_end (ap);
	return nassigned;

malformed:
	va_end (ap);
	return nassigned > 0 ? nassigned : EOF;
}
