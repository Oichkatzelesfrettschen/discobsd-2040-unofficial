/*
 * POSIX.1-2008 getline(3): this libc predates it. cut, nl and
 * uudecode all read a dynamically sized line, so this fills the gap
 * with the ordinary doubling-buffer implementation, built on fgetc()
 * alone so it needs nothing from stdio's internals.
 *
 * The buffer is the caller's at every return. A refused growth hands
 * back the block realloc() refused to resize, with the bytes read so far
 * and a terminator, since lib/libc/gen/malloc.c leaves that block with
 * the caller; a caller that passed a pointer with a zero size gets it
 * resized rather than replaced. The length returned is a ssize_t, so the
 * capacity never passes SSIZE_MAX + 1 and a line that would need more is
 * refused before the arithmetic wraps; sys/errno.h carries no EOVERFLOW,
 * so the refusal is ENOMEM, which is also what the allocator would have
 * said one doubling later. usr.bin/textbox/tests/getline_test.c pins
 * each of these.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "compat.h"

#define GETLINE_FIRST	128
#define GETLINE_CAP_MAX	((size_t)SSIZE_MAX + 1)

/*
 * The capacity after cap: doubled, held at GETLINE_CAP_MAX rather than
 * wrapped, and 0 when cap is already there, which is the caller's signal
 * that the line cannot be represented.
 */
static size_t
getline_grow(size_t cap)
{
	if (cap >= GETLINE_CAP_MAX)
		return 0;
	if (cap > GETLINE_CAP_MAX / 2)
		return GETLINE_CAP_MAX;
	return cap * 2;
}

ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
	char *buf, *nbuf;
	size_t cap, ncap, len;
	int c;

	if (lineptr == NULL || n == NULL || stream == NULL) {
		errno = EINVAL;
		return -1;
	}

	buf = *lineptr;
	cap = *n;
	if (buf == NULL || cap == 0) {
		nbuf = realloc(buf, GETLINE_FIRST);
		if (nbuf == NULL)
			return -1;
		buf = nbuf;
		cap = GETLINE_FIRST;
		*lineptr = buf;
		*n = cap;
	}

	len = 0;
	for (;;) {
		c = fgetc(stream);
		if (c == EOF) {
			buf[len] = '\0';
			if (len == 0 || ferror(stream))
				return -1;
			break;
		}
		if (len + 1 >= cap) {
			/* The byte just read has no room yet; a refusal gives it
			 * back to the stream so the next call starts with it. */
			ncap = getline_grow(cap);
			if (ncap == 0) {
				ungetc(c, stream);
				buf[len] = '\0';
				errno = ENOMEM;
				return -1;
			}
			nbuf = realloc(buf, ncap);
			if (nbuf == NULL) {
				ungetc(c, stream);
				buf[len] = '\0';
				return -1;
			}
			buf = nbuf;
			cap = ncap;
			*lineptr = buf;
			*n = cap;
		}
		buf[len++] = (char)c;
		if (c == '\n')
			break;
	}
	buf[len] = '\0';

	return (ssize_t)len;
}
