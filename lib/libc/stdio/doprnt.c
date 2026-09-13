/*
 * Scaled down version of printf(3).
 * Based on FreeBSD sources, heavily rewritten.
 *
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * One additional format beyond printf(3): %D prints a long decimal, the
 * same conversion %ld names, so a caller passing a long needs no length
 * modifier. usr.bin/find and usr.bin/grep both print block counts with it.
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
#include <unistd.h>
#include <float.h>
#include <math.h>

/* Max number conversion buffer length. */
#define MAXNBUF	\
	(1/*sign*/ + DBL_MAX_10_EXP+1/*max integral digits*/ + \
	1/*.*/ + DBL_DIG+1/*max fractional digits*/ + 1/*NUL*/)

static unsigned char *ksprintn (unsigned char *buf, unsigned long v, unsigned char base,
	int width, unsigned char *lp);
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

int
_doprnt (char const *fmt, va_list ap, FILE *stream)
{
#define PUTC(c) { putc (c, stream); ++retval; }
	unsigned char nbuf [MAXNBUF], padding;
	const unsigned char *s;
	unsigned char c, base, lflag, ladjust, sharpflag, neg, dot, size;
	int n, width, dwidth, retval, uppercase, extrazeros, sign;
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
		lflag = 0; ladjust = 0; sharpflag = 0; neg = 0;
		sign = 0; dot = 0; uppercase = 0; dwidth = -1;
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
				dwidth = va_arg (ap, int);
			}
			goto reswitch;

		case '0':
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (c == '0' && ! dot) {
				padding = '0';
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
			lflag=1;
			/* FALLTHROUGH */

		case 'd':
		case 'i':
			ul = lflag ? va_arg (ap, long) : va_arg (ap, int);
			if (! sign) sign = 1;
			base = 10;
			goto number;

		case 'l':
			lflag = 1;
			goto reswitch;

		case 'o':
			ul = lflag ? va_arg (ap, unsigned long) :
				va_arg (ap, unsigned int);
			base = 8;
			goto nosign;

		case 'p':
			ul = (size_t) va_arg (ap, void*);
			if (! ul) {
				s = (const unsigned char*) "(nil)";
				goto string;
			}
			base = 16;
			sharpflag = (width == 0);
			goto nosign;

		case 'n': /* TBD!!! fix this non-standard %n */
			ul = lflag ? va_arg (ap, unsigned long) :
				sign ? (unsigned long) va_arg (ap, int) :
				va_arg (ap, unsigned int);
			base = 10;
			goto number;

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


		case 'u':
			ul = lflag ? va_arg (ap, unsigned long) :
				va_arg (ap, unsigned int);
			base = 10;
			goto nosign;

		case 'x':
		case 'X':
			ul = lflag ? va_arg (ap, unsigned long) :
				va_arg (ap, unsigned int);
			base = 16;
			uppercase = (c == 'X');
			goto nosign;
nosign:			sign = 0;
number:			if (sign) {
				if ((long) ul < 0L) {
					neg = '-';
					ul = -(long) ul;
				} else if (sign < 0)
					neg = '+';
			}
			if (dwidth >= (int) sizeof(nbuf)) {
				extrazeros = dwidth - sizeof(nbuf) + 1;
				dwidth = sizeof(nbuf) - 1;
			}
			s = ksprintn (nbuf, ul, base, dwidth, &size);
			if (sharpflag && ul != 0) {
				if (base == 8)
					size++;
				else if (base == 16)
					size += 2;
			}
			if (neg)
				size++;

			if (! ladjust && width && padding == ' ' &&
			    (width -= size) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);

			if (neg)
				PUTC (neg);

			if (sharpflag && ul != 0) {
				if (base == 8) {
					PUTC ('0');
				} else if (base == 16) {
					PUTC ('0');
					PUTC (uppercase ? 'X' : 'x');
				}
			}

			if (extrazeros)
				do {
					PUTC ('0');
				} while (--extrazeros > 0);

			if (! ladjust && width && (width -= size) > 0)
				do {
					PUTC (padding);
				} while (--width > 0);

			for (; *s; --s) {
				if (uppercase && *s>='a' && *s<='z') {
					PUTC (*s + 'A' - 'a');
				} else {
					PUTC (*s);
				}
			}

			if (ladjust && width && (width -= size) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);
			break;

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
			/*
			 * don't do unrealistic precision; just pad it with
			 * zeroes later, so buffer size stays rational.
			 */
			if (dwidth > DBL_DIG) {
				if ((c != 'g' && c != 'G') || sharpflag)
					extrazeros = dwidth - DBL_DIG;
				dwidth = DBL_DIG;
			} else if (dwidth == -1) {
				dwidth = (lflag ? DBL_DIG : FLT_DIG);
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
				strcpy ((char*)nbuf, isnan (d) ? "NaN" : "Inf");
				size = 3;
				extrazeros = 0;
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
			if (! ladjust && width && padding == ' ' &&
			    (width -= size) > 0)
				do {
					PUTC (' ');
				} while (--width > 0);

			if (neg) {
				PUTC ('-');
			} else if (sign) {
				PUTC ('+');
			}

			if (! ladjust && width && (width -= size) > 0)
				do {
					PUTC (padding);
				} while (--width > 0);

			for (; *s; ++s) {
				if (extrazeros && (*s == 'e' || *s == 'E'))
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
			PUTC ('%');
			if (lflag)
				PUTC ('l');
			PUTC (c);
			break;
		}
	}
}

/*
 * Put a NUL-terminated ASCII number (base <= 16) in a buffer in reverse
 * order; return an optional length and a pointer to the last character
 * written in the buffer (i.e., the first character of the string).
 * The buffer pointed to by `nbuf' must have length >= MAXNBUF.
 */
static unsigned char *
ksprintn (unsigned char *nbuf, unsigned long ul, unsigned char base, int width,
	unsigned char *lenp)
{
	unsigned char *p;

	p = nbuf;
	*p = 0;
	for (;;) {
		*++p = mkhex (ul % base);
		ul /= base;
		if (--width > 0)
			continue;
		if (! ul)
			break;
	}
	if (lenp)
		*lenp = p - nbuf;
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

