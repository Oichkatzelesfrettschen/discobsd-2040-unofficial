/*
 * Copyright (c) 2022 Christopher Hettrick <chris@structfoo.com>
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

#include <syscall.h>

#define	ENTRY(x) \
	.text; \
	.align	2; \
	.thumb_func; \
	.globl	x; \
	.type	x, %function; \
x:

#define	END(x) \
	.size	x, . - x

#ifdef DISCOBSD_SHARED_LIBC
#if DISCOBSD_SHARED_LIBC != 1
#error DISCOBSD_SHARED_LIBC requires value 1
#endif
/* r9 names the process-local GOT; its errno cell holds the writable address. */
#define SYS_ERRNO_ADDRESS \
	ldr	r1, 2f; \
	mov	r2, r9; \
	ldr	r1, [r2, r1]
#define SYS_ERRNO_LITERAL \
	.align 2; \
2:	.word errno(GOT)
#else
#define SYS_ERRNO_LITERAL
#define SYS_ERRNO_ADDRESS \
	ldr	r1, =errno
#endif

#define SYS_ERROR \
	SYS_ERRNO_ADDRESS; \
	str	r0, [r1]; \
	mov	r0, #0; \
	mvn	r0, r0; \
	bx	lr; \
	SYS_ERRNO_LITERAL

#define	SYS(x) \
	ENTRY(x); \
	svc	#SYS_##x; \
	bcs	1f; \
	bx	lr; \
1:	SYS_ERROR; \
	END(x)
