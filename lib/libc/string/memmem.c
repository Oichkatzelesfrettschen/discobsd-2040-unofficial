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

/*-
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2005-2014 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <string.h>
#include <stdint.h>

#define MEMMEM_MAX(a, b) ((a) > (b) ? (a) : (b))
#ifndef MEMMEM_ORDER
#define MEMMEM_ORDER(a, b) (((a) > (b)) - ((a) < (b)))
#endif

static int
prefix_order(const unsigned char *a, const unsigned char *b, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		int order = MEMMEM_ORDER(a[i], b[i]);
		if (order)
			return order;
	}
	return 0;
}

static void *
search(const unsigned char *haystack, size_t remaining,
    const unsigned char *needle, size_t length)
{
	size_t suffix, candidate, k, period, split_suffix, saved_period;
	size_t memory, memory_after_period;

	/*
	 * SIZE_MAX represents suffix -1; suffix + 1 is the split count.
	 * Each comparison stays inside the needle: k < length - candidate.
	 */
	suffix = (size_t)-1;
	candidate = 0;
	k = period = 1;
	while (k < length - candidate) {
		int order = MEMMEM_ORDER(needle[suffix + k], needle[candidate + k]);
		if (order == 0) {
			if (k == period) {
				candidate += period;
				k = 1;
			} else
				k++;
		} else if (order > 0) {
			candidate += k;
			k = 1;
			period = candidate - suffix;
		} else {
			suffix = candidate++;
			k = period = 1;
		}
	}
	split_suffix = suffix;
	saved_period = period;

	/* The opposite byte order supplies the other maximal suffix. */
	suffix = (size_t)-1;
	candidate = 0;
	k = period = 1;
	while (k < length - candidate) {
		int order = MEMMEM_ORDER(needle[suffix + k], needle[candidate + k]);
		if (order == 0) {
			if (k == period) {
				candidate += period;
				k = 1;
			} else
				k++;
		} else if (order < 0) {
			candidate += k;
			k = 1;
			period = candidate - suffix;
		} else {
			suffix = candidate++;
			k = period = 1;
		}
	}
	if (suffix + 1 > split_suffix + 1)
		split_suffix = suffix;
	else
		period = saved_period;

	/*
	 * The selected suffix period bounds the prefix comparison within
	 * the needle. Periodic shifts preserve the matched prefix length.
	 */
	if (prefix_order(needle, needle + period, split_suffix + 1)) {
		memory_after_period = 0;
		period = MEMMEM_MAX(split_suffix, length - split_suffix - 1) + 1;
	} else
		memory_after_period = length - period;
	memory = 0;

	for (;;) {
		/* Every admitted shift stays within the remaining byte extent. */
		if (remaining < length)
			return NULL;

		/* A right mismatch advances past all excluded candidate starts. */
		for (k = MEMMEM_MAX(split_suffix + 1, memory); k < length &&
		    MEMMEM_ORDER(needle[k], haystack[k]) == 0; k++)
			;
		if (k < length) {
			size_t shift = k - split_suffix;
			haystack += shift;
			remaining -= shift;
			memory = 0;
			continue;
		}
		/* The remembered prefix already matches after a period shift. */
		for (k = split_suffix + 1; k > memory &&
		    MEMMEM_ORDER(needle[k - 1], haystack[k - 1]) == 0; k--)
			;
		if (k <= memory)
			return (void *)haystack;
		haystack += period;
		remaining -= period;
		memory = memory_after_period;
	}
}

void *
memmem(const void *haystack_pointer, size_t haystack_length,
    const void *needle_pointer, size_t needle_length)
{
	if (needle_length == 0)
		return (void *)haystack_pointer;
	if (haystack_length < needle_length)
		return NULL;
	if (needle_length == 1)
		return memchr(haystack_pointer,
		    *(const unsigned char *)needle_pointer, haystack_length);
	const unsigned char *haystack = haystack_pointer;
	const unsigned char *needle = needle_pointer;
	/* Two-byte candidates need at most two comparisons per position. */
	if (needle_length == 2) {
		unsigned char first = needle[0], second = needle[1];
		while (haystack_length >= 2) {
			if (MEMMEM_ORDER(haystack[0], first) == 0 &&
			    MEMMEM_ORDER(haystack[1], second) == 0)
				return (void *)haystack;
			haystack++;
			haystack_length--;
		}
		return NULL;
	}
	/*
	 * The optional filter subtracts pointers within ptrdiff_t's domain.
	 * Larger extents use the remaining-length search directly.
	 */
	if ((uintmax_t)haystack_length <= (uintmax_t)PTRDIFF_MAX) {
		const unsigned char *found = memchr(haystack, needle[0],
		    haystack_length - needle_length + 1);
		if (found == NULL)
			return NULL;
		haystack_length -= (size_t)(found - haystack);
		haystack = found;
	}
	/* A complete initial match returns before suffix preprocessing. */
	if (prefix_order(haystack, needle, needle_length) == 0)
		return (void *)haystack;
	/* The initial probe excludes its candidate before the bounded search. */
	haystack++;
	haystack_length--;
	if (haystack_length < needle_length)
		return NULL;
	return search(haystack, haystack_length, needle, needle_length);
}
