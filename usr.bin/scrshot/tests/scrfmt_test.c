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
 * Host test of the scrshot format writers (usr.bin/scrshot/scrfmt.h).
 * usage: scrfmt_test OUTDIR. For each case the program writes
 * OUTDIR/<case>.<ppm|bmp|raw> through scr_header() and scr_row(), one row
 * at a time as scrshot does; check.py compares each file with bytes it
 * builds independently. The panel rows hold the RGB565 value
 * ((y * w + x) * 40503) mod 65536, which visits every value once when
 * w * h is 65,536.
 */

#include <stdio.h>
#include <stdlib.h>

#include "scrfmt.h"

struct kase {
	const char	*name;
	unsigned	w, h;
};

static const struct kase cases[] = {
	{ "w1", 1, 3 }, { "w2", 2, 3 }, { "w3", 3, 3 }, { "w4", 4, 3 },
	{ "w5", 5, 3 }, { "w7", 7, 2 }, { "sweep", 256, 256 },
	{ "panel", 320, 320 }, { "ram", 320, 480 },
};

static const unsigned short pinned[6] = {
	0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0x8410
};

static unsigned
pixel(const struct kase *k, unsigned x, unsigned y)
{
	if (k->w == 3 && k->h == 2)
		return pinned[y * 3 + x];
	return (unsigned)(((unsigned long)(y * k->w + x) * 40503ul) & 0xffff);
}

static void
die(const char *what, const char *path)
{
	fprintf(stderr, "scrfmt_test: %s: %s\n", path, what);
	exit(1);
}

static void
emit(const char *dir, const struct kase *k, int fmt, const char *ext)
{
	unsigned char head[SCR_HDR_MAX], in[640], out[964];
	char path[256];
	FILE *f;
	unsigned x, y, n, v;

	snprintf(path, sizeof(path), "%s/%s.%s", dir, k->name, ext);
	f = fopen(path, "wb");
	if (f == NULL)
		die("open", path);
	n = scr_header(fmt, k->w, k->h, head);
	if (n != 0 && fwrite(head, 1, n, f) != n)
		die("write", path);
	for (y = 0; y < k->h; y++) {
		for (x = 0; x < k->w; x++) {
			v = pixel(k, x, y);
			in[2 * x] = (unsigned char)(v & 0xff);
			in[2 * x + 1] = (unsigned char)(v >> 8);
		}
		n = scr_row(fmt, k->w, in, out);
		if (n != scr_row_bytes(fmt, k->w) || fwrite(out, 1, n, f) != n)
			die("write", path);
	}
	if (fclose(f) != 0)
		die("close", path);
}

int
main(int argc, char **argv)
{
	static const struct kase pin = { "pin", 3, 2 };
	unsigned i;

	if (argc != 2) {
		fprintf(stderr, "usage: scrfmt_test OUTDIR\n");
		return 2;
	}
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		emit(argv[1], &cases[i], SCR_PPM, "ppm");
		emit(argv[1], &cases[i], SCR_BMP, "bmp");
		emit(argv[1], &cases[i], SCR_RAW, "raw");
	}
	emit(argv[1], &pin, SCR_PPM, "ppm");
	emit(argv[1], &pin, SCR_BMP, "bmp");
	emit(argv[1], &pin, SCR_RAW, "raw");
	return 0;
}
