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
 * Host test of the LCD_GFX_EXT commands of /dev/lcdgfx (lcdgfx.c, built
 * with -DLCD_GFX_EXT): drawing into all 480 lines of the controller's
 * memory, the pixel read path, sprites with a transparent index, the
 * scan-line wait and the power modes.
 *
 * The model is the controller's memory, 480 lines of 320 pixels of
 * eighteen bits, written through lcd_gfx_window() and lcd_gfx_byte() in
 * the COLMOD in force (a 16-bit pixel widens by bit replication) and read
 * through lcd_gfx_read(), whose reply is a lead byte that is not a pixel,
 * three bytes a pixel with the component in the upper six bits, and one
 * spare byte. The scan line advances with every lcd_gfx_scanline() and
 * the clock with every lcd_gfx_micros(). The model records a violation for
 * a window or a read outside the memory, a byte outside a window, a window
 * inside a window, a write window at the read clock, a read or a scan-line
 * sample at the write clock and a transfer of any kind while the panel
 * sleeps. lcd_gfx_power() of st7365p.c is not compiled here: the model
 * records the mode, and the command sequence of each transition rests on
 * the kernel build and a board run.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/lcdgfx.h>

#include <rp2040/dev/lcdterm.h>
#include <rp2040/dev/lcdgfx.h>

#define	PW	LCDG_WIDTH
#define	RL	LCDG_RAM_LINES
#define	SCAN_LINES	484		/* 480 and the two porches of B5h. */

static unsigned gram[RL][PW];		/* r6 << 12 | g6 << 6 | b6. */
static int colmod, win_open, wx0, wy0, wx1, wy1, wx, wy, nacc;
static unsigned acc[3];
static int rdclk, power, npower;
static int violations, failures;
static int nwin, win_multi;		/* Windows; those of several lines. */
static int scan_line, scan_step, scan_calls, scan_glitch_call, scan_glitch;
static unsigned clock_us;
static int uiomove_fail;

static void
violation(const char *what)
{
	fprintf(stderr, "VIOLATION %s\n", what);
	/* A mutant that loops on a violation must not fill the disk. */
	if (++violations >= 20)
		exit(1);
}

const unsigned char lcd_palette[16][3] = {
	{   0,   0,   0 }, { 205,   0,   0 }, {   0, 205,   0 },
	{ 205, 205,   0 }, {  60,  90, 238 }, { 205,   0, 205 },
	{   0, 205, 205 }, { 200, 200, 200 },
	{ 127, 127, 127 }, { 255,  60,  60 }, {  60, 255,  60 },
	{ 255, 255,  60 }, { 100, 120, 255 }, { 255,  60, 255 },
	{  60, 255, 255 }, { 255, 255, 255 },
};

void lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{ (void)prow; (void)col; (void)ch; (void)attr; }
void lcd_fill_cells(int prow, int col, int n, unsigned attr)
{ (void)prow; (void)col; (void)n; (void)attr; }
void lcd_set_top(int prow) { (void)prow; }
void lcd_bell(void) { }
void lcdterm_reply(const char *s, int n) { (void)s; (void)n; }
void lcd_gfx_scroll(int line) { (void)line; }
void lcd_gfx_scrdef(int top, int bottom) { (void)top; (void)bottom; }
void lcd_gfx_colmod(unsigned fmt) { colmod = (int)fmt; }
void lcd_gfx_reset(void) { colmod = 0x55; }

static int drain_lines;		/* Text the release drains after a takeover. */

int
lcdcons_gfx(int on)
{
	int i;

	lcdterm_gfx(on);
	/* lcdcons.c drains deferred output when it releases the drawing flag. */
	for (i = 0; on && i < drain_lines; i++)
		lcdterm_putc('\n');
	return 0;
}

static int reader = 1;

int
lcdcons_gfx_reader(void)
{
	return reader;
}

void
lcd_gfx_window(int x0, int y0, int x1, int y1)
{
	if (win_open)
		violation("window inside a window");
	if (rdclk)
		violation("write window at the read clock");
	if (power == LCDG_PWR_SLEEP)
		violation("window while asleep");
	if (x0 < 0 || y0 < 0 || x1 >= PW || y1 >= RL || x0 > x1 || y0 > y1) {
		violation("window outside the memory");
		x0 = y0 = x1 = y1 = 0;
	}
	win_open = 1;
	wx0 = wx = x0;
	wy0 = wy = y0;
	wx1 = x1;
	wy1 = y1;
	nacc = 0;
	nwin++;
	if (y1 != y0)
		win_multi++;
}

void
lcd_gfx_byte(unsigned b)
{
	unsigned r, g, bl, c;

	if (!win_open) {
		violation("byte outside a window");
		return;
	}
	acc[nacc++] = b & 0xff;
	if (nacc < (colmod == 0x55 ? 2 : 3))
		return;
	nacc = 0;
	if (colmod == 0x55) {
		c = acc[0] << 8 | acc[1];
		r = c >> 11;
		g = (c >> 5) & 0x3f;
		bl = c & 0x1f;
		r = r << 1 | r >> 4;
		bl = bl << 1 | bl >> 4;
	} else {
		r = acc[0] >> 2;
		g = acc[1] >> 2;
		bl = acc[2] >> 2;
	}
	if (wy > wy1) {
		violation("pixel beyond the window");
		return;
	}
	gram[wy][wx] = r << 12 | g << 6 | bl;
	if (++wx > wx1) {
		wx = wx0;
		wy++;
	}
}

void
lcd_gfx_end(void)
{
	if (nacc != 0)
		violation("window ended inside a pixel");
	win_open = 0;
}

void
lcd_gfx_read_begin(void)
{
	if (rdclk)
		violation("read clock selected twice");
	rdclk = 1;
}

void
lcd_gfx_read_end(void)
{
	if (!rdclk)
		violation("write clock selected twice");
	rdclk = 0;
}

void
lcd_gfx_read(int x, int y, int n, unsigned char *raw)
{
	int i;

	if (!rdclk)
		violation("memory read at the write clock");
	if (win_open)
		violation("memory read inside a window");
	if (power == LCDG_PWR_SLEEP)
		violation("memory read while asleep");
	if (n < 1 || n > LCDG_READ_MAX || x < 0 || y < 0 || x + n > PW ||
	    y >= RL) {
		violation("memory read outside the memory");
		memset(raw, 0, LCDG_RD_RAW_BYTES(1));
		return;
	}
	*raw++ = 0xa5;			/* The lead: not a pixel. */
	for (i = 0; i < n; i++) {
		*raw++ = (gram[y][x + i] >> 12) << 2;
		*raw++ = ((gram[y][x + i] >> 6) & 0x3f) << 2;
		*raw++ = (gram[y][x + i] & 0x3f) << 2;
	}
	*raw = 0x5a;			/* The spare byte. */
}

int
lcd_gfx_scanline(void)
{
	if (!rdclk)
		violation("scan line read at the write clock");
	if (++scan_calls == scan_glitch_call)
		return scan_glitch;
	scan_line = (scan_line + scan_step) % SCAN_LINES;
	return scan_line;
}

unsigned
lcd_gfx_micros(void)
{
	return clock_us += 10;
}

void
lcd_gfx_power(unsigned mode)
{
	if (win_open)
		violation("power command inside a window");
	power = (int)mode;
	npower++;
}

static int ndither, dither_on;

/* The probe leaves the panel in 77h, as st7365p.c's does. */
void
lcd_gfx_dither(int on)
{
	if (win_open)
		violation("dither probe inside a window");
	if (power == LCDG_PWR_SLEEP)
		violation("dither probe while asleep");
	colmod = 0x77;
	dither_on = on;
	ndither++;
}

int
uiomove(caddr_t cp, u_int n, struct uio *uio)
{
	struct iovec *iov = uio->uio_iov;

	if (uiomove_fail)
		return EFAULT;
	if (n > uio->uio_resid || n > iov->iov_len) {
		fprintf(stderr, "FAIL uiomove asked for %u of %u\n", n,
		    uio->uio_resid);
		exit(2);
	}
	if (uio->uio_rw == UIO_WRITE)
		memcpy(cp, iov->iov_base, n);
	else
		memcpy(iov->iov_base, cp, n);
	iov->iov_base += n;
	iov->iov_len -= n;
	uio->uio_resid -= n;
	uio->uio_offset += n;
	return 0;
}

static void
expect(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

static unsigned rstate = 12345;

static unsigned
rnd(unsigned n)
{
	rstate = rstate * 1103515245 + 12345;
	return (rstate >> 16) % n;
}

static int
ioc(unsigned cmd, void *payload)
{
	return lcdgfx_ioctl(cmd, (char *)payload);
}

static int resid_after;

static int
xfer(int wr, void *data, size_t len)
{
	struct iovec iov;
	struct uio uio;
	int error;

	iov.iov_base = data;
	iov.iov_len = len;
	memset(&uio, 0, sizeof(uio));
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = (u_int)len;
	uio.uio_rw = wr ? UIO_WRITE : UIO_READ;
	error = wr ? lcdgfx_write(&uio) : lcdgfx_read(&uio);
	resid_after = (int)uio.uio_resid;
	return error;
}

static void
fresh(void)
{
	if (lcdgfx_isopen())
		lcdgfx_close();
	lcdterm_init();
	memset(gram, 0, sizeof(gram));
	colmod = 0x55;
	win_open = rdclk = power = npower = 0;
	nwin = win_multi = 0;
	scan_line = 0;
	scan_step = 3;
	scan_calls = 0;
	scan_glitch_call = -1;
	uiomove_fail = 0;
	expect("open", lcdgfx_open(), 0);
}

/* The memory value a 565 color leaves in either COLMOD. */
static unsigned
mem(unsigned c)
{
	unsigned r = c >> 11, g = (c >> 5) & 0x3f, b = c & 0x1f;

	return (r << 1 | r >> 4) << 12 | g << 6 | (b << 1 | b >> 4);
}

static int
ramfill(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_rect r = { (u_short)x0, (u_short)y0, (u_short)x1,
	    (u_short)y1, (u_short)color };

	return ioc(LCDG_RAMFILL, &r);
}

static int
ramwin(int x, int y, int w, int h, int fmt)
{
	struct lcdg_window win = { (u_short)x, (u_short)y, (u_short)w,
	    (u_short)h, (u_char)fmt, 1, 0 };

	return ioc(LCDG_RAMWIN, &win);
}

static int
rdsel(int x, int y, int w, int h, int fmt)
{
	struct lcdg_read r = { (u_short)x, (u_short)y, (u_short)w,
	    (u_short)h, (u_char)fmt };

	return ioc(LCDG_READ, &r);
}

static int
sprite(int x, int y, int w, int h, int key)
{
	struct lcdg_sprite s = { (u_short)x, (u_short)y, (u_short)w,
	    (u_short)h, (u_char)key };

	return ioc(LCDG_SPRITE, &s);
}

static int
pwr(int mode)
{
	struct lcdg_power p = { (u_char)mode };

	return ioc(LCDG_POWER, &p);
}

static void
test_hidden_lines(void)
{
	struct lcdg_rect r = { 0, 320, 10, 330, 0xffff };
	unsigned short px[8];
	int x, y, bad = 0;

	fresh();
	expect("LCDG_FILL keeps the 320-line bound", ioc(LCDG_FILL, &r), EINVAL);
	expect("fill 320-479", ramfill(0, 320, 319, 479, 0xf81f), 0);
	for (y = 0; y < RL; y++)
		for (x = 0; x < PW; x++)
			bad += gram[y][x] != (y >= 320 ? mem(0xf81f) : 0);
	expect("the fill covers lines 320-479 and no other", bad, 0);
	expect("fill line 480", ramfill(0, 479, 0, 480, 1), EINVAL);
	expect("fill column 320", ramfill(319, 0, 320, 0, 1), EINVAL);
	expect("fill reversed x", ramfill(5, 0, 4, 0, 1), EINVAL);
	expect("fill reversed y", ramfill(0, 5, 0, 4, 1), EINVAL);
	expect("fill last pixel", ramfill(319, 479, 319, 479, 0x07e0), 0);
	expect("last pixel stored", gram[479][319], mem(0x07e0));

	expect("window at 478", ramwin(316, 478, 4, 2, LCDG_WIN_RGB565), 0);
	for (x = 0; x < 8; x++)
		px[x] = (unsigned short)(0x1000 + x);
	expect("window data", xfer(1, px, sizeof(px)), 0);
	bad = 0;
	for (x = 0; x < 8; x++)
		bad += gram[478 + x / 4][316 + x % 4] != mem(0x1000 + x);
	expect("window pixels at lines 478-479", bad, 0);
	expect("window past 480", ramwin(0, 479, 1, 2, LCDG_WIN_INDEX), EINVAL);
	expect("window at line 480", ramwin(0, 480, 1, 1, LCDG_WIN_INDEX), EINVAL);
	expect("window past column 320", ramwin(319, 0, 2, 1, LCDG_WIN_INDEX),
	    EINVAL);
	expect("window of no width", ramwin(0, 0, 0, 1, LCDG_WIN_INDEX), EINVAL);
	expect("no violation", violations, 0);
}

/* Fill the memory with a random 565 pattern through two windows. */
static unsigned short pat[RL][PW];

static void
draw_pattern(int fmt)
{
	struct lcdg_colmod cm = { (u_char)fmt };
	int x, y;

	expect("colmod", ioc(LCDG_COLMOD, &cm), 0);
	for (y = 0; y < RL; y++)
		for (x = 0; x < PW; x++)
			pat[y][x] = (unsigned short)rnd(65536);
	expect("pattern window", ramwin(0, 0, PW, RL, LCDG_WIN_RGB565), 0);
	for (y = 0; y < RL; y++)
		expect("pattern row", xfer(1, pat[y], sizeof(pat[y])), 0);
}

static unsigned short got[RL * PW];

static void
read_rect(const char *what, int x, int y, int w, int h, size_t step)
{
	size_t total = (size_t)w * h * 2, done = 0, n;
	int i, bad = 0;

	expect(what, rdsel(x, y, w, h, LCDG_RD_RGB565), 0);
	while (done < total) {
		n = total - done < step ? total - done : step;
		if (xfer(0, (char *)got + done, n) != 0 || resid_after != 0) {
			expect("read", 1, 0);
			return;
		}
		done += n;
	}
	for (i = 0; i < w * h; i++)
		bad += got[i] != pat[y + i / w][x + i % w];
	expect(what, bad, 0);
	expect("end of file after the rectangle", xfer(0, got, 2), 0);
	expect("nothing moved at end of file", resid_after, 2);
}

static void
test_read(void)
{
	unsigned char raw[LCDG_RD_RAW_BYTES(LCDG_READ_MAX) + 4];
	int fmt, i, x, bad;

	for (fmt = 0; fmt < 2; fmt++) {
		fresh();
		draw_pattern(fmt ? LCDG_COLMOD_RGB565 : LCDG_COLMOD_RGB666);
		read_rect("whole memory", 0, 0, PW, RL, 4096);
		read_rect("whole glass, odd pixel counts", 0, 0, PW, 320, 2 * 37);
		read_rect("one pixel", 319, 479, 1, 1, 2);
		read_rect("two bytes a call", 7, 300, 45, 3, 2);
		for (i = 0; i < 40; i++) {
			int w = 1 + (int)rnd(PW), h = 1 + (int)rnd(12);

			x = (int)rnd((unsigned)(PW - w + 1));
			read_rect("random rectangle", x,
			    (int)rnd((unsigned)(RL - h + 1)), w, h,
			    2 * (1 + rnd(300)));
		}
	}
	expect("no rectangle: end of file", xfer(0, got, 64), 0);
	expect("no rectangle: nothing moved", resid_after, 64);

	expect("select", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	expect("odd byte count", xfer(0, got, 3), EINVAL);
	expect("read beyond the rectangle", xfer(0, got, 64), 0);
	expect("only the four pixels move", resid_after, 64 - 8);

	expect("select", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	expect("another command", ramfill(0, 0, 0, 0, pat[0][0]), 0);
	expect("abandons the read", xfer(0, got, 8), 0);
	expect("nothing moved", resid_after, 8);

	expect("select", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	uiomove_fail = 1;
	expect("copy fault", xfer(0, got, 8), EFAULT);
	uiomove_fail = 0;
	expect("clock restored after a fault", rdclk, 0);

	expect("raw", rdsel(3, 470, LCDG_READ_MAX, 1, LCDG_RD_RAW), 0);
	expect("raw short buffer", xfer(0, raw,
	    LCDG_RD_RAW_BYTES(LCDG_READ_MAX) - 1), EINVAL);
	memset(raw, 0x11, sizeof(raw));
	expect("raw read", xfer(0, raw, sizeof(raw)), 0);
	expect("raw length", (long)sizeof(raw) - resid_after,
	    LCDG_RD_RAW_BYTES(LCDG_READ_MAX));
	bad = raw[0] != 0xa5 || raw[1 + 3 * LCDG_READ_MAX] != 0x5a;
	for (i = 0; i < LCDG_READ_MAX; i++)
		bad += (unsigned)(raw[1 + 3 * i] >> 3) != pat[470][3 + i] >> 11;
	expect("raw reply bytes as they arrived", bad, 0);
	expect("raw is one read", xfer(0, raw, sizeof(raw)), 0);
	expect("raw then end of file", resid_after, (long)sizeof(raw));

	expect("select by the superuser", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	reader = 0;
	expect("read by another user", xfer(0, got, 8), EPERM);
	expect("nothing moved", resid_after, 8);
	reader = 1;
	expect("the superuser still reads it", xfer(0, got, 8), 0);
	expect("all four pixels", resid_after, 0);
	reader = 0;
	expect("select by another user", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), EPERM);
	expect("raw select by another user", rdsel(0, 0, 4, 1, LCDG_RD_RAW),
	    EPERM);
	expect("nothing to read", xfer(0, got, 8), 0);
	expect("nothing moved", resid_after, 8);
	expect("drawing stays open to every user", ramfill(0, 0, 0, 0, 1), 0);
	reader = 1;
	expect("raw of two lines", rdsel(0, 0, 4, 2, LCDG_RD_RAW), EINVAL);
	expect("raw too wide", rdsel(0, 0, LCDG_READ_MAX + 1, 1, LCDG_RD_RAW),
	    EINVAL);
	expect("format 2", rdsel(0, 0, 1, 1, 2), EINVAL);
	expect("read past line 480", rdsel(0, 479, 1, 2, 0), EINVAL);
	expect("read past column 320", rdsel(319, 0, 2, 1, 0), EINVAL);
	expect("read of no height", rdsel(0, 0, 1, 0, 0), EINVAL);
	expect("read of no width", rdsel(0, 0, 0, 1, 0), EINVAL);
	expect("no violation", violations, 0);
}

static void
test_sprite(void)
{
	static unsigned char idx[64 * 40];
	struct lcdg_palette pl;
	unsigned short pal[LCDG_NPAL];
	int trial, i, x, y, w, h, key, bad, runs, before;
	size_t done, n, step;

	fresh();
	for (i = 0; i < LCDG_NPAL; i++) {
		pl.index = (u_char)i;
		pl.r = (u_char)rnd(256);
		pl.g = (u_char)rnd(256);
		pl.b = (u_char)rnd(256);
		pal[i] = (unsigned short)((pl.r >> 3) << 11 | (pl.g >> 2) << 5 |
		    pl.b >> 3);
		expect("palette", ioc(LCDG_PALETTE, &pl), 0);
	}
	for (trial = 0; trial < 60; trial++) {
		draw_pattern(LCDG_COLMOD_RGB666);
		w = 1 + (int)rnd(64);
		h = 1 + (int)rnd(40);
		x = (int)rnd((unsigned)(PW - w + 1));
		y = (int)rnd((unsigned)(RL - h + 1));
		key = (int)rnd(LCDG_NPAL);
		runs = 0;
		for (i = 0; i < w * h; i++) {
			/* The high nibble is not part of the index. */
			idx[i] = (unsigned char)(rnd(3) ? (unsigned)key :
			    rnd(LCDG_NPAL));
			if ((idx[i] & 15) != key &&
			    (i % w == 0 || (idx[i - 1] & 15) == key))
				runs++;
			idx[i] |= (unsigned char)(rnd(16) << 4);
		}
		before = nwin;
		win_multi = 0;
		expect("sprite", sprite(x, y, w, h, key), 0);
		step = trial < 20 ? (size_t)(w * h) : 1 + rnd(200);
		for (done = 0; done < (size_t)(w * h); done += n) {
			n = (size_t)(w * h) - done < step ?
			    (size_t)(w * h) - done : step;
			expect("sprite data", xfer(1, idx + done, n), 0);
		}
		expect("one window a run", nwin - before, runs);
		expect("every window is one line", win_multi, 0);
		bad = 0;
		for (i = 0; i < w * h; i++) {
			int px = x + i % w, py = y + i / w;

			bad += gram[py][px] != ((idx[i] & 15) == key ?
			    mem(pat[py][px]) : mem(pal[idx[i] & 15]));
		}
		expect("opaque pixels drawn, keyed pixels kept", bad, 0);
		/* Nothing outside the block changed. */
		bad = 0;
		for (i = 0; i < RL * PW; i++) {
			int px = i % PW, py = i / PW;

			if (px >= x && px < x + w && py >= y && py < y + h)
				continue;
			bad += gram[py][px] != mem(pat[py][px]);
		}
		expect("nothing outside the block", bad, 0);
		expect("write beyond the block", xfer(1, idx, 4), EINVAL);
	}
	expect("sprite key 16", sprite(0, 0, 1, 1, 16), EINVAL);
	expect("sprite past line 480", sprite(0, 479, 1, 2, 0), EINVAL);
	expect("sprite past column 320", sprite(319, 0, 2, 1, 0), EINVAL);
	expect("sprite of no width", sprite(0, 0, 0, 1, 0), EINVAL);
	expect("sprite", sprite(0, 0, 4, 4, 0), 0);
	idx[0] = idx[1] = 5;
	expect("two pixels of an unfinished run", xfer(1, idx, 2), 0);
	expect("the run's window is open", win_open, 1);
	expect("another command", ramfill(0, 0, 0, 0, 0), 0);
	expect("closed the run's window", win_open, 0);
	expect("abandons the sprite", xfer(1, idx, 4), EINVAL);
	expect("no violation", violations, 0);
}

static int
waitline(int line, int ms, int *at)
{
	struct lcdg_wait w = { (u_short)line, (u_short)ms, 0xffff };
	int error = ioc(LCDG_WAITLINE, &w);

	*at = w.at;
	return error;
}

static void
test_waitline(void)
{
	int at, line, i;
	unsigned t0;

	fresh();
	for (i = 0; i < 200; i++) {
		line = (int)rnd(RL - LCDG_WAIT_SPAN);
		scan_line = (int)rnd(SCAN_LINES);
		scan_step = 1 + (int)rnd(5);
		expect("wait", waitline(line, 100, &at), 0);
		if (at < line || at >= line + LCDG_WAIT_SPAN) {
			fprintf(stderr, "FAIL wait for %d returned at %d\n",
			    line, at);
			failures++;
		}
		expect("clock restored", rdclk, 0);
	}
	/* One misread inside the span, between samples outside it. */
	scan_line = 300;
	scan_step = 1;
	scan_calls = 0;
	scan_glitch_call = 2;
	scan_glitch = 105;
	expect("wait across a misread", waitline(100, 100, &at), 0);
	expect("the misread did not end the wait", scan_calls > 100, 1);
	scan_glitch_call = -1;
	/* A falling pair inside the span is a misread too. */
	scan_line = 110;
	scan_step = SCAN_LINES - 2;
	t0 = clock_us;
	expect("falling samples time out", waitline(100, 5, &at), ETIMEDOUT);
	expect("after the time asked for", clock_us - t0 >= 5000 &&
	    clock_us - t0 < 5100, 1);
	/* A scan that does not advance outside the span. */
	scan_line = 400;
	scan_step = 0;
	t0 = clock_us;
	expect("stuck scan times out", waitline(100, 20, &at), ETIMEDOUT);
	expect("after 20 ms", clock_us - t0 >= 20000 && clock_us - t0 < 20100, 1);
	expect("clock restored after a timeout", rdclk, 0);
	scan_step = 3;
	expect("line 480", waitline(480, 10, &at), EINVAL);
	expect("no time", waitline(0, 0, &at), EINVAL);
	expect("too long", waitline(0, LCDG_WAIT_MAX_MS + 1, &at), EINVAL);
	expect("longest", waitline(0, LCDG_WAIT_MAX_MS, &at), 0);
	expect("no violation", violations, 0);
}

static void
test_power(void)
{
	struct lcdg_xinfo xi;
	unsigned short px = 0;
	int mode, at, i;

	fresh();
	for (i = 0; i < 37; i++)
		lcdterm_putc('\n');	/* Scrolls the ring before the next open. */
	lcdgfx_close();
	at = lcdterm_top() * LT_CELL_H;
	drain_lines = 3;
	expect("reopen", lcdgfx_open(), 0);
	drain_lines = 0;
	expect("the takeover's drain moved the ring", lcdterm_top() * LT_CELL_H != at, 1);
	for (i = 0; i < 5; i++)
		lcdterm_putc('\n');	/* Text the open device does not draw. */
	expect("the ring moved on", lcdterm_top() * LT_CELL_H != at, 1);
	expect("xinfo", ioc(LCDG_XINFO, &xi), 0);
	expect("ram lines", xi.ram_lines, RL);
	expect("text top is the ring at the open", xi.text_top, at);
	expect("the ring moved", xi.text_top != 0, 1);
	expect("power on", xi.power, LCDG_PWR_ON);
	expect("read max", xi.read_max, LCDG_READ_MAX);

	for (mode = LCDG_PWR_IDLE; mode <= LCDG_PWR_OFF; mode++) {
		expect("mode", pwr(mode), 0);
		expect("panel mode", power, mode);
		expect("drawing allowed", ramfill(0, 0, 1, 1, 0xffff), 0);
		expect("xinfo", ioc(LCDG_XINFO, &xi), 0);
		expect("reported", xi.power, mode);
	}
	expect("sleep", pwr(LCDG_PWR_SLEEP), 0);
	expect("panel asleep", power, LCDG_PWR_SLEEP);
	expect("fill asleep", ramfill(0, 0, 1, 1, 0), EBUSY);
	expect("window asleep", ramwin(0, 0, 1, 1, LCDG_WIN_RGB565), EBUSY);
	expect("write asleep", xfer(1, &px, 2), EBUSY);
	expect("select asleep", rdsel(0, 0, 1, 1, 0), EBUSY);
	expect("wait asleep", waitline(0, 10, &at), EBUSY);
	expect("info asleep", ioc(LCDG_XINFO, &xi), 0);
	expect("reported", xi.power, LCDG_PWR_SLEEP);
	expect("mode 4", pwr(4), EINVAL);
	expect("still asleep", power, LCDG_PWR_SLEEP);
	expect("wake", pwr(LCDG_PWR_ON), 0);
	expect("select awake", rdsel(0, 0, 1, 1, 0), 0);
	expect("read awake", xfer(0, &px, 2), 0);
	expect("the pixel of the last fill", px, 0xffff);

	/* The power command abandons a selected read like any other. */
	expect("select", rdsel(0, 0, 1, 1, 0), 0);
	expect("sleep", pwr(LCDG_PWR_SLEEP), 0);
	expect("read asleep", xfer(0, &px, 2), 0);
	expect("nothing moved", resid_after, 2);

	{
		struct lcdg_dither d = { 0 };
		struct lcdg_colmod cm = { LCDG_COLMOD_RGB565 };

		expect("wake", pwr(LCDG_PWR_ON), 0);
		expect("probe", ioc(LCDG_DITHER, &d), 0);
		expect("probe without B9h", ndither == 1 && !dither_on, 1);
		expect("the open's format is back", colmod, LCDG_COLMOD_RGB666);
		expect("colmod", ioc(LCDG_COLMOD, &cm), 0);
		d.on = 7;
		expect("probe", ioc(LCDG_DITHER, &d), 0);
		expect("probe with B9h", ndither == 2 && dither_on, 1);
		expect("the selected format is back", colmod, LCDG_COLMOD_RGB565);
		expect("sleep", pwr(LCDG_PWR_SLEEP), 0);
		expect("probe asleep", ioc(LCDG_DITHER, &d), EBUSY);
	}
	expect("close", lcdgfx_close(), 0);
	expect("close wakes the panel", power, LCDG_PWR_ON);
	expect("reopen", lcdgfx_open(), 0);
	expect("xinfo", ioc(LCDG_XINFO, &xi), 0);
	expect("a new open is on", xi.power, LCDG_PWR_ON);
	expect("no violation", violations, 0);
}

int
main(void)
{
	test_hidden_lines();
	test_read();
	test_sprite();
	test_waitline();
	test_power();
	if (failures != 0 || violations != 0) {
		fprintf(stderr, "lcdgfx_ext_test: %d failures, %d violations\n",
		    failures, violations);
		return 1;
	}
	printf("lcdgfx_ext_test: ok\n");
	return 0;
}
