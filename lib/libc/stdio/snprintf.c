/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>

/*
 * A write-only string stream over the caller's array. _w and _bf._size
 * carry the capacity, __SSTR makes __swbuf drop a character at capacity
 * instead of flushing, and _fops stays NULL because no write function is
 * ever reached. The formatter's return value is the count it would have
 * produced, which is the C99 contract for the bounded forms.
 */
static void
strstream(FILE *fp, char *str, int capacity)
{
	fp->_flags = __SWR | __SSTR;
	fp->_p = fp->_bf._base = (unsigned char *)str;
	fp->_w = fp->_bf._size = capacity;
	fp->_r = 0;
	fp->_lbfsize = 0;
	fp->_file = -1;
	fp->_fops = NULL;
	fp->_up = NULL;
	fp->_ur = 0;
}

int
vsnprintf(char *str, size_t nbytes, const char *fmt, va_list ap)
{
	FILE f;
	char dummy;
	int length;

	if (nbytes > INT_MAX) {
		errno = EINVAL;
		return -1;
	}
	/* A zero capacity writes its terminator into dummy, never into str. */
	strstream(&f, nbytes == 0 ? &dummy : str,
	    nbytes == 0 ? 0 : (int)nbytes - 1);
	length = _doprnt(fmt, ap, &f);
	*f._p = '\0';
	return length;
}

int
snprintf(char *str, size_t nbytes, const char *fmt, ...)
{
	va_list ap;
	int length;

	va_start(ap, fmt);
	length = vsnprintf(str, nbytes, fmt, ap);
	va_end(ap);
	return length;
}
