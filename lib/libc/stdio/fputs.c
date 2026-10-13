/*
 * Copyright (c) 1984 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <alloca.h>

int
fputs(const char *text, FILE *stream)
{
	FILE temporary;
	FILE *output = stream;
	int result = 0;

	if (stream->_flag & _IONBF) {
		/* Preserve stream-owned buffer pointers across temporary batching. */
		temporary = *stream;
		temporary._flag &= ~_IONBF;
		temporary._ptr = temporary._base = alloca(BUFSIZ);
		temporary._bufsiz = BUFSIZ;
		temporary._cnt = 0;
		output = &temporary;
	}
	while (*text != '\0') {
		if (putc((unsigned char)*text++, output) == EOF) {
			result = EOF;
			break;
		}
	}
	if (output != stream) {
		if (result != EOF && fflush(output) == EOF)
			result = EOF;
		stream->_flag = output->_flag | _IONBF;
		stream->_cnt = 0;
	}
	return result;
}
