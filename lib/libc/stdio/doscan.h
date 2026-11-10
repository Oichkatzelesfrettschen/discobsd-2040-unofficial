#ifndef _LIBC_STDIO_DOSCAN_H_
#define _LIBC_STDIO_DOSCAN_H_

/*
 * State and verdicts that _doscan in doscan.c shares with the 64-bit
 * integer conversion in doscan_llong.c.
 */

#include <stdio.h>
#include <stdarg.h>

/*
 * Every conversion reports one of three verdicts. Assignment is not one
 * of them: a suppressed directive that converts reports CONV_OK, and
 * _doscan decides whether the directive reaches the returned count.
 */
#define CONV_INPUT	(-1)	/* input ended before the subject sequence */
#define CONV_FAIL	0	/* the input does not match the directive */
#define CONV_OK		1	/* the directive converted */

/* The directive state __doscan_ll reads; doscan.c defines the rest. */
#define F_SUPPRESS	0x01	/* * : convert, assign nothing */
#define F_SIGNED	0x10	/* d, i : saturate against the signed limits */
#define F_POINTER	0x20	/* p */

/*
 * __doscan_ll receives the directive's flags with its base, 8, 10 or 16,
 * or 0 for %i, above them, so the call passes its arguments in registers.
 * LL_BASE_COUNT in place of a base marks %n, which consumes nothing and
 * stores the count of characters read.
 */
#define LL_BASE_SHIFT	8
#define LL_BASE_COUNT	1

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

/*
 * 64-bit integer conversion, resolved only when doscan_llong.o is linked.
 * It skips leading white space, consumes the subject sequence under
 * width, and stores a long long, or an unsigned long long when the flags
 * lack F_SIGNED, through the next argument of *ap. A directive carrying
 * F_SUPPRESS converts and takes no argument, except under LL_BASE_COUNT.
 */
extern int __doscan_ll (struct scanstate *s, int width, int flags_base,
	va_list *ap) __attribute__((weak));

#endif /* !_LIBC_STDIO_DOSCAN_H_ */
