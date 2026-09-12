/*
 * POSIX.1-2008 getline(3): this libc predates it. cut, nl and
 * uudecode all read a dynamically sized line, so this fills the gap
 * with the ordinary doubling-buffer implementation, built on fgetc()
 * alone so it needs nothing from stdio's internals.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "compat.h"

ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
	char *buf;
	size_t cap, len;
	int c;

	if (lineptr == NULL || n == NULL || stream == NULL) {
		errno = EINVAL;
		return -1;
	}

	buf = *lineptr;
	cap = *n;
	if (buf == NULL || cap == 0) {
		cap = 128;
		buf = malloc(cap);
		if (buf == NULL)
			return -1;
	}

	len = 0;
	for (;;) {
		c = fgetc(stream);
		if (c == EOF) {
			if (len == 0 || ferror(stream)) {
				*lineptr = buf;
				*n = cap;
				return -1;
			}
			break;
		}
		if (len + 1 >= cap) {
			size_t ncap = cap * 2;
			char *nbuf = realloc(buf, ncap);
			if (nbuf == NULL) {
				*lineptr = buf;
				*n = cap;
				return -1;
			}
			buf = nbuf;
			cap = ncap;
		}
		buf[len++] = (char)c;
		if (c == '\n')
			break;
	}
	buf[len] = '\0';
	*lineptr = buf;
	*n = cap;

	return (ssize_t)len;
}
