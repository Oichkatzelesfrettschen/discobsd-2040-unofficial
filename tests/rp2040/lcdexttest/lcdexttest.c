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
 * lcdexttest: on-device test of the LCD_GFX_EXT commands of /dev/lcdgfx
 * (sys/sys/lcdgfx.h), which the PICOCALC_SD kernel carries: the framing of
 * the controller's memory read, a write and read-back of all 480 lines in
 * both pixel formats, a sprite over a background, the scan-line wait and
 * the power modes with the memory checked after sleep, and the dithering
 * probe.
 *
 *	lcdexttest		run every stage, print LCDEXTTEST OK or FAILED
 *	lcdexttest shot file	write the text console's screen to file as a
 *				binary PPM (P6, 320 by 320)
 *
 * On a kernel without the commands it prints LCDEXTTEST SKIP and exits 2.
 * Regression tool, not shipped in the root manifest; the PicoCalc SD root
 * installs it under /usr/tests/rp2040. A board run is opt-in: it takes
 * over the panel and blanks it for about three seconds.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE	"/dev/lcdgfx"
#define W	LCDG_WIDTH
#define LINES	LCDG_RAM_LINES

static int fd = -1;
static int errors;
static unsigned short row[W], back[W];

static void
fail(const char *what)
{
	printf("FAIL %s (errno %d)\n", what, errno);
	errors++;
}

static long
now_us(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
	return (t.tv_sec % 1000) * 1000000L + t.tv_usec;
}

/* The pattern: every 565 bit changes within a few pixels. */
static unsigned
pat(int x, int y, int seed)
{
	unsigned v = (unsigned)(x * 2053 + y * 4099 + seed * 977);

	return (v ^ v >> 7 ^ v << 5) & 0xffff;
}

static void
colmod(int fmt)
{
	struct lcdg_colmod c;

	c.fmt = fmt;
	if (ioctl(fd, LCDG_COLMOD, &c) < 0)
		fail("LCDG_COLMOD");
}

static int
select_read(int x, int y, int w, int h, int fmt)
{
	struct lcdg_read r;

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	r.fmt = fmt;
	return ioctl(fd, LCDG_READ, &r);
}

static void
ramfill(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_rect r;

	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	r.color = color;
	if (ioctl(fd, LCDG_RAMFILL, &r) < 0)
		fail("LCDG_RAMFILL");
}

static void
ramwin(int x, int y, int w, int h)
{
	struct lcdg_window v;

	v.x = x;
	v.y = y;
	v.w = w;
	v.h = h;
	v.fmt = LCDG_WIN_RGB565;
	v.fg = 15;
	v.bg = 0;
	if (ioctl(fd, LCDG_RAMWIN, &v) < 0)
		fail("LCDG_RAMWIN");
}

/* Reply bits bit .. bit + n - 1, the first bit received being bit 0. */
static unsigned
bits(const unsigned char *raw, unsigned bit, unsigned n)
{
	unsigned v = (unsigned)raw[bit >> 3] << 8 | raw[(bit >> 3) + 1];

	return (v >> (16 - (bit & 7) - n)) & ((1U << n) - 1);
}

/*
 * Write six known pixels to the last line of the memory, which the glass
 * does not show, read the reply of the memory read as it arrives, print it,
 * and report every lead of 0 to 15 bits under which the reply decodes to
 * the pixels written. The kernel decodes with LCDG_RD_LEAD_BITS.
 */
static void
framing(const char *name)
{
	static const unsigned short known[6] = {
		0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0x8410
	};
	unsigned char raw[LCDG_RD_RAW_BYTES(6) + 2];
	unsigned lead, i, c;
	int n, hit = 0;

	ramwin(0, LINES - 1, 6, 1);
	if (write(fd, known, sizeof(known)) != (int)sizeof(known))
		fail("framing write");
	memset(raw, 0, sizeof(raw));
	if (select_read(0, LINES - 1, 6, 1, LCDG_RD_RAW) < 0) {
		fail("LCDG_READ raw");
		return;
	}
	n = read(fd, raw, LCDG_RD_RAW_BYTES(6));
	printf("%s raw reply, %d bytes:", name, n);
	for (i = 0; i < LCDG_RD_RAW_BYTES(6); i++)
		printf(" %02x", raw[i]);
	printf("\n");
	for (lead = 0; lead < 16; lead++) {
		for (i = 0; i < 6; i++) {
			c = bits(raw, lead + 24 * i, 5) << 11 |
			    bits(raw, lead + 24 * i + 8, 6) << 5 |
			    bits(raw, lead + 24 * i + 16, 5);
			if (c != known[i])
				break;
		}
		if (i == 6) {
			printf("%s framing: a lead of %u bits decodes the "
			    "six pixels\n", name, lead);
			hit = 1;
			if (lead != LCDG_RD_LEAD_BITS)
				fail("the kernel's lead differs");
		}
	}
	if (!hit)
		fail("no lead of 0-15 bits decodes the reply");
}

/* Write all 480 lines, read them back and count the pixels that differ. */
static void
verify(const char *name, int seed)
{
	long t0, t_write, t_read, bad = 0;
	int x, y, shown = 0;

	t0 = now_us();
	ramwin(0, 0, W, LINES);
	for (y = 0; y < LINES; y++) {
		for (x = 0; x < W; x++)
			row[x] = pat(x, y, seed);
		if (write(fd, row, sizeof(row)) != (int)sizeof(row)) {
			fail("pattern write");
			return;
		}
	}
	t_write = now_us() - t0;
	t0 = now_us();
	if (select_read(0, 0, W, LINES, LCDG_RD_RGB565) < 0) {
		fail("LCDG_READ");
		return;
	}
	for (y = 0; y < LINES; y++) {
		if (read(fd, back, sizeof(back)) != (int)sizeof(back)) {
			fail("pattern read");
			return;
		}
		for (x = 0; x < W; x++) {
			if (back[x] == pat(x, y, seed))
				continue;
			if (shown++ < 5)
				printf("  (%d,%d) wrote %04x read %04x\n", x, y,
				    pat(x, y, seed), back[x]);
			bad++;
		}
	}
	t_read = now_us() - t0;
	printf("%s: %ld of %ld pixels differ, write %ld us, read %ld us\n",
	    name, bad, (long)W * LINES, t_write, t_read);
	if (bad != 0) {
		errno = 0;
		fail("read-back differs from the pattern");
	}
}

/* A 16 by 16 disc of index 9 on a key of 0, over the pattern. */
static void
sprite_test(int seed)
{
	static unsigned char idx[256];
	struct lcdg_sprite s;
	int x, y, bad = 0;

	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			idx[y * 16 + x] = (x - 8) * (x - 8) + (y - 8) * (y - 8)
			    < 50 ? 9 : 0;
	s.x = 100;
	s.y = 400;
	s.w = 16;
	s.h = 16;
	s.key = 0;
	if (ioctl(fd, LCDG_SPRITE, &s) < 0 ||
	    write(fd, idx, sizeof(idx)) != (int)sizeof(idx)) {
		fail("LCDG_SPRITE");
		return;
	}
	if (select_read(100, 400, 16, 16, LCDG_RD_RGB565) < 0) {
		fail("sprite read");
		return;
	}
	for (y = 0; y < 16; y++) {
		if (read(fd, back, 32) != 32) {
			fail("sprite read");
			return;
		}
		for (x = 0; x < 16; x++) {
			if (idx[y * 16 + x] == 0)
				bad += back[x] != pat(100 + x, 400 + y, seed);
			else	/* Palette entry 9 is the console's bright red. */
				bad += back[x] != 0xf9e7;
		}
	}
	printf("sprite: %d of 256 pixels differ\n", bad);
	if (bad != 0) {
		errno = 0;
		fail("sprite");
	}
}

static void
waits(void)
{
	static const int lines[] = { 0, 100, 200, 300, 400 };
	struct lcdg_wait w;
	long t0, total;
	int i, k, ok, lo, hi;

	for (i = 0; i < 5; i++) {
		ok = 0;
		lo = 9999;
		hi = -1;
		t0 = now_us();
		for (k = 0; k < 20; k++) {
			w.line = lines[i];
			w.ms = 100;
			w.at = 0;
			if (ioctl(fd, LCDG_WAITLINE, &w) < 0)
				continue;
			ok++;
			if (w.at < lo)
				lo = w.at;
			if (w.at > hi)
				hi = w.at;
		}
		total = now_us() - t0;
		printf("wait for line %3d: %2d of 20 returned, at %d-%d, "
		    "%ld us a wait\n", lines[i], ok, lo, hi, total / 20);
		if (ok != 20 || lo < lines[i] ||
		    hi >= lines[i] + LCDG_WAIT_SPAN) {
			errno = 0;
			fail("LCDG_WAITLINE");
		}
	}
}

/* Scroll all 480 lines past the glass, each step behind the scan. */
static void
scroll_hidden(void)
{
	struct lcdg_scrdef d;
	struct lcdg_scroll s;
	struct lcdg_wait w;
	long t0 = now_us();
	int i, late = 0;

	d.top = 0;
	d.bottom = 0;
	if (ioctl(fd, LCDG_SCRDEF, &d) < 0)
		fail("LCDG_SCRDEF 0/0");
	for (i = 0; i < LINES; i += 2) {
		w.line = 0;
		w.ms = 50;
		if (ioctl(fd, LCDG_WAITLINE, &w) < 0)
			late++;
		s.line = i;
		if (ioctl(fd, LCDG_SCROLL, &s) < 0)
			fail("LCDG_SCROLL");
	}
	printf("scroll through 480 lines: 240 steps, %d without a scan "
	    "line, %ld us\n", late, now_us() - t0);
	d.bottom = LINES - LCDG_HEIGHT;
	ioctl(fd, LCDG_SCRDEF, &d);
}

static void
power(int seed)
{
	static const char *const names[] = { "on", "idle", "off", "sleep" };
	struct lcdg_power p;
	struct lcdg_xinfo xi;
	int mode, x, bad = 0;

	for (mode = LCDG_PWR_IDLE; mode <= LCDG_PWR_SLEEP; mode++) {
		p.mode = mode;
		if (ioctl(fd, LCDG_POWER, &p) < 0)
			fail("LCDG_POWER");
		if (ioctl(fd, LCDG_XINFO, &xi) < 0 || xi.power != mode)
			fail("LCDG_XINFO power");
		if (mode == LCDG_PWR_SLEEP) {
			struct lcdg_rect r;

			memset(&r, 0, sizeof(r));
			errno = 0;
			if (ioctl(fd, LCDG_RAMFILL, &r) == 0 || errno != EBUSY)
				fail("a fill in sleep was not refused with EBUSY");
		}
		printf("power %s\n", names[mode]);
		sleep(1);
	}
	p.mode = LCDG_PWR_ON;
	if (ioctl(fd, LCDG_POWER, &p) < 0)
		fail("LCDG_POWER on");
	if (select_read(0, 200, W, 1, LCDG_RD_RGB565) < 0 ||
	    read(fd, back, sizeof(back)) != (int)sizeof(back))
		fail("read after sleep");
	for (x = 0; x < W; x++)
		bad += back[x] != pat(x, 200, seed);
	/* A fill in the hidden lines, read back. */
	ramfill(5, LINES - 2, 5, LINES - 2, 0x07e0);
	if (select_read(5, LINES - 2, 1, 1, LCDG_RD_RGB565) < 0 ||
	    read(fd, back, 2) != 2 || back[0] != 0x07e0) {
		errno = 0;
		fail("a fill at line 478 did not read back");
	}
	printf("memory after sleep: %d of 320 pixels of line 200 differ\n", bad);
	if (bad != 0) {
		errno = 0;
		fail("the memory changed in sleep");
	}
}

/*
 * The dithering probe: 24-bit pixels of LCDG_DITHER_BYTE without and with
 * MODESEL (B9h), each read back raw. Bytes that differ between the two
 * replies show the controller dithering into its memory; equal replies show
 * nothing either way.
 */
static void
dither(void)
{
	unsigned char raw[2][LCDG_RD_RAW_BYTES(LCDG_DITHER_N)];
	struct lcdg_dither d;
	unsigned i;
	int on;

	for (on = 0; on < 2; on++) {
		d.on = on;
		memset(raw[on], 0, sizeof(raw[on]));
		if (ioctl(fd, LCDG_DITHER, &d) < 0 ||
		    select_read(LCDG_DITHER_X, LINES - 1, LCDG_DITHER_N, 1,
		    LCDG_RD_RAW) < 0 ||
		    read(fd, raw[on], sizeof(raw[on])) != (int)sizeof(raw[on])) {
			fail("LCDG_DITHER");
			return;
		}
		printf("24-bit %02x pixels, B9h %s:", LCDG_DITHER_BYTE,
		    on ? "02h" : "not sent");
		for (i = 0; i < sizeof(raw[on]); i++)
			printf(" %02x", raw[on][i]);
		printf("\n");
	}
	printf("dither probe: the replies %s\n",
	    memcmp(raw[0], raw[1], sizeof(raw[0])) ? "differ" : "are equal");
}

/* The console's screen: its ring starts at text_top and spans 320 lines. */
static int
shot(const char *path, const struct lcdg_xinfo *xi)
{
	unsigned char rgb[3];
	FILE *f = fopen(path, "w");
	int x, y, line;

	if (f == NULL) {
		printf("cannot write %s\n", path);
		return 1;
	}
	fprintf(f, "P6\n%d %d\n255\n", W, LCDG_HEIGHT);
	for (y = 0; y < LCDG_HEIGHT; y++) {
		line = (xi->text_top + y) % LCDG_HEIGHT;
		if (select_read(0, line, W, 1, LCDG_RD_RGB565) < 0 ||
		    read(fd, back, sizeof(back)) != (int)sizeof(back)) {
			fclose(f);
			return 1;
		}
		for (x = 0; x < W; x++) {
			rgb[0] = (back[x] >> 11) * 255 / 31;
			rgb[1] = ((back[x] >> 5) & 63) * 255 / 63;
			rgb[2] = (back[x] & 31) * 255 / 31;
			fwrite(rgb, 1, 3, f);
		}
	}
	return fclose(f) != 0;
}

int
main(int argc, char **argv)
{
	struct lcdg_xinfo xi;
	int rc;

	fd = open(DEVICE, O_RDWR);
	if (fd < 0) {
		printf("cannot open %s\n", DEVICE);
		return 1;
	}
	if (ioctl(fd, LCDG_XINFO, &xi) < 0) {
		close(fd);
		printf("LCDEXTTEST SKIP: this kernel has no LCD_GFX_EXT\n");
		return 2;
	}
	if (argc == 3 && strcmp(argv[1], "shot") == 0) {
		rc = shot(argv[2], &xi);
		close(fd);
		return rc;
	}
	printf("memory %d lines, console ring at line %d, %d pixels a read\n",
	    xi.ram_lines, xi.text_top, xi.read_max);

	framing("COLMOD 66h");
	verify("COLMOD 66h, 480 lines", 1);
	colmod(LCDG_COLMOD_RGB565);
	framing("COLMOD 55h");
	verify("COLMOD 55h, 480 lines", 2);
	sprite_test(2);
	waits();
	scroll_hidden();
	power(2);
	dither();
	colmod(LCDG_COLMOD_RGB666);
	close(fd);
	printf(errors ? "LCDEXTTEST FAILED\n" : "LCDEXTTEST OK\n");
	return errors != 0;
}
