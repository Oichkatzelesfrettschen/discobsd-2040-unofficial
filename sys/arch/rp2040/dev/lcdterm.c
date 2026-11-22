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
 * Character-cell terminal for the PicoCalc console; see lcdterm.h.
 *
 * The sequences accepted are those the xterm-basic entry of
 * lib/libtermlib/termcap/termcap.small emits, which /etc/ttys selects for
 * the console: cursor addressing and relative motion, erase in display
 * and line, insert and delete of lines and characters, a scrolling
 * region, insert mode, eight-color SGR with bold, underline and reverse,
 * save and restore cursor, index and reverse index, the DEC line-drawing
 * set, the cursor-key mode the keyboard driver consults, cursor
 * visibility, and the cursor-position and device-attribute reports.
 * Anything else is consumed and ignored. The terminal has auto margins
 * with the pending-wrap behavior termcap calls xn, and erases with the
 * current background color (termcap ut).
 *
 * A full-screen scroll moves one physical row out of the ring and
 * reprograms the panel's scroll start, which costs one row of fill.
 * Scrolls confined to a region, and line insertion or deletion, move
 * the cell store and redraw the affected rows cell by cell.
 *
 * While a pixel program owns the panel (gfx_active, set by lcdterm_gfx())
 * the terminal still interprets its input and updates the cell store, the
 * ring and the cursor, and skips the three panel primitives; leaving that
 * mode repaints from the store, so text written meanwhile appears.
 *
 * Cell codes 0x80-0xff address the glyph bank. The DEC line-drawing set
 * (ESC ( 0) maps 0x5f-0x7e onto bank codes 0x80-0x9f, which hold the real
 * graphics until a program replaces them with LCDIOC_SETGLYPH. A raw byte
 * 0xa0-0xff is stored as its own bank code. A raw byte 0x80-0x9f is a C1
 * control, which this terminal does not interpret; it draws as '?', so the
 * line-drawing codes stay reachable only through the escape and a stray
 * UTF-8 continuation byte cannot draw a box edge. The bank starts with '?'
 * in every code the escape cannot reach, so text that carries bytes above
 * 0x9f looks as it did before the bank existed.
 */

#include "lcdfont.h"
#include "lcdterm.h"

#define	LT_NPARAM	8
#define	LT_PARAM_MAX	9999

enum {
	ST_GROUND,
	ST_ESC,
	ST_CSI,
	ST_CHARSET,
};

/*
 * Everything that belongs to one console's screen, gathered so that a second
 * console can exchange it whole. The scalars come first so that the code reaches them with small offsets from one base
 * address. The panel-wide state stays outside: the pixel-program flag, the
 * cursor overlay (hidden before a save and redrawn after a repaint), the
 * glyph bank and the palette.
 */
struct lterm {
	int top;		/* Physical row shown on panel line 0. */
	int crow, ccol;		/* Logical cursor. */
	int wrapnext;		/* A glyph written to the last column. */
	int mtop, mbot;		/* Scrolling region, inclusive. */

	int sgr_fg, sgr_bg, sgr_bold, sgr_ul, sgr_rev;
	unsigned cattr;		/* Attribute for cells written now. */

	int autowrap, insertmode, cursor_on, appcursor, gfx;

	int s_row, s_col, s_fg, s_bg, s_bold, s_ul, s_rev, s_gfx;

	int state;
	int params[LT_NPARAM];
	int nparam;
	int priv;		/* '?' after the CSI. */
	int inter;		/* '!' intermediate. */

	unsigned char cells[LT_ROWS][LT_COLS];
	unsigned char attrs[LT_ROWS][LT_COLS];
};

static struct lterm lt;

static int gfx_active;		/* A pixel program owns the panel. */
#ifdef LCD_GFX_EXT
static int gfx_top;		/* lt.top when the pixel program took the panel. */
#endif

static int cursor_drawn, cd_prow, cd_col;
/*
 * The panel shows the cell store at every cell, the cursor overlay aside,
 * which lcdterm_putc() hides before any output: a glyph or an erase that
 * leaves a cell as the store already holds it is then not sent. A console
 * exchange leaves the other console's cells on the panel and clears it; a
 * full repaint and lcdterm_init() set it. The unseen-output marker covers
 * one cell the store does not describe, marker_prow's last column.
 */
static int panel_true, marker_prow = -1;

#define	LT_ACS_FIRST	0x5f		/* First code the DEC set replaces. */
#define	LT_ACS_N	32		/* 0x5f through 0x7e. */
#define	LT_C1_END	0xa0		/* Raw bytes below it are C1 controls. */

/*
 * The DEC special graphics shapes, in the order of the codes they replace.
 * One stroke is one pixel wide; vertical strokes sit in column 2 and
 * horizontal strokes in row 4, so adjacent cells join. The box edges
 * reach the cell borders: a stroke on glyph row 0 or 8 continues through
 * the repeated tenth row.
 */
static const unsigned char dec_graphics[LT_ACS_N][LT_GLYPH_ROWS] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	/* 0x5f blank */
	{ 0x00, 0x00, 0x30, 0x78, 0xfc, 0x78, 0x30, 0x00, 0x00 },	/* 0x60 diamond */
	{ 0xa8, 0x54, 0xa8, 0x54, 0xa8, 0x54, 0xa8, 0x54, 0xa8 },	/* 0x61 checker */
	{ 0xa0, 0xa0, 0xe0, 0xa0, 0xbc, 0x08, 0x08, 0x08, 0x08 },	/* 0x62 HT */
	{ 0xe0, 0x80, 0xc0, 0x80, 0x9c, 0x10, 0x18, 0x10, 0x10 },	/* 0x63 FF */
	{ 0xe0, 0x80, 0x80, 0x80, 0xf8, 0x14, 0x18, 0x14, 0x14 },	/* 0x64 CR */
	{ 0x80, 0x80, 0x80, 0x80, 0xfc, 0x10, 0x18, 0x10, 0x10 },	/* 0x65 LF */
	{ 0x00, 0x30, 0x48, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },	/* 0x66 degree */
	{ 0x00, 0x20, 0x20, 0xf8, 0x20, 0x20, 0x00, 0xf8, 0x00 },	/* 0x67 plus-minus */
	{ 0xa0, 0xe0, 0xe0, 0xa0, 0xb0, 0x10, 0x10, 0x10, 0x1c },	/* 0x68 NL */
	{ 0xa0, 0xa0, 0xa0, 0xa0, 0x5c, 0x08, 0x08, 0x08, 0x08 },	/* 0x69 VT */
	{ 0x20, 0x20, 0x20, 0x20, 0xe0, 0x00, 0x00, 0x00, 0x00 },	/* 0x6a lower-right corner */
	{ 0x00, 0x00, 0x00, 0x00, 0xe0, 0x20, 0x20, 0x20, 0x20 },	/* 0x6b upper-right corner */
	{ 0x00, 0x00, 0x00, 0x00, 0x3c, 0x20, 0x20, 0x20, 0x20 },	/* 0x6c upper-left corner */
	{ 0x20, 0x20, 0x20, 0x20, 0x3c, 0x00, 0x00, 0x00, 0x00 },	/* 0x6d lower-left corner */
	{ 0x20, 0x20, 0x20, 0x20, 0xfc, 0x20, 0x20, 0x20, 0x20 },	/* 0x6e crossing */
	{ 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	/* 0x6f scan line 1 */
	{ 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	/* 0x70 scan line 3 */
	{ 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00 },	/* 0x71 horizontal line */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00 },	/* 0x72 scan line 7 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc },	/* 0x73 scan line 9 */
	{ 0x20, 0x20, 0x20, 0x20, 0x3c, 0x20, 0x20, 0x20, 0x20 },	/* 0x74 left tee */
	{ 0x20, 0x20, 0x20, 0x20, 0xe0, 0x20, 0x20, 0x20, 0x20 },	/* 0x75 right tee */
	{ 0x20, 0x20, 0x20, 0x20, 0xfc, 0x00, 0x00, 0x00, 0x00 },	/* 0x76 bottom tee */
	{ 0x00, 0x00, 0x00, 0x00, 0xfc, 0x20, 0x20, 0x20, 0x20 },	/* 0x77 top tee */
	{ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 },	/* 0x78 vertical line */
	{ 0x00, 0x0c, 0x30, 0xc0, 0x30, 0x0c, 0x00, 0xfc, 0x00 },	/* 0x79 less or equal */
	{ 0x00, 0xc0, 0x30, 0x0c, 0x30, 0xc0, 0x00, 0xfc, 0x00 },	/* 0x7a greater or equal */
	{ 0x00, 0x00, 0xfc, 0x48, 0x48, 0x48, 0x48, 0x00, 0x00 },	/* 0x7b pi */
	{ 0x00, 0x04, 0x08, 0xfc, 0x20, 0xfc, 0x80, 0x00, 0x00 },	/* 0x7c not equal */
	{ 0x00, 0x30, 0x48, 0x40, 0xe0, 0x40, 0x40, 0xf8, 0x00 },	/* 0x7d pound sterling */
	{ 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00 },	/* 0x7e bullet */
};

/*
 * The bank is RAM; lcdterm_init() and ESC c fill it. A glyph row carries six
 * pixels (LT_GLYPH_MASK), so a glyph's nine rows pack into 54 bits, stored
 * least significant bit first in LT_GLYPH_PACKED bytes: 896 bytes for the
 * bank instead of 1,152.
 */
#define	LT_GLYPH_BITS	6
#define	LT_GLYPH_SHIFT	2		/* Bits 7-2 of a row are the pixels. */
#define	LT_GLYPH_PACKED	((LT_GLYPH_ROWS * LT_GLYPH_BITS + 7) / 8)
_Static_assert(LT_GLYPH_MASK == (((1u << LT_GLYPH_BITS) - 1) << LT_GLYPH_SHIFT),
    "the packed bank holds bits 7-2 of each row");
static unsigned char glyph_bank[LT_GLYPH_N][LT_GLYPH_PACKED];

static void
glyph_pack(unsigned char *packed, const unsigned char *rows)
{
	unsigned acc = 0;
	int i, nbits = 0, k = 0;

	for (i = 0; i < LT_GLYPH_ROWS; i++) {
		acc |= (unsigned)(rows[i] >> LT_GLYPH_SHIFT) << nbits;
		for (nbits += LT_GLYPH_BITS; nbits >= 8; nbits -= 8) {
			packed[k++] = (unsigned char)acc;
			acc >>= 8;
		}
	}
	if (nbits > 0)
		packed[k] = (unsigned char)acc;
}

static void
glyph_unpack(unsigned char *rows, const unsigned char *packed)
{
	unsigned acc = 0;
	int i, nbits = 0, k = 0;

	for (i = 0; i < LT_GLYPH_ROWS; i++) {
		for (; nbits < LT_GLYPH_BITS; nbits += 8)
			acc |= (unsigned)packed[k++] << nbits;
		rows[i] = (unsigned char)((acc & ((1u << LT_GLYPH_BITS) - 1)) <<
		    LT_GLYPH_SHIFT);
		acc >>= LT_GLYPH_BITS;
		nbits -= LT_GLYPH_BITS;
	}
}

static int
phys(int r)
{
	return (lt.top + r) % LT_ROWS;
}

/*
 * The three panel primitives, skipped while gfx_active: the cell store,
 * the ring and the cursor keep their state, and lcdterm_gfx(0) repaints
 * the panel from them.
 */
static void
panel_cell(int prow, int col, int ch, unsigned attr)
{
	if (!gfx_active)
		lcd_draw_cell(prow, col, ch, attr);
}

static void
panel_fill(int prow, int col, int n, unsigned attr)
{
	if (!gfx_active)
		lcd_fill_cells(prow, col, n, attr);
}

static void
panel_top(int prow)
{
	if (!gfx_active)
		lcd_set_top(prow);
}

/* Bank codes 0-31 take the line-drawing shapes, the rest the '?' glyph. */
static void
glyph_defaults(void)
{
	int i;

	for (i = 0; i < LT_GLYPH_N; i++)
		glyph_pack(glyph_bank[i], i < LT_ACS_N ? dec_graphics[i] :
		    lcdfont['?' - LCDFONT_FIRST]);
}

int
lcdterm_glyph(int code, unsigned char rows[LT_GLYPH_ROWS])
{
	if (code < LT_GLYPH_FIRST || code >= LT_GLYPH_FIRST + LT_GLYPH_N)
		return 0;
	glyph_unpack(rows, glyph_bank[code - LT_GLYPH_FIRST]);
	return 1;
}

static void
resolve_attr(void)
{
	int fg = lt.sgr_fg, bg = lt.sgr_bg;

	if (lt.sgr_rev) {
		fg = lt.sgr_bg;
		bg = lt.sgr_fg;
	}
	if (lt.sgr_bold)
		fg |= 8;
	lt.cattr = LT_ATTR(fg, bg, lt.sgr_ul);
}

/* Erased cells take the background the current rendition displays. */
static unsigned
erase_attr(void)
{
	return LT_ATTR(7, lt.sgr_rev ? lt.sgr_fg : lt.sgr_bg, 0);
}

/* Whether the panel already shows ch in attr at physical cell (p, c). */
static int
shown(int p, int c, int ch, unsigned attr)
{
	return panel_true && lt.cells[p][c] == ch && lt.attrs[p][c] == attr &&
	    !(p == marker_prow && c == LT_COLS - 1);
}

static void
draw(int r, int c)
{
	int p = phys(r);

	panel_cell(p, c, lt.cells[p][c], lt.attrs[p][c]);
}

static void
redraw_span(int r, int c0, int c1)
{
	int c;

	for (c = c0; c <= c1; c++)
		draw(r, c);
}

static void
cursor_hide(void)
{
	if (cursor_drawn) {
		panel_cell(cd_prow, cd_col, lt.cells[cd_prow][cd_col],
		    lt.attrs[cd_prow][cd_col]);
		cursor_drawn = 0;
	}
}

void
lcdterm_cursor_show(void)
{
	unsigned a, inv;
	int p;

	if (!lt.cursor_on || cursor_drawn)
		return;
	p = phys(lt.crow);
	a = lt.attrs[p][lt.ccol];
	inv = LT_ATTR(LT_ATTR_BG(a), LT_ATTR_FG(a) & 7, a & LT_ATTR_UL);
	panel_cell(p, lt.ccol, lt.cells[p][lt.ccol], inv);
	cursor_drawn = 1;
	cd_prow = p;
	cd_col = lt.ccol;
}

/* Blank n cells of logical row r from column c, in store and on panel. */
static void
erase_cells(int r, int c, int n)
{
	unsigned a = erase_attr();
	int p = phys(r), i;

	if (c < 0) {
		n += c;
		c = 0;
	}
	if (c + n > LT_COLS)
		n = LT_COLS - c;
	if (n <= 0)
		return;
	/* Only the span from the first to the last cell not yet blank in a. */
	while (n > 0 && shown(p, c, ' ', a)) {
		c++;
		n--;
	}
	while (n > 0 && shown(p, c + n - 1, ' ', a))
		n--;
	if (n <= 0)
		return;
	for (i = c; i < c + n; i++) {
		lt.cells[p][i] = ' ';
		lt.attrs[p][i] = (unsigned char)a;
	}
	panel_fill(p, c, n, a);
}

static void
erase_rows(int r0, int r1)
{
	int r;

	for (r = r0; r <= r1; r++)
		erase_cells(r, 0, LT_COLS);
}

static void
copy_row(int dst, int src)
{
	int pd = phys(dst), ps = phys(src), c;

	for (c = 0; c < LT_COLS; c++) {
		lt.cells[pd][c] = lt.cells[ps][c];
		lt.attrs[pd][c] = lt.attrs[ps][c];
	}
}

/* Scroll logical rows t..b up by n, blanking the n rows that open. */
static void
scroll_up(int t, int b, int n)
{
	int r;

	if (n > b - t + 1)
		n = b - t + 1;
	if (n <= 0)
		return;
	if (t == 0 && b == LT_ROWS - 1) {
		while (n-- > 0) {
			erase_cells(0, 0, LT_COLS);
			lt.top = (lt.top + 1) % LT_ROWS;
			panel_top(lt.top);
		}
		return;
	}
	for (r = t; r + n <= b; r++) {
		copy_row(r, r + n);
		redraw_span(r, 0, LT_COLS - 1);
	}
	erase_rows(b - n + 1, b);
}

/* Scroll logical rows t..b down by n, blanking the n rows that open. */
static void
scroll_down(int t, int b, int n)
{
	int r;

	if (n > b - t + 1)
		n = b - t + 1;
	if (n <= 0)
		return;
	if (t == 0 && b == LT_ROWS - 1) {
		while (n-- > 0) {
			erase_cells(LT_ROWS - 1, 0, LT_COLS);
			lt.top = (lt.top + LT_ROWS - 1) % LT_ROWS;
			panel_top(lt.top);
		}
		return;
	}
	for (r = b; r - n >= t; r--) {
		copy_row(r, r - n);
		redraw_span(r, 0, LT_COLS - 1);
	}
	erase_rows(t, t + n - 1);
}

static void
index_down(void)
{
	if (lt.crow == lt.mbot)
		scroll_up(lt.mtop, lt.mbot, 1);
	else if (lt.crow < LT_ROWS - 1)
		lt.crow++;
}

static void
index_up(void)
{
	if (lt.crow == lt.mtop)
		scroll_down(lt.mtop, lt.mbot, 1);
	else if (lt.crow > 0)
		lt.crow--;
}

static void
put_glyph(int ch)
{
	int p, c, same;

	if (lt.wrapnext) {
		lt.wrapnext = 0;
		lt.ccol = 0;
		index_down();
	}
	p = phys(lt.crow);
	same = !lt.insertmode && shown(p, lt.ccol, ch, lt.cattr);
	if (lt.insertmode) {
		for (c = LT_COLS - 1; c > lt.ccol; c--) {
			lt.cells[p][c] = lt.cells[p][c - 1];
			lt.attrs[p][c] = lt.attrs[p][c - 1];
		}
	}
	lt.cells[p][lt.ccol] = (unsigned char)ch;
	lt.attrs[p][lt.ccol] = (unsigned char)lt.cattr;
	if (lt.insertmode)
		redraw_span(lt.crow, lt.ccol, LT_COLS - 1);
	else if (!same)
		draw(lt.crow, lt.ccol);
	if (lt.ccol == LT_COLS - 1) {
		if (lt.autowrap)
			lt.wrapnext = 1;
	} else
		lt.ccol++;
}

static int
clamp(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

/* Parameter i, with 0 and absence both meaning dflt. */
static int
param(int i, int dflt)
{
	if (i >= lt.nparam || lt.params[i] == 0)
		return dflt;
	return lt.params[i];
}

static void
reply_number(char *buf, int *len, int v)
{
	char tmp[6];
	int n = 0;

	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v > 0 && n < (int)sizeof(tmp));
	while (n > 0)
		buf[(*len)++] = tmp[--n];
}

static void
report_cursor(void)
{
	char buf[16];
	int len = 0;

	buf[len++] = '\033';
	buf[len++] = '[';
	reply_number(buf, &len, lt.crow + 1);
	buf[len++] = ';';
	reply_number(buf, &len, lt.ccol + 1);
	buf[len++] = 'R';
	lcdterm_reply(buf, len);
}

static void
reset_modes(void)
{
	lt.sgr_fg = 7;
	lt.sgr_bg = 0;
	lt.sgr_bold = lt.sgr_ul = lt.sgr_rev = 0;
	resolve_attr();
	lt.mtop = 0;
	lt.mbot = LT_ROWS - 1;
	lt.autowrap = 1;
	lt.insertmode = 0;
	lt.cursor_on = 1;
	lt.appcursor = 0;
	lt.gfx = 0;
	lt.wrapnext = 0;
	lt.s_row = lt.s_col = 0;
	lt.s_fg = 7;
	lt.s_bg = lt.s_bold = lt.s_ul = lt.s_rev = lt.s_gfx = 0;
}

static void
do_sgr(void)
{
	int i, v;

	if (lt.nparam == 0)
		lt.nparam = 1, lt.params[0] = 0;
	for (i = 0; i < lt.nparam; i++) {
		v = lt.params[i];
		/*
		 * The bright codes branch off first so the chain below spans
		 * 0-49 only; a wider span makes the compiler lower the chain
		 * to a larger jump table.
		 */
		if (v >= 90) {
			if (v <= 97)
				lt.sgr_fg = v - 90 + 8;
			else if (v >= 100 && v <= 107)
				lt.sgr_bg = v - 100;
		} else if (v == 0) {
			lt.sgr_fg = 7;
			lt.sgr_bg = 0;
			lt.sgr_bold = lt.sgr_ul = lt.sgr_rev = 0;
		} else if (v == 1)
			lt.sgr_bold = 1;
		else if (v == 4)
			lt.sgr_ul = 1;
		else if (v == 7)
			lt.sgr_rev = 1;
		else if (v == 22)
			lt.sgr_bold = 0;
		else if (v == 24)
			lt.sgr_ul = 0;
		else if (v == 27)
			lt.sgr_rev = 0;
		else if (v >= 30 && v <= 37)
			lt.sgr_fg = v - 30;
		else if (v == 39)
			lt.sgr_fg = 7;
		else if (v >= 40 && v <= 47)
			lt.sgr_bg = v - 40;
		else if (v == 49)
			lt.sgr_bg = 0;
	}
	resolve_attr();
}

static void
set_mode(int on)
{
	int i;

	for (i = 0; i < lt.nparam; i++) {
		if (lt.priv) {
			switch (lt.params[i]) {
			case 1:
				lt.appcursor = on;
				break;
			case 7:
				lt.autowrap = on;
				if (!on)
					lt.wrapnext = 0;
				break;
			case 25:
				lt.cursor_on = on;
				break;
			}
		} else if (lt.params[i] == 4)
			lt.insertmode = on;
	}
}

static void
do_csi(int final)
{
	int n, r, c, lo, hi;

	if (lt.inter) {
		if (lt.inter == '!' && final == 'p') {
			reset_modes();	/* DECSTR. */
		}
		return;
	}
	switch (final) {
	case 'A':
	case 'B':
		n = param(0, 1);
		lo = (lt.crow >= lt.mtop && lt.crow <= lt.mbot) ? lt.mtop : 0;
		hi = (lt.crow >= lt.mtop && lt.crow <= lt.mbot) ? lt.mbot : LT_ROWS - 1;
		lt.crow = clamp(final == 'A' ? lt.crow - n : lt.crow + n, lo, hi);
		lt.wrapnext = 0;
		break;
	case 'C':
		lt.ccol = clamp(lt.ccol + param(0, 1), 0, LT_COLS - 1);
		lt.wrapnext = 0;
		break;
	case 'D':
		lt.ccol = clamp(lt.ccol - param(0, 1), 0, LT_COLS - 1);
		lt.wrapnext = 0;
		break;
	case 'G':
		lt.ccol = clamp(param(0, 1) - 1, 0, LT_COLS - 1);
		lt.wrapnext = 0;
		break;
	case 'd':
		lt.crow = clamp(param(0, 1) - 1, 0, LT_ROWS - 1);
		lt.wrapnext = 0;
		break;
	case 'H':
	case 'f':
		lt.crow = clamp(param(0, 1) - 1, 0, LT_ROWS - 1);
		lt.ccol = clamp(param(1, 1) - 1, 0, LT_COLS - 1);
		lt.wrapnext = 0;
		break;
	case 'J':
		switch (lt.nparam > 0 ? lt.params[0] : 0) {
		case 0:
			erase_cells(lt.crow, lt.ccol, LT_COLS - lt.ccol);
			if (lt.crow < LT_ROWS - 1)
				erase_rows(lt.crow + 1, LT_ROWS - 1);
			break;
		case 1:
			if (lt.crow > 0)
				erase_rows(0, lt.crow - 1);
			erase_cells(lt.crow, 0, lt.ccol + 1);
			break;
		case 2:
			erase_rows(0, LT_ROWS - 1);
			break;
		}
		break;
	case 'K':
		switch (lt.nparam > 0 ? lt.params[0] : 0) {
		case 0:
			erase_cells(lt.crow, lt.ccol, LT_COLS - lt.ccol);
			break;
		case 1:
			erase_cells(lt.crow, 0, lt.ccol + 1);
			break;
		case 2:
			erase_cells(lt.crow, 0, LT_COLS);
			break;
		}
		break;
	case 'L':
		if (lt.crow >= lt.mtop && lt.crow <= lt.mbot)
			scroll_down(lt.crow, lt.mbot, param(0, 1));
		lt.ccol = 0;
		lt.wrapnext = 0;
		break;
	case 'M':
		if (lt.crow >= lt.mtop && lt.crow <= lt.mbot)
			scroll_up(lt.crow, lt.mbot, param(0, 1));
		lt.ccol = 0;
		lt.wrapnext = 0;
		break;
	case '@':
		n = clamp(param(0, 1), 1, LT_COLS - lt.ccol);
		r = phys(lt.crow);
		for (c = LT_COLS - 1; c >= lt.ccol + n; c--) {
			lt.cells[r][c] = lt.cells[r][c - n];
			lt.attrs[r][c] = lt.attrs[r][c - n];
		}
		erase_cells(lt.crow, lt.ccol, n);
		redraw_span(lt.crow, lt.ccol + n, LT_COLS - 1);
		lt.wrapnext = 0;
		break;
	case 'P':
		n = clamp(param(0, 1), 1, LT_COLS - lt.ccol);
		r = phys(lt.crow);
		for (c = lt.ccol; c + n < LT_COLS; c++) {
			lt.cells[r][c] = lt.cells[r][c + n];
			lt.attrs[r][c] = lt.attrs[r][c + n];
		}
		redraw_span(lt.crow, lt.ccol, LT_COLS - n - 1);
		erase_cells(lt.crow, LT_COLS - n, n);
		lt.wrapnext = 0;
		break;
	case 'X':
		erase_cells(lt.crow, lt.ccol, clamp(param(0, 1), 1, LT_COLS - lt.ccol));
		break;
	case 'r':
		lo = param(0, 1) - 1;
		hi = param(1, LT_ROWS) - 1;
		if (lo < hi && hi < LT_ROWS) {
			lt.mtop = lo;
			lt.mbot = hi;
			lt.crow = lt.ccol = 0;
			lt.wrapnext = 0;
		}
		break;
	case 'm':
		do_sgr();
		break;
	case 'h':
		set_mode(1);
		break;
	case 'l':
		set_mode(0);
		break;
	case 'n':
		if (lt.priv)
			break;
		if (lt.nparam > 0 && lt.params[0] == 6)
			report_cursor();
		else if (lt.nparam > 0 && lt.params[0] == 5)
			lcdterm_reply("\033[0n", 4);
		break;
	case 'c':
		if (!lt.priv && param(0, 0) == 0)
			lcdterm_reply("\033[?6c", 5);
		break;
	}
}

static void
save_cursor(void)
{
	lt.s_row = lt.crow;
	lt.s_col = lt.ccol;
	lt.s_fg = lt.sgr_fg;
	lt.s_bg = lt.sgr_bg;
	lt.s_bold = lt.sgr_bold;
	lt.s_ul = lt.sgr_ul;
	lt.s_rev = lt.sgr_rev;
	lt.s_gfx = lt.gfx;
}

static void
restore_cursor(void)
{
	lt.crow = lt.s_row;
	lt.ccol = lt.s_col;
	lt.sgr_fg = lt.s_fg;
	lt.sgr_bg = lt.s_bg;
	lt.sgr_bold = lt.s_bold;
	lt.sgr_ul = lt.s_ul;
	lt.sgr_rev = lt.s_rev;
	lt.gfx = lt.s_gfx;
	lt.wrapnext = 0;
	resolve_attr();
}

static void
full_reset(void)
{
	reset_modes();
	lt.top = 0;
	panel_top(0);
	erase_rows(0, LT_ROWS - 1);
	lt.crow = lt.ccol = 0;
	lt.state = ST_GROUND;
}

/*
 * Forget a sequence, a drawn cursor and the modes an abandoned drawer or
 * the last program left behind; lcdcons.c calls this on the fatal path,
 * where the interrupted drawer never resumes. The fault report then draws
 * in plain ASCII with full-screen scrolling, and the panel's scroll start
 * is written again in case the drawer stopped between moving the ring and
 * telling the panel.
 */
void
lcdterm_abort(void)
{
	lt.state = ST_GROUND;
	cursor_drawn = 0;
	panel_true = 0;		/* the drawer may have stopped mid-cell */
	reset_modes();
	if (gfx_active)
		lcdterm_gfx(0);
	else
		lcd_set_top(lt.top);
}

/*
 * Draw every cell of the store and the cursor on the panel, with the scroll
 * start at the ring top, whatever the panel showed before. The caller holds
 * the drawing flag of lcdcons.c.
 */
void
lcdterm_repaint(void)
{
	int p, c;

	cursor_drawn = 0;
	lcd_set_top(lt.top);
	for (p = 0; p < LT_ROWS; p++)
		for (c = 0; c < LT_COLS; c++)
			lcd_draw_cell(p, c, lt.cells[p][c], lt.attrs[p][c]);
	panel_true = 1;
	marker_prow = -1;
	lcdterm_cursor_show();
}

/*
 * Hand the panel to a pixel program (on) or take it back (off). Entering
 * only stops the panel primitives; leaving returns the panel to the text
 * configuration, which a pixel program may have changed, and repaints.
 */
void
lcdterm_gfx(int on)
{
	cursor_drawn = 0;
	gfx_active = on;
	panel_true = 0;
#ifdef LCD_GFX_EXT
	if (on)
		gfx_top = lt.top;
#endif
	if (!on) {
		lcd_gfx_reset();
		lcdterm_repaint();
	}
}

void
lcdterm_init(void)
{
	cursor_drawn = 0;
	gfx_active = 0;
	panel_true = 0;
	marker_prow = -1;
	glyph_defaults();
	full_reset();
#ifdef LCD_VT
	/*
	 * The frozen console starts as a reset terminal too: reset its state
	 * in the live struct with the panel primitives off, then swap back.
	 */
	lcdterm_exchange();
	gfx_active = 1;
	full_reset();
	gfx_active = 0;
	lcdterm_exchange();
#endif
	panel_true = 1;
}

static void
esc_final(int c)
{
	lt.state = ST_GROUND;
	switch (c) {
	case '[':
		lt.state = ST_CSI;
		lt.nparam = 0;
		lt.params[0] = 0;
		lt.priv = lt.inter = 0;
		break;
	case '(':
		lt.state = ST_CHARSET;
		lt.inter = 0;	/* G0; a prior CSI may have left '!'. */
		break;
	case ')':
	case '*':
	case '+':
		lt.state = ST_CHARSET;
		lt.inter = c;	/* Designates G1-G3: consumed, ignored. */
		break;
	case '7':
		save_cursor();
		break;
	case '8':
		restore_cursor();
		break;
	case 'D':
		lt.wrapnext = 0;
		index_down();
		break;
	case 'E':
		lt.ccol = 0;
		lt.wrapnext = 0;
		index_down();
		break;
	case 'M':
		lt.wrapnext = 0;
		index_up();
		break;
	case 'c':
		glyph_defaults();	/* RIS discards uploaded glyphs. */
		full_reset();
		break;
	default:
		break;		/* ESC =, ESC >, ESC H, ESC l, ESC m. */
	}
}

static void
csi_byte(int c)
{
	if (c >= '0' && c <= '9') {
		if (lt.nparam == 0)
			lt.nparam = 1;
		if (lt.params[lt.nparam - 1] < LT_PARAM_MAX)
			lt.params[lt.nparam - 1] = lt.params[lt.nparam - 1] * 10 + (c - '0');
	} else if (c == ';') {
		if (lt.nparam == 0)
			lt.nparam = 1;
		if (lt.nparam < LT_NPARAM)
			lt.params[lt.nparam++] = 0;
	} else if (c == '?' || c == '>') {
		lt.priv = c;
	} else if (c >= 0x20 && c <= 0x2f) {
		lt.inter = c;
	} else if (c >= 0x40 && c <= 0x7e) {
		lt.state = ST_GROUND;
		do_csi(c);
	} else if (c == 0x18 || c == 0x1a) {
		lt.state = ST_GROUND;	/* CAN, SUB abort the sequence. */
	}
}

static void
control(int c)
{
	switch (c) {
	case '\b':
		/*
		 * Backspace from a pending wrap returns onto the last column,
		 * so the tty's rubout ("\b \b") of the 53rd character of a
		 * line erases that character rather than the one before it.
		 */
		if (lt.wrapnext)
			lt.wrapnext = 0;
		else if (lt.ccol > 0)
			lt.ccol--;
		break;
	case '\t':
		lt.wrapnext = 0;
		lt.ccol = clamp((lt.ccol / 8 + 1) * 8, 0, LT_COLS - 1);
		break;
	case '\n':
	case '\v':
	case '\f':
		lt.wrapnext = 0;
		index_down();
		break;
	case '\r':
		lt.ccol = 0;
		lt.wrapnext = 0;
		break;
	case 0x1b:
		lt.state = ST_ESC;
		break;
	case '\a':
		lcd_bell();
		break;
	default:
		break;		/* SO, SI and the rest. */
	}
}

void
lcdterm_putc(int c)
{
	c &= 0xff;
	cursor_hide();

	switch (lt.state) {
	case ST_ESC:
		if (c == 0x1b)
			return;
		if (c < 0x20) {
			lt.state = ST_GROUND;
			control(c);
			return;
		}
		esc_final(c);
		return;
	case ST_CSI:
		if (c < 0x20 && c != 0x18 && c != 0x1a) {
			if (c == 0x1b)
				lt.state = ST_ESC;
			else
				control(c);
			return;
		}
		csi_byte(c);
		return;
	case ST_CHARSET:
		if (lt.inter == 0)
			lt.gfx = (c == '0');
		lt.inter = 0;
		lt.state = ST_GROUND;
		return;
	default:
		break;
	}

	if (c < 0x20) {
		control(c);
		return;
	}
	if (c == 0x7f)
		return;
	if (c >= LT_GLYPH_FIRST) {
		if (c < LT_C1_END)
			c = '?';
	} else if (lt.gfx && c >= LT_ACS_FIRST)
		c = LT_GLYPH_FIRST + (c - LT_ACS_FIRST);
	put_glyph(c);
}

int
lcdterm_setglyph(int code, const unsigned char *rows)
{
	int i, r, c;

	if (code < LT_GLYPH_FIRST || code >= LT_GLYPH_FIRST + LT_GLYPH_N)
		return -1;
	for (i = 0; i < LT_GLYPH_ROWS; i++)
		if ((rows[i] & ~LT_GLYPH_MASK) != 0)
			return -1;
	glyph_pack(glyph_bank[code - LT_GLYPH_FIRST], rows);
	cursor_hide();
	for (r = 0; r < LT_ROWS; r++)
		for (c = 0; c < LT_COLS; c++)
			if (lt.cells[phys(r)][c] == code)
				draw(r, c);
	return 0;
}

int
lcdterm_app_cursor(void)
{
	return lt.appcursor;
}

int
lcdterm_cell(int row, int col)
{
	return lt.cells[phys(row)][col];
}

unsigned
lcdterm_attr(int row, int col)
{
	return lt.attrs[phys(row)][col];
}

int
lcdterm_top(void)
{
	return lt.top;
}

#ifdef LCD_VT
/*
 * The screen state of the frozen console. lcdterm_exchange() swaps it with
 * lt word by word, with no third buffer; __may_alias__ lets the unsigned
 * accesses read and write the struct's int and byte members.
 */
typedef unsigned __attribute__((__may_alias__)) lt_word;
_Static_assert(sizeof(struct lterm) % sizeof(lt_word) == 0,
    "the exchange moves whole words");
static struct lterm lt_frozen;

void
lcdterm_exchange(void)
{
	lt_word *a = (lt_word *)&lt, *b = (lt_word *)&lt_frozen, w;
	unsigned i;

	for (i = 0; i < sizeof(lt) / sizeof(lt_word); i++) {
		w = a[i];
		a[i] = b[i];
		b[i] = w;
	}
	cursor_drawn = 0;
	panel_true = 0;
}

void
lcdterm_marker(void)
{
	panel_cell(phys(0), LT_COLS - 1, '!', LT_ATTR(7, 1, 0));
	/* A second marker while the first is still on the panel: only a
	 * full repaint knows where both are. */
	if (marker_prow >= 0 && marker_prow != phys(0))
		panel_true = 0;
	marker_prow = phys(0);
}

#ifdef LCDTERM_HOST_TEST
const void *
lcdterm_state(int frozen, unsigned *size)
{
	*size = sizeof(lt);
	return frozen ? (const void *)&lt_frozen : (const void *)&lt;
}
#endif
#endif /* LCD_VT */

#ifdef LCD_GFX_EXT
/*
 * The physical row the panel's memory holds at its first line while a pixel
 * program owns it: text written since the takeover moves top and draws
 * nothing, so the memory keeps the ring as lcdterm_gfx(1) found it.
 */
int
lcdterm_gfx_top(void)
{
	return gfx_top;
}
#endif

void
lcdterm_cursor(int *row, int *col)
{
	*row = lt.crow;
	*col = lt.ccol;
}
