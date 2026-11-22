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


/*
 * The panel display behind gfx.h, on /dev/lcdgfx (sys/lcdgfx.h).
 *
 * The 200 lines of the adapter sit in the middle of the panel's 320, from
 * line GFX_Y0. A text cell is 8 pixels high and 8 wide with 40 columns or
 * 4 wide with 80, drawn from font.h as one-bit windows, a run of changed
 * cells of one attribute at a time. Mode 4 and 5 pixels go out as palette
 * indexes, one to one. Mode 6 has 640 pixels a line: each pair becomes
 * one panel pixel of three levels, so a line one pixel wide stays visible
 * at half brightness.
 *
 * Nothing is compared to find what changed in a graphics mode. Display
 * memory is watched (mem.h): the first write to one of its sixteen pages
 * sets a bit, and a refresh draws the scan lines of the pages whose bits
 * are set. In a text mode the bits select the pages whose cells are
 * compared with the copy last drawn.
 *
 * A mode set that asks for memory to be kept (bit 7) keeps all of it
 * between graphics modes. Between a text and a graphics mode only the
 * cells' 4 KB exist on both sides; the other 12 KB are zeros on entering
 * a graphics mode.
 *
 * A graphics mode needs 16 KB where a text mode needs 4. The even scan
 * lines take the 8 KB that hold the cells and their copy in a text mode;
 * the odd ones take eight page frames from the pager for as long as the
 * mode lasts.
 *
 * The device's palette is set for the mode: the sixteen colors of the
 * adapter for text, the background and three colors of the selected
 * palette for modes 4 and 5, and black, half and full foreground for
 * mode 6. A pixel already on the panel keeps its color, so a palette
 * change draws every line again.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "font.h"
#include "gfx.h"
#include "mem.h"

#ifdef GFX_MODEL
/* The host test's model of the device (tests/panel.c). */
int panel_open(void);
int panel_close(void);
int panel_ioctl(unsigned long cmd, void *arg);
long panel_write(const void *p, unsigned long n);
long panel_read(void *p, unsigned long n);
#define DEV_OPEN()		panel_open()
#define DEV_READ(fd, p, n)	panel_read(p, n)
#define DEV_CLOSE(fd)		panel_close()
#define DEV_IOCTL(fd, cmd, arg)	panel_ioctl(cmd, arg)
#define DEV_WRITE(fd, p, n)	panel_write(p, n)
#else
#define DEV_OPEN()		open("/dev/lcdgfx", O_RDWR)
#define DEV_READ(fd, p, n)	read(fd, p, n)
#define DEV_CLOSE(fd)		close(fd)
#define DEV_IOCTL(fd, cmd, arg)	ioctl(fd, cmd, arg)
#define DEV_WRITE(fd, p, n)	write(fd, p, n)
#endif

#define GFX_Y0		60		/* panel line of the adapter's line 0 */
#define LINES		200
#define FIELD		8000u		/* bytes of one field's 100 lines */
#define BANK_PAGES	8
#define SENT_PAGES	16		/* the lines last sent: 16,000 bytes */
#define K_TEXT		0
#define K_CGA4		1		/* 320x200, four colors */
#define K_CGA6		2		/* 640x200, two colors */

int gfx_on, gfx_graphic;

static int fd = -1;
static int kind;
/* The panel shows nothing current: 1, every line of the adapter's area
 * is to be cleared and drawn; 2, every graphics line is to be drawn whole
 * over the last graphics mode's lines, so no clear is needed. */
static int stale;
/* The panel's lines outside the adapter's area are black: set by the
 * first clear after an open, which covers all 320 lines because the
 * superuser's open keeps the console's screen. Nothing else this file
 * draws reaches them but the pointer mark, which draws its whole box. */
static int edge_black;
static unsigned pal_key = ~0u;		/* what the device's palette holds */
static uint8_t *bank[2];		/* even and odd scan lines */
static uint8_t reg_mode = 0x29, reg_color = 0x30;
static unsigned cur_shown = GFX_NOCURSOR;
static uint8_t line[LCDG_WIDTH];
static int pointer;			/* mouse keys are on: show the mark */
/* Graphics lines written since they were last sent, one bit a line. A
 * refresh adds the lines of the pages written since the last one; a drain,
 * once a poll (gfx_drain()), sends at most GFX_DRAIN of them, so a redraw
 * of the whole frame leaves the emulator running between its parts. */
#define GFX_DRAIN	16		/* 6.6 ms of wire at 12.5 MHz */
static uint8_t owed[LINES / 8];
static unsigned drain_at;		/* the line a drain looks at first */
/* The display memory of each graphics line as last sent, 80 bytes a line,
 * in page frames lent with the odd lines' when the pager has them; null
 * otherwise, and then every owed line goes out whole. A line's bit in
 * fresh says the panel shows its copy: a drain then sends only the bytes
 * that differ from it. A clear or a palette change makes every copy
 * stale. */
static uint8_t *sent;
static uint8_t fresh[LINES / 8];
int gfx_plain;				/* every owed line whole (-G) */
const char *gfx_capture_path;		/* -P */
/* In a text mode the adapter's 200 lines are the controller's scroll
 * area (VSCRDEF 60, 200, 220): a scroll of the whole screen moves the
 * scroll start (VSCSAD) by its rows and draws the new rows alone. Cell
 * row r is on the controller's line GFX_Y0 + (r + scroll_rows) % 25 * 8. */
static unsigned scroll_rows;
static int scroll_ok;			/* the device took the scroll area */

static void graphic_drain(unsigned max);

/* Device output. A failed command draws nothing and the next refresh
 * finds its pages rearmed, so nothing is retried: the device rejects
 * only what this file never sends. */

/* Returns 0 once all n bytes are written, -1 when the device stops. */
static int
put(const uint8_t *p, unsigned n)
{
	while (n) {
		long k = DEV_WRITE(fd, p, n);

		if (k <= 0)
			return (-1);
		p += k;
		n -= (unsigned)k;
	}
	return (0);
}

static int
window(unsigned x, unsigned y, unsigned w, unsigned h, int fmt, unsigned fg,
    unsigned bg)
{
	struct lcdg_window win;

	win.x = (u_short)x;
	win.y = (u_short)y;
	win.w = (u_short)w;
	win.h = (u_short)h;
	win.fmt = (u_char)fmt;
	win.fg = (u_char)fg;
	win.bg = (u_char)bg;
	return (DEV_IOCTL(fd, LCDG_WINDOW, &win));
}

static void
pal_set(unsigned index, unsigned irgb, int half)
{
	struct lcdg_palette p;
	unsigned lo = (irgb & 8) ? 0x55 : 0, hi = 0xaa;

	p.index = (u_char)index;
	p.r = (u_char)(((irgb & 4) ? hi : 0) + lo);
	p.g = (u_char)(((irgb & 2) ? hi : 0) + lo);
	p.b = (u_char)(((irgb & 1) ? hi : 0) + lo);
	if (irgb == 6)
		p.g = 0x55;		/* the monitor's brown */
	if (half) {
		p.r >>= 1;
		p.g >>= 1;
		p.b >>= 1;
	}
	(void)DEV_IOCTL(fd, LCDG_PALETTE, &p);
}

static void
palette(void)
{
	unsigned i, set, key, bright = (reg_color & 0x10) ? 8 : 0;

	if (!gfx_on)
		return;
	/* The registers the colors below depend on, for each kind. A write
	 * that leaves them as they were does not reach the device. */
	key = kind == K_TEXT ? 0 : kind == K_CGA4 ? 0x100 |
	    (reg_mode & 4) << 4 | (reg_color & 0x3f) : 0x200 | (reg_color & 15);
	if (key == pal_key)
		return;
	pal_key = key;
	switch (kind) {
	case K_TEXT:
		for (i = 0; i < 16; i++)
			pal_set(i, i, 0);
		break;
	case K_CGA4:
		/* Green, red and brown, or cyan, magenta and white; with
		 * the color burst off, cyan, red and white. */
		set = (reg_mode & 4) ? 0x743 : (reg_color & 0x20) ? 0x753 :
		    0x642;
		pal_set(0, reg_color & 15, 0);
		for (i = 1; i < 4; i++)
			pal_set(i, (set >> 4 * (i - 1) & 15) | bright, 0);
		break;
	default:
		pal_set(0, 0, 0);
		pal_set(1, reg_color & 15, 1);
		pal_set(2, reg_color & 15, 0);
		break;
	}
	/* What is on the panel has the old colors. */
	mem_watched = 0xffff;
	memset(fresh, 0, sizeof(fresh));
}

/* Put display memory where the kind of mode wants it. Returns -1 when
 * the pager has no frames to lend for a graphics mode. */
static int
kind_set(int k)
{
	if (k != K_TEXT && bank[1] == NULL) {
		sent = NULL;
		if (!gfx_plain &&
		    (bank[1] = mem_lend(BANK_PAGES + SENT_PAGES)) != NULL)
			sent = bank[1] + (BANK_PAGES << MEM_PGSHIFT);
		else if ((bank[1] = mem_lend(BANK_PAGES)) == NULL)
			return (-1);
		/* From cells to pixels: the cells' four kilobytes are the
		 * same memory in both and stay; the rest held the copy of
		 * the cells last drawn and the pager's pages, which are not
		 * display memory, and reads as zeros. */
		memset(bank[0] + 0x1000, 0, 0x1000);
		memset(bank[1], 0, BANK_PAGES << MEM_PGSHIFT);
	}
	if (k == K_TEXT && bank[1] != NULL) {
		mem_return();
		bank[1] = NULL;
		sent = NULL;
	}
	if (k == K_TEXT) {
		mem_watch(GFX_VRAM, 0, bank[0], 4);
		mem_watch(GFX_VRAM, 4, NULL, 12);
	} else {
		mem_watch(GFX_VRAM, 0, bank[0], BANK_PAGES);
		mem_watch(GFX_VRAM, BANK_PAGES, bank[1], BANK_PAGES);
	}
	/* Between two graphics modes every line is drawn whole and covers
	 * the last mode's; anything else clears the area first. */
	stale = stale == 1 || k == K_TEXT || kind == K_TEXT ? 1 : 2;
	kind = k;
	gfx_graphic = k != K_TEXT;
	palette();
	return (0);
}

void
gfx_init(uint8_t *vmem)
{
	bank[0] = vmem;
}

const uint8_t *
gfx_font(void)
{
	return (&font8[0][0]);
}

int
gfx_open(void)
{
	struct lcdg_colmod cm;

	fd = DEV_OPEN();
	if (fd < 0) {
		(void)kind_set(K_TEXT);
		return (-1);
	}
	gfx_on = 1;
	stale = 1;
	edge_black = 0;
	pal_key = ~0u;
	/* Two bytes a pixel on the wire where the device starts with
	 * three; the adapter's sixteen colors lose nothing by it. */
	cm.fmt = LCDG_COLMOD_RGB565;
	(void)DEV_IOCTL(fd, LCDG_COLMOD, &cm);
	(void)kind_set(kind);
	return (0);
}

void
gfx_close(void)
{
	if (!gfx_on)
		return;
	/* The panel keeps what was sent; the lines still owed go first. */
	if (!stale && kind != K_TEXT)
		graphic_drain(LINES);
	gfx_on = 0;
	(void)DEV_CLOSE(fd);
	fd = -1;
}

int
gfx_mode(unsigned mode)
{
	unsigned m = mode & 0x7f;
	int k = m == 6 ? K_CGA6 : (m == 4 || m == 5) ? K_CGA4 : K_TEXT;

	/* A graphics mode set again keeps the panel, and the lines its
	 * clear below changes are drawn like a program's own writes. */
	if ((k == K_TEXT || k != kind) && kind_set(k) != 0)
		k = K_TEXT;
	reg_mode = (uint8_t)(m == 6 ? 0x1e : m == 5 ? 0x2e : m == 4 ? 0x2a :
	    m < 2 ? 0x28 : 0x29);
	reg_color = m == 6 ? 0x3f : 0x30;
	mem_wb(0x465, reg_mode);
	mem_wb(0x466, reg_color);
	palette();
	if (k != K_TEXT && !(mode & 0x80)) {
		memset(bank[0], 0, BANK_PAGES << MEM_PGSHIFT);
		memset(bank[1], 0, BANK_PAGES << MEM_PGSHIFT);
		mem_watched = 0xffff;
	}
	return (k != K_TEXT);
}

void
gfx_out(unsigned port, unsigned v)
{
	int k;

	if (port == 0x3d9) {
		reg_color = (uint8_t)v;
		if (kind != K_TEXT)
			palette();
		return;
	}
	/* A program that sets the registers itself: follow it between
	 * cells and pixels, and leave a text mode's width alone. */
	k = !(v & 2) ? K_TEXT : (v & 0x10) ? K_CGA6 : K_CGA4;
	reg_mode = (uint8_t)v;
	if (k != kind)
		(void)kind_set(k);
	else if (kind == K_CGA4)
		palette();
}

/* Text. */

static void
cells_draw(unsigned cols, unsigned first, unsigned n, unsigned cursor)
{
	const uint8_t *cell = bank[0] + 2 * first;
	unsigned cw = cols == 80 ? 4 : 8, attr = cell[1];
	unsigned bytes = (n * cw + 7) / 8, y, k, bits;

	memset(line, 0, bytes * 8);
	for (y = 0; y < 8; y++)
		for (k = 0; k < n; k++) {
			unsigned ch = cell[2 * k];

			if (cw == 8)
				bits = font8[ch][y];
			else
				bits = font4[ch][y >> 1] >> ((y & 1) ? 0 : 4) & 15;
			if (first + k == cursor && y >= 6)
				bits = cw == 8 ? 0xff : 15;
			if (cw == 8)
				line[y * bytes + k] = (uint8_t)bits;
			else
				line[y * bytes + k / 2] |=
				    (uint8_t)(bits << ((k & 1) ? 0 : 4));
		}
	if (window(first % cols * cw,
	    GFX_Y0 + (first / cols + scroll_rows) % 25 * 8, n * cw, 8,
	    LCDG_WIN_BITS, attr & 15, attr >> 4 & 7) == 0)
		put(line, bytes * 8);
}

/* When the cells are those drawn moved up by k rows for at least two
 * rows more than they match unmoved, move the scroll start by k rows and
 * the copy of the cells drawn with it: the rows that leave the top come
 * in at the bottom, as the controller's lines do, and the compare below
 * draws what differs. */
static void
text_scroll(unsigned cols)
{
	struct lcdg_scroll sc;
	uint8_t *cell = bank[0], *shown = bank[0] + 0x1000;
	unsigned row = 2 * cols, best = 0, most = 0, k, r, same;

	if (!scroll_ok || memcmp(cell, shown, row) == 0)
		return;
	for (r = 0; r < 25; r++)
		most += memcmp(cell + r * row, shown + r * row, row) == 0;
	for (k = 1; k < 25; k++) {
		if (memcmp(cell, shown + k * row, row) != 0)
			continue;
		for (same = 0, r = 0; r + k < 25; r++)
			same += memcmp(cell + r * row, shown + (r + k) * row,
			    row) == 0;
		if (same > most + 1) {
			most = same;
			best = k;
		}
	}
	if (best == 0)
		return;
	sc.line = (u_short)(GFX_Y0 + (scroll_rows + best) % 25 * 8);
	if (DEV_IOCTL(fd, LCDG_SCROLL, &sc) != 0) {
		scroll_ok = 0;
		return;
	}
	scroll_rows = (scroll_rows + best) % 25;
	for (k = 0; k < best; k++) {
		memcpy(line, shown, row);
		memmove(shown, shown + row, 24 * row);
		memcpy(shown + 24 * row, line, row);
	}
	/* The cursor's mark moved with its cell. */
	if (cur_shown < 25 * cols)
		cur_shown = (cur_shown + (25 - best) * cols) % (25 * cols);
	mem_watched |= 0xf;
}

static void
text_refresh(unsigned cols, unsigned cursor)
{
	uint8_t *cell = bank[0], *shown = bank[0] + 0x1000;
	unsigned total = cols * 25, pages, i, n;

	text_scroll(cols);
	if (cursor >= total)
		cursor = GFX_NOCURSOR;
	if (cursor != cur_shown) {
		/* The cell that loses the cursor and the one that gets it
		 * are both out of date. */
		for (n = 0; n < 2; n++) {
			i = n ? cursor : cur_shown;
			if (i < total) {
				shown[2 * i] = (uint8_t)~cell[2 * i];
				mem_watched |= 1u << (2 * i >> MEM_PGSHIFT);
			}
		}
		cur_shown = cursor;
	}
	pages = mem_rearm();
	for (i = 0; i < total; i += n) {
		n = 1;
		if (!(pages >> (2 * i >> MEM_PGSHIFT) & 1)) {
			/* On to the first cell of the next page. */
			n = (((2 * i >> MEM_PGSHIFT) + 1) << MEM_PGSHIFT) / 2 - i;
			continue;
		}
		if (cell[2 * i] == shown[2 * i] &&
		    cell[2 * i + 1] == shown[2 * i + 1])
			continue;
		/* A run of changed cells of one attribute in one row. */
		while ((i + n) % cols != 0 &&
		    cell[2 * (i + n) + 1] == cell[2 * i + 1] &&
		    (cell[2 * (i + n)] != shown[2 * (i + n)] ||
		    cell[2 * (i + n) + 1] != shown[2 * (i + n) + 1]))
			n++;
		memcpy(shown + 2 * i, cell + 2 * i, 2 * n);
		cells_draw(cols, i, n, cursor);
	}
}

/* Graphics. */

static void
line_expand(unsigned y)
{
	const uint8_t *p = bank[y & 1] + (y >> 1) * 80;
	uint8_t *q = line;
	unsigned i, b;

	for (i = 0; i < 80; i++) {
		b = *p++;
		if (kind == K_CGA4) {
			*q++ = (uint8_t)(b >> 6);
			*q++ = b >> 4 & 3;
			*q++ = b >> 2 & 3;
			*q++ = b & 3;
		} else {
			*q++ = (uint8_t)((b >> 7) + (b >> 6 & 1));
			*q++ = (b >> 5 & 1) + (b >> 4 & 1);
			*q++ = (b >> 3 & 1) + (b >> 2 & 1);
			*q++ = (b >> 1 & 1) + (b & 1);
		}
	}
}

#define BIT(set, y)	((set)[(y) >> 3] >> ((y) & 7) & 1)
#define WIN_BYTES	11		/* CASET, RASET and RAMWR of a window */
#define COL_BYTES	8		/* a byte of a line: four pixels of two bytes */

/* The bytes of line y to send, from *at: those from the first to the last
 * that differ from the line's copy, none when it equals the copy, all 80
 * when the panel may show something else. */
static unsigned
line_span(unsigned y, unsigned *at)
{
	const uint8_t *p = bank[y & 1] + (y >> 1) * 80, *q;
	unsigned a, b;

	*at = 0;
	if (sent == NULL || !BIT(fresh, y))
		return (80);
	q = sent + y * 80;
	for (a = 0; a < 80 && p[a] == q[a]; a++)
		;
	if (a == 80)
		return (0);
	for (b = 80; p[b - 1] == q[b - 1]; b--)
		;
	*at = a;
	return (b - a);
}

/* Send at most max owed lines, a run of up to GFX_DRAIN adjacent ones a
 * window. A line's window covers the bytes that differ from its copy; the
 * next line joins it while one window of their joint columns costs no
 * more wire bytes than two. */
static void
graphic_drain(unsigned max)
{
	unsigned i, k, y = drain_at, n, a, w, a2, w2, lo, hi;
	int ok;

	/* The scan starts where the last drain stopped and wraps, so lines
	 * written again and again near the top cannot starve the rest. */
	for (k = 0; k < LINES && max > 0; k += n, y = (y + n) % LINES) {
		n = 1;
		if (!BIT(owed, y))
			continue;
		owed[y >> 3] &= (uint8_t)~(1u << (y & 7));
		if ((w = line_span(y, &a)) == 0)
			continue;
		while (n < max && n < GFX_DRAIN && y + n < LINES &&
		    BIT(owed, y + n) && (w2 = line_span(y + n, &a2)) != 0) {
			lo = a < a2 ? a : a2;
			hi = a + w > a2 + w2 ? a + w : a2 + w2;
			if (COL_BYTES * (n + 1) * (hi - lo) >
			    COL_BYTES * (n * w + w2) + WIN_BYTES)
				break;
			owed[(y + n) >> 3] &= (uint8_t)~(1u << ((y + n) & 7));
			a = lo;
			w = hi - lo;
			n++;
		}
		max -= n;
		drain_at = (y + n) % LINES;
		/* A line's copy holds what the panel shows only once all its
		 * pixels went out. */
		ok = window(4 * a, GFX_Y0 + y, 4 * w, n, LCDG_WIN_INDEX, 0, 0) == 0;
		for (i = y; i < y + n; i++) {
			if (ok) {
				line_expand(i);
				ok = put(line + 4 * a, 4 * w) == 0;
			}
			if (ok && sent != NULL) {
				memcpy(sent + i * 80, bank[i & 1] + (i >> 1) * 80,
				    80);
				fresh[i >> 3] |= (uint8_t)(1u << (i & 7));
			} else
				fresh[i >> 3] &= (uint8_t)~(1u << (i & 7));
		}
	}
}

static void
graphic_refresh(void)
{
	unsigned pages = mem_rearm(), i, r, y;

	for (i = 0; i < 2 * BANK_PAGES; i++) {
		if (!(pages >> i & 1))
			continue;
		/* The field lines page i of its bank has bytes of. */
		for (r = (i % BANK_PAGES << MEM_PGSHIFT) / 80;
		    r < LINES / 2 && r * 80 < (i % BANK_PAGES + 1) << MEM_PGSHIFT;
		    r++) {
			y = 2 * r + i / BANK_PAGES;
			owed[y >> 3] |= (uint8_t)(1u << (y & 7));
		}
	}
}

/*
 * The panel as the glass shows it, read back from the controller's memory
 * (LCDG_READ, kernels with LCD_GFX_EXT) into a binary PPM at
 * gfx_capture_path: each of the 320 glass lines from the memory line the
 * scroll area puts there, after the owed lines are sent. Returns 0, 1 when
 * gfx_capture_path is null or the panel is closed, and -1 when the device
 * or the file fails.
 */
int
gfx_capture(void)
{
	static const char hdr[] = "P6\n320 320\n255\n";
	struct lcdg_read rd;
	uint8_t out[3 * LCDG_WIDTH / 2];
	unsigned y, half, i, v;
	int f, ok;

	if (gfx_capture_path == NULL || !gfx_on)
		return (1);
	if (!stale && kind != K_TEXT)
		graphic_drain(LINES);
	if ((f = open(gfx_capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
		return (-1);
	ok = write(f, hdr, sizeof(hdr) - 1) == (long)sizeof(hdr) - 1;
	for (y = 0; ok && y < LCDG_HEIGHT; y++)
		for (half = 0; ok && half < 2; half++) {
			rd.x = (u_short)(half * LCDG_WIDTH / 2);
			rd.y = (u_short)(y < GFX_Y0 || y >= GFX_Y0 + LINES ? y :
			    GFX_Y0 + (y - GFX_Y0 + scroll_rows * 8) % LINES);
			rd.w = LCDG_WIDTH / 2;
			rd.h = 1;
			rd.fmt = LCDG_RD_RGB565;
			ok = DEV_IOCTL(fd, LCDG_READ, &rd) == 0 &&
			    DEV_READ(fd, line, LCDG_WIDTH) == LCDG_WIDTH;
			/* RGB565, low byte first, to 8-bit components by
			 * bit replication, as scrshot(1) writes them. */
			for (i = 0; ok && i < LCDG_WIDTH / 2; i++) {
				v = line[2 * i] | (unsigned)line[2 * i + 1] << 8;
				out[3 * i] = (uint8_t)((v >> 8 & 0xf8) | v >> 13);
				out[3 * i + 1] = (uint8_t)((v >> 3 & 0xfc) |
				    (v >> 9 & 3));
				out[3 * i + 2] = (uint8_t)((v << 3 & 0xf8) |
				    (v >> 2 & 7));
			}
			if (ok)
				ok = write(f, out, sizeof(out)) == (long)sizeof(out);
		}
	if (close(f) != 0)
		ok = 0;
	return (ok ? 0 : -1);
}

void
gfx_drain(void)
{
	if (gfx_on && !stale && kind != K_TEXT)
		graphic_drain(GFX_DRAIN);
}

/* The mark of mouse keys (pc.c): a white pointer arrow in the panel's top
 * margin while they are on, and black there otherwise. Fills take their
 * color as RGB565 whatever the mode's palette holds. */
#define PTR_X		308
#define PTR_Y		26

static void
pointer_draw(void)
{
	static const uint8_t rows[8] = {
		0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xd8, 0x8c, 0x06
	};
	struct lcdg_rect r;
	unsigned x, y, end;

	r.x0 = PTR_X;
	r.y0 = PTR_Y;
	r.x1 = PTR_X + 7;
	r.y1 = PTR_Y + 7;
	r.color = 0;
	(void)DEV_IOCTL(fd, LCDG_FILL, &r);
	if (!pointer)
		return;
	r.color = 0xffff;
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x = end + 1) {
			/* One fill a run of the row's pixels. */
			for (end = x; end < 8 && (rows[y] << end & 0x80); end++)
				;
			if (end == x)
				continue;
			r.x0 = (u_short)(PTR_X + x);
			r.x1 = (u_short)(PTR_X + end - 1);
			r.y0 = r.y1 = (u_short)(PTR_Y + y);
			(void)DEV_IOCTL(fd, LCDG_FILL, &r);
		}
}

void
gfx_pointer(int on)
{
	pointer = on;
	if (gfx_on)
		pointer_draw();
}

void
gfx_refresh(unsigned cols, unsigned cursor)
{
	struct lcdg_rect all;
	struct lcdg_scrdef sd;

	if (!gfx_on)
		return;
	if (stale) {
		/* A mode set or a panel taken back: black where the panel
		 * may show something else, then every cell or line. */
		all.x0 = 0;
		all.x1 = LCDG_WIDTH - 1;
		all.y0 = edge_black ? GFX_Y0 : 0;
		all.y1 = edge_black ? GFX_Y0 + LINES - 1 : LCDG_HEIGHT - 1;
		all.color = 0;
		if (!edge_black || stale == 1)
			(void)DEV_IOCTL(fd, LCDG_FILL, &all);
		if (!edge_black && pointer)
			pointer_draw();
		edge_black = 1;
		if (kind == K_TEXT)
			memset(bank[0] + 0x1000, 0, 0x1000);
		cur_shown = GFX_NOCURSOR;
		memset(owed, 0, sizeof(owed));
		memset(fresh, 0, sizeof(fresh));
		/* The scroll area with its start at the adapter's line 0;
		 * a nonzero start left by a text mode is only ever reset
		 * here after the clear of all 200 lines (stale 1). */
		sd.top = GFX_Y0;
		sd.bottom = LCDG_RAM_LINES - GFX_Y0 - LINES;
		scroll_rows = 0;
		scroll_ok = !gfx_plain && DEV_IOCTL(fd, LCDG_SCRDEF, &sd) == 0;
		mem_watched = 0xffff;
		stale = 0;
	}
	if (kind == K_TEXT)
		text_refresh(cols, cursor);
	else
		graphic_refresh();
}

/* The firmware's services in a graphics mode. They write display memory
 * through the watched path, so what they change is drawn like a
 * program's own writes. */

static uint32_t
line_addr(unsigned y)
{
	return (GFX_VRAM + (y & 1) * 0x2000 + (y >> 1) * 80);
}

void
gfx_putc(unsigned row, unsigned col, unsigned ch, unsigned color)
{
	unsigned y, x, bits, v, old;
	uint32_t a;

	if (row >= 25 || col >= (kind == K_CGA6 ? 80u : 40u))
		return;
	for (y = 0; y < 8; y++) {
		bits = font8[ch & 0xff][y];
		a = line_addr(row * 8 + y);
		if (kind == K_CGA6) {
			v = (color & 1) ? bits : 0;
			if (color & 0x80)
				v ^= mem_rb(a + col);
			mem_wb(a + col, v);
			continue;
		}
		for (v = 0, x = 0; x < 8; x++)
			if (bits >> (7 - x) & 1)
				v |= (color & 3) << (14 - 2 * x);
		if (color & 0x80) {
			old = mem_rb(a + 2 * col) << 8 | mem_rb(a + 2 * col + 1);
			v ^= old;
		}
		mem_wb(a + 2 * col, v >> 8);
		mem_wb(a + 2 * col + 1, v & 0xff);
	}
}

/* The fill is a byte of display memory, not a color: the IBM firmware
 * stores BH as it is (MOV AL,BH ahead of the row fill in its graphics
 * scroll, Technical Reference 6025005, BIOS listing). */
void
gfx_scroll(unsigned top, unsigned left, unsigned bot, unsigned right,
    unsigned n, unsigned fill, int down)
{
	unsigned per = kind == K_CGA6 ? 1 : 2;
	unsigned x0 = left * per, w = (right - left + 1) * per;
	unsigned h = (bot - top + 1) * 8, k, x, y;

	n *= 8;
	for (k = 0; k < h; k++) {
		y = down ? (bot + 1) * 8 - 1 - k : top * 8 + k;
		for (x = x0; x < x0 + w; x++)
			mem_wb(line_addr(y) + x, k >= h - n ? fill :
			    mem_rb(line_addr(down ? y - n : y + n) + x));
	}
}

int
gfx_video(struct i86 *c)
{
	unsigned ah = c->r[R_AX] >> 8, al = c->r[R_AX] & 0xff;
	unsigned bh = c->r[R_BX] >> 8, bl = c->r[R_BX] & 0xff;
	unsigned x = c->r[R_CX], y = c->r[R_DX], shift, mask, v;
	uint32_t a;

	switch (ah) {
	case 0x0b:
		if (bh == 0)
			reg_color = (uint8_t)((reg_color & 0x20) | (bl & 0x1f));
		else
			reg_color = (uint8_t)((reg_color & 0x1f) | (bl & 1) << 5);
		mem_wb(0x466, reg_color);
		palette();
		return (1);
	case 0x0c:
	case 0x0d:
		if (y >= LINES || x >= (kind == K_CGA6 ? 640u : 320u))
			return (1);
		if (kind == K_CGA6) {
			a = line_addr(y) + (x >> 3);
			shift = 7 - (x & 7);
			mask = 1;
		} else {
			a = line_addr(y) + (x >> 2);
			shift = 6 - 2 * (x & 3);
			mask = 3;
		}
		v = mem_rb(a);
		if (ah == 0x0d) {
			c->r[R_AX] = (uint16_t)(ah << 8 | (v >> shift & mask));
			return (1);
		}
		if (al & 0x80)
			v ^= (al & mask) << shift;
		else
			v = (v & ~(mask << shift)) | (al & mask) << shift;
		mem_wb(a, v);
		return (1);
	default:
		return (0);
	}
}
