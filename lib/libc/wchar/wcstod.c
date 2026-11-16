/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

/*
 * C17 7.29.4.1.1: strtod() for a wide string.  The subject sequence is
 * made of characters that each have a one-byte form, so the candidate
 * prefix is copied to a byte buffer and converted there, and the end
 * pointer is carried back by offset.  The copy stops at the first wide
 * character strtod() could not use, and at the buffer's capacity: digits
 * beyond 62 characters cannot change a double but would end the
 * conversion early, which callers see only as a shorter consumed length.
 */
#define WCSTOD_CAPACITY	64

double
wcstod(const wchar_t *restrict s, wchar_t **restrict end)
{
	char buffer[WCSTOD_CAPACITY];
	const wchar_t *start = s;
	char *stop;
	double value;
	size_t i;

	while (iswspace(*s))
		s++;
	for (i = 0; i < sizeof(buffer) - 1; i++) {
		wchar_t c = s[i];

		if (!(iswalnum(c) || c == L'+' || c == L'-' || c == L'.'))
			break;
		buffer[i] = (char)c;
	}
	buffer[i] = '\0';
	value = strtod(buffer, &stop);
	if (end != NULL)
		*end = (wchar_t *)(stop == buffer ? start : s + (stop - buffer));
	return value;
}
