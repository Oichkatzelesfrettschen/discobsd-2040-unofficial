/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

/* f1b_harness.c -- drives the real usr.bin/i86/ems.c move() (ems.c is #included so its statics are reachable) over the real mem.c.
 * Built out of tree against two revisions; the repository is not modified. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "ems.c"

/* The backing file of the page store; argv[1] names it. */
static const char *store_path = "ems-store.bin";
#define STORE store_path
#define BASE 0xE0000u
#define PG 0x4000u
static int ppage[4];
static unsigned char old[2 * PG], want[2 * PG];
static unsigned SB;

static long st(uint32_t conv)
{
	unsigned p;
	for (p = 0; p < 4; p++)
		if (ppage[p] >= 0 && conv >= BASE + p * PG && conv < BASE + (p + 1) * PG)
			return (long)ppage[p] * PG + (conv - (BASE + p * PG));
	return -1;
}
static uint32_t conv_of(unsigned s)
{
	unsigned p;
	for (p = 0; p < 4; p++) if (ppage[p] == (int)(s / PG)) return BASE + p * PG + s % PG;
	return 0;
}
static void put_end(uint32_t d, uint32_t a)
{
	gwb(d, 0); gww(d + 1, 0); gww(d + 3, a & 15); gww(d + 5, a >> 4);
}
static int run(const char *name, uint32_t src, uint32_t dst, uint32_t len, int xchg)
{
	uint32_t a = 0x500, i; unsigned s, status; int bad = 0;
	for (s = 0; s < SB; s++) { old[s] = (unsigned char)(s * 7 + (s >> 8) * 31 + (s >> 14) * 17 + 3); gwb(conv_of(s), old[s]); }
	memcpy(want, old, SB);
	for (i = 0; i < len; i++) want[st(dst + i)] = old[st(src + i)];
	gww(a, len & 0xffff); gww(a + 2, len >> 16);
	put_end(a + 4, src); put_end(a + 11, dst);
	status = move(a, xchg);
	for (s = 0; s < SB; s++) if ((unsigned char)grb(conv_of(s)) != want[s]) bad++;
	printf("%-58s len %5u status %02Xh  bytes differing from the buffered result: %5d\n", name, len, status, bad);
	return bad;
}
int main(int argc, char **argv)
{
	if (argc > 1)
		store_path = argv[1];
	unsigned h;
	unlink(STORE);
	if (mem_init(655360u, 700, STORE) != 0) return 2;
	if (ems_init(256) != 0) return 3;
	if (alloc(2, 0, &h) != OK) return 4;
	/* Configuration 1: logical page 0 shown at E000h and E400h. */
	ppage[0] = 0; ppage[1] = 0; ppage[2] = ppage[3] = -1; SB = PG;
	if (map(0, h, 0) != OK || map(1, h, 0) != OK) return 5;
	printf("configuration 1: one logical page shown at E000h and E400h\n");
	run("same window, dst above src (linear overlap)", BASE, BASE + 256, 512, 0);
	run("same window, dst below src (linear overlap)", BASE + 256, BASE, 512, 0);
	run("cross windows, storage dst above src (+256)", BASE, BASE + PG + 256, 512, 0);
	run("cross windows, storage dst above src (+256), reverse windows", BASE + PG, BASE + 256, 512, 0);
	run("cross windows, storage dst below src (-256)", BASE + 256, BASE + PG, 512, 0);
	run("cross windows, +256, len 256 (one chunk)", BASE, BASE + PG + 256, 256, 0);
	run("cross windows, +128, len 768", BASE, BASE + PG + 128, 768, 0);
	run("cross windows, +256, exchange (57h al=1)", BASE, BASE + PG + 256, 512, 1);
	/* Configuration 2: E000h shows pages 0,1 and E800h shows pages 1,0 (a page swap). */
	if (map(0, h, 0) != OK || map(1, h, 1) != OK || map(2, h, 1) != OK || map(3, h, 0) != OK) return 6;
	ppage[0] = 0; ppage[1] = 1; ppage[2] = 1; ppage[3] = 0; SB = 2 * PG;
	printf("configuration 2: E000h shows pages 0,1; E800h shows pages 1,0\n");
	run("A[0..32K) -> B[0..32K): page swap", BASE, BASE + 2 * PG, 2 * PG, 0);
	run("B[0..32K) -> A[0..32K): page swap", BASE + 2 * PG, BASE, 2 * PG, 0);
	run("A[0..16K) -> B[0..16K): page 0 copied over page 1", BASE, BASE + 2 * PG, PG, 0);
	unlink(STORE);
	return 0;
}
