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
 * Admission rules for the SwapRAM pool, measured by run.py under the
 * Cortex-M0+ cycle model of docs/research/swap-admission-early-stop.md.
 *
 * enc() is the counting pass of sr_encode() in
 * sys/arch/rp2040/rp2040/swapram.c (dst NULL): it returns the length the
 * input compresses to. enc_stop() is the same loop with two exits a
 * kernel version would add: a byte budget (rule E1, abort once the output
 * passes limit) and a running ratio (rule R, abort once at least probe
 * input bytes have been encoded and the output exceeds 15/16 of them).
 * The encoder keeps up to one lookahead of sunk input unencoded, so the
 * ratio compares the output with the bytes the encoder has consumed,
 * in - (input_size - match_scan_index), not with the bytes sunk.
 * enc_prefix() is the separate probe pass (rule P): encode and finish the
 * first n bytes, then judge them. entropy_q4() is the byte-histogram
 * estimate (rule H) over the first n bytes, in sixteenths of a bit per
 * byte; its counts live in a caller buffer, which a kernel would take
 * from the codec workspace the encoder is not yet using. same_filled()
 * (rule S) reports an input of one repeated 32-bit word. largest_run()
 * is the free-run pre-check (rule E0), the loop of
 * swapram_pool_largest() in sys/arch/rp2040/rp2040/swapram_pool.c.
 */
#include <sys/types.h>
#include "heatshrink_encoder.h"

static heatshrink_encoder henc;

#define RATIO_NUM	15	/* refuse above 15/16 of the input */
#define RATIO_DEN	16

int
enc(const uint8_t *src, unsigned len)
{
	unsigned in = 0, out = 0, moved = 0;
	uint8_t scratch[64];
	size_t n;
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
			pres = heatshrink_encoder_poll(&henc, scratch,
			    sizeof scratch, &n);
			if (pres < 0)
				return -1;
			out += n;
		} while (pres == HSER_POLL_MORE);
	}
	return (int)out;
}

/*
 * As enc(), returning -2 once the output passes limit and -3 once the
 * consumed input reaches probe with the output above RATIO_NUM/RATIO_DEN
 * of it. probe 0 disables the ratio exit and limit ~0U the budget.
 * RULES_NO_BUDGET and RULES_NO_RATIO compile one exit alone, which
 * build.sh uses to size each.
 */
int
enc_stop(const uint8_t *src, unsigned len, unsigned limit, unsigned probe)
{
	unsigned in = 0, out = 0, moved = 0;
	uint8_t scratch[64];
	size_t n;
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
			pres = heatshrink_encoder_poll(&henc, scratch,
			    sizeof scratch, &n);
			if (pres < 0)
				return -1;
			out += n;
		} while (pres == HSER_POLL_MORE);
#ifndef RULES_MUTANT_NO_STOP
#ifndef RULES_NO_BUDGET
		if (out > limit)
			return -2;
#else
		(void)limit;
#endif
#ifndef RULES_NO_RATIO
		if (probe != 0) {
			unsigned done = in -
			    (henc.input_size - henc.match_scan_index);

			if (done >= probe &&
			    out * RATIO_DEN > done * RATIO_NUM)
				return -3;
		}
#else
		(void)probe;
#endif
#else
		/* Known-bad build for selftest.sh: no early exit at all. */
		(void)limit;
		(void)probe;
#endif
	}
	return (int)out;
}

/* 1 when the first n bytes compress above RATIO_NUM/RATIO_DEN, else 0. */
int
enc_prefix(const uint8_t *src, unsigned len, unsigned n)
{
	int c;

	if (n > len)
		n = len;
	c = enc(src, n);
	if (c < 0)
		return -1;
	return (unsigned)c * RATIO_DEN > n * RATIO_NUM;
}

/*
 * log2(x) in sixteenths for 1 <= x < 65536: the integer part from the
 * highest set bit, the fraction from a 16-entry table of
 * 16 * log2(1 + m / 16).
 */
static const uint8_t log2frac[16] = {
	0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15
};

static unsigned
log2_q4(unsigned x)
{
	unsigned k = 0;

	while ((x >> k) > 1)
		k++;
	return k * 16 + log2frac[((x << 4) >> k) & 15];
}

/*
 * Order-0 byte entropy of the first n bytes (n at most 65535) in
 * sixteenths of a bit per byte: n * log2(n) less the sum of
 * c * log2(c) over the 256 counts, divided by n.
 */
unsigned
entropy_q4(const uint8_t *src, unsigned len, unsigned n, uint16_t *counts)
{
	unsigned i, sum = 0;

	if (n > len)
		n = len;
	if (n == 0)
		return 0;
	for (i = 0; i < 256; i++)
		counts[i] = 0;
	for (i = 0; i < n; i++)
		counts[src[i]]++;
	for (i = 0; i < 256; i++)
		if (counts[i] > 1)
			sum += counts[i] * log2_q4(counts[i]);
	return (n * log2_q4(n) - sum) / n;
}

/* 1 when len bytes at a word-aligned src repeat one 32-bit word. */
int
same_filled(const uint8_t *src, unsigned len)
{
	const uint32_t *w = (const uint32_t *)src;
	unsigned i, words = len / 4;

	if (len == 0 || (len & 3) != 0)
		return 0;
	for (i = 1; i < words; i++)
		if (w[i] != w[0])
			return 0;
	return 1;
}

/* The largest free run among n run sizes. */
unsigned
largest_run(const uint16_t *size, unsigned n)
{
	unsigned i, best = 0;

	for (i = 0; i < n; i++)
		if (size[i] > best)
			best = size[i];
	return best;
}
