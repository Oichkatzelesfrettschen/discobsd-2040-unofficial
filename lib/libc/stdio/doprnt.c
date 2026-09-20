/*
 * Scaled down version of printf(3).
 * Based on FreeBSD sources, heavily rewritten.
 *
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * The conversions and length modifiers of C17 7.21.6.1, less the wide
 * forms (%lc, %ls) and the ' grouping flag, which is POSIX rather than C.
 * One extension beyond the standard: %D prints a long decimal, the same
 * conversion %ld names, so a caller passing a long needs no length
 * modifier; usr.bin/find and usr.bin/grep print block counts with it.
 *
 * Values convert through an unsigned long, the natural word of this
 * target; z and t fetch size_t and ptrdiff_t by their own names so the
 * conversion is right wherever those differ from long. A value that a
 * wider modifier names and that does not fit the word goes through
 * __doprnt_ll in doprnt_llong.c, linked only for a program declaring
 * PRINTF_LLONG=yes, the arrangement the float conversion uses: the member
 * costs about 200 bytes of text and the shipped programs print no 64-bit
 * value. Without it such a value prints as a question mark.
 *
 * The 4.4BSD kernel conversions %b (register bit decode), %r (saturated
 * counter) and %z (signed hexadecimal) live in the kernel's own printf,
 * sys/kern/subr_prf.c, and are absent here: no userland caller names them,
 * and each one cost text in all 27 shipped programs that link _doprnt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>
#include <float.h>
#include <math.h>

/*
 * Number conversion buffer: sign, the integral digits of the largest
 * double, the point, the fractional digits, and the terminator. An
 * unsigned long long in octal needs 22 digits, a hexadecimal double under
 * %a needs 25 characters, both inside this.
 */
#define MAXNBUF	\
	(1/*sign*/ + DBL_MAX_10_EXP+1/*max integral digits*/ + \
	1/*.*/ + DBL_DIG+1/*max fractional digits*/ + 1/*NUL*/)

/* Length modifier, from the format. The wide types decay to these on this target. */
#define SZ_INT		0	/* none */
#define SZ_CHAR		1	/* hh */
#define SZ_SHORT	2	/* h */
#define SZ_LONG		3	/* l */
#define SZ_LLONG	4	/* ll, j, q: long long and intmax_t */
#define SZ_SIZE		5	/* z: size_t and ssize_t */
#define SZ_PTRDIFF	6	/* t: ptrdiff_t */

static unsigned char *ksprintn (unsigned char *buf, unsigned long v,
	unsigned char base, int width, int *lp);
static unsigned char mkhex (unsigned char ch);

/*
 * Floating point conversion lives in doprnt_float.c and is linked only on
 * request, because it carries the software double arithmetic, about 10
 * kbytes on Thumb-1, and few programs print a float. A program that does
 * sets PRINTF_FLOAT=yes in its Makefile; without it the conversion prints
 * a question mark in place of the number.
 */
extern int __doprnt_cvt (double number, int prec, int sharpflag,
	unsigned char *negp, unsigned char fmtch, unsigned char *startp,
	unsigned char *endp) __attribute__((weak));

/*
 * The 64-bit conversion, doprnt_llong.c, linked on PRINTF_LLONG=yes. It
 * fetches the long long itself, because fetching, negating and testing a
 * 64-bit value inline is most of what the wide path costs on Thumb-1, and
 * hands back the digits in nbuf the way ksprintn does.
 */
extern unsigned char *__doprnt_ll (va_list *app, int issigned,
	unsigned char base, int width, unsigned char *nbuf,
	int *lenp, unsigned char *negp, unsigned char *nonzerop)
	__attribute__((weak));

int
_doprnt (char const *fmt, va_list ap, FILE *stream)
{
#define PUTC(c) { putc (c, stream); ++retval; }
	unsigned char nbuf [MAXNBUF], padding;
	const unsigned char *s;
	unsigned char c, base, ladjust, sharpflag, neg, dot, sz, nonzero, hexfmt, zeroflag;
	int n, width, dwidth, retval, uppercase, extrazeros, sign, issigned, size;
	unsigned long ul;

	if (! stream)
		return 0;
	if (! fmt)
		fmt = "(null)\n";

	retval = 0;
	for (;;) {
		while ((c = *fmt++) != '%') {
			if (! c)
				return retval;
			PUTC (c);
		}
		padding = ' ';
		width = 0; extrazeros = 0;
		sz = SZ_INT; ladjust = 0; sharpflag = 0; neg = 0; hexfmt = 0; zeroflag = 0;
		sign = 0; dot = 0; uppercase = 0; dwidth = -1;
		ul = 0;
reswitch:	switch (c = *fmt++) {
		case '.':
			dot = 1;
			padding = ' ';
			dwidth = 0;
			goto reswitch;

		case '#':
			sharpflag = 1;
			goto reswitch;

		case '+':
			sign = -1;
			goto reswitch;

		case ' ':
			/* A blank for a positive value where + would put a sign. */
			if (sign == 0)
				sign = -2;
			goto reswitch;

		case '-':
			ladjust = 1;
			goto reswitch;

		case '%':
			PUTC (c);
			break;

		case '*':
			if (! dot) {
				width = va_arg (ap, int);
				if (width < 0) {
					ladjust = !ladjust;
					width = -width;
				}
			} else {
				/*
				 * C17 7.21.6.1p5: a negative precision is
				 * taken as though it were omitted.
				 */
				dwidth = va_arg (ap, int);
				if (dwidth < 0) {
					dot = 0;
					dwidth = -1;
					padding = zeroflag ? '0' : ' ';
				}
			}
			goto reswitch;

		case '0':
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (c == '0' && ! dot) {
				padding = '0';
				zeroflag = 1;
				goto reswitch;
			}
			for (n=0; ; ++fmt) {
				n = n * 10 + c - '0';
				c = *fmt;
				if (c < '0' || c > '9')
					break;
			}
			if (dot)
				dwidth = n;
			else
				width = n;
			goto reswitch;

		/* Length modifiers. */
		case 'h':
			if (*fmt == 'h') {
				fmt++;
				sz = SZ_CHAR;
			} else
				sz = SZ_SHORT;
			goto reswitch;

		case 'l':
			if (*fmt == 'l') {
				fmt++;
				sz = SZ_LLONG;
			} else
				sz = SZ_LONG;
			goto reswitch;

		case 'q':
		case 'j':
			sz = SZ_LLONG;
			goto reswitch;

		case 'z':
			sz = SZ_SIZE;
			goto reswitch;

		case 't':
			sz = SZ_PTRDIFF;
			goto reswitch;

		case 'L':
			/* long double is double on this target. */
			goto reswitch;

		case 'c':
			if (! ladjust && width > 0)
				while (width--)
					PUTC (' ');

			PUTC (va_arg (ap, int));

			if (ladjust && width > 0)
				while (width--)
					PUTC (' ');
			break;

		case 'D':
			sz = SZ_LONG;
			/* FALLTHROUGH */

		case 'd':
		case 'i':
			/*
			 * A signed value is fetched at its promoted width
			 * and narrowed the way the caller's type would have
			 * narrowed it, then its sign is peeled before the
			 * magnitude converts as unsigned.
			 */
			base = 10;
			if (sz == SZ_LLONG) {
				issigned = 1;
				goto wide;
			}
			{
				long l;

				switch (sz) {
				case SZ_LONG:
					l = va_arg (ap, long);
					break;
				case SZ_SIZE:
					l = (long) va_arg (ap, ssize_t);
					break;
				case SZ_PTRDIFF:
					l = (long) va_arg (ap, ptrdiff_t);
					break;
				case SZ_CHAR:
					l = (signed char) va_arg (ap, int);
					break;
				case SZ_SHORT:
					l = (short) va_arg (ap, int);
					break;
				default:
					l = va_arg (ap, int);
					break;
				}
				if (l < 0) {
					neg = '-';
					ul = 0UL - (unsigned long) l;
				} else
					ul = (unsigned long) l;
			}
			goto number;

		case 'o':
			base = 8;
			goto unsign;

		case 'u':
			base = 10;
			goto unsign;

		case 'x':
		case 'X':
			base = 16;
			uppercase = (c == 'X');
			goto unsign;

		case 'p':
			ul = (unsigned long) va_arg (ap, void*);
			if (! ul) {
				s = (const unsigned char*) "(nil)";
				goto string;
			}
			base = 16;
			sharpflag = (width == 0);
			goto number;

unsign:			sign = 0;
			if (sz == SZ_LLONG) {
				issigned = 0;
				goto wide;
			}
			switch (sz) {
			case SZ_LONG:
				ul = va_arg (ap, unsigned long);
				break;
			case SZ_SIZE:
				ul = (unsigned long) va_arg (ap, size_t);
				break;
			case SZ_PTRDIFF:
				ul = (unsigned long) va_arg (ap, ptrdiff_t);
				break;
			default:
				ul = va_arg (ap, unsigned int);
				if (sz == SZ_CHAR)
					ul = (unsigned char) ul;
				else if (sz == SZ_SHORT)
					ul = (unsigned short) ul;
				break;
			}
			goto number;

		case 'n':
			/*
			 * C17 7.21.6.1p8: the count of characters written so
			 * far is stored through the argument, at the width
			 * the length modifier names; nothing is printed.
			 */
			switch (sz) {
			case SZ_CHAR:
				*va_arg (ap, signed char *) = (signed char) retval;
				break;
			case SZ_SHORT:
				*va_arg (ap, short *) = (short) retval;
				break;
			case SZ_LONG:
				*va_arg (ap, long *) = retval;
				break;
			case SZ_LLONG:
				*va_arg (ap, long long *) = retval;
				break;
			case SZ_SIZE:
				*va_arg (ap, ssize_t *) = (ssize_t) retval;
				break;
			case SZ_PTRDIFF:
				*va_arg (ap, ptrdiff_t *) = (ptrdiff_t) retval;
				break;
			default:
				*va_arg (ap, int *) = retval;
				break;
			}
			break;

		case 's':
			s = va_arg (ap, unsigned char*);
			if (! s)
				s = (const unsigned char*) "(null)";
string:			if (! dot)
				n = strlen ((char*)s);
			else
				for (n=0; n<dwidth && s[n]; n++)
					continue;

			width -= n;

			if (! ladjust && width > 0)
				while (width--)
					PUTC (' ');
			while (n--)
				PUTC (*s++);
			if (ladjust && width > 0)
				while (width--)
					PUTC (' ');
			break;

wide:
			/*
			 * A long long. Without doprnt_llong.o linked the
			 * argument is still stepped over, so the following
			 * conversions read their own arguments, and the
			 * value prints as the same mark the float path uses.
			 */
			if (dwidth >= (int) sizeof(nbuf)) {
				extrazeros = dwidth - sizeof(nbuf) + 1;
				dwidth = sizeof(nbuf) - 1;
			}
			if (__doprnt_ll == 0) {
				(void) va_arg (ap, unsigned long long);
				s = (const unsigned char *) "?";
				goto string;
			}
			{
				/*
				 * The member advances the list; a copy keeps
				 * the address a va_list * on every ABI, since a
				 * va_list parameter has already decayed where
				 * the type is an array.
				 */
				va_list apc;

				va_copy (apc, ap);
				s = __doprnt_ll (&apc, issigned, base, dwidth,
				    nbuf, &size, &neg, &nonzero);
				va_end (ap);
				va_copy (ap, apc);
				va_end (apc);
			}
			goto emit;

number:
			if (dwidth >= (int) sizeof(nbuf)) {
				extrazeros = dwidth - sizeof(nbuf) + 1;
				dwidth = sizeof(nbuf) - 1;
			}
			s = ksprintn (nbuf, ul, base, dwidth, &size);
			nonzero = (ul != 0);
emit:
			if (! neg) {
				if (sign == -1)
					neg = '+';
				else if (sign == -2)
					neg = ' ';
			}
			/*
			 * The field the digits occupy: every digit the
			 * precision asked for, the zeros the buffer could not
			 * hold included, plus sign and prefix, in an int so a
			 * precision above 255 does not wrap the count.
			 */
			n = size + extrazeros;
			if (sharpflag && nonzero) {
				if (base == 8)
					n++;
				else if (base == 16)
					n += 2;
			}
			if (neg)
				n++;

			if (! ladjust && width && padding == ' ' &&
			    (width -= n) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);

			if (neg)
				PUTC (neg);

			if (sharpflag && nonzero) {
				if (base == 8) {
					PUTC ('0');
				} else if (base == 16) {
					PUTC ('0');
					PUTC (uppercase ? 'X' : 'x');
				}
			}

			if (! ladjust && width && (width -= n) > 0)
				do {
					PUTC (padding);
				} while (--width > 0);

			if (extrazeros)
				do {
					PUTC ('0');
				} while (--extrazeros > 0);

			for (; *s; --s) {
				if (uppercase && *s>='a' && *s<='z') {
					PUTC (*s + 'A' - 'a');
				} else {
					PUTC (*s);
				}
			}

			if (ladjust && width && (width -= n) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);
			break;

		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G': {
#ifdef NO_DOPRNT_FLOATFMT
			/*
			 * A libc built with NO_DOPRNT_FLOATFMT (the board
			 * library in distrib/rp2040/Makefile.inc) never
			 * links doprnt_float.c and carries no libgcc, so the
			 * double comparison and classification below (d < 0,
			 * isnan, isinf) would pull __aeabi_dcmp*, __eqdf2 and
			 * __ledf2 out of libgcc's GPL runtime for a feature
			 * the board never uses. Cutting the branch here,
			 * before it touches d at all, avoids that pull
			 * entirely; the result matches what __doprnt_cvt == 0
			 * already prints below when float support is not
			 * linked in on any machine.
			 */
			(void) va_arg (ap, double);
			nbuf [0] = '?';
			nbuf [1] = 0;
			size = 1;
			extrazeros = 0;
			s = nbuf;
#else
			double d = va_arg (ap, double);

			hexfmt = (c == 'a' || c == 'A');

			/*
			 * don't do unrealistic precision; just pad it with
			 * zeroes later, so buffer size stays rational. A
			 * hexadecimal double has 13 fraction digits exactly;
			 * an omitted precision under %a means all of them,
			 * trailing zeros trimmed, which the converter does.
			 */
			if (hexfmt) {
				if (dwidth > 13) {
					extrazeros = dwidth - 13;
					dwidth = 13;
				}
			} else if (dwidth > DBL_DIG) {
				if ((c != 'g' && c != 'G') || sharpflag)
					extrazeros = dwidth - DBL_DIG;
				dwidth = DBL_DIG;
			} else if (dwidth == -1) {
				dwidth = (sz == SZ_LONG ? DBL_DIG : FLT_DIG);
			}
			/*
			 * softsign avoids negative 0 if d is < 0 and
			 * no significant digits will be shown
			 */
			if (d < 0) {
				neg = 1;
				d = -d;
			}
			/*
			 * cvt may have to round up past the "start" of the
			 * buffer, i.e. ``intf("%.2f", (double)9.999);'';
			 * if the first char isn't NULL, it did.
			 */
			if (isnan (d) || isinf (d)) {
				/* C17 7.21.6.1p8: inf and nan, upper case under A, E, F and G */
				strcpy ((char*)nbuf, isnan (d) ?
				    (c < 'a' ? "NAN" : "nan") :
				    (c < 'a' ? "INF" : "inf"));
				size = 3;
				extrazeros = 0;
				padding = ' ';
				s = nbuf;
			} else if (__doprnt_cvt == 0) {
				nbuf [0] = '?';
				nbuf [1] = 0;
				size = 1;
				extrazeros = 0;
				s = nbuf;
			} else {
				*nbuf = 0;
				size = __doprnt_cvt (d, dwidth, sharpflag, &neg, c,
					nbuf, nbuf + sizeof(nbuf) - 1);
				if (*nbuf) {
					s = nbuf;
					nbuf [size] = 0;
				} else {
					s = nbuf + 1;
					nbuf [size + 1] = 0;
				}
			}
#endif
			if (neg || sign)
				size++;
			size += extrazeros;	/* the deferred zeros are part of the field */
			if (! ladjust && width && padding == ' ' &&
			    (width -= size) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);

			if (neg) {
				PUTC ('-');
			} else if (sign == -1) {
				PUTC ('+');
			} else if (sign == -2) {
				PUTC (' ');
			}

			/*
			 * Zero padding under %a goes after the 0x prefix, as it
			 * does after an integer's alternate-form prefix, so the
			 * field stays a hexadecimal floating constant.
			 */
			if (hexfmt && padding == '0' && s[0] == '0' &&
			    (s[1] == 'x' || s[1] == 'X')) {
				PUTC (s[0]);
				PUTC (s[1]);
				s += 2;
			}

			if (! ladjust && width && (width -= size) > 0)
				do {
					PUTC (padding);
				} while (--width > 0);

			for (; *s; ++s) {
				/*
				 * The deferred zeros go before the exponent
				 * marker: p under %a, where e is a mantissa
				 * digit, and e under the decimal conversions.
				 */
				if (extrazeros && (hexfmt ? (*s == 'p' || *s == 'P')
				    : (*s == 'e' || *s == 'E')))
					do {
						PUTC ('0');
					} while (--extrazeros > 0);

				PUTC (*s);
			}
			if (extrazeros)
				do {
					PUTC ('0');
				} while (--extrazeros > 0);

			if (ladjust && width && (width -= size) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);
			break;
		}
		default:
			if (c == 'm' && (stream->_flag & _IOSYSLOG) != 0) {
				s = (const unsigned char *)stream->_base;
				if (!s)
					s = (const unsigned char *)"(null)";
				goto string;
			}
			PUTC ('%');
			PUTC (c);
			break;
		}
	}
}

/*
 * Put a NUL-terminated number (base <= 16) in a buffer in reverse
 * order; return an optional length and a pointer to the last character
 * written in the buffer (i.e., the first character of the string).
 * The buffer pointed to by `nbuf' must have length >= MAXNBUF.
 */
static unsigned char *
ksprintn (unsigned char *nbuf, unsigned long ul, unsigned char base, int width,
	int *lenp)
{
	unsigned char *p;

	p = nbuf;
	*p = 0;
	/* C17 7.21.6.1p8: a zero with an explicit zero precision has no digits. */
	if (ul == 0 && width == 0) {
		if (lenp)
			*lenp = 0;
		return (p);
	}
	for (;;) {
		*++p = mkhex (ul % base);
		ul /= base;
		if (--width > 0)
			continue;
		if (! ul)
			break;
	}
	if (lenp)
		*lenp = (int)(p - nbuf);
	return (p);
}

static unsigned char
mkhex (unsigned char ch)
{
	ch &= 15;
	if (ch > 9)
		return ch + 'a' - 10;
	return ch + '0';
}
