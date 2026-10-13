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
#include <alloca.h>
#include "output.h"

int
vfprintf(FILE *stream, const char *format, va_list arguments)
{
	FILE temporary;
	FILE *output = stream;
	int result;

	if (stream->_flag & _IONBF) {
		/* The copy borrows the descriptor and owns only automatic storage. */
		temporary = *stream;
		temporary._flag &= ~_IONBF;
		temporary._ptr = temporary._base = alloca(STDIO_OUTPUT_CHUNK);
		temporary._bufsiz = STDIO_OUTPUT_CHUNK;
		temporary._cnt = 0;
		output = &temporary;
	}
	result = _doprnt(format, arguments, output);
	if (output != stream) {
		if (!ferror(output) && fflush(output) == EOF)
			result = EOF;
		stream->_flag = output->_flag | _IONBF;
		stream->_cnt = 0;
	}
	return ferror(stream) ? EOF : result;
}
