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
#include <stddef.h>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#ifdef COUNT_COMPARISONS
size_t search_comparisons;
#define BYTE_ORDER(a, b) (search_comparisons++, ((a) > (b)) - ((a) < (b)))
#else
#define BYTE_ORDER(a, b) (((a) > (b)) - ((a) < (b)))
#endif
static int period_compare(const unsigned char *a, const unsigned char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        int order = BYTE_ORDER(a[i], b[i]);
        if (order) return order;
    }
    return 0;
}
static char *
twoway_memmem(const unsigned char *h, const unsigned char *z,
    const unsigned char *n, size_t l)
{
	size_t ip, jp, k, p, ms, p0, mem, mem0;
	/* Compute maximal suffix */
	ip = -1;
	jp = 0;
	k = p = 1;
	while (jp + k < l) {
		if (BYTE_ORDER(n[ip + k], n[jp + k]) == 0) {
			if (k == p) {
				jp += p;
				k = 1;
			} else
				k++;
		} else if (BYTE_ORDER(n[ip + k], n[jp + k]) > 0) {
			jp += k;
			k = 1;
			p = jp - ip;
		} else {
			ip = jp++;
			k = p = 1;
		}
	}
	ms = ip;
	p0 = p;

	/* And with the opposite comparison */
	ip = -1;
	jp = 0;
	k = p = 1;
	while (jp + k < l) {
		if (BYTE_ORDER(n[ip + k], n[jp + k]) == 0) {
			if (k == p) {
				jp += p;
				k = 1;
			} else
				k++;
		} else if (BYTE_ORDER(n[ip + k], n[jp + k]) < 0) {
			jp += k;
			k = 1;
			p = jp - ip;
		} else {
			ip = jp++;
			k = p = 1;
		}
	}
	if (ip + 1 > ms + 1)
		ms = ip;
	else
		p = p0;

	/* Periodic needle? */
	if (period_compare(n, n + p, ms + 1)) {
		mem0 = 0;
		p = MAX(ms, l - ms - 1) + 1;
	} else
		mem0 = l - p;
	mem = 0;

	/* Search loop */
	for (;;) {
		/* If remainder of haystack is shorter than needle, done */
		if ((size_t)(z - h) < l)
			return 0;

		/* Compare right half */
		for (k = MAX(ms + 1, mem); k < l && BYTE_ORDER(n[k], h[k]) == 0; k++)
			;
		if (k < l) {
			h += k - ms;
			mem = 0;
			continue;
		}
		/* Compare left half */
		for (k = ms + 1; k > mem && BYTE_ORDER(n[k - 1], h[k - 1]) == 0; k--)
			;
		if (k <= mem)
			return (char *)h;
		h += p;
		mem = mem0;
	}
}

void *candidate_memmem(const void *hay, size_t n, const void *needle, size_t m)
{
    if (m == 0) return (void *)hay;
    if (m > n) return NULL;
    const unsigned char *h = hay;
    return twoway_memmem(h, h + n, needle, m);
}
