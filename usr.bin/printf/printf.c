/*
 * Copyright (c) 1989, 1993
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORMAT_LIMIT 65535

static char **operands;
static int operand_count;
static int operand_base;
static int operand_next;
static int operand_used;
static int utility_status;
static int number_valid;

static void diagnostic(const char *);
static int decimal(const char **, int *);
static const char *argument(int);
static long number(int);
static int output_byte(unsigned char);
static int decode(const unsigned char **, int, unsigned char *, int *);
static int output_b(const char *, int, int, int, int *);
static int conversion(const char **, int *);
static int format_escape(const char **);
static int run_format(const char *);

static void
diagnostic(const char *message)
{
	(void)fputs("printf: ", stderr);
	(void)fputs(message, stderr);
	(void)fputc('\n', stderr);
}

static int
decimal(const char **cursor_pointer, int *value_pointer)
{
	const char *cursor;
	int digits;
	int overflow;
	int value;

	cursor = *cursor_pointer;
	digits = 0;
	overflow = 0;
	value = 0;
	while (*cursor >= '0' && *cursor <= '9') {
		int digit;

		digit = *cursor++ - '0';
		if (value > 6553 || (value == 6553 && digit > 5))
			overflow = 1;
		else
			value = value * 10 + digit;
		digits = 1;
	}
	*cursor_pointer = cursor;
	if (!digits)
		return (0);
	if (overflow)
		return (-1);
	*value_pointer = value;
	return (1);
}

static const char *
argument(int number)
{
	int offset;

	if (number != 0)
		offset = number - 1;
	else
		offset = operand_next++;
	if (offset + 1 > operand_used)
		operand_used = offset + 1;
	if (operand_base + offset < operand_count)
		return (operands[operand_base + offset]);
	if (number != 0) {
		diagnostic("numbered argument is missing");
		utility_status = 1;
	}
	return (NULL);
}

static long
number(int argument_number)
{
	const char *operand;
	char *end_pointer;
	long value;

	operand = argument(argument_number);
	number_valid = 1;
	if (operand == NULL)
		return (0);
	if (*operand == '\'' || *operand == '"') {
		value = (unsigned char)operand[1];
		if (operand[1] == '\0' || operand[2] != '\0')
			number_valid = 0;
	} else {
		errno = 0;
		value = strtol(operand, &end_pointer, 0);
		if (end_pointer == operand || *end_pointer != '\0' || errno == ERANGE)
			number_valid = 0;
	}
	if (!number_valid) {
		diagnostic("numeric conversion failed");
		utility_status = 1;
	}
	return (value);
}

static int
output_byte(unsigned char byte)
{
	return (fputc(byte, stdout) != EOF);
}

/*
 * The format operand accepts a one-to-three-digit octal escape.  A %b
 * operand accepts the POSIX \0ddd form and treats \c as an output stop.
 */
static int
decode(const unsigned char **cursor_pointer, int percent_b,
	unsigned char *byte_pointer, int *stop_pointer)
{
	const unsigned char *cursor;
	unsigned int byte;
	int remaining;

	cursor = *cursor_pointer;
	if (*cursor == '\0')
		return (0);
	if (*cursor != '\\') {
		*byte_pointer = *cursor;
		*cursor_pointer = cursor + 1;
		return (1);
	}
	cursor++;
	if (*cursor == '\0') {
		*byte_pointer = '\\';
		*cursor_pointer = cursor;
		return (1);
	}
	byte = *cursor++;
	switch (byte) {
	case '\\': break;
	case 'a': byte = '\a'; break;
	case 'b': byte = '\b'; break;
	case 'c':
		if (percent_b) {
			*stop_pointer = 1;
			*cursor_pointer = cursor;
			return (0);
		}
		break;
	case 'f': byte = '\f'; break;
	case 'n': byte = '\n'; break;
	case 'r': byte = '\r'; break;
	case 't': byte = '\t'; break;
	case 'v': byte = '\v'; break;
	default:
		if ((percent_b && byte == '0') ||
		    (!percent_b && byte >= '0' && byte <= '7')) {
			if (percent_b) {
				byte = 0;
				remaining = 3;
			} else {
				byte -= '0';
				remaining = 2;
			}
			while (remaining-- > 0 && *cursor >= '0' && *cursor <= '7')
				byte = (byte << 3) + (unsigned int)(*cursor++ - '0');
		}
		break;
	}
	*byte_pointer = (unsigned char)byte;
	*cursor_pointer = cursor;
	return (1);
}

static int
output_b(const char *operand, int width, int precision, int left_adjust,
	int *stop_pointer)
{
	const unsigned char *cursor;
	unsigned char byte;
	int ignored_stop;
	int length;
	int padding;

	cursor = (const unsigned char *)operand;
	ignored_stop = 0;
	length = 0;
	while (decode(&cursor, 1, &byte, &ignored_stop))
		if (precision < 0 || length < precision)
			length++;
	if (width < 0) {
		left_adjust = 1;
		width = -width;
	}
	padding = width > length ? width - length : 0;
	while (!left_adjust && padding-- > 0)
		if (!output_byte(' '))
			return (0);
	cursor = (const unsigned char *)operand;
	length = 0;
	while (decode(&cursor, 1, &byte, stop_pointer)) {
		if (precision < 0 || length < precision) {
			if (!output_byte(byte))
				return (0);
			length++;
		}
	}
	while (left_adjust && padding-- > 0)
		if (!output_byte(' '))
			return (0);
	return (1);
}

static int
conversion(const char **cursor_pointer, int *stop_pointer)
{
	const char *cursor;
	const char *number_start;
	const char *operand;
	char normalized[16];
	char *flag_cursor;
	char *format_cursor;
	char code;
	int argument_number;
	int left_adjust;
	int parsed;
	int precision;
	int value;
	int width;
	long parsed_number;

	cursor = *cursor_pointer;
	argument_number = 0;
	number_start = cursor;
	parsed = decimal(&cursor, &value);
	if (*cursor == '$') {
		if (parsed != 1 || value == 0)
			goto format_error;
		argument_number = value;
		cursor++;
	} else
		cursor = number_start;

	format_cursor = normalized;
	*format_cursor++ = '%';
	left_adjust = 0;
	while (*cursor != '\0' && strchr("#-+ 0", *cursor) != NULL) {
		if (*cursor == '-')
			left_adjust = 1;
		for (flag_cursor = normalized + 1; flag_cursor < format_cursor;
		    flag_cursor++)
			if (*flag_cursor == *cursor)
				break;
		if (flag_cursor == format_cursor)
			*format_cursor++ = *cursor;
		cursor++;
	}
	if (*cursor == '*') {
		cursor++;
		parsed_number = number(0);
		if (!number_valid || parsed_number < -FORMAT_LIMIT ||
		    parsed_number > FORMAT_LIMIT)
			goto range_error;
		width = (int)parsed_number;
	} else {
		parsed = decimal(&cursor, &width);
		if (parsed < 0)
			goto range_error;
		if (parsed == 0)
			width = 0;
	}
	precision = -1;
	if (*cursor == '.') {
		cursor++;
		if (*cursor == '*') {
			cursor++;
			parsed_number = number(0);
			if (!number_valid || parsed_number < -FORMAT_LIMIT ||
			    parsed_number > FORMAT_LIMIT)
				goto range_error;
			precision = (int)parsed_number;
		} else {
			parsed = decimal(&cursor, &precision);
			if (parsed < 0)
				goto range_error;
			if (parsed == 0)
				precision = 0;
		}
	}
	code = *cursor++;
	if (code == '\0' || strchr("bcsdiouxX", code) == NULL)
		goto format_error;
	*cursor_pointer = cursor;
	if (code == 'b') {
		operand = argument(argument_number);
		return (output_b(operand == NULL ? "" : operand, width, precision,
		    left_adjust, stop_pointer));
	}
	*format_cursor++ = '*';
	if (code != 'c') {
		*format_cursor++ = '.';
		*format_cursor++ = '*';
	}
	if (strchr("diouxX", code) != NULL)
		*format_cursor++ = 'l';
	*format_cursor++ = code;
	*format_cursor = '\0';
	if (code == 's') {
		operand = argument(argument_number);
		return (printf(normalized, width, precision,
		    operand == NULL ? "" : operand) >= 0);
	}
	if (code == 'c') {
		operand = argument(argument_number);
		return (printf(normalized, width,
		    operand == NULL ? 0 : (unsigned char)*operand) >= 0);
	}
	if (code == 'd' || code == 'i')
		return (printf(normalized, width, precision,
		    number(argument_number)) >= 0);
	return (printf(normalized, width, precision,
	    (unsigned long)number(argument_number)) >= 0);

range_error:
	diagnostic("field width or precision is out of range");
	return (0);
format_error:
	diagnostic("invalid format");
	return (0);
}

static int
format_escape(const char **cursor_pointer)
{
	const unsigned char *cursor;
	unsigned char byte;
	int stop;

	cursor = (const unsigned char *)*cursor_pointer;
	stop = 0;
	if (!decode(&cursor, 0, &byte, &stop))
		return (0);
	*cursor_pointer = (const char *)cursor;
	return (output_byte(byte));
}

static int
run_format(const char *format)
{
	for (;;) {
		const char *cursor;
		int stop;

		cursor = format;
		operand_next = 0;
		operand_used = 0;
		stop = 0;
		while (*cursor != '\0') {
			const char *literal;

			literal = cursor;
			while (*cursor != '\0' && *cursor != '%' && *cursor != '\\')
				cursor++;
			if (fwrite(literal, 1, (size_t)(cursor - literal), stdout) !=
			    (size_t)(cursor - literal))
				return (1);
			if (*cursor == '\\') {
				if (!format_escape(&cursor))
					return (1);
			} else if (*cursor == '%') {
				cursor++;
				if (*cursor == '%') {
					if (!output_byte('%'))
						return (1);
					cursor++;
				} else if (!conversion(&cursor, &stop))
					return (1);
				else if (stop)
					return (utility_status);
			}
		}
		if (operand_used == 0)
			return (utility_status);
		operand_base += operand_used;
		if (operand_base >= operand_count)
			return (utility_status);
	}
}

int
main(int argc, char *argv[])
{
	const char *format;
	int first_argument;
	int status;

	first_argument = 1;
	if (argc > 1 && strcmp(argv[1], "--") == 0)
		first_argument++;
	if (first_argument >= argc) {
		diagnostic("usage: printf format [argument ...]");
		return (1);
	}
	format = argv[first_argument++];
	operands = argv + first_argument;
	operand_count = argc - first_argument;
	operand_base = 0;
	utility_status = 0;
	status = run_format(format);
	if (fflush(stdout) == EOF || ferror(stdout)) {
		diagnostic("standard output write failed");
		return (1);
	}
	return (status);
}
