/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <limits.h>
#include <stdio.h>

#define TAB_WIDTH 8UL

int colrm_parse_column(const char *, unsigned long *);
int colrm_next_column(unsigned long *, int);
int colrm_filter(FILE *, FILE *, unsigned long, unsigned long);
int main(int, char **);

int
colrm_parse_column(const char *text, unsigned long *value)
{
	unsigned long parsed = 0;
	const unsigned char *cursor = (const unsigned char *)text;

	if (*cursor == '\0')
		return -1;
	while (*cursor != '\0') {
		unsigned long digit;

		if (*cursor < '0' || *cursor > '9')
			return -1;
		digit = (unsigned long)(*cursor - '0');
		if (parsed > ULONG_MAX / 10UL ||
		    (parsed == ULONG_MAX / 10UL && digit > ULONG_MAX % 10UL))
			return -1;
		parsed = parsed * 10UL + digit;
		cursor++;
	}
	if (parsed == 0)
		return -1;
	*value = parsed;
	return 0;
}

int
colrm_next_column(unsigned long *column, int character)
{
	unsigned long advance;

	if (character == '\b') {
		if (*column != 0)
			(*column)--;
		return 0;
	}
	if (character == '\n') {
		*column = 0;
		return 0;
	}
	if (character == '\t') {
		advance = TAB_WIDTH - (*column % TAB_WIDTH);
		if (*column > ULONG_MAX - advance)
			return -1;
		*column += advance;
		return 0;
	}
	if (*column == ULONG_MAX)
		return -1;
	(*column)++;
	return 0;
}

static int
colrm_error(const char *message)
{
	(void)fprintf(stderr, "colrm: %s\n", message);
	return 1;
}

int
colrm_filter(FILE *input, FILE *output, unsigned long start,
	unsigned long stop)
{
	unsigned long column = 0;
	int character;

	while ((character = fgetc(input)) != EOF) {
		if (colrm_next_column(&column, character) != 0)
			return colrm_error("column count overflow");
		if ((start == 0 || column < start ||
		    (stop != 0 && column > stop)) &&
		    fputc(character, output) == EOF)
			return colrm_error("stdout: write error");
	}
	if (ferror(input))
		return colrm_error("stdin: read error");
	if (fflush(output) == EOF)
		return colrm_error("stdout: flush error");
	return 0;
}

static int
colrm_usage(void)
{
	(void)fprintf(stderr, "usage: colrm [start [stop]]\n");
	return 1;
}

int
main(int argc, char **argv)
{
	unsigned long start = 0;
	unsigned long stop = 0;

	if (argc < 1 || argc > 3)
		return colrm_usage();
	if (argc >= 2 && colrm_parse_column(argv[1], &start) != 0) {
		(void)fprintf(stderr, "colrm: illegal column -- %s\n", argv[1]);
		return 1;
	}
	if (argc == 3 && colrm_parse_column(argv[2], &stop) != 0) {
		(void)fprintf(stderr, "colrm: illegal column -- %s\n", argv[2]);
		return 1;
	}
	if (stop != 0 && start > stop)
		return colrm_error("illegal start and stop columns");
	return colrm_filter(stdin, stdout, start, stop);
}
