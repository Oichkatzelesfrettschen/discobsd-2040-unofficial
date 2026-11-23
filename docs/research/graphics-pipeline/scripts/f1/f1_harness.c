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

/* f1_harness.c -- drives the real usr.bin/i86/ems.c move() (included, so its statics are reachable) against the real mem.c.
 * Built out of tree; the repository is not modified. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "ems.c"

/* The backing file of the page store; argv[1] names it. */
static const char *store_path = "ems-store.bin";
#define STORE store_path
#define WIN_A 0xE0000u
#define WIN_B 0xE4000u
#define NB 1024

static unsigned char want[NB], old[NB];

static unsigned st(uint32_t conv, int *ok)
{
	*ok = 1;
	if (conv >= WIN_A && conv < WIN_A + 0x4000) return conv - WIN_A;
	if (conv >= WIN_B && conv < WIN_B + 0x4000) return conv - WIN_B;
	*ok = 0; return 0;
}

static void put_end(uint32_t d, uint32_t lin_addr)
{
	gwb(d, 0);                          /* type: conventional */
	gww(d + 1, 0);
	gww(d + 3, lin_addr & 15);          /* offset */
	gww(d + 5, lin_addr >> 4);          /* segment */
}

static int run(const char *name, uint32_t src, uint32_t dst, uint32_t len)
{
	uint32_t a = 0x500, i; int ok1, ok2, bad = 0; unsigned status;
	for (i = 0; i < NB; i++) { old[i] = (unsigned char)(i * 7 + (i >> 8) * 31 + 3); gwb(WIN_A + i, old[i]); }
	memcpy(want, old, NB);
	for (i = 0; i < len; i++) { unsigned s = st(src + i, &ok1), d = st(dst + i, &ok2); want[d] = old[s]; }
	gww(a, len & 0xffff); gww(a + 2, len >> 16);
	put_end(a + 4, src); put_end(a + 11, dst);
	status = move(a, 0);
	for (i = 0; i < NB; i++) if ((unsigned char)grb(WIN_A + i) != want[i]) bad++;
	printf("%-52s len %4u status %02Xh  bytes differing from the buffered result: %4d\n", name, len, status, bad);
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
	if (map(0, h, 0) != OK || map(1, h, 0) != OK) return 5;
	printf("handle %u; logical page 0 shown at E000h and E400h (two physical pages, one storage)\n", h);
	run("control  same window, dst above src, conv overlap", WIN_A, WIN_A + 256, 512);
	run("control  same window, dst below src, conv overlap", WIN_A + 256, WIN_A, 512);
	run("cross    E000->E400, storage dst above src (+256)", WIN_A, WIN_B + 256, 512);
	run("cross    E400->E000, storage dst above src (+256)", WIN_B, WIN_A + 256, 512);
	run("control  cross, storage dst below src (-256)", WIN_A + 256, WIN_B, 512);
	run("control  cross, +256 but len 256 (one chunk)", WIN_A, WIN_B + 256, 256);
	run("cross    +128 shift, len 1024", WIN_A, WIN_B + 128, 768);
	unlink(STORE);
	return 0;
}
