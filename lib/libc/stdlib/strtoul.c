/*
 * Copyright (c) 1990, 1993
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
#include <errno.h>
#include <stdlib.h>

/*
 * Convert a string to an unsigned long integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
unsigned long
strtoul(const char *string, char **end_pointer, int base)
{
	const unsigned char *cursor;
	unsigned long value, cutoff;
	unsigned char character;
	int digit, negative, converted, cutoff_digit;

	if (base != 0 && (base < 2 || base > 36)) {
		errno = EINVAL;
		if (end_pointer != NULL)
			*end_pointer = (char *)string;
		return 0;
	}

	/*
	 * See strtol for comments as to the logic used.
	 */
	cursor = (const unsigned char *)string;
	/*
	 * The library has no setlocale() implementation, so the six C-locale
	 * whitespace bytes preserve its only supported contract while keeping
	 * ctype_.o's writable lookup table out of ctype-independent processes.
	 */
	do {
		character = *cursor++;
	} while (character == ' ' || character == '\t' ||
	    character == '\n' || character == '\v' ||
	    character == '\f' || character == '\r');
	if (character == '-') {
		negative = 1;
		character = *cursor++;
	} else {
		negative = 0;
		if (character == '+')
			character = *cursor++;
	}
	if ((base == 0 || base == 16) &&
	    character == '0' && (*cursor == 'x' || *cursor == 'X') &&
	    ((cursor[1] >= '0' && cursor[1] <= '9') ||
	    (cursor[1] >= 'a' && cursor[1] <= 'f') ||
	    (cursor[1] >= 'A' && cursor[1] <= 'F'))) {
		character = cursor[1];
		cursor += 2;
		base = 16;
	} else if (base == 0)
		base = character == '0' ? 8 : 10;
	cutoff = ULONG_MAX / (unsigned long)base;
	cutoff_digit = (int)(ULONG_MAX % (unsigned long)base);
	for (value = 0, converted = 0;; character = *cursor++) {
		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (character >= 'a' && character <= 'z')
			digit = character - 'a' + 10;
		else if (character >= 'A' && character <= 'Z')
			digit = character - 'A' + 10;
		else
			break;
		if (digit >= base)
			break;
		if (converted < 0)
			continue;
		if (value > cutoff ||
		    (value == cutoff && digit > cutoff_digit)) {
			value = ULONG_MAX;
			converted = -1;
			errno = ERANGE;
		} else {
			converted = 1;
			value *= (unsigned long)base;
			value += (unsigned long)digit;
		}
	}
	if (negative && converted > 0)
		value = 0UL - value;
	if (end_pointer != NULL)
		*end_pointer = (char *)(converted ? cursor - 1 :
		    (const unsigned char *)string);
	return value;
}
