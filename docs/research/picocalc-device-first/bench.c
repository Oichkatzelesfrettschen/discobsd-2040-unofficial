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
 * decoder. The copy routines are byte loops; run.py attributes their share.
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
	return (int)hsx_dec_run(&hdec, dst, ~0U, rlen);
}


void bzero(void *p, unsigned n)
{ unsigned char *q = p; while (n--) *q++ = 0; }
void bcopy(const void *s, void *d, unsigned n)
{ unsigned char *a = d; const unsigned char *b = s;
  if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; } }
void *memset(void *p, int c, size_t n)
{ unsigned char *q = p; while (n--) *q++ = (unsigned char)c; return p; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n)
{ unsigned char *a = d; const unsigned char *b = s;
  if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
  return d; }
