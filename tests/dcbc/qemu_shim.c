/*
 * err(3) family and __progname for the newlib build of dc under qemu-arm.
 * Messages follow the tree's lib/libc/gen/err.c: "dc: " then the formatted
 * text, then ": " and strerror(errno) for the forms that take errno.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"

const char *__progname = "dc";

static void
emit(int with_errno, const char *fmt, va_list ap)
{
	int saved = errno;

	fprintf(stderr, "%s: ", __progname);
	if (fmt != NULL) {
		vfprintf(stderr, fmt, ap);
		if (with_errno)
			fputs(": ", stderr);
	}
	if (with_errno)
		fputs(strerror(saved), stderr);
	fputc('\n', stderr);
}

void
err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(1, fmt, ap);
	va_end(ap);
	exit(eval);
}

void
errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(0, fmt, ap);
	va_end(ap);
	exit(eval);
}

void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(1, fmt, ap);
	va_end(ap);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emit(0, fmt, ap);
	va_end(ap);
}
