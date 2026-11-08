/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Host test of the /dev/lcdgfx pixel device (sys/arch/rp2040/dev/lcdgfx.c)
 * together with the console terminal it takes the panel from (lcdterm.c),
 * compiled unchanged against the shim headers in shim/.
 *
 * The model panel has two layers. The text layer is the one lcdterm_test.c
 * uses: one character, attribute and set of glyph rows per physical cell
 * and the ring's scroll start. The pixel layer models the controller's
 * write path: a 320x320 array of pixels, a write window set by
 * lcd_gfx_window() that fills row by row as bytes arrive, the COLMOD format
 * that decides how many bytes make a pixel (three, six bits kept of each, or
 * two), the scroll start (VSCSAD) and the scroll definition (VSCRDEF). The
 * model records a protocol violation, and the test fails on any, for a
 * window or a pixel outside the panel, a byte outside a window, a command
 * issued inside an open window, a window left half-filled by anything but
 * an abandoned stream, and a scroll start outside the scroll area.
 *
 * The model also logs every window with the pixel count it received, so a
 * tile row is checked as one window of the right geometry; the pixel layer
 * stores each pixel at the window's raster position, so a tile row that
 * does not interleave its pixel lines lands in the wrong places.
 *
 * The pixel layer is the oracle for the drawing commands: a fill is a
 * rectangle and a frame its outline, each pixel drawn once; a line is
 * checked against the ideal line (the major axis advances one pixel per
 * step and the minor coordinate stays within half a pixel of the real
 * line), a circle against its radius and its eightfold symmetry, so no
 * check repeats the code under test.
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

#define	PW		LCDG_WIDTH
#define	PH		LCDG_HEIGHT
#define	TAG_666		(1U << 24)	/* A pixel of three 6-bit components. */
#define	TAG_565		(2U << 24)	/* A pixel of two bytes. */
#define	TAG_BLANK	(3U << 24)	/* The text console's cleared margin. */
#define	CHUNK_MAX	64		/* lcdgfx_write()'s stack buffer. */

_Static_assert(sizeof(struct lcdg_rect) <= 12 && sizeof(struct lcdg_line) <= 12 &&
    sizeof(struct lcdg_pixel) <= 12 && sizeof(struct lcdg_circle) <= 12 &&
    sizeof(struct lcdg_window) <= 12 && sizeof(struct lcdg_palette) <= 12 &&
    sizeof(struct lcdg_scroll) <= 12 && sizeof(struct lcdg_scrdef) <= 12 &&
    sizeof(struct lcdg_colmod) <= 12 && sizeof(struct lcdg_info) <= 12 &&
    sizeof(struct lcdg_tileset) <= 12 && sizeof(struct lcdg_tilewin) <= 12,
    "every payload is at most twelve bytes");

/* The model's palette stands in for the console's: distinct, so a swapped index shows. */
const unsigned char lcd_palette[16][3] = {
	{ 0, 0, 0 }, { 205, 0, 0 }, { 0, 205, 0 }, { 205, 205, 0 },
	{ 60, 90, 238 }, { 205, 0, 205 }, { 0, 205, 205 }, { 200, 200, 200 },
	{ 127, 127, 127 }, { 255, 60, 60 }, { 60, 255, 60 }, { 255, 255, 60 },
	{ 100, 120, 255 }, { 255, 60, 255 }, { 60, 255, 255 }, { 255, 255, 255 },
};

/* The text layer. */
static unsigned char panel_ch[LT_ROWS][LT_COLS];
static unsigned char panel_at[LT_ROWS][LT_COLS];
static unsigned char panel_bits[LT_ROWS][LT_COLS][LT_GLYPH_ROWS];
static int text_top;
static int ndraw, nfill, ntop;

/* The pixel layer. */
static unsigned fb[PH][PW];
static unsigned panel_colmod = 0x66;
static int def_top, def_bottom = 160;	/* VSCRDEF fixed areas. */
static int vscsad;
static int nwindow, nscroll, nscrdef, ncolmod, nreset;
static long npixels;			/* Pixels stored since the last clear. */
static int win_open, wx0, wy0, wx1, wy1, wcx, wcy, wnb;
#define	WLOG_MAX	64
static struct {
	int x0, y0, x1, y1;
	long pixels;
	int complete;
} wlog[WLOG_MAX];
static int wlog_n;			/* Windows logged since counters_clear(). */
static unsigned wacc[3];
static long win_pixels;			/* Pixels the last window received. */
static int win_complete;		/* The last window received all of them. */
static int last_x0, last_y0, last_x1, last_y1;
static int violations;
static int failures;

/* The console side of the switch. */
static int gfx_calls;			/* lcdcons_gfx() calls, either way. */
static int gfx_busy;			/* Make lcdcons_gfx(1) fail with EAGAIN. */

/* The uio side. */
static int uiomove_fail_after = -1;
static int uiomove_calls;
static unsigned uiomove_max;

static void
violation(const char *what)
{
	fprintf(stderr, "FAIL model violation: %s\n", what);
	violations++;
}

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	const unsigned char *glyph;

	if (prow < 0 || prow >= LT_ROWS || col < 0 || col >= LT_COLS) {
		fprintf(stderr, "draw outside panel: %d,%d\n", prow, col);
		exit(2);
	}
	ndraw++;
	panel_ch[prow][col] = (unsigned char)ch;
	panel_at[prow][col] = (unsigned char)attr;
	glyph = lcdterm_glyph(ch);
	if (glyph != NULL)
		memcpy(panel_bits[prow][col], glyph, LT_GLYPH_ROWS);
	else
		memset(panel_bits[prow][col], 0, LT_GLYPH_ROWS);
}

void
lcd_fill_cells(int prow, int col, int n, unsigned attr)
{
	if (prow < 0 || prow >= LT_ROWS || col < 0 || n < 0 ||
	    col + n > LT_COLS) {
		fprintf(stderr, "fill outside panel: %d,%d+%d\n", prow, col, n);
		exit(2);
	}
	nfill++;
	while (n-- > 0) {
		panel_ch[prow][col] = ' ';
		memset(panel_bits[prow][col], 0, LT_GLYPH_ROWS);
		panel_at[prow][col++] = (unsigned char)attr;
	}
}

void
lcd_gfx_scroll(int line)
{
	if (win_open)
		violation("scroll start programmed inside a window");
	if (line < def_top || line >= LCDG_RAM_LINES - def_bottom)
		violation("scroll start outside the scroll area");
	vscsad = line;
	nscroll++;
}

void
lcd_set_top(int prow)
{
	ntop++;
	text_top = prow;
	lcd_gfx_scroll(prow * LT_CELL_H);
}

void
lcd_bell(void)
{
}

void
lcdterm_reply(const char *s, int n)
{
	(void)s;
	(void)n;
}

void
lcd_gfx_window(int x0, int y0, int x1, int y1)
{
	if (win_open)
		violation("window opened inside a window");
	if (x0 < 0 || y0 < 0 || x0 > x1 || y0 > y1 || x1 >= PW || y1 >= PH) {
		fprintf(stderr, "FAIL window outside the panel: %d,%d..%d,%d\n",
		    x0, y0, x1, y1);
		exit(2);
	}
	if (wlog_n < WLOG_MAX) {
		wlog[wlog_n].x0 = x0;
		wlog[wlog_n].y0 = y0;
		wlog[wlog_n].x1 = x1;
		wlog[wlog_n].y1 = y1;
		wlog[wlog_n].pixels = 0;
		wlog[wlog_n].complete = 0;
	}
	wlog_n++;
	win_open = 1;
	wx0 = last_x0 = x0;
	wy0 = last_y0 = y0;
	wx1 = last_x1 = x1;
	wy1 = last_y1 = y1;
	wcx = x0;
	wcy = y0;
	wnb = 0;
	win_pixels = 0;
	win_complete = 0;
	nwindow++;
}

void
lcd_gfx_byte(unsigned b)
{
	int per = panel_colmod == 0x66 ? 3 : 2;

	if (!win_open) {
		violation("pixel byte outside a window");
		return;
	}
	if (b > 0xff)
		violation("pixel byte wider than eight bits");
	wacc[wnb++] = b & 0xff;
	if (wnb < per)
		return;
	wnb = 0;
	if (wcy > wy1) {
		fprintf(stderr, "FAIL pixel past the end of the window\n");
		exit(2);
	}
	if (per == 3)
		fb[wcy][wcx] = TAG_666 | (wacc[0] >> 2) << 12 |
		    (wacc[1] >> 2) << 6 | wacc[2] >> 2;
	else
		fb[wcy][wcx] = TAG_565 | wacc[0] << 8 | wacc[1];
	npixels++;
	win_pixels++;
	if (++wcx > wx1) {
		wcx = wx0;
		wcy++;
	}
}

void
lcd_gfx_end(void)
{
	if (!win_open) {
		violation("window ended twice");
		return;
	}
	if (wnb != 0)
		violation("window ended inside a pixel");
	win_complete = wcy > wy1;
	if (wlog_n >= 1 && wlog_n <= WLOG_MAX) {
		wlog[wlog_n - 1].pixels = win_pixels;
		wlog[wlog_n - 1].complete = win_complete;
	}
	win_open = 0;
}

void
lcd_gfx_scrdef(int top, int bottom)
{
	if (win_open)
		violation("scroll definition programmed inside a window");
	if (top < 0 || bottom < 0 || top + bottom > LCDG_RAM_LINES)
		violation("scroll definition outside the controller");
	def_top = top;
	def_bottom = bottom;
	nscrdef++;
}

void
lcd_gfx_colmod(unsigned fmt)
{
	if (win_open)
		violation("color format programmed inside a window");
	panel_colmod = fmt;
	ncolmod++;
}

void
lcd_gfx_reset(void)
{
	int y;

	if (win_open)
		violation("text restore inside a window");
	panel_colmod = 0x66;
	def_top = 0;
	def_bottom = LCDG_RAM_LINES - PH;
	for (y = 0; y < PH; y++)
		fb[y][PW - 1] = fb[y][PW - 2] = TAG_BLANK;
	nreset++;
}

int
lcdcons_gfx(int on)
{
	gfx_calls++;
	if (on && gfx_busy)
		return EAGAIN;
	lcdterm_gfx(on);
	return 0;
}

int
uiomove(caddr_t cp, u_int n, struct uio *uio)
{
	struct iovec *iov = uio->uio_iov;

	if (uiomove_fail_after >= 0 && uiomove_calls++ >= uiomove_fail_after)
		return EFAULT;
	if (n > uiomove_max)
		uiomove_max = n;
	if (n > uio->uio_resid || n > iov->iov_len || uio->uio_rw != UIO_WRITE) {
		fprintf(stderr, "FAIL uiomove asked for %u of %u\n", n,
		    uio->uio_resid);
		exit(2);
	}
	memcpy(cp, iov->iov_base, n);
	iov->iov_base += n;
	iov->iov_len -= n;
	uio->uio_resid -= n;
	uio->uio_offset += n;
	return 0;
}

static void
expect_int(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

/* The pixel a color is expected to leave under COLMOD 0x66 and 0x55. */
static unsigned
exp666(unsigned c)
{
	unsigned r = c >> 11, g = (c >> 5) & 0x3f, b = c & 0x1f;

	return TAG_666 | (r << 1 | r >> 4) << 12 | g << 6 | (b << 1 | b >> 4);
}

static unsigned
exp565(unsigned c)
{
	return TAG_565 | c;
}

static unsigned
rgb565(unsigned r, unsigned g, unsigned b)
{
	return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
}

static void
fb_clear(void)
{
	memset(fb, 0, sizeof(fb));
	npixels = 0;
}

static void
text_clear(void)
{
	memset(panel_ch, 0, sizeof(panel_ch));
	memset(panel_at, 0, sizeof(panel_at));
	memset(panel_bits, 0, sizeof(panel_bits));
}

static void
counters_clear(void)
{
	ndraw = nfill = ntop = 0;
	nwindow = nscroll = nscrdef = ncolmod = nreset = 0;
	gfx_calls = 0;
	wlog_n = 0;
}

/* A fresh device, terminal and model. */
static void
reset(void)
{
	if (lcdgfx_isopen())
		lcdgfx_close();
	text_clear();
	lcdterm_init();
	fb_clear();
	panel_colmod = 0x66;
	def_top = 0;
	def_bottom = 160;
	vscsad = 0;
	win_open = 0;
	gfx_busy = 0;
	uiomove_fail_after = -1;
	uiomove_calls = 0;
	counters_clear();
}

static void
out(const char *s)
{
	while (*s != '\0')
		lcdterm_putc((unsigned char)*s++);
}

static int
ioc(unsigned cmd, void *payload)
{
	return lcdgfx_ioctl(cmd, (char *)payload);
}

static void
expect_err(const char *what, int err, int want)
{
	if (err != want) {
		fprintf(stderr, "FAIL %s: error %d, want %d\n", what, err, want);
		failures++;
	}
}

static int
fill(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_rect r = { (u_short)x0, (u_short)y0, (u_short)x1,
	    (u_short)y1, (u_short)color };

	return ioc(LCDG_FILL, &r);
}

static int
pixel(int x, int y, unsigned color)
{
	struct lcdg_pixel p = { (u_short)x, (u_short)y, (u_short)color };

	return ioc(LCDG_PIXEL, &p);
}

static int
line(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_line l = { (u_short)x0, (u_short)y0, (u_short)x1,
	    (u_short)y1, (u_short)color };

	return ioc(LCDG_LINE, &l);
}

static int
frame(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_rect r = { (u_short)x0, (u_short)y0, (u_short)x1,
	    (u_short)y1, (u_short)color };

	return ioc(LCDG_FRAME, &r);
}

static int
circle(int x, int y, int r, unsigned color)
{
	struct lcdg_circle c = { (u_short)x, (u_short)y, (u_short)r,
	    (u_short)color };

	return ioc(LCDG_CIRCLE, &c);
}

static int
window(int x, int y, int w, int h, int fmt, int fg, int bg)
{
	struct lcdg_window v = { (u_short)x, (u_short)y, (u_short)w,
	    (u_short)h, (u_char)fmt, (u_char)fg, (u_char)bg };

	return ioc(LCDG_WINDOW, &v);
}

static int
palette(int index, int r, int g, int b)
{
	struct lcdg_palette p = { (u_char)index, (u_char)r, (u_char)g,
	    (u_char)b };

	return ioc(LCDG_PALETTE, &p);
}

static int
scroll(int line_no)
{
	struct lcdg_scroll s = { (u_short)line_no };

	return ioc(LCDG_SCROLL, &s);
}

static int
scrdef(int top, int bottom)
{
	struct lcdg_scrdef s = { (u_short)top, (u_short)bottom };

	return ioc(LCDG_SCRDEF, &s);
}

static int
colmod(int fmt)
{
	struct lcdg_colmod c = { (u_char)fmt };

	return ioc(LCDG_COLMOD, &c);
}

static int
tileset(int first, int count)
{
	struct lcdg_tileset t = { (u_char)first, (u_char)count };

	return ioc(LCDG_TILESET, &t);
}

static int
tilewin(int tx, int ty, int tw, int th)
{
	struct lcdg_tilewin t = { (u_char)tx, (u_char)ty, (u_char)tw,
	    (u_char)th };

	return ioc(LCDG_TILEWIN, &t);
}

static int resid_after;		/* Bytes the last do_write() left unmoved. */

/* One write(2) of len bytes. */
static int
do_write(const void *data, size_t len)
{
	struct iovec iov;
	struct uio uio;
	int error;

	iov.iov_base = (char *)(void *)(unsigned char *)data;
	iov.iov_len = len;
	memset(&uio, 0, sizeof(uio));
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = (u_int)len;
	uio.uio_rw = UIO_WRITE;
	error = lcdgfx_write(&uio);
	resid_after = (int)uio.uio_resid;
	return error;
}

/* Pixels set inside a box, in raster order. */
static int
set_count(int x0, int y0, int x1, int y1)
{
	int x, y, n = 0;

	for (y = y0; y <= y1; y++)
		for (x = x0; x <= x1; x++)
			if (fb[y][x] != 0)
				n++;
	return n;
}

/* Every pixel of the box has the value, and nothing outside it is set. */
static void
expect_solid(const char *what, int x0, int y0, int x1, int y1, unsigned v)
{
	int x, y, bad = 0;

	for (y = y0; y <= y1; y++)
		for (x = x0; x <= x1; x++)
			if (fb[y][x] != v)
				bad++;
	if (bad != 0) {
		fprintf(stderr, "FAIL %s: %d pixels of the box are wrong\n",
		    what, bad);
		failures++;
	}
	expect_int(what, npixels, (long)(x1 - x0 + 1) * (y1 - y0 + 1));
}

static void
no_violations(const char *what)
{
	if (violations != 0) {
		fprintf(stderr, "FAIL %s: %d model violations\n", what,
		    violations);
		failures++;
		violations = 0;
	}
}

static void
open_ok(const char *what)
{
	expect_err(what, lcdgfx_open(), 0);
}

static void
test_open_close(void)
{
	int i;

	reset();
	out("text");
	expect_int("text draws before the open", ndraw >= 4, 1);
	for (i = 0; i < 40; i++)
		out("\r\n");
	expect_int("the ring has moved the text scroll start", vscsad != 0, 1);
	counters_clear();

	expect_int("closed at the start", lcdgfx_isopen(), 0);
	open_ok("open");
	expect_int("open", lcdgfx_isopen(), 1);
	expect_int("open asks the console once", gfx_calls, 1);
	expect_int("open puts the scroll start at line 0", vscsad, 0);
	expect_int("open sends exactly one scroll start", nscroll, 1);
	expect_err("second open", lcdgfx_open(), EBUSY);
	expect_int("a refused open leaves the console alone", gfx_calls, 1);
	expect_int("a refused open leaves the device open", lcdgfx_isopen(), 1);
	expect_err("third open", lcdgfx_open(), EBUSY);
	expect_int("close", lcdgfx_close(), 0);
	expect_int("closed", lcdgfx_isopen(), 0);
	expect_int("close asks the console once more", gfx_calls, 2);
	expect_int("close restores the text configuration", nreset, 1);

	open_ok("reopen after close");
	expect_int("reopened", lcdgfx_isopen(), 1);
	lcdgfx_close();

	/* A console that refuses the switch leaves the device closed. */
	gfx_busy = 1;
	expect_err("open while the drawing flag is held", lcdgfx_open(), EAGAIN);
	expect_int("a failed open does not claim the device", lcdgfx_isopen(), 0);
	gfx_busy = 0;
	open_ok("open after the console frees");
	lcdgfx_close();

	/* Read is end of file; the console's own commands are not ours. */
	open_ok("open for unknown command");
	expect_err("unknown command", ioc(0x74667301U, NULL), ENOTTY);
	lcdgfx_close();
	no_violations("open and close");
}

/*
 * A pixel program owns the panel: text written to the console updates the
 * cell store and draws nothing.
 */
static void
test_text_stays_off_the_panel(void)
{
	char buf[16];
	int i;

	reset();
	out("keep");
	open_ok("open");
	counters_clear();
	out("\033[2J\033[1;1Hhidden\r\n");
	for (i = 0; i < 40; i++) {
		snprintf(buf, sizeof(buf), "line %d\r\n", i);
		out(buf);
	}
	expect_int("no text cell reaches the panel", ndraw, 0);
	expect_int("no text fill reaches the panel", nfill, 0);
	expect_int("no text scroll reaches the panel", ntop, 0);
	expect_int("no panel command at all", nscroll + nwindow, 0);
	expect_int("the store follows the text", lcdterm_cell(LT_ROWS - 2, 0), 'l');
	lcdgfx_close();
	no_violations("text during the mode");
}

static void
test_fill_pixel(void)
{
	reset();
	open_ok("open");
	fb_clear();
	counters_clear();

	expect_err("fill", fill(10, 20, 19, 24, 0xf800), 0);
	expect_int("one window", nwindow, 1);
	expect_int("window x0", last_x0, 10);
	expect_int("window y0", last_y0, 20);
	expect_int("window x1", last_x1, 19);
	expect_int("window y1", last_y1, 24);
	expect_int("window complete", win_complete, 1);
	expect_solid("fill red", 10, 20, 19, 24, exp666(0xf800));
	expect_int("fill touches nothing else", set_count(0, 0, PW - 1, PH - 1), 50);

	fb_clear();
	nwindow = 0;
	expect_err("full-screen fill", fill(0, 0, PW - 1, PH - 1, 0x07e0), 0);
	expect_int("a clear is one window", nwindow, 1);
	expect_solid("full screen", 0, 0, PW - 1, PH - 1, exp666(0x07e0));

	fb_clear();
	expect_err("one-pixel fill", fill(319, 319, 319, 319, 0x001f), 0);
	expect_solid("corner pixel by fill", 319, 319, 319, 319, exp666(0x001f));

	fb_clear();
	expect_err("pixel", pixel(0, 0, 0xffff), 0);
	expect_err("pixel at the far corner", pixel(319, 319, 0x8410), 0);
	expect_err("pixel mid-screen", pixel(160, 7, 0x1234), 0);
	expect_int("three pixels", set_count(0, 0, PW - 1, PH - 1), 3);
	expect_int("pixel (0,0)", fb[0][0], exp666(0xffff));
	expect_int("pixel (319,319)", fb[319][319], exp666(0x8410));
	expect_int("pixel (160,7)", fb[7][160], exp666(0x1234));
	expect_int("colors widen by replication: white", exp666(0xffff),
	    TAG_666 | 0x3f << 12 | 0x3f << 6 | 0x3f);
	expect_int("black stays black", exp666(0), TAG_666);

	/* Under COLMOD 0x55 the two bytes go out unchanged. */
	fb_clear();
	expect_err("colmod 0x55", colmod(0x55), 0);
	expect_err("fill in 565", fill(3, 4, 5, 6, 0xabcd), 0);
	expect_solid("565 fill", 3, 4, 5, 6, exp565(0xabcd));
	expect_err("pixel in 565", pixel(100, 100, 0x0001), 0);
	expect_int("565 pixel", fb[100][100], exp565(0x0001));
	lcdgfx_close();
	no_violations("fill and pixel");
}

static int
iabs(int v)
{
	return v < 0 ? -v : v;
}

/*
 * The set of pixels of a drawn line against the ideal line. The major axis
 * advances by one pixel per step, so there are max(dx, dy) + 1 pixels, each
 * major coordinate once, and the minor coordinate is within half a pixel of
 * the real line; both ends are drawn.
 */
static void
check_line(int x0, int y0, int x1, int y1)
{
	int dx = iabs(x1 - x0), dy = iabs(y1 - y0);
	int n, k, c, x, y, bad = 0, minor = 0;
	int bx0 = x0 < x1 ? x0 : x1, bx1 = x0 < x1 ? x1 : x0;
	int by0 = y0 < y1 ? y0 : y1, by1 = y0 < y1 ? y1 : y0;
	double ideal;
	char what[64];

	fb_clear();
	nwindow = 0;
	snprintf(what, sizeof(what), "line (%d,%d)-(%d,%d)", x0, y0, x1, y1);
	expect_err(what, line(x0, y0, x1, y1, 0xf81f), 0);
	n = (dx > dy ? dx : dy) + 1;
	expect_int(what, npixels, n);
	expect_int(what, set_count(bx0, by0, bx1, by1), n);
	expect_int(what, set_count(0, 0, PW - 1, PH - 1), n);
	if (fb[y0][x0] == 0 || fb[y1][x1] == 0) {
		fprintf(stderr, "FAIL %s: an endpoint is missing\n", what);
		failures++;
	}
	if (dx == 0 || dy == 0) {
		expect_int(what, nwindow, 1);
		return;
	}
	for (k = 0; k < n; k++) {
		c = 0;
		if (dx >= dy) {
			x = x0 < x1 ? x0 + k : x0 - k;
			for (y = by0; y <= by1; y++)
				if (fb[y][x] != 0) {
					c++;
					minor = y;
				}
			ideal = y0 + (double)(x - x0) * (y1 - y0) / (x1 - x0);
		} else {
			y = y0 < y1 ? y0 + k : y0 - k;
			for (x = bx0; x <= bx1; x++)
				if (fb[y][x] != 0) {
					c++;
					minor = x;
				}
			ideal = x0 + (double)(y - y0) * (x1 - x0) / (y1 - y0);
		}
		if (c != 1 || (double)minor - ideal > 0.5 + 1e-9 ||
		    ideal - (double)minor > 0.5 + 1e-9)
			bad++;
	}
	if (bad != 0) {
		fprintf(stderr, "FAIL %s: %d steps leave the ideal line\n", what,
		    bad);
		failures++;
	}
}

static void
test_line(void)
{
	static const int oct[8][2] = {
		{ 40, 10 }, { 40, -10 }, { -40, 10 }, { -40, -10 },
		{ 10, 40 }, { 10, -40 }, { -10, 40 }, { -10, -40 },
	};
	unsigned seed = 12345;
	int i, x0, y0, x1, y1;

	reset();
	open_ok("open");
	for (i = 0; i < 8; i++)
		check_line(100, 100, 100 + oct[i][0], 100 + oct[i][1]);
	check_line(0, 0, 319, 319);
	check_line(319, 319, 0, 0);
	check_line(0, 319, 319, 0);
	check_line(0, 0, 319, 1);
	check_line(0, 0, 1, 319);
	check_line(5, 5, 5, 5);
	check_line(5, 9, 5, 200);
	check_line(200, 9, 5, 9);
	check_line(7, 7, 8, 8);
	check_line(0, 0, 319, 0);
	check_line(319, 0, 319, 319);
	for (i = 0; i < 300; i++) {
		seed = seed * 1103515245U + 12345U;
		x0 = (int)((seed >> 8) % PW);
		seed = seed * 1103515245U + 12345U;
		y0 = (int)((seed >> 8) % PH);
		seed = seed * 1103515245U + 12345U;
		x1 = (int)((seed >> 8) % PW);
		seed = seed * 1103515245U + 12345U;
		y1 = (int)((seed >> 8) % PH);
		check_line(x0, y0, x1, y1);
	}
	lcdgfx_close();
	no_violations("lines");
}

static void
check_frame(int x0, int y0, int x1, int y1)
{
	int x, y, w = x1 - x0 + 1, h = y1 - y0 + 1, want, bad = 0, edge;
	char what[64];

	fb_clear();
	nwindow = 0;
	snprintf(what, sizeof(what), "frame (%d,%d)-(%d,%d)", x0, y0, x1, y1);
	expect_err(what, frame(x0, y0, x1, y1, 0x07ff), 0);
	want = w == 1 || h == 1 ? w * h : 2 * w + 2 * h - 4;
	expect_int(what, npixels, want);	/* Each pixel drawn once. */
	for (y = y0; y <= y1; y++)
		for (x = x0; x <= x1; x++) {
			edge = x == x0 || x == x1 || y == y0 || y == y1;
			if ((fb[y][x] != 0) != edge)
				bad++;
		}
	expect_int(what, bad, 0);
	expect_int(what, set_count(0, 0, PW - 1, PH - 1), want);
	expect_int(what, nwindow <= 4, 1);
}

static void
test_frame(void)
{
	reset();
	open_ok("open");
	check_frame(10, 10, 29, 19);
	check_frame(0, 0, 319, 319);
	check_frame(5, 5, 5, 5);
	check_frame(5, 5, 5, 9);
	check_frame(5, 5, 9, 5);
	check_frame(5, 5, 6, 6);
	check_frame(5, 5, 7, 7);
	check_frame(100, 100, 101, 140);
	check_frame(100, 100, 140, 102);
	lcdgfx_close();
	no_violations("frames");
}

static void
check_circle(int cx, int cy, int r)
{
	int x, y, n = 0, bad = 0, asym = 0, lone = 0, nb, i, j;
	double d;
	char what[64];

	fb_clear();
	snprintf(what, sizeof(what), "circle (%d,%d) r %d", cx, cy, r);
	expect_err(what, circle(cx, cy, r, 0xffe0), 0);
	for (y = cy - r; y <= cy + r; y++)
		for (x = cx - r; x <= cx + r; x++) {
			if (fb[y][x] == 0)
				continue;
			n++;
			d = (double)(x - cx) * (x - cx) + (double)(y - cy) * (y - cy);
			/* Within 0.75 pixel of the radius, compared squared. */
			if (d > ((double)r + 0.75) * ((double)r + 0.75) ||
			    (r > 0 && d < ((double)r - 0.75) * ((double)r - 0.75)))
				bad++;
			/* Mirror and swap images of the pixel are set too. */
			if (fb[cy + (cx - x)][cx + (y - cy)] == 0 ||
			    fb[cy + (y - cy)][cx - (x - cx)] == 0 ||
			    fb[cy - (y - cy)][cx + (x - cx)] == 0)
				asym++;
			nb = 0;
			for (j = -1; j <= 1; j++)
				for (i = -1; i <= 1; i++)
					if ((i != 0 || j != 0) && x + i >= 0 &&
					    x + i < PW && y + j >= 0 && y + j < PH &&
					    fb[y + j][x + i] != 0)
						nb++;
			if (r > 1 && nb < 2)
				lone++;
		}
	expect_int(what, bad, 0);
	expect_int(what, asym, 0);
	expect_int(what, lone, 0);
	expect_int(what, set_count(0, 0, PW - 1, PH - 1), n);
	expect_int(what, npixels >= n, 1);
	if (fb[cy][cx + r] == 0 || fb[cy][cx - r] == 0 || fb[cy + r][cx] == 0 ||
	    fb[cy - r][cx] == 0) {
		fprintf(stderr, "FAIL %s: an axis extreme is missing\n", what);
		failures++;
	}
	if (r > 2)
		expect_int(what, fb[cy][cx], 0);	/* Outline, not a disc. */
}

static void
test_circle(void)
{
	static const int radii[] = { 1, 2, 3, 4, 5, 8, 13, 31, 64, 100 };
	size_t i;

	reset();
	open_ok("open");
	fb_clear();
	expect_err("r 0", circle(50, 60, 0, 0x1111), 0);
	expect_int("r 0 is the center pixel", set_count(0, 0, PW - 1, PH - 1), 1);
	expect_int("r 0 pixel", fb[60][50], exp666(0x1111));
	fb_clear();
	expect_err("r 1", circle(50, 60, 1, 0x1111), 0);
	expect_int("r 1 is a cross: four pixels", set_count(0, 0, PW - 1, PH - 1), 4);
	expect_int("r 1 right", fb[60][51] != 0, 1);
	expect_int("r 1 left", fb[60][49] != 0, 1);
	expect_int("r 1 below", fb[61][50] != 0, 1);
	expect_int("r 1 above", fb[59][50] != 0, 1);
	for (i = 0; i < sizeof(radii) / sizeof(radii[0]); i++)
		check_circle(160, 160, radii[i]);
	check_circle(159, 159, 159);		/* Touches all four edges. */
	check_circle(5, 7, 5);
	check_circle(314, 312, 5);
	lcdgfx_close();
	no_violations("circles");
}

/* A pattern of distinct RGB565 colors. */
static unsigned short
pat(int i)
{
	return (unsigned short)(0x0821 * (i % 31 + 1) + i * 37);
}

static void
test_window_rgb565(void)
{
	unsigned short img[20 * 13];
	int i, x, y, bad;

	reset();
	open_ok("open");
	for (i = 0; i < 20 * 13; i++)
		img[i] = pat(i);

	/* One write larger than the driver's chunk. */
	fb_clear();
	nwindow = 0;
	expect_err("window", window(30, 40, 20, 13, LCDG_WIN_RGB565, 0, 0), 0);
	expect_int("window x0", last_x0, 30);
	expect_int("window y1", last_y1, 40 + 13 - 1);
	expect_int("the window stays open for the stream", win_open, 1);
	expect_err("write", do_write(img, sizeof(img)), 0);
	expect_int("the write is consumed", resid_after, 0);
	expect_int("the window closes at the last pixel", win_open, 0);
	expect_int("window complete", win_complete, 1);
	bad = 0;
	for (y = 0; y < 13; y++)
		for (x = 0; x < 20; x++)
			if (fb[40 + y][30 + x] != exp666(img[y * 20 + x]))
				bad++;
	expect_int("every pixel of the image", bad, 0);
	expect_int("nothing outside", npixels, 20 * 13);
	expect_int("the driver copies at most a chunk", uiomove_max <= CHUNK_MAX, 1);
	expect_err("write after the last pixel", do_write(img, 2), EINVAL);

	/* The same image in two-byte writes and in 130-byte writes. */
	for (i = 0; i < 2; i++) {
		size_t step = i == 0 ? 2 : 130, off, len;

		fb_clear();
		expect_err("window again", window(30, 40, 20, 13,
		    LCDG_WIN_RGB565, 0, 0), 0);
		for (off = 0; off < sizeof(img); off += len) {
			len = sizeof(img) - off < step ? sizeof(img) - off : step;
			expect_err("piece", do_write((char *)img + off, len), 0);
		}
		bad = 0;
		for (y = 0; y < 13; y++)
			for (x = 0; x < 20; x++)
				if (fb[40 + y][30 + x] != exp666(img[y * 20 + x]))
					bad++;
		expect_int("pieces", bad, 0);
		expect_int("pieces complete", win_complete, 1);
	}

	/* A write past the end moves only the pixels that remain. */
	fb_clear();
	expect_err("small window", window(0, 0, 2, 2, LCDG_WIN_RGB565, 0, 0), 0);
	/* 2x2 pixels: eight bytes. */
	expect_err("overlong write", do_write(img, 40), 0);
	expect_int("residual", resid_after, 40 - 8);
	expect_int("the four pixels of a 2x2 window", npixels, 4);
	expect_int("closed", win_open, 0);

	/* An odd count is refused before any pixel. */
	fb_clear();
	expect_err("window for odd", window(0, 0, 2, 2, LCDG_WIN_RGB565, 0, 0), 0);
	expect_err("odd write", do_write(img, 5), EINVAL);
	expect_int("odd write moves nothing", npixels, 0);
	expect_err("even write follows", do_write(img, 4), 0);
	expect_int("two pixels", npixels, 2);

	/* A copy fault reports and keeps the window for a retry. */
	uiomove_fail_after = 0;
	expect_err("faulting write", do_write(img, 4), EFAULT);
	uiomove_fail_after = -1;
	expect_int("the window is still open", win_open, 1);
	expect_err("retry", do_write(img, 4), 0);
	expect_int("four pixels", npixels, 4);

	/* Another command abandons a half-filled window. */
	expect_err("abandon by pixel", pixel(200, 200, 0x1111), 0);
	expect_int("the abandoned window was ended", win_open, 0);
	expect_err("write to the abandoned window", do_write(img, 2), EINVAL);

	/* Closing with a window open ends it. */
	expect_err("window for close", window(0, 0, 4, 4, LCDG_WIN_RGB565, 0, 0), 0);
	expect_err("partial", do_write(img, 6), 0);
	expect_int("open window", win_open, 1);
	lcdgfx_close();
	expect_int("close ends the window", win_open, 0);
	no_violations("rgb565 windows");
}

static void
test_window_index_bits(void)
{
	unsigned char data[64];
	int i, x, y, bad, fmt;
	unsigned expect = 0;

	reset();
	open_ok("open");
	expect_err("palette 5", palette(5, 255, 0, 0), 0);
	expect_err("palette 9", palette(9, 0, 255, 0), 0);
	expect_err("palette 3", palette(3, 0, 0, 255), 0);

	/* Palette indexes: the low four bits select, the rest are ignored. */
	for (i = 0; i < 12; i++)
		data[i] = (unsigned char)((i % 3 == 0 ? 5 : i % 3 == 1 ? 9 : 3) |
		    (i & 1 ? 0xf0 : 0));
	fb_clear();
	expect_err("index window", window(10, 10, 4, 3, LCDG_WIN_INDEX, 0, 0), 0);
	expect_err("index data", do_write(data, 12), 0);
	bad = 0;
	for (y = 0; y < 3; y++)
		for (x = 0; x < 4; x++) {
			switch (data[y * 4 + x] & 15) {
			case 5: expect = exp666(0xf800); break;
			case 9: expect = exp666(0x07e0); break;
			default: expect = exp666(0x001f); break;
			}
			if (fb[10 + y][10 + x] != expect)
				bad++;
		}
	expect_int("palette indexes", bad, 0);
	expect_int("index pixel count", npixels, 12);

	/* 1 bpp: rows padded to bytes, the leftover bits ignored. */
	for (fmt = 0; fmt < 4; fmt++) {
		static const int widths[4] = { 10, 8, 1, 17 };
		int w = widths[fmt], bpr = (w + 7) / 8, h = 3, nbytes = bpr * h;
		int split = fmt == 0 ? 1 : nbytes;
		int off, len;

		for (i = 0; i < nbytes; i++)
			data[i] = (unsigned char)(0xa5 ^ (i * 0x3b));
		fb_clear();
		expect_err("bits window", window(50, 60, w, h, LCDG_WIN_BITS, 5, 9), 0);
		for (off = 0; off < nbytes; off += len) {
			len = nbytes - off < split ? nbytes - off : split;
			expect_err("bits data", do_write(data + off, (size_t)len), 0);
		}
		bad = 0;
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				int bit = (data[y * bpr + x / 8] >> (7 - x % 8)) & 1;

				expect = exp666(bit ? 0xf800 : 0x07e0);
				if (fb[60 + y][50 + x] != expect)
					bad++;
			}
		expect_int("1 bpp pixels", bad, 0);
		expect_int("1 bpp count", npixels, w * h);
		expect_int("1 bpp complete", win_complete, 1);
	}
	lcdgfx_close();
	no_violations("index and bit windows");
}

static void
test_palette_default_and_reset(void)
{
	unsigned char one = 3;
	int i;

	reset();
	open_ok("open");
	/* The palette starts as the console's colors. */
	for (i = 0; i < 16; i++) {
		unsigned char idx = (unsigned char)i;

		fb_clear();
		expect_err("default window", window(0, 0, 1, 1, LCDG_WIN_INDEX, 0, 0), 0);
		expect_err("default pixel", do_write(&idx, 1), 0);
		expect_int("default palette entry", fb[0][0], exp666(rgb565(
		    lcd_palette[i][0], lcd_palette[i][1], lcd_palette[i][2])));
	}
	/* The component truncation: five, six and five bits kept. */
	expect_err("palette", palette(3, 0xff, 0xff, 0xff), 0);
	fb_clear();
	expect_err("w", window(0, 0, 1, 1, LCDG_WIN_INDEX, 0, 0), 0);
	expect_err("d", do_write(&one, 1), 0);
	expect_int("white is white", fb[0][0], exp666(0xffff));
	expect_err("palette", palette(3, 0x07, 0x03, 0x07), 0);
	fb_clear();
	expect_err("w", window(0, 0, 1, 1, LCDG_WIN_INDEX, 0, 0), 0);
	expect_err("d", do_write(&one, 1), 0);
	expect_int("low bits are dropped", fb[0][0], exp666(0));
	expect_err("palette", palette(3, 0x08, 0x04, 0x08), 0);
	fb_clear();
	expect_err("w", window(0, 0, 1, 1, LCDG_WIN_INDEX, 0, 0), 0);
	expect_err("d", do_write(&one, 1), 0);
	expect_int("lowest kept bits", fb[0][0], exp666(rgb565(8, 4, 8)));
	expect_int("lowest kept bits value", rgb565(8, 4, 8), 1 << 11 | 1 << 5 | 1);
	lcdgfx_close();

	/* A new open starts from the console's palette again. */
	open_ok("reopen");
	fb_clear();
	expect_err("w", window(0, 0, 1, 1, LCDG_WIN_INDEX, 0, 0), 0);
	expect_err("d", do_write(&one, 1), 0);
	expect_int("entry 3 is back to the console color", fb[0][0], exp666(
	    rgb565(lcd_palette[3][0], lcd_palette[3][1], lcd_palette[3][2])));
	lcdgfx_close();
	no_violations("palette");
}

static void
test_scroll_colmod_info(void)
{
	struct lcdg_info info;

	reset();
	open_ok("open");
	expect_int("open state: scroll start", vscsad, 0);
	expect_err("scroll 0", scroll(0), 0);
	expect_err("scroll 319", scroll(319), 0);
	expect_int("scroll start programmed", vscsad, 319);
	expect_err("scroll 479", scroll(479), EINVAL);
	expect_int("a refused scroll programs nothing", vscsad, 319);

	nscrdef = 0;
	expect_err("scrdef 20/100", scrdef(20, 100), 0);
	expect_int("scrdef programmed", nscrdef, 1);
	expect_int("top fixed area", def_top, 20);
	expect_int("bottom fixed area", def_bottom, 100);
	expect_int("scrdef moves the scroll start to the scroll area", vscsad, 20);
	expect_err("scroll below the area", scroll(19), EINVAL);
	expect_err("scroll first line", scroll(20), 0);
	expect_err("scroll last line", scroll(LCDG_RAM_LINES - 100 - 1), 0);
	expect_err("scroll past the area", scroll(LCDG_RAM_LINES - 100), EINVAL);
	expect_err("scrdef 0/479", scrdef(0, 479), 0);
	expect_err("scrdef leaving no area", scrdef(0, 480), EINVAL);
	expect_err("scrdef 240/240", scrdef(240, 240), EINVAL);
	expect_err("scrdef negative top", scrdef(-1, 10), EINVAL);
	expect_err("scrdef negative bottom", scrdef(10, -1), EINVAL);
	expect_int("refused scrdefs leave the definition", def_bottom, 479);
	expect_err("scrdef back", scrdef(0, 160), 0);

	memset(&info, 0xee, sizeof(info));
	expect_err("info", ioc(LCDG_INFO, &info), 0);
	expect_int("info width", info.w, 320);
	expect_int("info height", info.h, 320);
	expect_int("info tiles", info.tiles, 128);
	expect_int("info bits per pixel under RGB666", info.bpp, 18);
	expect_err("colmod 0x55", colmod(0x55), 0);
	expect_int("colmod programmed", panel_colmod, 0x55);
	expect_err("info again", ioc(LCDG_INFO, &info), 0);
	expect_int("info bits per pixel under RGB565", info.bpp, 16);
	expect_err("colmod 0x66", colmod(0x66), 0);
	expect_int("colmod back", panel_colmod, 0x66);
	expect_err("colmod 0x56", colmod(0x56), EINVAL);
	expect_err("colmod 0", colmod(0), EINVAL);
	expect_err("colmod 0xff", colmod(0xff), EINVAL);
	expect_int("refused colmods leave the format", panel_colmod, 0x66);

	/* The close puts everything back. */
	colmod(0x55);
	scrdef(30, 30);
	scroll(100);
	lcdgfx_close();
	expect_int("close restores COLMOD", panel_colmod, 0x66);
	expect_int("close restores the top fixed area", def_top, 0);
	expect_int("close restores the bottom fixed area", def_bottom, 160);

	/* A new open starts again from RGB666 and the default scroll area. */
	open_ok("reopen");
	fb_clear();
	expect_err("fill after reopen", fill(1, 1, 1, 1, 0x4321), 0);
	expect_int("reopen is RGB666 again", fb[1][1], exp666(0x4321));
	expect_err("scroll in the default area", scroll(319), 0);
	expect_err("scroll outside the default area", scroll(320), EINVAL);
	expect_err("info after reopen", ioc(LCDG_INFO, &info), 0);
	expect_int("info bits per pixel after reopen", info.bpp, 18);
	lcdgfx_close();
	no_violations("scroll, colmod and info");
}

/* Rejected payloads change nothing on the panel; the good edges are drawn. */
static void
test_ranges(void)
{
	struct {
		const char *what;
		int err;
		int want;
	} t[64];
	int n = 0, i;

#define	CASE(text, expr, errno_want)	do { t[n].what = (text); \
	t[n].err = (expr); t[n].want = (errno_want); n++; } while (0)

	reset();
	open_ok("open");
	fb_clear();
	counters_clear();

	CASE("fill x1 past the panel", fill(0, 0, 320, 10, 1), EINVAL);
	CASE("fill y1 past the panel", fill(0, 0, 10, 320, 1), EINVAL);
	CASE("fill x0 above x1", fill(11, 0, 10, 10, 1), EINVAL);
	CASE("fill y0 above y1", fill(0, 11, 10, 10, 1), EINVAL);
	CASE("fill negative x0", fill(-1, 0, 10, 10, 1), EINVAL);
	CASE("fill negative y0", fill(0, -1, 10, 10, 1), EINVAL);
	CASE("frame x1 past the panel", frame(0, 0, 320, 10, 1), EINVAL);
	CASE("frame y0 above y1", frame(0, 11, 10, 10, 1), EINVAL);
	CASE("frame negative", frame(-5, 0, 10, 10, 1), EINVAL);
	CASE("pixel x past the panel", pixel(320, 0, 1), EINVAL);
	CASE("pixel y past the panel", pixel(0, 320, 1), EINVAL);
	CASE("pixel negative x", pixel(-1, 0, 1), EINVAL);
	CASE("pixel negative y", pixel(0, -1, 1), EINVAL);
	CASE("line x0 past the panel", line(320, 0, 5, 5, 1), EINVAL);
	CASE("line y0 past the panel", line(0, 320, 5, 5, 1), EINVAL);
	CASE("line x1 past the panel", line(0, 0, 320, 5, 1), EINVAL);
	CASE("line y1 past the panel", line(0, 0, 5, 320, 1), EINVAL);
	CASE("line negative x1", line(0, 0, -1, 5, 1), EINVAL);
	CASE("circle left of the panel", circle(4, 100, 5, 1), EINVAL);
	CASE("circle above the panel", circle(100, 4, 5, 1), EINVAL);
	CASE("circle right of the panel", circle(315, 100, 5, 1), EINVAL);
	CASE("circle below the panel", circle(100, 315, 5, 1), EINVAL);
	CASE("circle negative radius", circle(100, 100, -1, 1), EINVAL);
	CASE("circle center off the panel", circle(320, 100, 0, 1), EINVAL);
	CASE("window w zero", window(0, 0, 0, 5, 0, 0, 0), EINVAL);
	CASE("window h zero", window(0, 0, 5, 0, 0, 0, 0), EINVAL);
	CASE("window past the right edge", window(316, 0, 5, 5, 0, 0, 0), EINVAL);
	CASE("window past the bottom edge", window(0, 316, 5, 5, 0, 0, 0), EINVAL);
	CASE("window origin off the panel", window(320, 0, 1, 1, 0, 0, 0), EINVAL);
	CASE("window negative width", window(10, 10, -1, 5, 0, 0, 0), EINVAL);
	CASE("window format 3", window(0, 0, 5, 5, 3, 0, 0), EINVAL);
	CASE("window foreground 16", window(0, 0, 5, 5, LCDG_WIN_BITS, 16, 0), EINVAL);
	CASE("window background 16", window(0, 0, 5, 5, LCDG_WIN_BITS, 0, 16), EINVAL);
	CASE("palette index 16", palette(16, 1, 2, 3), EINVAL);
	CASE("palette index 255", palette(255, 1, 2, 3), EINVAL);
	CASE("scroll 480", scroll(480), EINVAL);
	CASE("scroll 320 at the default area", scroll(320), EINVAL);
	CASE("scroll negative", scroll(-1), EINVAL);
	CASE("scrdef sum 480", scrdef(300, 180), EINVAL);
	CASE("scrdef huge", scrdef(30000, 30000), EINVAL);
	CASE("colmod 0x65", colmod(0x65), EINVAL);
	for (i = 0; i < n; i++)
		expect_err(t[i].what, t[i].err, t[i].want);
	expect_int("a refused command touches no pixel", npixels, 0);
	expect_int("a refused command opens no window", nwindow, 0);
	expect_int("a refused command programs no scroll", nscroll, 0);
	expect_int("a refused command programs no definition", nscrdef, 0);
	expect_int("a refused command programs no format", ncolmod, 0);
	expect_int("a refused window leaves nothing to write to", win_open, 0);
	expect_err("write after refused window", do_write("ab", 2), EINVAL);

	/* The edges that are valid. */
	n = 0;
	CASE("fill to the corner", fill(0, 0, 319, 319, 1), 0);
	CASE("pixel in the far corner", pixel(319, 319, 1), 0);
	CASE("line corner to corner", line(0, 0, 319, 319, 1), 0);
	CASE("circle touching four edges", circle(159, 159, 159, 1), 0);
	CASE("circle in the corner", circle(5, 5, 5, 1), 0);
	CASE("window to the corner", window(315, 315, 5, 5, 0, 0, 0), 0);
	CASE("palette index 15", palette(15, 1, 2, 3), 0);
	CASE("window foreground 15", window(0, 0, 5, 5, LCDG_WIN_BITS, 15, 15), 0);
	CASE("scrdef sum 479", scrdef(300, 179), 0);
	CASE("scroll to the last line", scroll(480 - 179 - 1), 0);
	for (i = 0; i < n; i++)
		expect_err(t[i].what, t[i].err, t[i].want);
	lcdgfx_close();
	no_violations("ranges");
#undef	CASE
}

/*
 * The tile atlas. The reference atlas and palette below are the test's own
 * copy of what the driver must hold; a tile pixel is expected from them, not
 * from the driver's arithmetic.
 */
static unsigned char ref_atlas[LCDG_ATLAS_N][LCDG_TILE_BYTES];
static unsigned short ref_pal[LCDG_NPAL];
static unsigned rng_state = 12345;

static unsigned
rnd(unsigned n)
{
	rng_state = rng_state * 1103515245U + 12345U;
	return (rng_state >> 16) % n;
}

static void
ref_pal_init(void)
{
	int i;

	for (i = 0; i < LCDG_NPAL; i++)
		ref_pal[i] = (unsigned short)rgb565(lcd_palette[i][0],
		    lcd_palette[i][1], lcd_palette[i][2]);
}

static void
ref_atlas_random(int first, int count)
{
	int t, b;

	for (t = first; t < first + count; t++)
		for (b = 0; b < LCDG_TILE_BYTES; b++)
			ref_atlas[t][b] = (unsigned char)rnd(256);
}

/* The pixel tile t shows at column x, line y under the palette and format in force. */
static unsigned
ref_color(int t, int x, int y)
{
	unsigned char b = ref_atlas[t][y * 4 + x / 2];
	unsigned idx = x % 2 == 0 ? b >> 4U : b & 15U;

	return panel_colmod == 0x66 ? exp666(ref_pal[idx]) : exp565(ref_pal[idx]);
}

/* Pixels of the block that differ from the reference render of its numbers. */
static int
tile_block_bad(int tx, int ty, int tw, int th, const unsigned char *nums)
{
	int r, c, x, y, bad = 0;

	for (r = 0; r < th; r++)
		for (c = 0; c < tw; c++)
			for (y = 0; y < 8; y++)
				for (x = 0; x < 8; x++)
					if (fb[(ty + r) * 8 + y][(tx + c) * 8 + x] !=
					    ref_color(nums[r * tw + c], x, y))
						bad++;
	return bad;
}

/* Exactly one window per tile row, each tw * 8 by 8 and fully filled. */
static void
expect_tile_windows(const char *what, int tx, int ty, int tw, int th)
{
	int r, bad = 0;

	expect_int(what, nwindow, th);
	expect_int(what, wlog_n, th);
	for (r = 0; r < th && r < WLOG_MAX; r++) {
		if (wlog[r].x0 != tx * 8 || wlog[r].x1 != (tx + tw) * 8 - 1 ||
		    wlog[r].y0 != (ty + r) * 8 || wlog[r].y1 != (ty + r) * 8 + 7 ||
		    wlog[r].pixels != tw * 64 || wlog[r].complete != 1)
			bad++;
	}
	expect_int("tile row windows have the row's geometry and pixels", bad, 0);
}

/* Draw a block from a list of numbers written in pieces of `step` bytes (0: one write). */
static int
draw_block(int tx, int ty, int tw, int th, const unsigned char *nums, int step)
{
	int total = tw * th, off, len, error;

	error = tilewin(tx, ty, tw, th);
	if (error != 0)
		return error;
	for (off = 0; off < total; off += len) {
		len = step == 0 || total - off < step ? total - off : step;
		error = do_write(nums + off, (size_t)len);
		if (error != 0)
			return error;
	}
	return 0;
}

/* Upload tiles first .. first + count - 1 from the reference in pieces of `step` bytes. */
static int
upload(int first, int count, int step)
{
	const unsigned char *data = &ref_atlas[first][0];
	int total = count * LCDG_TILE_BYTES, off, len, error;

	error = tileset(first, count);
	if (error != 0)
		return error;
	for (off = 0; off < total; off += len) {
		len = step == 0 || total - off < step ? total - off : step;
		error = do_write(data + off, (size_t)len);
		if (error != 0)
			return error;
	}
	return 0;
}

/* Draw all 128 tiles in a 16 by 8 block and compare with the reference atlas. */
static void
check_atlas(const char *what)
{
	unsigned char nums[LCDG_ATLAS_N];
	int i;

	for (i = 0; i < LCDG_ATLAS_N; i++)
		nums[i] = (unsigned char)i;
	fb_clear();
	counters_clear();
	expect_err(what, draw_block(0, 0, 16, 8, nums, 0), 0);
	expect_int(what, tile_block_bad(0, 0, 16, 8, nums), 0);
	expect_tile_windows(what, 0, 0, 16, 8);
}

static void
test_tile_upload_draw(void)
{
	unsigned char nums[LCDG_GRID * LCDG_GRID];
	struct lcdg_info info;
	static const int steps[] = { 0, 1, 7, 31, 32, 33, 64, 65, 1000 };
	unsigned i, k;

	reset();
	open_ok("open");
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	expect_err("info", ioc(LCDG_INFO, &info), 0);
	expect_int("the atlas holds 128 tiles", info.tiles, 128);

	/* The atlas starts all zero: every pixel is palette entry 0. */
	for (i = 0; i < sizeof(nums); i++)
		nums[i] = (unsigned char)(i % LCDG_ATLAS_N);
	check_atlas("a fresh atlas is zero");

	/* The whole atlas in one write, then drawn as blocks. */
	ref_atlas_random(0, LCDG_ATLAS_N);
	fb_clear();
	counters_clear();
	expect_err("upload 128 tiles", upload(0, LCDG_ATLAS_N, 0), 0);
	expect_int("the upload is consumed", resid_after, 0);
	expect_int("an upload draws no pixel", npixels, 0);
	expect_int("an upload opens no window", nwindow, 0);
	check_atlas("128 tiles uploaded in one write");

	/* A 5 x 3 block: three windows, 320 pixels each. */
	for (i = 0; i < 15; i++)
		nums[i] = (unsigned char)rnd(LCDG_ATLAS_N);
	fb_clear();
	counters_clear();
	expect_err("5x3 block", draw_block(7, 11, 5, 3, nums, 0), 0);
	expect_tile_windows("5x3 block", 7, 11, 5, 3);
	expect_int("5x3 pixels", tile_block_bad(7, 11, 5, 3, nums), 0);
	expect_int("5x3 draws only its tiles", npixels, 5 * 3 * 64);
	expect_int("5x3 leaves the rest", set_count(0, 0, PW - 1, PH - 1), 5 * 3 * 64);

	/* The whole grid: 40 windows of 320 x 8. */
	for (i = 0; i < LCDG_GRID * LCDG_GRID; i++)
		nums[i] = (unsigned char)rnd(LCDG_ATLAS_N);
	fb_clear();
	counters_clear();
	expect_err("full grid", draw_block(0, 0, 40, 40, nums, 0), 0);
	expect_tile_windows("full grid", 0, 0, 40, 40);
	expect_int("full grid pixels", tile_block_bad(0, 0, 40, 40, nums), 0);
	expect_int("full grid covers the panel but the margin",
	    set_count(0, 0, PW - 1, PH - 1), PW * PH);

	/* One tile at each corner of the grid. */
	for (i = 0; i < 4; i++) {
		int tx = i & 1 ? 39 : 0, ty = i & 2 ? 39 : 0;

		nums[0] = (unsigned char)(i * 37 + 5);
		fb_clear();
		counters_clear();
		expect_err("corner tile", draw_block(tx, ty, 1, 1, nums, 0), 0);
		expect_tile_windows("corner tile", tx, ty, 1, 1);
		expect_int("corner tile pixels", tile_block_bad(tx, ty, 1, 1, nums), 0);
	}

	/* The result is independent of how the writes split the stream. */
	for (k = 0; k < sizeof(steps) / sizeof(steps[0]); k++) {
		for (i = 0; i < 6 * 4; i++)
			nums[i] = (unsigned char)rnd(LCDG_ATLAS_N);
		fb_clear();
		counters_clear();
		expect_err("split block", draw_block(3, 30, 6, 4, nums, steps[k]), 0);
		expect_tile_windows("split block", 3, 30, 6, 4);
		expect_int("split block pixels", tile_block_bad(3, 30, 6, 4, nums), 0);

		memset(ref_atlas, 0, sizeof(ref_atlas));
		ref_atlas_random(0, LCDG_ATLAS_N);
		fb_clear();
		expect_err("split upload", upload(0, LCDG_ATLAS_N, steps[k]), 0);
		check_atlas("atlas uploaded in split writes");
	}

	/* A window is complete exactly when its tw-th number arrives. */
	for (i = 0; i < 4 * 2; i++)
		nums[i] = (unsigned char)(i + 1);
	fb_clear();
	counters_clear();
	expect_err("4x2 block", tilewin(10, 10, 4, 2), 0);
	expect_int("no window before data", nwindow, 0);
	expect_err("three numbers", do_write(nums, 3), 0);
	expect_int("three of four numbers draw nothing", nwindow, 0);
	expect_err("fourth number", do_write(nums + 3, 1), 0);
	expect_int("the row draws at its last number", nwindow, 1);
	expect_err("first of the next row", do_write(nums + 4, 1), 0);
	expect_int("the next row waits", nwindow, 1);
	expect_err("rest", do_write(nums + 5, 3), 0);
	expect_tile_windows("4x2 block", 10, 10, 4, 2);
	expect_int("4x2 pixels", tile_block_bad(10, 10, 4, 2, nums), 0);
	lcdgfx_close();
	no_violations("tile upload and draw");
}

static void
test_tile_palette(void)
{
	unsigned char nums[6];
	int i;

	reset();
	open_ok("open");
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_atlas_random(0, 8);
	expect_err("upload 8 tiles", upload(0, 8, 0), 0);
	for (i = 0; i < 6; i++)
		nums[i] = (unsigned char)(i + 1);

	/* The palette in force when the row draws colors the tiles. */
	fb_clear();
	counters_clear();
	expect_err("block", draw_block(0, 0, 3, 2, nums, 0), 0);
	expect_int("default palette", tile_block_bad(0, 0, 3, 2, nums), 0);

	for (i = 0; i < LCDG_NPAL; i++) {
		expect_err("palette", palette(i, 255 - i * 9, i * 15, 40 + i * 3), 0);
		ref_pal[i] = (unsigned short)rgb565(255U - (unsigned)i * 9,
		    (unsigned)i * 15, 40U + (unsigned)i * 3);
	}
	fb_clear();
	counters_clear();
	expect_err("block after palette", draw_block(0, 0, 3, 2, nums, 0), 0);
	expect_int("new palette applies to the stored tiles",
	    tile_block_bad(0, 0, 3, 2, nums), 0);

	/* One entry changed: only its pixels move. */
	expect_err("one entry", palette(5, 1, 2, 3), 0);
	ref_pal[5] = (unsigned short)rgb565(1, 2, 3);
	fb_clear();
	counters_clear();
	expect_err("block after one entry", draw_block(0, 0, 3, 2, nums, 0), 0);
	expect_int("one entry", tile_block_bad(0, 0, 3, 2, nums), 0);

	/* Both nibbles of a byte select entries: high first. */
	memset(ref_atlas[9], 0, LCDG_TILE_BYTES);
	ref_atlas[9][0] = 0x3c;
	ref_atlas[9][4] = 0xa5;
	expect_err("upload tile 9", upload(9, 1, 0), 0);
	nums[0] = 9;
	fb_clear();
	counters_clear();
	expect_err("tile 9", draw_block(20, 20, 1, 1, nums, 0), 0);
	expect_int("tile 9 pixel 0 is the high nibble",
	    fb[160][160], exp666(ref_pal[3]));
	expect_int("tile 9 pixel 1 is the low nibble",
	    fb[160][161], exp666(ref_pal[12]));
	expect_int("tile 9 line 1 pixel 0", fb[161][160], exp666(ref_pal[10]));
	expect_int("tile 9 line 1 pixel 1", fb[161][161], exp666(ref_pal[5]));
	expect_int("tile 9 line 0 pixel 7", fb[160][167], exp666(ref_pal[0]));
	expect_int("tile 9 pixels", tile_block_bad(20, 20, 1, 1, nums), 0);

	/* Under COLMOD 0x55 the same tiles go out as two-byte pixels. */
	expect_err("colmod 0x55", colmod(0x55), 0);
	fb_clear();
	counters_clear();
	expect_err("block in 565", draw_block(0, 0, 3, 2, nums, 0), 0);
	expect_int("565 pixels", tile_block_bad(0, 0, 3, 2, nums), 0);
	expect_int("565 window count", nwindow, 2);
	lcdgfx_close();
	no_violations("tile palette");
}

static void
test_tile_ranges(void)
{
	struct {
		const char *what;
		int err;
		int want;
	} t[64];
	int n = 0, i;
	unsigned char nums[LCDG_GRID * LCDG_GRID];

#define	CASE(text, expr, errno_want)	do { t[n].what = (text); \
	t[n].err = (expr); t[n].want = (errno_want); n++; } while (0)

	reset();
	open_ok("open");
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_atlas_random(0, LCDG_ATLAS_N);
	expect_err("upload", upload(0, LCDG_ATLAS_N, 0), 0);
	memset(nums, 1, sizeof(nums));
	fb_clear();
	counters_clear();

	CASE("tileset first 128", tileset(128, 1), EINVAL);
	CASE("tileset first 127 count 2", tileset(127, 2), EINVAL);
	CASE("tileset first 0 count 129", tileset(0, 129), EINVAL);
	CASE("tileset count 0", tileset(0, 0), EINVAL);
	CASE("tileset first 64 count 65", tileset(64, 65), EINVAL);
	CASE("tileset first 200 count 100", tileset(200, 100), EINVAL);
	CASE("tileset 255 255", tileset(255, 255), EINVAL);
	CASE("tileset first 128 count 0", tileset(128, 0), EINVAL);
	CASE("tilewin tw 0", tilewin(0, 0, 0, 1), EINVAL);
	CASE("tilewin th 0", tilewin(0, 0, 1, 0), EINVAL);
	CASE("tilewin tx 40", tilewin(40, 0, 1, 1), EINVAL);
	CASE("tilewin ty 40", tilewin(0, 40, 1, 1), EINVAL);
	CASE("tilewin tx + tw 41", tilewin(1, 0, 40, 1), EINVAL);
	CASE("tilewin ty + th 41", tilewin(0, 1, 1, 40), EINVAL);
	CASE("tilewin tw 41", tilewin(0, 0, 41, 1), EINVAL);
	CASE("tilewin th 41", tilewin(0, 0, 1, 41), EINVAL);
	CASE("tilewin tx 39 tw 2", tilewin(39, 0, 2, 1), EINVAL);
	CASE("tilewin ty 39 th 2", tilewin(0, 39, 1, 2), EINVAL);
	CASE("tilewin 255 255", tilewin(255, 255, 255, 255), EINVAL);
	CASE("tilewin tw 255", tilewin(0, 0, 255, 1), EINVAL);
	for (i = 0; i < n; i++) {
		expect_err(t[i].what, t[i].err, t[i].want);
		expect_err("write after a refused command", do_write(nums, 8), EINVAL);
	}
	expect_int("a refused command draws no pixel", npixels, 0);
	expect_int("a refused command opens no window", nwindow, 0);
	expect_int("a refused command opens no panel window", win_open, 0);
	check_atlas("a refused upload leaves the atlas");
	counters_clear();
	fb_clear();

	/* The edges that are valid. */
	n = 0;
	CASE("tileset the last tile", tileset(127, 1), 0);
	CASE("tileset the first tile", tileset(0, 1), 0);
	CASE("tileset all", tileset(0, 128), 0);
	CASE("tilewin the last tile", tilewin(39, 39, 1, 1), 0);
	CASE("tilewin the first tile", tilewin(0, 0, 1, 1), 0);
	CASE("tilewin the full width", tilewin(0, 5, 40, 1), 0);
	CASE("tilewin the full height", tilewin(5, 0, 1, 40), 0);
	CASE("tilewin the whole grid", tilewin(0, 0, 40, 40), 0);
	for (i = 0; i < n; i++)
		expect_err(t[i].what, t[i].err, t[i].want);
	lcdgfx_close();
	no_violations("tile ranges");
#undef	CASE
}

static void
test_tile_upload_bounds(void)
{
	unsigned char data[LCDG_TILE_BYTES * 4 + 64];
	unsigned char nums[2];
	int i;

	reset();
	open_ok("open");
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_atlas_random(0, LCDG_ATLAS_N);
	expect_err("upload all", upload(0, LCDG_ATLAS_N, 0), 0);
	check_atlas("baseline");

	/* Three tiles in the middle change; their neighbors do not. */
	for (i = 0; i < 3 * LCDG_TILE_BYTES; i++)
		data[i] = (unsigned char)rnd(256);
	memcpy(ref_atlas[100], data, 3 * LCDG_TILE_BYTES);
	expect_err("tileset 100..102", tileset(100, 3), 0);
	expect_err("write", do_write(data, 3 * LCDG_TILE_BYTES), 0);
	check_atlas("tiles 100..102 replaced");

	/* More bytes than the upload wants: the excess stays unread. */
	for (i = 0; i < (int)sizeof(data); i++)
		data[i] = (unsigned char)rnd(256);
	memcpy(ref_atlas[50], data, 2 * LCDG_TILE_BYTES);
	expect_err("tileset 50..51", tileset(50, 2), 0);
	expect_err("overlong write", do_write(data, 4 * LCDG_TILE_BYTES + 50), 0);
	expect_int("the excess is not consumed", resid_after, 2 * LCDG_TILE_BYTES + 50);
	check_atlas("an overlong write stops at the upload");
	expect_err("write after the upload", do_write(data, 32), EINVAL);

	/* The last tile: first 127, count 1. */
	memcpy(ref_atlas[127], data + 10, LCDG_TILE_BYTES);
	expect_err("tileset 127", tileset(127, 1), 0);
	expect_err("write 127", do_write(data + 10, LCDG_TILE_BYTES + 5), 0);
	expect_int("tile 127 leaves five bytes unread", resid_after, 5);
	check_atlas("tile 127 replaced");

	/* Another command abandons an upload; the bytes that arrived stay. */
	memcpy(ref_atlas[20], data, LCDG_TILE_BYTES);
	memcpy(ref_atlas[21], data + LCDG_TILE_BYTES, 8);
	expect_err("tileset 20..22", tileset(20, 3), 0);
	expect_err("a tile and a quarter", do_write(data, LCDG_TILE_BYTES + 8), 0);
	expect_err("abandon by pixel", pixel(300, 300, 1), 0);
	expect_err("write to the abandoned upload", do_write(data, 8), EINVAL);
	check_atlas("an abandoned upload keeps what arrived");

	/* A rejected command abandons it too. */
	expect_err("tileset 30..31", tileset(30, 2), 0);
	expect_err("abandon by a refused command", palette(16, 0, 0, 0), EINVAL);
	expect_err("write to the abandoned upload again", do_write(data, 8), EINVAL);
	check_atlas("a refused abandon keeps the atlas");

	/* A tile window and an upload exclude each other. */
	expect_err("tileset 40..41", tileset(40, 2), 0);
	expect_err("tilewin takes over", tilewin(0, 0, 2, 1), 0);
	nums[0] = 1;
	nums[1] = 2;
	fb_clear();
	counters_clear();
	expect_err("tile numbers", do_write(nums, 2), 0);
	expect_int("the numbers draw a row", nwindow, 1);
	check_atlas("the upload that tilewin abandoned stored nothing");
	expect_err("tilewin 2", tilewin(0, 0, 2, 1), 0);
	expect_err("tileset takes over", tileset(40, 1), 0);
	counters_clear();
	expect_err("data", do_write(data, 8), 0);
	expect_int("the data go to the atlas, not the panel", nwindow, 0);
	memcpy(ref_atlas[40], data, 8);
	check_atlas("tileset abandoned the tile window");

	/* A copy fault reports and keeps the upload for a retry. */
	for (i = 0; i < 3 * LCDG_TILE_BYTES; i++)
		data[i] = (unsigned char)rnd(256);
	memcpy(ref_atlas[70], data, 3 * LCDG_TILE_BYTES);
	expect_err("tileset 70..72", tileset(70, 3), 0);
	uiomove_fail_after = 0;
	expect_err("faulting write", do_write(data, 3 * LCDG_TILE_BYTES), EFAULT);
	uiomove_fail_after = -1;
	expect_err("retry", do_write(data, 3 * LCDG_TILE_BYTES), 0);
	check_atlas("a faulted upload retries from its position");

	/* Closing with an upload open abandons it; a reopen clears the atlas. */
	expect_err("tileset 1", tileset(1, 1), 0);
	lcdgfx_close();
	open_ok("reopen");
	expect_err("write after close", do_write(data, 8), EINVAL);
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_pal_init();
	check_atlas("the atlas is zero at every open");
	lcdgfx_close();
	no_violations("tile upload bounds");
}

static void
test_tile_stream(void)
{
	unsigned char nums[LCDG_GRID * 4];
	unsigned i;

	reset();
	open_ok("open");
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_atlas_random(0, LCDG_ATLAS_N);
	expect_err("upload", upload(0, LCDG_ATLAS_N, 0), 0);
	for (i = 0; i < sizeof(nums); i++)
		nums[i] = (unsigned char)rnd(LCDG_ATLAS_N);

	/* More numbers than the block wants: the excess stays unread. */
	fb_clear();
	counters_clear();
	expect_err("2x2 block", tilewin(4, 4, 2, 2), 0);
	expect_err("overlong write", do_write(nums, 40), 0);
	expect_int("the excess is not consumed", resid_after, 36);
	expect_tile_windows("2x2 block", 4, 4, 2, 2);
	expect_int("2x2 pixels", tile_block_bad(4, 4, 2, 2, nums), 0);
	expect_err("write after the block", do_write(nums, 2), EINVAL);
	expect_int("no further window", nwindow, 2);

	/* A tile number past the atlas: the rows before it stay, the stream ends. */
	fb_clear();
	counters_clear();
	for (i = 0; i < 12; i++)
		nums[i] = (unsigned char)i;
	nums[7] = LCDG_ATLAS_N;
	expect_err("4x3 block", tilewin(0, 0, 4, 3), 0);
	expect_err("a bad number in row 1", do_write(nums, 12), EINVAL);
	expect_int("row 0 was drawn, row 1 was not", nwindow, 1);
	expect_int("row 0 pixels", tile_block_bad(0, 0, 4, 1, nums), 0);
	expect_int("only row 0 is on the panel", npixels, 4 * 64);
	expect_err("the stream is over", do_write(nums, 4), EINVAL);
	expect_int("still one window", nwindow, 1);

	/* A bad number in the first row draws nothing. */
	fb_clear();
	counters_clear();
	nums[0] = 255;
	expect_err("2x1 block", tilewin(0, 0, 2, 1), 0);
	expect_err("a bad first number", do_write(nums, 2), EINVAL);
	expect_int("no window", nwindow, 0);
	expect_int("no pixel", npixels, 0);
	nums[0] = 127;
	nums[1] = 127;
	expect_err("2x1 block again", tilewin(0, 0, 2, 1), 0);
	expect_err("the largest tile number", do_write(nums, 2), 0);
	expect_int("tile 127 draws", nwindow, 1);

	/* A bad number split from the rest of its row across writes. */
	fb_clear();
	counters_clear();
	nums[0] = 3;
	nums[1] = 128;
	expect_err("3x1 block", tilewin(0, 0, 3, 1), 0);
	expect_err("good prefix", do_write(nums, 1), 0);
	expect_err("bad middle", do_write(nums + 1, 1), EINVAL);
	expect_err("the rest", do_write(nums + 2, 1), EINVAL);
	expect_int("nothing drawn", nwindow, 0);

	/* Another command abandons a block; so does the close. */
	fb_clear();
	counters_clear();
	expect_err("2x2 block", tilewin(0, 0, 2, 2), 0);
	expect_err("first row", do_write(nums + 8, 2), 0);
	expect_int("one row drawn", nwindow, 1);
	expect_err("abandon by palette", palette(1, 9, 9, 9), 0);
	expect_err("write to the abandoned block", do_write(nums + 8, 2), EINVAL);
	expect_int("the second row never draws", nwindow, 1);
	expect_err("2x1 block", tilewin(0, 0, 2, 1), 0);
	expect_err("abandon by a refused tilewin", tilewin(40, 0, 1, 1), EINVAL);
	expect_err("write to the block a refusal abandoned", do_write(nums + 8, 2),
	    EINVAL);
	expect_err("2x1 block", tilewin(0, 0, 2, 1), 0);
	lcdgfx_close();
	open_ok("reopen");
	expect_err("write after close", do_write(nums + 8, 2), EINVAL);

	/* A block abandoned inside a row leaves no numbers to the next one. */
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_atlas_random(0, LCDG_ATLAS_N);
	expect_err("upload", upload(0, LCDG_ATLAS_N, 0), 0);
	for (i = 0; i < 4; i++)
		nums[i] = (unsigned char)(i + 60);
	fb_clear();
	counters_clear();
	expect_err("4x1 block", tilewin(2, 2, 4, 1), 0);
	expect_err("two numbers", do_write(nums, 2), 0);
	expect_err("4x1 block again", tilewin(2, 2, 4, 1), 0);
	expect_int("a half row draws nothing", nwindow, 0);
	expect_err("four numbers", do_write(nums, 4), 0);
	expect_tile_windows("restarted block", 2, 2, 4, 1);
	expect_int("restarted block pixels", tile_block_bad(2, 2, 4, 1, nums), 0);

	/* A copy fault reports; the block resumes where it stopped. */
	ref_pal_init();
	memset(ref_atlas, 0, sizeof(ref_atlas));
	ref_atlas_random(0, LCDG_ATLAS_N);
	expect_err("upload", upload(0, LCDG_ATLAS_N, 0), 0);
	for (i = 0; i < 12; i++)
		nums[i] = (unsigned char)(i * 3);
	fb_clear();
	counters_clear();
	expect_err("4x3 block", tilewin(10, 10, 4, 3), 0);
	uiomove_fail_after = 1;
	uiomove_calls = 0;
	expect_err("faulting write", do_write(nums, 12), EFAULT);
	uiomove_fail_after = -1;
	expect_int("the first row was drawn before the fault", nwindow, 1);
	expect_err("retry the rest", do_write(nums + 4, 8), 0);
	expect_tile_windows("faulted block", 10, 10, 4, 3);
	expect_int("faulted block pixels", tile_block_bad(10, 10, 4, 3, nums), 0);

	/* A window stream is not a tile stream. */
	expect_err("pixel window", window(0, 0, 8, 8, LCDG_WIN_INDEX, 0, 0), 0);
	expect_err("tilewin takes over", tilewin(0, 0, 1, 1), 0);
	expect_int("the pixel window was ended", win_open, 0);
	expect_err("tile number", do_write(nums, 1), 0);
	expect_err("tilewin again", tilewin(0, 0, 1, 1), 0);
	expect_err("pixel window again", window(0, 0, 8, 8, LCDG_WIN_INDEX, 0, 0), 0);
	counters_clear();
	expect_err("index data", do_write(nums, 64), 0);
	expect_int("pixel data draw into the pixel window", nwindow, 0);
	lcdgfx_close();
	no_violations("tile stream");
}

/*
 * The text on the panel after a close equals a text-only redraw of the cell
 * store: the same bytes through a terminal that never gave the panel up.
 */
static void
snapshot(unsigned char ch[LT_ROWS][LT_COLS], unsigned char at[LT_ROWS][LT_COLS],
    unsigned char bits[LT_ROWS][LT_COLS][LT_GLYPH_ROWS], int *top)
{
	memcpy(ch, panel_ch, sizeof(panel_ch));
	memcpy(at, panel_at, sizeof(panel_at));
	memcpy(bits, panel_bits, sizeof(panel_bits));
	*top = text_top;
}

static unsigned char s_ch[2][LT_ROWS][LT_COLS];
static unsigned char s_at[2][LT_ROWS][LT_COLS];
static unsigned char s_bits[2][LT_ROWS][LT_COLS][LT_GLYPH_ROWS];

static void
test_restore(void)
{
	static const unsigned char tile[LT_GLYPH_ROWS] = {
		0xfc, 0x84, 0x84, 0xb4, 0xb4, 0xb4, 0x84, 0x84, 0xfc
	};
	const char *before = "\033[1;31mred\033[0m text\r\nsecond \033(0lqk\033(B\r\n";
	const char *during = "\033[3;1Hwritten while graphics \xa5\r\n"
	    "\033[10;5Hmore\033[32;1H\r\n\r\nscrolled\033[7;9H";
	char buf[16];
	int tops[2], i, y, x, bad;

	/* The run with graphics in the middle. */
	reset();
	lcdterm_setglyph(0xa5, tile);
	out(before);
	for (i = 0; i < 20; i++) {
		snprintf(buf, sizeof(buf), "pad %d\r\n", i);
		out(buf);
	}
	open_ok("open");
	counters_clear();
	fill(0, 0, 319, 319, 0xffff);
	expect_err("margin draw", fill(318, 0, 319, 319, 0x1234), 0);
	colmod(0x55);
	scrdef(40, 40);
	scroll(100);
	circle(160, 160, 100, 0xf800);
	window(5, 5, 10, 10, LCDG_WIN_RGB565, 0, 0);
	out(during);
	expect_int("text during graphics is silent", ndraw + nfill + ntop, 0);
	lcdgfx_close();
	expect_int("restore: COLMOD", panel_colmod, 0x66);
	expect_int("restore: top fixed area", def_top, 0);
	expect_int("restore: bottom fixed area", def_bottom, 160);
	expect_int("restore: scroll start at the ring top", vscsad,
	    lcdterm_top() * LT_CELL_H);
	expect_int("restore: text ring top", text_top, lcdterm_top());
	expect_int("restore: the ring moved while graphics ran", lcdterm_top() != 0, 1);
	bad = 0;
	for (y = 0; y < PH; y++)
		if (fb[y][PW - 1] != TAG_BLANK || fb[y][PW - 2] != TAG_BLANK)
			bad++;
	expect_int("restore: margin columns cleared", bad, 0);
	expect_int("restore: every cell drawn", ndraw >= LT_ROWS * LT_COLS, 1);
	snapshot(s_ch[0], s_at[0], s_bits[0], &tops[0]);
	out("\033[m");			/* Hides the cursor the repaint drew. */
	bad = 0;
	for (y = 0; y < LT_ROWS; y++)
		for (x = 0; x < LT_COLS; x++)
			if (panel_ch[(text_top + y) % LT_ROWS][x] !=
			    lcdterm_cell(y, x) ||
			    panel_at[(text_top + y) % LT_ROWS][x] !=
			    lcdterm_attr(y, x))
				bad++;
	expect_int("restore: panel cells equal the store", bad, 0);
	no_violations("restore");

	/* The same bytes through a terminal that never left text mode. */
	reset();
	lcdterm_setglyph(0xa5, tile);
	out(before);
	for (i = 0; i < 20; i++) {
		snprintf(buf, sizeof(buf), "pad %d\r\n", i);
		out(buf);
	}
	out(during);
	lcdterm_cursor_show();
	snapshot(s_ch[1], s_at[1], s_bits[1], &tops[1]);
	expect_int("text-only redraw: same ring top", tops[0], tops[1]);
	expect_int("restore equals the text-only redraw: characters",
	    memcmp(s_ch[0], s_ch[1], sizeof(s_ch[0])), 0);
	expect_int("restore equals the text-only redraw: attributes",
	    memcmp(s_at[0], s_at[1], sizeof(s_at[0])), 0);
	expect_int("restore equals the text-only redraw: glyph rows",
	    memcmp(s_bits[0], s_bits[1], sizeof(s_bits[0])), 0);
	no_violations("text-only run");
}

/* The fatal path: the console abandons the mode and the report draws. */
static void
test_fatal_path(void)
{
	reset();
	out("report me");
	open_ok("open");
	colmod(0x55);
	scrdef(10, 10);
	counters_clear();
	lcdterm_abort();
	expect_int("abort restores COLMOD", panel_colmod, 0x66);
	expect_int("abort restores the scroll definition", def_bottom, 160);
	expect_int("abort repaints the store", panel_ch[0][0], 'r');
	expect_int("abort puts the scroll start at the ring", vscsad,
	    lcdterm_top() * LT_CELL_H);
	counters_clear();
	out("x");
	expect_int("the report draws", ndraw >= 1, 1);
	lcdgfx_close();
	no_violations("fatal path");
}

int
main(void)
{
	test_open_close();
	test_text_stays_off_the_panel();
	test_fill_pixel();
	test_line();
	test_frame();
	test_circle();
	test_window_rgb565();
	test_window_index_bits();
	test_palette_default_and_reset();
	test_scroll_colmod_info();
	test_ranges();
	test_tile_upload_draw();
	test_tile_palette();
	test_tile_ranges();
	test_tile_upload_bounds();
	test_tile_stream();
	test_restore();
	test_fatal_path();
	if (failures != 0) {
		fprintf(stderr, "lcdgfx_test: %d failures\n", failures);
		return 1;
	}
	printf("lcdgfx_test: ok\n");
	return 0;
}
