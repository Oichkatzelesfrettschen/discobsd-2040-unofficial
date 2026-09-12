/*
 * A byte-wide stand-in for sbase's libutf: this port's console is
 * ASCII, so a "rune" is one byte, fullrune() is always true, and
 * utflen() is strlen(). This drops multibyte decoding, not the octet
 * stream cut, paste, expand, unexpand and nl already process one
 * character at a time.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"

int
fullrune(const char *s, size_t n)
{
	(void)s;
	return n >= 1;
}

size_t
utflen(const char *s)
{
	return strlen(s);
}

int
fgetrune(Rune *r, FILE *fp)
{
	int c;

	if ((c = fgetc(fp)) == EOF) {
		if (ferror(fp))
			return -1;
		return 0;
	}
	*r = (Rune)c;
	return 1;
}

int
efgetrune(Rune *r, FILE *fp, const char *file)
{
	int ret;

	if ((ret = fgetrune(r, fp)) < 0) {
		fprintf(stderr, "fgetrune %s: %s\n", file, strerror(errno));
		exit(1);
	}
	return ret;
}

int
fputrune(const Rune *r, FILE *fp)
{
	return fputc(*r, fp) == EOF ? -1 : 1;
}

int
efputrune(const Rune *r, FILE *fp, const char *file)
{
	int ret;

	if ((ret = fputrune(r, fp)) < 0) {
		fprintf(stderr, "fputrune %s: %s\n", file, strerror(errno));
		exit(1);
	}
	return ret;
}
