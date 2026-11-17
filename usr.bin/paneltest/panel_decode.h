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

#ifndef _PANEL_DECODE_H
#define _PANEL_DECODE_H

/*
 * Decoding of the ST7365P readback and scanline sample of the panel test
 * (sys/arch/rp2040/doc/PANELTEST.md), shared by paneltest and its host
 * test (tests/picocalc/panel_decode_test.c). Static functions over plain
 * bytes, so the header needs no kernel type.
 *
 * The board's multi-byte reads carry one dummy clock ahead of the data, so
 * a reply is the register shifted right by one bit; pd_shift() undoes it.
 * Single-byte reads (0Ah-0Fh) need no shift. Field names follow
 * ST7365P_SPEC_V1.0 pages 150-158.
 */

#include <stdio.h>
#include <string.h>

/* out[i] = raw[i] << 1 with the top bit of raw[i + 1] carried in. */
static void
pd_shift(const unsigned char *raw, int n, unsigned char *out)
{
	int i;

	for (i = 0; i < n; i++)
		out[i] = (unsigned char)(raw[i] << 1 |
		    (i + 1 < n ? raw[i + 1] >> 7 : 0));
}

struct pd_bit {
	unsigned char	byte, mask;
	const char	*name;
};

/* Append the name of every set bit in t to buf, space separated. */
static void
pd_names(char *buf, size_t len, const unsigned char *v,
    const struct pd_bit *t, int nt)
{
	size_t off = strlen(buf);
	int i;

	for (i = 0; i < nt && off < len; i++)
		if (v[t[i].byte] & t[i].mask)
			off += (size_t)snprintf(buf + off, len - off, "%s%s",
			    off > 0 ? " " : "", t[i].name);
}

static const char *
pd_bpp(unsigned f)
{
	switch (f & 7) {
	case 5: return "16bit";
	case 6: return "18bit";
	case 7: return "24bit";
	}
	return "bpp?";
}

/*
 * RDDST (09h, page 150) from the five raw reply bytes: D31-D0 after the
 * shift. RGB set is BGR order; ST15 is the vertical scroll status.
 */
static void
pd_rddst(char *buf, size_t len, const unsigned char *raw)
{
	static const struct pd_bit t1[] = {
		{ 0, 0x80, "BSTON" }, { 0, 0x40, "MY" }, { 0, 0x20, "MX" },
		{ 0, 0x10, "MV" }, { 0, 0x08, "ML" }, { 0, 0x04, "BGR" },
	};
	static const struct pd_bit t2[] = {
		{ 1, 0x08, "IDMON" }, { 1, 0x04, "PTLON" },
		{ 1, 0x02, "SLPOUT" }, { 1, 0x01, "NORON" },
		{ 2, 0x80, "SCROLL" }, { 2, 0x20, "INVON" },
		{ 2, 0x04, "DISON" }, { 2, 0x02, "TEON" }, { 3, 0x20, "TEM" },
	};
	unsigned char v[5];
	unsigned gcs;
	size_t off;

	pd_shift(raw, 5, v);
	buf[0] = '\0';
	pd_names(buf, len, v, t1, (int)(sizeof(t1) / sizeof(t1[0])));
	off = strlen(buf);
	if (off < len)
		(void)snprintf(buf + off, len - off, "%s%s", off > 0 ? " " : "",
		    pd_bpp((unsigned)v[1] >> 4));
	pd_names(buf, len, v, t2, (int)(sizeof(t2) / sizeof(t2[0])));
	gcs = (unsigned)(v[2] & 1) << 2 | (unsigned)v[3] >> 6;
	off = strlen(buf);
	if (gcs != 0 && off < len)
		(void)snprintf(buf + off, len - off, " GC%u", gcs);
}

/* RDDPM (0Ah, page 153), the first reply byte. */
static void
pd_rddpm(char *buf, size_t len, unsigned char b)
{
	static const struct pd_bit t[] = {
		{ 0, 0x80, "BSTON" }, { 0, 0x40, "IDMON" }, { 0, 0x20, "PTLON" },
		{ 0, 0x10, "SLPOUT" }, { 0, 0x08, "NORON" }, { 0, 0x04, "DISON" },
	};

	buf[0] = '\0';
	pd_names(buf, len, &b, t, (int)(sizeof(t) / sizeof(t[0])));
}

/* RDDMADCTL (0Bh, page 155), the first reply byte. */
static void
pd_madctl(char *buf, size_t len, unsigned char b)
{
	static const struct pd_bit t[] = {
		{ 0, 0x80, "MY" }, { 0, 0x40, "MX" }, { 0, 0x20, "MV" },
		{ 0, 0x10, "ML" }, { 0, 0x08, "BGR" }, { 0, 0x04, "MH" },
	};

	buf[0] = '\0';
	pd_names(buf, len, &b, t, (int)(sizeof(t) / sizeof(t[0])));
}

/* RDDCOLMOD (0Ch, page 157), the first reply byte. */
static void
pd_colmod(char *buf, size_t len, unsigned char b)
{
	(void)snprintf(buf, len, "rgb %s ctl %s", pd_bpp((unsigned)b >> 4),
	    pd_bpp(b));
}

/*
 * The frame rate of a scanline sample (struct picocalc_panel_scan).
 * wraps - 1 whole frames lie between the first and the last wrap. Each
 * wrap's time is that of the sample after it, which lags the wrap by at
 * most one sample interval plus the read in progress, so the span is off
 * by at most 2 max_gap_us, plus 2 us for the timer's 1 us steps. err_chz
 * bounds the rate's error under that span error; a span no longer than
 * twice that bound gives no rate (ok stays 0). The line count is the
 * largest line seen plus one, decoded with the dummy-clock shift (lines)
 * and without it (lines_raw); a sample interval longer than a line can
 * miss the last lines, by at most lines_slack.
 */
struct pd_scan {
	int		ok;
	unsigned long	chz;		/* Frame rate, hundredths of Hz. */
	unsigned long	err_chz;	/* Bound on its error. */
	unsigned	lines, lines_raw, lines_slack;
	unsigned	sample_us;	/* Mean interval between reads. */
};

static void
pd_scan(struct pd_scan *r, unsigned samples, unsigned wraps,
    unsigned first_us, unsigned last_us, unsigned elapsed_us,
    unsigned max_gap_us, unsigned raw_max)
{
	unsigned long long span, slop, chz;

	memset(r, 0, sizeof(*r));
	r->lines = ((raw_max >> 7) & 0xffff) + 1;
	r->lines_raw = ((raw_max >> 8) & 0xffff) + 1;
	if (samples > 0)
		r->sample_us = elapsed_us / samples;
	if (wraps < 2 || last_us <= first_us)
		return;
	span = last_us - first_us;
	slop = 2ULL * max_gap_us + 2;
	if (span <= 2 * slop)
		return;
	chz = (wraps - 1) * 100000000ULL / span;
	r->ok = 1;
	r->chz = (unsigned long)chz;
	r->err_chz = (unsigned long)((chz * slop + (span - slop) - 1) /
	    (span - slop));
	r->lines_slack = (unsigned)((unsigned long long)r->sample_us * chz *
	    r->lines / 100000000ULL) + 1;
}

#endif /* !_PANEL_DECODE_H */
