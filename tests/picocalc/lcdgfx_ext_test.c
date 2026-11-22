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
 * through lcd_gfx_read(), whose reply is a lead byte that is not a pixel
 * and then pixels in the COLMOD in force, two bytes under 0x55 and three
 * under 0x66, as the board returned them on 2026-11-19. The scan line advances with every lcd_gfx_scanline() and
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

static int cells_drawn;		/* Console cells drawn onto the panel. */
void lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{ (void)prow; (void)col; (void)ch; (void)attr; cells_drawn++; }
void lcd_fill_cells(int prow, int col, int n, unsigned attr)
{ (void)prow; (void)col; (void)n; (void)attr; cells_drawn++; }
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

/* The opener's effective uid: 0 is the superuser, whose open skips blanking GRAM. */
#define	OTHER_UID	1001

static unsigned cur_uid;

int
lcdcons_gfx_root(void)
{
	return cur_uid == 0;
}

/*
 * What a read selection returns while the open still clears the panel: the
 * first window the open draws stands in for a concurrent caller.
 */
static int probe_armed, probe_result;

void
lcd_gfx_window(int x0, int y0, int x1, int y1)
{
	if (probe_armed) {
		struct lcdg_read r = { 0, 0, 4, 1, LCDG_RD_RGB565 };

		probe_armed = 0;
		probe_result = lcdgfx_ioctl(LCDG_READ, (char *)&r);
	}
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

/*
 * The reply the board returned: a lead byte that is not a pixel, then the
 * n pixels of the window in the COLMOD in force (two bytes of RGB565, high
 * byte first, under 0x55; otherwise three bytes with the component in the
 * upper six bits), the window repeating for as long as bytes are clocked.
 */
void
lcd_gfx_read(int x, int y, int n, unsigned char *raw, unsigned len)
{
	unsigned k, bpp = colmod == 0x55 ? 2 : 3, v, c;

	if (!rdclk)
		violation("memory read at the write clock");
	if (win_open)
		violation("memory read inside a window");
	if (power == LCDG_PWR_SLEEP)
		violation("memory read while asleep");
	if (n < 1 || n > LCDG_READ_MAX || x < 0 || y < 0 || x + n > PW ||
	    y >= RL || len < 1 || len > LCDG_RD_RAW_BYTES(LCDG_READ_MAX)) {
		violation("memory read outside the memory or the reply buffer");
		return;
	}
	if (len < 1 + bpp * (unsigned)n)
		violation("reply shorter than the window's pixels");
	*raw++ = 0xa5;			/* The lead: not a pixel. */
	for (k = 0; k + 1 < len; k++) {
		v = gram[y][x + (int)(k / bpp) % n];
		if (bpp == 2) {
			c = (v >> 13) << 11 | ((v >> 6) & 0x3f) << 5 |
			    (v & 0x3f) >> 1;
			*raw++ = k % 2 ? c & 0xff : c >> 8;
		} else
			*raw++ = ((v >> (12 - 6 * (k % 3))) & 0x3f) << 2;
	}
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

/* A fresh model whose memory holds fill, as the console leaves it, opened as uid. */
static void
fresh_as(unsigned uid, unsigned fill)
{
	unsigned y, x;

	if (lcdgfx_isopen())
		lcdgfx_close();
	lcdterm_init();
	for (y = 0; y < RL; y++)
		for (x = 0; x < PW; x++)
			gram[y][x] = fill;
	cur_uid = uid;
	probe_result = -1;
	probe_armed = 1;
	colmod = 0x55;
	win_open = rdclk = power = npower = 0;
	nwin = win_multi = 0;
	scan_line = 0;
	scan_step = 3;
	scan_calls = 0;
	scan_glitch_call = -1;
	uiomove_fail = 0;
	expect("open", lcdgfx_open(), 0);
	probe_armed = 0;
}

static void
fresh(void)
{
	fresh_as(0, 0);
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

/*
 * Every drawing, scrolling, tile, sprite, palette, power and wait function
 * of the device answers a caller who is neither the superuser nor the
 * console's owner exactly as it answers the superuser: none checks the
 * caller. Only the memory read does.
 */
static void
test_open_to_every_user(unsigned uid)
{
	struct lcdg_rect rc = { 0, 0, 3, 3, 0x07e0 };
	struct lcdg_line ln = { 0, 0, 9, 9, 0xffff };
	struct lcdg_pixel px = { 5, 5, 0x001f };
	struct lcdg_circle ci = { 40, 40, 8, 0xf800 };
	struct lcdg_palette pl = { 3, 10, 20, 30 };
	struct lcdg_scroll sc = { 7 };
	struct lcdg_scrdef sd = { 0, 0 };
	struct lcdg_colmod cm = { LCDG_COLMOD_RGB565 };
	struct lcdg_info in;
	struct lcdg_tileset ts = { 0, 1 };
	struct lcdg_tilewin tw = { 0, 0, 1, 1 };
	struct lcdg_wait wt = { 0, 20, 0 };
	struct lcdg_window win = { 0, 0, 1, 1, LCDG_WIN_RGB565, 1, 0 };
	struct lcdg_dither dt = { 1 };
	struct lcdg_xinfo xi;
	unsigned char pix[4] = { 0, 0, 0, 0 };

	fresh_as(uid, 0);
	expect("fill", ioc(LCDG_FILL, &rc), 0);
	expect("frame", ioc(LCDG_FRAME, &rc), 0);
	expect("line", ioc(LCDG_LINE, &ln), 0);
	expect("pixel", ioc(LCDG_PIXEL, &px), 0);
	expect("circle", ioc(LCDG_CIRCLE, &ci), 0);
	expect("palette", ioc(LCDG_PALETTE, &pl), 0);
	expect("scroll", ioc(LCDG_SCROLL, &sc), 0);
	sd.bottom = LCDG_RAM_LINES - LCDG_HEIGHT;
	expect("scroll area", ioc(LCDG_SCRDEF, &sd), 0);
	expect("color mode", ioc(LCDG_COLMOD, &cm), 0);
	expect("info", ioc(LCDG_INFO, &in), 0);
	expect("window", ramwin(0, 0, 2, 2, LCDG_WIN_RGB565), 0);
	expect("window data", xfer(1, pix, 4), 0);
	expect("window in the first 320 lines", ioc(LCDG_WINDOW, &win), 0);
	expect("window data again", xfer(1, pix, 2), 0);
	expect("memory fill", ramfill(0, 320, 9, 329, 0x1234), 0);
	expect("sprite", sprite(0, 0, 2, 2, 0), 0);
	expect("sprite data", xfer(1, pix, 2), 0);
	expect("tile set", ioc(LCDG_TILESET, &ts), 0);
	expect("tile window", ioc(LCDG_TILEWIN, &tw), 0);
	expect("wait for a line", ioc(LCDG_WAITLINE, &wt), 0);
	expect("idle", pwr(LCDG_PWR_IDLE), 0);
	expect("display off", pwr(LCDG_PWR_OFF), 0);
	expect("sleep", pwr(LCDG_PWR_SLEEP), 0);
	expect("report in sleep", ioc(LCDG_XINFO, &xi), 0);
	expect("on", pwr(LCDG_PWR_ON), 0);
	expect("dither", ioc(LCDG_DITHER, &dt), 0);
	ndither = dither_on = 0;	/* test_power() counts its own probes. */
	expect("no violation", violations, 0);
}

/* The number of memory words that differ from v. */
static long
differ(unsigned v)
{
	long n = 0;
	int y, x;

	for (y = 0; y < RL; y++)
		for (x = 0; x < PW; x++)
			n += gram[y][x] != v;
	return n;
}

#define	CONSOLE	mem(0xf81f)	/* What the console and an earlier opener left. */

static void
test_access(void)
{
	unsigned short got[4];
	int cells, wins;

	/* The superuser's open skips blanking GRAM and reads the memory as it stands. */
	fresh_as(0, CONSOLE);
	expect("root open leaves every word", differ(CONSOLE), 0);
	expect("root open drew no window", nwin, 0);
	expect("root selects", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	expect("root reads the console", xfer(0, got, 8), 0);
	expect("root's pixel", got[3], 0xf81f);
	expect("root selects an off-screen line", rdsel(0, 400, 4, 1, 0), 0);
	expect("root reads it", xfer(0, got, 8), 0);
	expect("root's off-screen pixel", got[0], 0xf81f);

	/* Another user's open blanks all 480 lines before it returns. */
	fresh_as(OTHER_UID, CONSOLE);
	expect("open blanked every word, lines 0-479", differ(0), 0);
	expect("the clear is one window", nwin, 1);
	expect("a read selected during the clear is refused", probe_result, EPERM);
	expect("no violation", violations, 0);
	expect("select at the top", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	expect("read at the top", xfer(0, got, 8), 0);
	expect("blank pixel", got[0], 0);
	expect("select past the glass", rdsel(316, 479, 4, 1, LCDG_RD_RGB565), 0);
	expect("read past the glass", xfer(0, got, 8), 0);
	expect("blank off-screen pixel", got[3], 0);
	expect("draws", ramfill(0, 330, 3, 330, 0x07e0), 0);
	expect("select what it drew", rdsel(0, 330, 4, 1, LCDG_RD_RGB565), 0);
	expect("read what it drew", xfer(0, got, 8), 0);
	expect("its own pixel", got[2], 0x07e0);

	/* The descriptor carries the right: a forked or exec'd holder of it. */
	cur_uid = 0;
	expect("inherited select", rdsel(0, 330, 4, 1, LCDG_RD_RGB565), 0);
	cur_uid = OTHER_UID + 1;
	expect("inherited by a third user", rdsel(0, 330, 4, 1, 0), 0);

	/* Console text cannot draw onto the panel or into the memory. */
	cells = cells_drawn;
	wins = nwin;
	lcdterm_putc('x');
	lcdterm_putc('\n');
	expect("kernel text draws no cell", cells_drawn, cells);
	expect("kernel text draws no window", nwin, wins);

	/* The right ends with the open. */
	lcdgfx_close();
	expect("no read after the close", rdsel(0, 0, 4, 1, 0), EPERM);
	fresh_as(OTHER_UID, CONSOLE);
	expect("a second non-root open clears again", differ(0), 0);

	test_open_to_every_user(OTHER_UID);
	test_open_to_every_user(0);
	cur_uid = 0;
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

	for (fmt = 0; fmt < 2; fmt++) {
		struct lcdg_colmod cm = { (u_char)(fmt ? LCDG_COLMOD_RGB565 :
		    LCDG_COLMOD_RGB666) };
		unsigned c;

		expect("colmod", ioc(LCDG_COLMOD, &cm), 0);
		expect("raw", rdsel(3, 470, LCDG_READ_MAX, 1, LCDG_RD_RAW), 0);
		expect("raw short buffer", xfer(0, raw,
		    LCDG_RD_RAW_BYTES(LCDG_READ_MAX) - 1), EINVAL);
		memset(raw, 0x11, sizeof(raw));
		expect("raw read", xfer(0, raw, sizeof(raw)), 0);
		expect("raw length", (long)sizeof(raw) - resid_after,
		    LCDG_RD_RAW_BYTES(LCDG_READ_MAX));
		bad = raw[0] != 0xa5;
		for (i = 0; i < LCDG_READ_MAX; i++) {
			if (fmt)
				c = (unsigned)raw[1 + 2 * i] << 8 | raw[2 + 2 * i];
			else
				c = (unsigned)(raw[1 + 3 * i] >> 3) << 11 |
				    (unsigned)(raw[2 + 3 * i] >> 2) << 5 |
				    raw[3 + 3 * i] >> 3;
			bad += c != pat[470][3 + i];
		}
		expect("raw reply bytes in the format in force", bad, 0);
		if (fmt)	/* Two bytes a pixel: the line starts again. */
			expect("the window repeats", raw[1 + 2 * LCDG_READ_MAX],
			    pat[470][3] >> 8);
	}
	expect("raw is one read", xfer(0, raw, sizeof(raw)), 0);
	expect("raw then end of file", resid_after, (long)sizeof(raw));

	expect("select by the superuser", rdsel(0, 0, 4, 1, LCDG_RD_RGB565), 0);
	expect("read of the rectangle", xfer(0, got, 8), 0);
	expect("all four pixels", resid_after, 0);
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
	test_access();
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
