/*	$OpenBSD: memmem.c,v 1.4 2015/08/31 02:53:57 guenther Exp $	*/

/*
 * Copyright (c) 2005 Pascal Gloor <pascal.gloor@spale.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>

void *
memmem(const void *haystack_pointer, size_t haystack_length,
    const void *needle_pointer, size_t needle_length)
{
	const unsigned char *candidate;
	const unsigned char *last_candidate;
	const unsigned char *haystack = haystack_pointer;
	const unsigned char *needle = needle_pointer;

	if (needle_length == 0)
		return (void *)haystack;
	if (haystack_length < needle_length)
		return NULL;
	if (needle_length == 1)
		return memchr(haystack, needle[0], haystack_length);

	last_candidate = haystack + haystack_length - needle_length;
	for (candidate = haystack; candidate <= last_candidate; candidate++)
		if (candidate[0] == needle[0] &&
		    memcmp(candidate, needle, needle_length) == 0)
			return (void *)candidate;
	return NULL;
}
