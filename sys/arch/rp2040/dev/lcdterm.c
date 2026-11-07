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

static unsigned char cells[LT_ROWS][LT_COLS];
static unsigned char attrs[LT_ROWS][LT_COLS];

static int top;			/* Physical row shown on panel line 0. */
static int crow, ccol;		/* Logical cursor. */
static int wrapnext;		/* A glyph written to the last column. */
static int mtop, mbot;		/* Scrolling region, inclusive. */

static int sgr_fg, sgr_bg, sgr_bold, sgr_ul, sgr_rev;
static unsigned cattr;		/* Attribute for cells written now. */

static int autowrap, insertmode, cursor_on, appcursor, gfx;

static int cursor_drawn, cd_prow, cd_col;

static int s_row, s_col, s_fg, s_bg, s_bold, s_ul, s_rev, s_gfx;

static int state;
static int params[LT_NPARAM];
static int nparam;
static int priv;		/* '?' after the CSI. */
static int inter;		/* '!' intermediate. */

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

/* The bank is RAM; lcdterm_init() and ESC c fill it. */
static unsigned char glyph_bank[LT_GLYPH_N][LT_GLYPH_ROWS];

static int
phys(int r)
{
	return (top + r) % LT_ROWS;
}

/* Bank codes 0-31 take the line-drawing shapes, the rest the '?' glyph. */
static void
glyph_defaults(void)
{
	int i, j;

	for (i = 0; i < LT_GLYPH_N; i++)
		for (j = 0; j < LT_GLYPH_ROWS; j++)
			glyph_bank[i][j] = i < LT_ACS_N ? dec_graphics[i][j] :
			    lcdfont['?' - LCDFONT_FIRST][j];
}

const unsigned char *
lcdterm_glyph(int code)
{
	if (code < LT_GLYPH_FIRST || code >= LT_GLYPH_FIRST + LT_GLYPH_N)
		return 0;
	return glyph_bank[code - LT_GLYPH_FIRST];
}

static void
resolve_attr(void)
{
	int fg = sgr_fg, bg = sgr_bg;

	if (sgr_rev) {
		fg = sgr_bg;
		bg = sgr_fg;
	}
	if (sgr_bold)
		fg |= 8;
	cattr = LT_ATTR(fg, bg, sgr_ul);
}

/* Erased cells take the background the current rendition displays. */
static unsigned
erase_attr(void)
{
	return LT_ATTR(7, sgr_rev ? sgr_fg : sgr_bg, 0);
}

static void
draw(int r, int c)
{
	int p = phys(r);

	lcd_draw_cell(p, c, cells[p][c], attrs[p][c]);
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
		lcd_draw_cell(cd_prow, cd_col, cells[cd_prow][cd_col],
		    attrs[cd_prow][cd_col]);
		cursor_drawn = 0;
	}
}

void
lcdterm_cursor_show(void)
{
	unsigned a, inv;
	int p;

	if (!cursor_on || cursor_drawn)
		return;
	p = phys(crow);
	a = attrs[p][ccol];
	inv = LT_ATTR(LT_ATTR_BG(a), LT_ATTR_FG(a) & 7, a & LT_ATTR_UL);
	lcd_draw_cell(p, ccol, cells[p][ccol], inv);
	cursor_drawn = 1;
	cd_prow = p;
	cd_col = ccol;
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
	for (i = c; i < c + n; i++) {
		cells[p][i] = ' ';
		attrs[p][i] = (unsigned char)a;
	}
	lcd_fill_cells(p, c, n, a);
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
		cells[pd][c] = cells[ps][c];
		attrs[pd][c] = attrs[ps][c];
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
			top = (top + 1) % LT_ROWS;
			lcd_set_top(top);
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
			top = (top + LT_ROWS - 1) % LT_ROWS;
			lcd_set_top(top);
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
	if (crow == mbot)
		scroll_up(mtop, mbot, 1);
	else if (crow < LT_ROWS - 1)
		crow++;
}

static void
index_up(void)
{
	if (crow == mtop)
		scroll_down(mtop, mbot, 1);
	else if (crow > 0)
		crow--;
}

static void
put_glyph(int ch)
{
	int p, c;

	if (wrapnext) {
		wrapnext = 0;
		ccol = 0;
		index_down();
	}
	p = phys(crow);
	if (insertmode) {
		for (c = LT_COLS - 1; c > ccol; c--) {
			cells[p][c] = cells[p][c - 1];
			attrs[p][c] = attrs[p][c - 1];
		}
	}
	cells[p][ccol] = (unsigned char)ch;
	attrs[p][ccol] = (unsigned char)cattr;
	if (insertmode)
		redraw_span(crow, ccol, LT_COLS - 1);
	else
		draw(crow, ccol);
	if (ccol == LT_COLS - 1) {
		if (autowrap)
			wrapnext = 1;
	} else
		ccol++;
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
	if (i >= nparam || params[i] == 0)
		return dflt;
	return params[i];
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
	reply_number(buf, &len, crow + 1);
	buf[len++] = ';';
	reply_number(buf, &len, ccol + 1);
	buf[len++] = 'R';
	lcdterm_reply(buf, len);
}

static void
reset_modes(void)
{
	sgr_fg = 7;
	sgr_bg = 0;
	sgr_bold = sgr_ul = sgr_rev = 0;
	resolve_attr();
	mtop = 0;
	mbot = LT_ROWS - 1;
	autowrap = 1;
	insertmode = 0;
	cursor_on = 1;
	appcursor = 0;
	gfx = 0;
	wrapnext = 0;
	s_row = s_col = 0;
	s_fg = 7;
	s_bg = s_bold = s_ul = s_rev = s_gfx = 0;
}

static void
do_sgr(void)
{
	int i, v;

	if (nparam == 0)
		nparam = 1, params[0] = 0;
	for (i = 0; i < nparam; i++) {
		v = params[i];
		/*
		 * The bright codes branch off first so the chain below spans
		 * 0-49 only; a wider span makes the compiler lower the chain
		 * to a larger jump table.
		 */
		if (v >= 90) {
			if (v <= 97)
				sgr_fg = v - 90 + 8;
			else if (v >= 100 && v <= 107)
				sgr_bg = v - 100;
		} else if (v == 0) {
			sgr_fg = 7;
			sgr_bg = 0;
			sgr_bold = sgr_ul = sgr_rev = 0;
		} else if (v == 1)
			sgr_bold = 1;
		else if (v == 4)
			sgr_ul = 1;
		else if (v == 7)
			sgr_rev = 1;
		else if (v == 22)
			sgr_bold = 0;
		else if (v == 24)
			sgr_ul = 0;
		else if (v == 27)
			sgr_rev = 0;
		else if (v >= 30 && v <= 37)
			sgr_fg = v - 30;
		else if (v == 39)
			sgr_fg = 7;
		else if (v >= 40 && v <= 47)
			sgr_bg = v - 40;
		else if (v == 49)
			sgr_bg = 0;
	}
	resolve_attr();
}

static void
set_mode(int on)
{
	int i;

	for (i = 0; i < nparam; i++) {
		if (priv) {
			switch (params[i]) {
			case 1:
				appcursor = on;
				break;
			case 7:
				autowrap = on;
				if (!on)
					wrapnext = 0;
				break;
			case 25:
				cursor_on = on;
				break;
			}
		} else if (params[i] == 4)
			insertmode = on;
	}
}

static void
do_csi(int final)
{
	int n, r, c, lo, hi;

	if (inter) {
		if (inter == '!' && final == 'p') {
			reset_modes();	/* DECSTR. */
		}
		return;
	}
	switch (final) {
	case 'A':
	case 'B':
		n = param(0, 1);
		lo = (crow >= mtop && crow <= mbot) ? mtop : 0;
		hi = (crow >= mtop && crow <= mbot) ? mbot : LT_ROWS - 1;
		crow = clamp(final == 'A' ? crow - n : crow + n, lo, hi);
		wrapnext = 0;
		break;
	case 'C':
		ccol = clamp(ccol + param(0, 1), 0, LT_COLS - 1);
		wrapnext = 0;
		break;
	case 'D':
		ccol = clamp(ccol - param(0, 1), 0, LT_COLS - 1);
		wrapnext = 0;
		break;
	case 'G':
		ccol = clamp(param(0, 1) - 1, 0, LT_COLS - 1);
		wrapnext = 0;
		break;
	case 'd':
		crow = clamp(param(0, 1) - 1, 0, LT_ROWS - 1);
		wrapnext = 0;
		break;
	case 'H':
	case 'f':
		crow = clamp(param(0, 1) - 1, 0, LT_ROWS - 1);
		ccol = clamp(param(1, 1) - 1, 0, LT_COLS - 1);
		wrapnext = 0;
		break;
	case 'J':
		switch (nparam > 0 ? params[0] : 0) {
		case 0:
			erase_cells(crow, ccol, LT_COLS - ccol);
			if (crow < LT_ROWS - 1)
				erase_rows(crow + 1, LT_ROWS - 1);
			break;
		case 1:
			if (crow > 0)
				erase_rows(0, crow - 1);
			erase_cells(crow, 0, ccol + 1);
			break;
		case 2:
			erase_rows(0, LT_ROWS - 1);
			break;
		}
		break;
	case 'K':
		switch (nparam > 0 ? params[0] : 0) {
		case 0:
			erase_cells(crow, ccol, LT_COLS - ccol);
			break;
		case 1:
			erase_cells(crow, 0, ccol + 1);
			break;
		case 2:
			erase_cells(crow, 0, LT_COLS);
			break;
		}
		break;
	case 'L':
		if (crow >= mtop && crow <= mbot)
			scroll_down(crow, mbot, param(0, 1));
		ccol = 0;
		wrapnext = 0;
		break;
	case 'M':
		if (crow >= mtop && crow <= mbot)
			scroll_up(crow, mbot, param(0, 1));
		ccol = 0;
		wrapnext = 0;
		break;
	case '@':
		n = clamp(param(0, 1), 1, LT_COLS - ccol);
		r = phys(crow);
		for (c = LT_COLS - 1; c >= ccol + n; c--) {
			cells[r][c] = cells[r][c - n];
			attrs[r][c] = attrs[r][c - n];
		}
		erase_cells(crow, ccol, n);
		redraw_span(crow, ccol + n, LT_COLS - 1);
		wrapnext = 0;
		break;
	case 'P':
		n = clamp(param(0, 1), 1, LT_COLS - ccol);
		r = phys(crow);
		for (c = ccol; c + n < LT_COLS; c++) {
			cells[r][c] = cells[r][c + n];
			attrs[r][c] = attrs[r][c + n];
		}
		redraw_span(crow, ccol, LT_COLS - n - 1);
		erase_cells(crow, LT_COLS - n, n);
		wrapnext = 0;
		break;
	case 'X':
		erase_cells(crow, ccol, clamp(param(0, 1), 1, LT_COLS - ccol));
		break;
	case 'r':
		lo = param(0, 1) - 1;
		hi = param(1, LT_ROWS) - 1;
		if (lo < hi && hi < LT_ROWS) {
			mtop = lo;
			mbot = hi;
			crow = ccol = 0;
			wrapnext = 0;
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
		if (priv)
			break;
		if (nparam > 0 && params[0] == 6)
			report_cursor();
		else if (nparam > 0 && params[0] == 5)
			lcdterm_reply("\033[0n", 4);
		break;
	case 'c':
		if (!priv && param(0, 0) == 0)
			lcdterm_reply("\033[?6c", 5);
		break;
	}
}

static void
save_cursor(void)
{
	s_row = crow;
	s_col = ccol;
	s_fg = sgr_fg;
	s_bg = sgr_bg;
	s_bold = sgr_bold;
	s_ul = sgr_ul;
	s_rev = sgr_rev;
	s_gfx = gfx;
}

static void
restore_cursor(void)
{
	crow = s_row;
	ccol = s_col;
	sgr_fg = s_fg;
	sgr_bg = s_bg;
	sgr_bold = s_bold;
	sgr_ul = s_ul;
	sgr_rev = s_rev;
	gfx = s_gfx;
	wrapnext = 0;
	resolve_attr();
}

static void
full_reset(void)
{
	reset_modes();
	top = 0;
	lcd_set_top(0);
	erase_rows(0, LT_ROWS - 1);
	crow = ccol = 0;
	state = ST_GROUND;
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
	state = ST_GROUND;
	cursor_drawn = 0;
	reset_modes();
	lcd_set_top(top);
}

void
lcdterm_init(void)
{
	cursor_drawn = 0;
	glyph_defaults();
	full_reset();
}

static void
esc_final(int c)
{
	state = ST_GROUND;
	switch (c) {
	case '[':
		state = ST_CSI;
		nparam = 0;
		params[0] = 0;
		priv = inter = 0;
		break;
	case '(':
		state = ST_CHARSET;
		inter = 0;	/* G0; a prior CSI may have left '!'. */
		break;
	case ')':
	case '*':
	case '+':
		state = ST_CHARSET;
		inter = c;	/* Designates G1-G3: consumed, ignored. */
		break;
	case '7':
		save_cursor();
		break;
	case '8':
		restore_cursor();
		break;
	case 'D':
		wrapnext = 0;
		index_down();
		break;
	case 'E':
		ccol = 0;
		wrapnext = 0;
		index_down();
		break;
	case 'M':
		wrapnext = 0;
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
		if (nparam == 0)
			nparam = 1;
		if (params[nparam - 1] < LT_PARAM_MAX)
			params[nparam - 1] = params[nparam - 1] * 10 + (c - '0');
	} else if (c == ';') {
		if (nparam == 0)
			nparam = 1;
		if (nparam < LT_NPARAM)
			params[nparam++] = 0;
	} else if (c == '?' || c == '>') {
		priv = c;
	} else if (c >= 0x20 && c <= 0x2f) {
		inter = c;
	} else if (c >= 0x40 && c <= 0x7e) {
		state = ST_GROUND;
		do_csi(c);
	} else if (c == 0x18 || c == 0x1a) {
		state = ST_GROUND;	/* CAN, SUB abort the sequence. */
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
		if (wrapnext)
			wrapnext = 0;
		else if (ccol > 0)
			ccol--;
		break;
	case '\t':
		wrapnext = 0;
		ccol = clamp((ccol / 8 + 1) * 8, 0, LT_COLS - 1);
		break;
	case '\n':
	case '\v':
	case '\f':
		wrapnext = 0;
		index_down();
		break;
	case '\r':
		ccol = 0;
		wrapnext = 0;
		break;
	case 0x1b:
		state = ST_ESC;
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

	switch (state) {
	case ST_ESC:
		if (c == 0x1b)
			return;
		if (c < 0x20) {
			state = ST_GROUND;
			control(c);
			return;
		}
		esc_final(c);
		return;
	case ST_CSI:
		if (c < 0x20 && c != 0x18 && c != 0x1a) {
			if (c == 0x1b)
				state = ST_ESC;
			else
				control(c);
			return;
		}
		csi_byte(c);
		return;
	case ST_CHARSET:
		if (inter == 0)
			gfx = (c == '0');
		inter = 0;
		state = ST_GROUND;
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
	} else if (gfx && c >= LT_ACS_FIRST)
		c = LT_GLYPH_FIRST + (c - LT_ACS_FIRST);
	put_glyph(c);
}

int
lcdterm_setglyph(int code, const unsigned char *rows)
{
	unsigned char *g;
	int i, r, c;

	if (code < LT_GLYPH_FIRST || code >= LT_GLYPH_FIRST + LT_GLYPH_N)
		return -1;
	for (i = 0; i < LT_GLYPH_ROWS; i++)
		if ((rows[i] & ~LT_GLYPH_MASK) != 0)
			return -1;
	g = glyph_bank[code - LT_GLYPH_FIRST];
	for (i = 0; i < LT_GLYPH_ROWS; i++)
		g[i] = rows[i];
	cursor_hide();
	for (r = 0; r < LT_ROWS; r++)
		for (c = 0; c < LT_COLS; c++)
			if (cells[phys(r)][c] == code)
				draw(r, c);
	return 0;
}

int
lcdterm_app_cursor(void)
{
	return appcursor;
}

int
lcdterm_cell(int row, int col)
{
	return cells[phys(row)][col];
}

unsigned
lcdterm_attr(int row, int col)
{
	return attrs[phys(row)][col];
}

int
lcdterm_top(void)
{
	return top;
}

void
lcdterm_cursor(int *row, int *col)
{
	*row = crow;
	*col = ccol;
}
