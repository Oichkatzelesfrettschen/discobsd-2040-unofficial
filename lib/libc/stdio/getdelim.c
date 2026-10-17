/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roy Marples.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define GETDELIM_FIRST 128
#define GETDELIM_CAP_MAX ((size_t)SSIZE_MAX + 1)

static size_t
getdelim_grow(size_t capacity)
{
	if (capacity >= GETDELIM_CAP_MAX)
		return 0;
	if (capacity > GETDELIM_CAP_MAX / 2)
		return GETDELIM_CAP_MAX;
	return capacity * 2;
}

ssize_t
getdelim(char **line_pointer, size_t *capacity_pointer, int delimiter,
    FILE *stream)
{
	char *buffer;
	char *grown_buffer;
	size_t capacity;
	size_t grown_capacity;
	size_t length;
	int input_byte;

	if (line_pointer == NULL || capacity_pointer == NULL || stream == NULL) {
		errno = EINVAL;
		return -1;
	}

	buffer = *line_pointer;
	capacity = *capacity_pointer;
	if (buffer == NULL || capacity == 0) {
		grown_buffer = realloc(buffer, GETDELIM_FIRST);
		if (grown_buffer == NULL)
			return -1;
		buffer = grown_buffer;
		capacity = GETDELIM_FIRST;
		*line_pointer = buffer;
		*capacity_pointer = capacity;
	}

	length = 0;
	for (;;) {
		input_byte = fgetc(stream);
		if (input_byte == EOF)
			break;
		if (length == capacity - 1) {
			grown_capacity = getdelim_grow(capacity);
			grown_buffer = grown_capacity == 0 ? NULL :
			    realloc(buffer, grown_capacity);
			if (grown_buffer == NULL) {
				ungetc(input_byte, stream);
				buffer[length] = '\0';
				errno = ENOMEM;
				return -1;
			}
			buffer = grown_buffer;
			capacity = grown_capacity;
			*line_pointer = buffer;
			*capacity_pointer = capacity;
		}
		buffer[length++] = (char)input_byte;
		if ((unsigned char)input_byte == (unsigned char)delimiter)
			break;
	}
	buffer[length] = '\0';
	if (input_byte == EOF && (length == 0 || ferror(stream)))
		return -1;
	return (ssize_t)length;
}
