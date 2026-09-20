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
sprintf(char *str, const char *fmt, ...)
{
	FILE f;
	va_list ap;

	strstream(&f, str, INT_MAX);
	va_start(ap, fmt);
	(void) _doprnt(fmt, ap, &f);
	va_end(ap);
	*f._p = '\0';
	return (int)((char *)f._p - str);
}

int
vsprintf(char *str, const char *fmt, va_list ap)
{
	FILE f;

	strstream(&f, str, INT_MAX);
	(void) _doprnt(fmt, ap, &f);
	*f._p = '\0';
	return (int)((char *)f._p - str);
}
