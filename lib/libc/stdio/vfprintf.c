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

/*
 * Formatted output through _doprnt, which writes one character at a time
 * with putc. On an unbuffered stream that is one write(2) per character,
 * so the conversion runs into a stack buffer inside a copy of the stream
 * and is written out once; the copy shares the descriptor and the file
 * operations, and its error flag is copied back.
 */
int
vfprintf(FILE *fp, const char *fmt, va_list ap)
{
	FILE fake;
	unsigned char buf[BUFSIZ];
	int ret;

	if ((fp->_flags & __SNBF) == 0) {
		ret = _doprnt(fmt, ap, fp);
		return (ferror(fp) ? EOF : ret);
	}

	fake._flags = fp->_flags & ~__SNBF;
	fake._file = fp->_file;
	fake._fops = fp->_fops;
	fake._bf._base = fake._p = buf;
	fake._bf._size = fake._w = sizeof buf;
	fake._r = 0;
	fake._lbfsize = 0;
	fake._up = NULL;
	fake._ur = 0;
	fake._offset = 0;

	ret = _doprnt(fmt, ap, &fake);
	if (ret >= 0 && fflush(&fake))
		ret = EOF;
	if (fake._flags & __SERR)
		fp->_flags |= __SERR;
	return (ferror(fp) ? EOF : ret);
}
