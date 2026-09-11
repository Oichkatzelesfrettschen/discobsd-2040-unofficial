/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_DHARA_COMPAT_STRING_H_
#define	_DHARA_COMPAT_STRING_H_

#include <sys/types.h>
#include <sys/systm.h>		/* bcopy, bzero */

/*
 * Dhara calls memcpy and memset; the kernel offers bcopy and bzero, whose
 * arguments run the other way and which cannot fill with a non-zero byte.
 */
static __inline void *
memcpy(void *d, const void *s, size_t n)
{
	bcopy(s, d, n);
	return d;
}

static __inline void *
memset(void *d, int c, size_t n)
{
	u_char *p = (u_char *)d;
	size_t i;

	if (c == 0) {
		bzero(d, n);
		return d;
	}
	for (i = 0; i < n; i++)
		p[i] = (u_char)c;
	return d;
}

#endif	/* !_DHARA_COMPAT_STRING_H_ */
