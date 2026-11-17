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
 * Host test of the panel test's readback and scanline decode
 * (usr.bin/paneltest/panel_decode.h) against the bytes the PicoCalc board
 * returned on 2026-11-17 (sys/arch/rp2040/doc/PANELTEST.md) and against
 * scanline samples whose rate and line count are known by construction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panel_decode.h"

static int failures;

static void
check_bytes(const char *what, const unsigned char *got,
    const unsigned char *want, int n)
{
	if (memcmp(got, want, (size_t)n) != 0) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static void
check_str(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("FAIL %s: \"%s\", want \"%s\"\n", what, got, want);
		failures++;
	}
}

static void
check_num(const char *what, unsigned long got, unsigned long want)
{
	if (got != want) {
		printf("FAIL %s: %lu, want %lu\n", what, got, want);
		failures++;
	}
}

static void
test_shift(void)
{
	static const unsigned char d3[4] = { 0x00, 0x3b, 0xcb, 0x00 };
	static const unsigned char d3s[4] = { 0x00, 0x77, 0x96, 0x00 };
	static const unsigned char st[5] = { 0xd2, 0x31, 0xd2, 0x00, 0x00 };
	static const unsigned char sts[5] = { 0xa4, 0x63, 0xa4, 0x00, 0x00 };
	static const unsigned char carry[3] = { 0x01, 0x80, 0xff };
	static const unsigned char carrys[3] = { 0x03, 0x01, 0xfe };
	unsigned char v[5];

	pd_shift(d3, 4, v);
	check_bytes("D3h shift gives the ST7796 ID 00 77 96", v, d3s, 4);
	pd_shift(st, 5, v);
	check_bytes("09h shift", v, sts, 5);
	pd_shift(carry, 3, v);
	check_bytes("shift carries the next byte's top bit", v, carrys, 3);
}

static void
test_names(void)
{
	static const unsigned char set0[5] = { 0xd2, 0x31, 0xd2, 0x00, 0x00 };
	static const unsigned char set19[5] = { 0x50, 0x31, 0xd2, 0x00, 0x00 };
	static const unsigned char set20[5] = { 0x42, 0x31, 0xd2, 0x00, 0x00 };
	/* Shifted 58 5c 03 60: MY MV ML, 16 bit, IDMON PTLON, TEON, TEM, GC5. */
	static const unsigned char other[5] = { 0x2c, 0x2e, 0x01, 0xb0, 0x00 };
	char buf[160];

	pd_rddst(buf, sizeof(buf), set0);
	check_str("09h set 0", buf,
	    "BSTON MX BGR 18bit SLPOUT NORON SCROLL INVON DISON");
	pd_rddst(buf, sizeof(buf), set19);
	check_str("09h set 19", buf,
	    "BSTON MX 18bit SLPOUT NORON SCROLL INVON DISON");
	pd_rddst(buf, sizeof(buf), set20);
	check_str("09h set 20", buf,
	    "BSTON BGR 18bit SLPOUT NORON SCROLL INVON DISON");
	pd_rddst(buf, sizeof(buf), other);
	check_str("09h other fields", buf,
	    "MY MV ML 16bit IDMON PTLON TEON TEM GC5");
	pd_rddpm(buf, sizeof(buf), 0x9c);
	check_str("0Ah 9Ch", buf, "BSTON SLPOUT NORON DISON");
	pd_rddpm(buf, sizeof(buf), 0x60);
	check_str("0Ah 60h", buf, "IDMON PTLON");
	pd_madctl(buf, sizeof(buf), 0x48);
	check_str("0Bh 48h", buf, "MX BGR");
	pd_madctl(buf, sizeof(buf), 0xb4);
	check_str("0Bh B4h", buf, "MY MV ML MH");
	pd_colmod(buf, sizeof(buf), 0x66);
	check_str("0Ch 66h", buf, "rgb 18bit ctl 18bit");
	pd_colmod(buf, sizeof(buf), 0x57);
	check_str("0Ch 57h", buf, "rgb 16bit ctl 24bit");
	pd_colmod(buf, sizeof(buf), 0x00);
	check_str("0Ch 00h", buf, "rgb bpp? ctl bpp?");
}

/*
 * A sample of 484-line frames at a 16650 us period (60.06 Hz): nine wraps,
 * the first at 1000 us, every read 11 us apart.
 */
static void
test_scan(void)
{
	struct pd_scan r;
	unsigned raw = 483u << 7;

	pd_scan(&r, 12200, 9, 1000, 1000 + 8 * 16650, 134250, 11, raw);
	check_num("scan ok", (unsigned long)r.ok, 1);
	/* 8 frames / 133200 us = 60.06 Hz. */
	check_num("scan rate", r.chz, 6006);
	/* 6006 * 24 / (133200 - 24) = 1.08, rounded up. */
	check_num("scan error bound", r.err_chz, 2);
	check_num("scan lines", r.lines, 484);
	check_num("scan lines unshifted", r.lines_raw, 242);
	check_num("scan read interval", r.sample_us, 11);
	/* 11 us * 60.06 Hz * 484 lines = 0.32 line, so at most one missed. */
	check_num("scan line slack", r.lines_slack, 1);

	pd_scan(&r, 3000, 9, 2000, 2000 + 8 * 8325, 70000, 900, raw);
	/* 8 / 66600 us = 120.12 Hz; 1802 us of slop: 12012 * 1802 / 64798. */
	check_num("slow-sampled scan rate", r.chz, 12012);
	check_num("slow-sampled scan bound", r.err_chz, 335);

	/* first_us differs from last_us so only the wrap count refuses it. */
	pd_scan(&r, 100, 1, 1000, 50000, 400000, 11, raw);
	check_num("one wrap gives no rate", (unsigned long)r.ok, 0);
	check_num("one wrap rate", r.chz, 0);
	pd_scan(&r, 100, 0, 0, 0, 400000, 11, 0);
	check_num("no wrap gives no rate", (unsigned long)r.ok, 0);
	check_num("no wrap lines", r.lines, 1);
	pd_scan(&r, 100, 3, 1000, 1100, 400000, 30, raw);
	check_num("a span inside the slop gives no rate", (unsigned long)r.ok,
	    0);
	pd_scan(&r, 0, 0, 0, 0, 0, 0, 0);
	check_num("no reads", r.sample_us, 0);
}

int
main(void)
{
	test_shift();
	test_names();
	test_scan();
	if (failures != 0) {
		printf("panel_decode_test: %d failures\n", failures);
		return 1;
	}
	printf("panel_decode_test: ok\n");
	return 0;
}
