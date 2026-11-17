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

#include <errno.h>
#include <limits.h>
#include <wchar.h>

/*
 * C17 7.29.6.4.2: convert a wide string.  A wide character above
 * UCHAR_MAX stops the conversion with EILSEQ and *src at that character.
 * Otherwise the rules of mbsrtowcs() apply.
 */
size_t
wcsrtombs(char *restrict dst, const wchar_t **restrict src, size_t n,
    mbstate_t *restrict state)
{
	const wchar_t *s = *src;
	size_t i;

	(void)state;
	for (i = 0; dst == NULL || i < n; i++) {
		if ((unsigned long)s[i] > UCHAR_MAX) {
			if (dst != NULL)
				*src = s + i;
			errno = EILSEQ;
			return (size_t)-1;
		}
		if (dst != NULL)
			dst[i] = (char)s[i];
		if (s[i] == L'\0') {
			if (dst != NULL)
				*src = NULL;
			return i;
		}
	}
	*src = s + i;
	return i;
}
