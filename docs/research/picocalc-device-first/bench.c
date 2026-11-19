/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Cycle-model workload for docs/research/picocalc-device-first-memory.md:
 * the kernel's swap encode (enc follows sr_encode in
 * sys/arch/rp2040/rp2040/swapram.c, counting pass with dst NULL, writing
 * pass with dst set) and swap decode (dec follows sr_expand with a
 * destination), on the vendored heatshrink encoder and the port's hsx
 * decoder. bzero and bcopy are the kernel's own routines.
 */
#include "heatshrink_encoder.h"
#include <machine/hsx_decoder.h>

static heatshrink_encoder henc;
static struct hsx_dec hdec;

int
enc(const uint8_t *src, unsigned len, uint8_t *dst, unsigned cap)
{
	unsigned in = 0, out = 0, moved = 0;
	uint8_t scratch[64], *ob;
	size_t room, n;
	HSE_poll_res pres;
	HSE_finish_res fres;

	heatshrink_encoder_reset(&henc);
	for (;;) {
		if (in < len) {
			if (heatshrink_encoder_sink(&henc, (uint8_t *)src + in,
			    len - in, &n) < 0)
				return -1;
			if (n == 0 && out == moved)
				return -1;
			in += n;
		} else {
			fres = heatshrink_encoder_finish(&henc);
			if (fres < 0)
				return -1;
			if (fres == HSER_FINISH_DONE)
				break;
		}
		moved = out;
		do {
			if (dst != NULL && out < cap) {
				ob = dst + out;
				room = cap - out;
			} else {
				ob = scratch;
				room = dst != NULL ? 1 : sizeof scratch;
			}
			pres = heatshrink_encoder_poll(&henc, ob, room, &n);
			if (pres < 0)
				return -1;
			if (ob == scratch && dst != NULL && n > 0)
				return -1;
			out += n;
		} while (pres == HSER_POLL_MORE);
	}
	return (int)out;
}

int
dec(const uint8_t *src, unsigned clen, uint8_t *dst, unsigned rlen)
{
	hsx_dec_init(&hdec, HEATSHRINK_STATIC_LOOKAHEAD_BITS);
	hsx_dec_input(&hdec, src, clen);
#ifdef BENCH_MUTANT_SHORT_DECODE
	/* Known-bad input for selftest.sh: one byte short, so run.py must fail. */
	rlen--;
#endif
	return (int)hsx_dec_run(&hdec, dst, ~0U, rlen);
}


/*
 * bzero and bcopy are the kernel's routines, copied verbatim from
 * sys/arch/rp2040/rp2040/machdep.c: the encoder's memset (through
 * dhara/compat/string.h) reaches bzero, and memcpy and the ascending memmove
 * case reach bcopy, so the cycle counts need the production word loops.
 * build.sh compares both function bodies and the UNALIGNED macro with
 * machdep.c and fails on any difference.
 */
/*
 * The two routines below keep the notice of the file they come from. It
 * follows verbatim, and build.sh fails when bench.c does not carry it.
 */
/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)machdep.c	2.4 (2.11BSD) 1999/9/13
 */
#define UNALIGNED(p, sz)	((u_int)(p) & ((sz) - 1))

void
bcopy(const void *src0, void *dst0, size_t nbytes)
{
	u_char		*dst = dst0;
	const u_char	*src = src0;
	u_int		*aligned_dst;
	const u_int	*aligned_src;

	/* dst inside (src, src + nbytes): an unsigned distance below nbytes. */
	if ((u_int)dst - (u_int)src < nbytes && dst != src) {
		dst += nbytes;
		src += nbytes;
		while (nbytes--)
			*--dst = *--src;
		return;
	}
	/* printf("bcopy (%08x, %08x, %d)\n", src0, dst0, nbytes); */
	/* If the size is small, or either SRC or DST is unaligned,
	 * then punt into the byte copy loop.  This should be rare. */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(src, sizeof(u_int)) &&
	    !UNALIGNED(dst, sizeof(u_int))) {
		aligned_dst = (u_int *)dst;
		aligned_src = (const u_int *)src;

		/* Copy 4X unsigned words at a time if possible. */
		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			nbytes -= 4 * sizeof(u_int);
		}

		/* Copy one unsigned word at a time if possible. */
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			nbytes -= sizeof(u_int);
		}

		/* Pick up any residual with a byte copier. */
		dst = (u_char *)aligned_dst;
		src = (const u_char *)aligned_src;
	}

	while (nbytes--)
		*dst++ = *src++;
}

void
bzero(void *dst0, size_t nbytes)
{
	u_char *dst;
	u_int *aligned_dst;

	dst = (u_char *)dst0;
	/*
	 * Return before the alignment prefix at length zero. Otherwise an
	 * unaligned dst writes one byte, then --nbytes wraps 0 to SIZE_MAX and
	 * the word loops below scribble the whole address space.
	 */
	if (nbytes == 0)
		return;
	while (UNALIGNED(dst, sizeof(u_int))) {
		*dst++ = 0;
		if (--nbytes == 0)
			return;
	}

	if (nbytes >= sizeof(u_int)) {
		/*
		 * If we get this far, we know that nbytes is large
		 * and dst is word-aligned.
		 */
		aligned_dst = (u_int *)dst;

		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			nbytes -= 4 * sizeof(u_int);
		}
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = 0;
			nbytes -= sizeof(u_int);
		}
		dst = (u_char *)aligned_dst;
	}

	/* Pick up the remainder with a bytewise loop. */
	while (nbytes--)
		*dst++ = 0;
}

void *memset(void *p, int c, size_t n)
{ unsigned char *q = p; while (n--) *q++ = (unsigned char)c; return p; }
void *memcpy(void *d, const void *s, size_t n)
{ bcopy(s, d, n); return d; }
void *memmove(void *d, const void *s, size_t n)
{ unsigned char *a = d; const unsigned char *b = s;
  if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
  return d; }
