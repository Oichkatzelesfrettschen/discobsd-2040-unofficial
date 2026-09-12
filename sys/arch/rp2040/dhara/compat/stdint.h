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

/*
 * Dhara is written against the C standard library, and a kernel compiled with
 * -nostdinc has none. These three headers supply only what it names, so the
 * vendored sources stay byte-identical to upstream and can be re-fetched
 * without re-applying edits. The vendored heatshrink under
 * sys/arch/rp2040/heatshrink takes the same three and adds the two it needs
 * beyond them in its own compat directory.
 */

#ifndef	_DHARA_COMPAT_STDINT_H_
#define	_DHARA_COMPAT_STDINT_H_

#include <sys/types.h>

typedef	unsigned char		uint8_t;
typedef	unsigned short		uint16_t;
typedef	unsigned int		uint32_t;

/* heatshrink's search index is an array of int16_t. */
typedef	signed char		int8_t;
typedef	short			int16_t;
typedef	int			int32_t;

#endif	/* !_DHARA_COMPAT_STDINT_H_ */
