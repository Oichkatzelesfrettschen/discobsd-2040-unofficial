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

#ifndef SCRFMT_H
#define SCRFMT_H

/*
 * Pixel conversion and file-format writers of scrshot(1), shared by the
 * program and its host test (usr.bin/scrshot/tests). Static functions over
 * plain bytes, so the header needs no kernel type and compiles on the host.
 *
 * A panel row arrives as w RGB565 values, two bytes each, low byte first,
 * which is what read(2) on /dev/lcdgfx returns (sys/sys/lcdgfx.h). Each
 * writer turns one such row into the bytes of one image row, so a caller
 * holds one row of each at a time:
 *
 *	SCR_PPM	binary PPM (P6), 8 bits per channel, rows of w * 3 bytes in
 *		R, G, B order under a text header
 *	SCR_BMP	24-bit BMP with a negative height (rows stored top down), rows
 *		of B, G, R triples each padded with zeros to a multiple of
 *		four bytes, under a 54-byte header
 *	SCR_RAW	the RGB565 row unchanged, no header
 *
 * A component widens to eight bits by bit replication (five bits v become
 * v << 3 | v >> 2, six bits v << 2 | v >> 4), so 0x1f and 0x3f give 0xff.
 */

#define SCR_PPM		0
#define SCR_BMP		1
#define SCR_RAW		2

#define SCR_HDR_MAX	64	/* Room for the longest header. */
#define SCR_BMP_HDR	54

/* Bytes of one image row of width w in format fmt, padding included. */
static unsigned
scr_row_bytes(int fmt, unsigned w)
{
	if (fmt == SCR_PPM)
		return w * 3;
	if (fmt == SCR_BMP)
		return (w * 3 + 3) & ~3u;
	return w * 2;
}

static unsigned
scr_put_dec(unsigned char *out, unsigned v)
{
	unsigned char t[10];
	unsigned n = 0, i;

	do {
		t[n++] = (unsigned char)('0' + v % 10);
		v /= 10;
	} while (v != 0);
	for (i = 0; i < n; i++)
		out[i] = t[n - 1 - i];
	return n;
}

static unsigned
scr_put_le32(unsigned char *out, unsigned long v)
{
	out[0] = (unsigned char)(v & 0xff);
	out[1] = (unsigned char)(v >> 8 & 0xff);
	out[2] = (unsigned char)(v >> 16 & 0xff);
	out[3] = (unsigned char)(v >> 24 & 0xff);
	return 4;
}

static unsigned
scr_put_le16(unsigned char *out, unsigned v)
{
	out[0] = (unsigned char)(v & 0xff);
	out[1] = (unsigned char)(v >> 8 & 0xff);
	return 2;
}

/*
 * The header of a w x h image in out, SCR_HDR_MAX bytes at least; returns
 * its length (0 for SCR_RAW).
 */
static unsigned
scr_header(int fmt, unsigned w, unsigned h, unsigned char *out)
{
	unsigned n = 0;
	unsigned long size;

	if (fmt == SCR_PPM) {
		out[n++] = 'P';
		out[n++] = '6';
		out[n++] = '\n';
		n += scr_put_dec(out + n, w);
		out[n++] = ' ';
		n += scr_put_dec(out + n, h);
		out[n++] = '\n';
		out[n++] = '2';
		out[n++] = '5';
		out[n++] = '5';
		out[n++] = '\n';
		return n;
	}
	if (fmt != SCR_BMP)
		return 0;
	size = (unsigned long)scr_row_bytes(SCR_BMP, w) * h;
	out[n++] = 'B';
	out[n++] = 'M';
	n += scr_put_le32(out + n, SCR_BMP_HDR + size);
	n += scr_put_le32(out + n, 0);		/* Reserved. */
	n += scr_put_le32(out + n, SCR_BMP_HDR);	/* Pixel data offset. */
	n += scr_put_le32(out + n, 40);		/* BITMAPINFOHEADER. */
	n += scr_put_le32(out + n, w);
	n += scr_put_le32(out + n, 0ul - h);	/* Negative: top down. */
	n += scr_put_le16(out + n, 1);		/* Planes. */
	n += scr_put_le16(out + n, 24);		/* Bits per pixel. */
	n += scr_put_le32(out + n, 0);		/* BI_RGB. */
	n += scr_put_le32(out + n, size);
	n += scr_put_le32(out + n, 2835);	/* 72 dpi in pixels per meter. */
	n += scr_put_le32(out + n, 2835);
	n += scr_put_le32(out + n, 0);		/* Palette colors. */
	n += scr_put_le32(out + n, 0);		/* Important colors. */
	return n;
}

/*
 * One image row from w RGB565 values in in (2 w bytes) into out
 * (scr_row_bytes() bytes); returns the byte count.
 */
static unsigned
scr_row(int fmt, unsigned w, const unsigned char *in, unsigned char *out)
{
	unsigned i, v, r, g, b, n;

	if (fmt == SCR_RAW) {
		for (i = 0; i < 2 * w; i++)
			out[i] = in[i];
		return 2 * w;
	}
	n = 0;
	for (i = 0; i < w; i++) {
		v = (unsigned)in[2 * i] | (unsigned)in[2 * i + 1] << 8;
		r = v >> 11 & 0x1f;
		g = v >> 5 & 0x3f;
		b = v & 0x1f;
		r = r << 3 | r >> 2;
		g = g << 2 | g >> 4;
		b = b << 3 | b >> 2;
		if (fmt == SCR_PPM) {
			out[n++] = (unsigned char)r;
			out[n++] = (unsigned char)g;
			out[n++] = (unsigned char)b;
		} else {
			out[n++] = (unsigned char)b;
			out[n++] = (unsigned char)g;
			out[n++] = (unsigned char)r;
		}
	}
	while (fmt == SCR_BMP && (n & 3) != 0)
		out[n++] = 0;
	return n;
}

#endif /* SCRFMT_H */
