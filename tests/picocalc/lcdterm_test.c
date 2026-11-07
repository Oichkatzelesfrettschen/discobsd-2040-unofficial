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
 * Host test of the PicoCalc console terminal (sys/arch/rp2040/dev/lcdterm.c)
 * and key translation (picokbd_map.c).
 *
 * The panel primitives here record into a model panel: one character and
 * attribute per physical cell, and the scroll start. A panel row shown on
 * screen line r is physical row (top + r) % LT_ROWS, as the ST7365P's
 * vertical scroll maps it. Every check compares the model panel, which is
 * what the primitives drew, against the expected screen text, so a store
 * update the terminal forgot to draw fails the same way a wrong store does.
 * A cell with a bank code (0x80-0xff) also records the nine glyph rows the
 * bank held when the cell was drawn, which is what st7365p.c would clock out.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcdterm.h"
#include "picokbd_map.h"

static unsigned char panel_ch[LT_ROWS][LT_COLS];
static unsigned char panel_at[LT_ROWS][LT_COLS];
static unsigned char panel_bits[LT_ROWS][LT_COLS][LT_GLYPH_ROWS];
static int panel_top;
static char replies[64];
static int nreplies;
static int nbells;
static int failures;

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	const unsigned char *glyph;

	if (prow < 0 || prow >= LT_ROWS || col < 0 || col >= LT_COLS) {
		fprintf(stderr, "draw outside panel: %d,%d\n", prow, col);
		exit(2);
	}
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
	while (n-- > 0) {
		panel_ch[prow][col] = ' ';
		memset(panel_bits[prow][col], 0, LT_GLYPH_ROWS);
		panel_at[prow][col++] = (unsigned char)attr;
	}
}

void
lcd_set_top(int prow)
{
	panel_top = prow;
}

void
lcd_bell(void)
{
	nbells++;
}

void
lcdterm_reply(const char *s, int n)
{
	if (nreplies + n < (int)sizeof(replies)) {
		memcpy(replies + nreplies, s, (size_t)n);
		nreplies += n;
	}
}

static void
out(const char *s)
{
	while (*s != '\0')
		lcdterm_putc((unsigned char)*s++);
}

/* Screen line r as text with trailing blanks removed. */
static const char *
line(int r)
{
	static char buf[LT_COLS + 1];
	int p = (panel_top + r) % LT_ROWS, n = LT_COLS;

	memcpy(buf, panel_ch[p], LT_COLS);
	while (n > 0 && buf[n - 1] == ' ')
		n--;
	buf[n] = '\0';
	return buf;
}

static void
expect_line(const char *what, int r, const char *want)
{
	const char *got = line(r);

	if (strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL %s: line %d is \"%s\", want \"%s\"\n",
		    what, r, got, want);
		failures++;
	}
}

static void
expect_int(const char *what, int got, int want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %d, want %d\n", what, got, want);
		failures++;
	}
}

static void
expect_bytes(const char *what, const char *got, int n, const char *want)
{
	if (n != (int)strlen(want) || memcmp(got, want, (size_t)n) != 0) {
		fprintf(stderr, "FAIL %s: %d bytes, want \"", what, n);
		fprintf(stderr, "%s\"\n", want[0] == '\033' ? want + 1 : want);
		failures++;
	}
}

/*
 * The store and the model panel agree on the character and the attribute of
 * every visible cell, the cursor cell excepted while the cursor is drawn.
 */
static void
expect_consistent(const char *what)
{
	const unsigned char *glyph;
	int r, c, p;

	for (r = 0; r < LT_ROWS; r++) {
		p = (panel_top + r) % LT_ROWS;
		for (c = 0; c < LT_COLS; c++) {
			if (lcdterm_cell(r, c) != panel_ch[p][c] ||
			    lcdterm_attr(r, c) != panel_at[p][c]) {
				fprintf(stderr, "FAIL %s: store and panel differ "
				    "at %d,%d\n", what, r, c);
				failures++;
				return;
			}
			glyph = lcdterm_glyph(panel_ch[p][c]);
			if (glyph != NULL && memcmp(glyph, panel_bits[p][c],
			    LT_GLYPH_ROWS) != 0) {
				fprintf(stderr, "FAIL %s: panel glyph is stale "
				    "at %d,%d\n", what, r, c);
				failures++;
				return;
			}
		}
	}
}

static void
reset(void)
{
	memset(panel_ch, 0, sizeof(panel_ch));
	memset(panel_at, 0, sizeof(panel_at));
	memset(panel_bits, 0, sizeof(panel_bits));
	lcdterm_init();
	nreplies = 0;
	nbells = 0;
}

static void
test_scroll(void)
{
	char buf[16];
	int i, row, col;

	reset();
	for (i = 0; i < 40; i++) {
		snprintf(buf, sizeof(buf), "line %d\r\n", i);
		out(buf);
	}
	/* 40 lines and the final newline: lines 9..39, then the blank. */
	expect_line("scroll first", 0, "line 9");
	expect_line("scroll last", LT_ROWS - 2, "line 39");
	expect_line("scroll blank", LT_ROWS - 1, "");
	expect_int("scroll top", panel_top, (40 + 1 - LT_ROWS) % LT_ROWS);
	lcdterm_cursor(&row, &col);
	expect_int("scroll cursor row", row, LT_ROWS - 1);
	expect_int("scroll cursor col", col, 0);
	expect_consistent("scroll");
}

static void
test_wrap(void)
{
	int i, row, col;

	reset();
	for (i = 0; i < LT_COLS; i++)
		lcdterm_putc('a' + i % 26);
	lcdterm_cursor(&row, &col);
	expect_int("xn holds row", row, 0);
	expect_int("xn holds col", col, LT_COLS - 1);
	lcdterm_putc('Z');
	expect_line("wrap next line", 1, "Z");

	/* Rubout of the 53rd character erases it, not the 52nd. */
	out("\033[5;1H");
	for (i = 0; i < LT_COLS; i++)
		lcdterm_putc('r');
	out("\b \b");
	lcdterm_cursor(&row, &col);
	expect_int("rubout at margin col", col, LT_COLS - 1);
	expect_int("rubout keeps 52nd", lcdterm_cell(4, LT_COLS - 2), 'r');
	expect_int("rubout clears 53rd", lcdterm_cell(4, LT_COLS - 1), ' ');

	/* A full line then LF does not leave a pending wrap behind. */
	out("\033[8;1H");
	for (i = 0; i < LT_COLS; i++)
		lcdterm_putc('f');
	out("\nX");
	lcdterm_cursor(&row, &col);
	expect_int("LF clears wrap row", row, 8);
	expect_int("LF clears wrap col", col, LT_COLS - 1);
	expect_int("LF then glyph stays on its row", lcdterm_cell(8, LT_COLS - 1),
	    'X');
	out("\033[?7l");
	out("\033[3;1H");
	for (i = 0; i < LT_COLS + 5; i++)
		lcdterm_putc('x');
	lcdterm_cursor(&row, &col);
	expect_int("nowrap row", row, 2);
	expect_int("nowrap col", col, LT_COLS - 1);
	expect_consistent("wrap");
}

static void
test_addressing_and_erase(void)
{
	reset();
	out("hello world\r\nsecond line");
	out("\033[1;7H");
	out("\033[K");
	expect_line("EL 0", 0, "hello");
	out("\033[2;3H\033[1K");
	expect_line("EL 1", 1, "   ond line");
	out("\033[2;1H\033[2K");
	expect_line("EL 2", 1, "");
	out("\033[5;5Habc\033[5;2H\033[J");
	expect_line("ED 0", 4, "");
	expect_line("ED 0 keeps above", 0, "hello");
	out("\033[2J");
	expect_line("ED 2", 0, "");
	out("\033[H\033[10B\033[3C*");
	expect_line("CUD CUF", 10, "   *");
	out("\033[4A\033[2D+");
	expect_line("CUU CUB", 6, "  +");
	out("\033[20G!\033[3d?");
	expect_line("CHA", 6, "  +                !");
	expect_line("VPA", 2, "                    ?");
	expect_consistent("addressing");
}

static void
test_insert_delete(void)
{
	reset();
	out("abcdef");
	out("\033[1;3H\033[2@");
	expect_line("ICH", 0, "ab  cdef");
	out("\033[1;3H\033[3P");
	expect_line("DCH", 0, "abdef");
	out("\033[1;2H\033[2X");
	expect_line("ECH", 0, "a  ef");
	out("\033[1;1H\033[4hXY\033[4l");
	expect_line("insert mode", 0, "XYa  ef");
	out("\033[2;1Hrow2\r\nrow3\r\nrow4");
	out("\033[3;1H\033[L");
	expect_line("IL opens", 2, "");
	expect_line("IL pushes", 3, "row3");
	expect_line("IL pushes 2", 4, "row4");
	out("\033[2;1H\033[2M");
	expect_line("DL pulls", 1, "row3");
	expect_line("DL pulls 2", 2, "row4");
	expect_consistent("insert-delete");
}

static void
test_region(void)
{
	int i, top0;
	char buf[8];

	reset();
	for (i = 1; i <= 10; i++) {
		snprintf(buf, sizeof(buf), "%d\r\n", i);
		out(buf);
	}
	top0 = panel_top;
	out("\033[3;6r");		/* Rows 3-6 scroll; cursor homes. */
	out("\033[6;1H\nX");
	expect_int("region keeps hw scroll", panel_top, top0);
	expect_line("region above", 1, "2");
	expect_line("region scrolled", 2, "4");
	expect_line("region bottom", 5, "X");
	expect_line("region below", 6, "7");
	out("\033[3;1H\033M");
	expect_line("RI opens", 2, "");
	expect_line("RI pushes", 3, "4");
	out("\033[r");
	expect_consistent("region");
}

static void
test_attributes(void)
{
	unsigned a;

	reset();
	out("\033[1;31mR\033[7mV\033[0mN\033[4mU\033[24;44mB");
	a = lcdterm_attr(0, 0);
	expect_int("bold red fg", LT_ATTR_FG(a), 9);
	a = lcdterm_attr(0, 1);
	expect_int("reverse fg", LT_ATTR_FG(a), 8);	/* Black, bold. */
	expect_int("reverse bg", LT_ATTR_BG(a), 1);
	a = lcdterm_attr(0, 2);
	expect_int("reset", (int)a, LT_ATTR_DEFAULT);
	a = lcdterm_attr(0, 3);
	expect_int("underline", (a & LT_ATTR_UL) != 0, 1);
	a = lcdterm_attr(0, 4);
	expect_int("bg blue", LT_ATTR_BG(a), 4);
	expect_int("panel attr drawn", panel_at[(panel_top) % LT_ROWS][4], (int)a);
	out("\033[0m\033(0lqk\033(B");
	expect_line("line drawing", 0, "RVNUB\x8d\x92\x8c");

	/* Bright foregrounds are palette 8-15; bright backgrounds fold to 0-7. */
	out("\033[2;1H\033[90mG\033[97mW\033[0;101mX\033[0m");
	expect_int("SGR 90 gray fg", LT_ATTR_FG(lcdterm_attr(1, 0)), 8);
	expect_int("SGR 97 white fg", LT_ATTR_FG(lcdterm_attr(1, 1)), 15);
	expect_int("SGR 101 bg", LT_ATTR_BG(lcdterm_attr(1, 2)), 1);
	expect_int("SGR 101 keeps fg", LT_ATTR_FG(lcdterm_attr(1, 2)), 7);
	expect_consistent("attributes");

	/* Reverse video erases with the background it displays: red here. */
	out("\033[2;1H\033[31;44;7m\033[K\033[0m");
	a = lcdterm_attr(1, 10);
	expect_int("reverse erase bg", LT_ATTR_BG(a), 1);
	out("\033[3;1H\033[32;7m\033[J\033[0m");
	expect_int("reverse ED bg", LT_ATTR_BG(lcdterm_attr(5, 0)), 2);
	out("\033[4;1H\033[33;7m\033[3X\033[0m");
	expect_int("reverse ECH bg", LT_ATTR_BG(lcdterm_attr(3, 2)), 3);
	expect_consistent("reverse erase");
}

static void
test_save_restore_reports(void)
{
	int row, col;

	reset();
	out("\033[5;10H\0337\033[1;1H\0338");
	lcdterm_cursor(&row, &col);
	expect_int("restore row", row, 4);
	expect_int("restore col", col, 9);
	out("\033[6n");
	expect_bytes("DSR", replies, nreplies, "\033[5;10R");
	nreplies = 0;
	out("\033[c");
	expect_bytes("DA", replies, nreplies, "\033[?6c");
	out("\033[?1h");
	expect_int("DECCKM set", lcdterm_app_cursor(), 1);
	out("\033[!p");
	expect_int("DECSTR clears DECCKM", lcdterm_app_cursor(), 0);
}

static void
test_cursor_overlay(void)
{
	unsigned a;
	int p;

	reset();
	out("ab");
	lcdterm_cursor_show();
	p = panel_top % LT_ROWS;
	a = panel_at[p][2];
	expect_int("cursor drawn inverted", LT_ATTR_BG(a), 7);
	out("c");
	expect_int("cursor cell restored", panel_at[p][2], LT_ATTR_DEFAULT);
	expect_line("after cursor", 0, "abc");
}

static void
test_review_cases(void)
{
	int i, row, col;

	/* G0 designation right after DECSTR selects line drawing. */
	reset();
	out("\033[!p\033(0q\033(B");
	expect_line("ESC ( after CSI ! p", 0, "\x92");

	/* IND at a pending wrap indexes once and keeps the column. */
	reset();
	for (i = 0; i < LT_COLS; i++)
		lcdterm_putc('i');
	out("\033DX");
	lcdterm_cursor(&row, &col);
	expect_int("IND clears wrap row", row, 1);
	expect_int("IND keeps column", lcdterm_cell(1, LT_COLS - 1), 'X');

	/* The fatal reset leaves plain ASCII and full-screen scrolling. */
	reset();
	out("\033(0\033[3;6r\033[7m");
	lcdterm_abort();
	out("\033[32;1Hq\nz");
	expect_line("abort clears line drawing", LT_ROWS - 2, "q");
	expect_int("abort resets SGR", (int)lcdterm_attr(LT_ROWS - 2, 0),
	    LT_ATTR_DEFAULT);
	expect_int("abort restores full-screen scroll", panel_top, 1);
	expect_consistent("review cases");
}

/* The nine rows of a bank cell as the model panel recorded them. */
static void
expect_bits(const char *what, int r, int c, const unsigned char *want)
{
	int p = (panel_top + r) % LT_ROWS;

	if (memcmp(panel_bits[p][c], want, LT_GLYPH_ROWS) != 0) {
		fprintf(stderr, "FAIL %s: panel rows at %d,%d are "
		    "%02x %02x %02x %02x %02x %02x %02x %02x %02x\n", what, r, c,
		    panel_bits[p][c][0], panel_bits[p][c][1],
		    panel_bits[p][c][2], panel_bits[p][c][3],
		    panel_bits[p][c][4], panel_bits[p][c][5],
		    panel_bits[p][c][6], panel_bits[p][c][7],
		    panel_bits[p][c][8]);
		failures++;
	}
}

static const unsigned char tile_a[LT_GLYPH_ROWS] = {
	0xfc, 0x84, 0x84, 0xb4, 0xb4, 0xb4, 0x84, 0x84, 0xfc
};
static const unsigned char tile_b[LT_GLYPH_ROWS] = {
	0x00, 0x30, 0x48, 0x84, 0xfc, 0x84, 0x84, 0x00, 0xa8
};

/*
 * The shapes of the DEC special graphics set that curses draws boxes with.
 * Each stroke is a pixel wide, so a glyph is characterized by where it
 * meets the cell border: the left and right edges on row 4, the top and
 * bottom edges in column 2. Two neighbors join when their facing edges
 * both carry a stroke, which is what makes a box read as lines.
 */
static void
test_line_drawing_shapes(void)
{
	static const struct {
		char final;
		int left, right, up, down;
		const char *name;
	} box[] = {
		{ 'j', 1, 0, 1, 0, "lower right" },
		{ 'k', 1, 0, 0, 1, "upper right" },
		{ 'l', 0, 1, 0, 1, "upper left" },
		{ 'm', 0, 1, 1, 0, "lower left" },
		{ 'n', 1, 1, 1, 1, "crossing" },
		{ 'q', 1, 1, 0, 0, "horizontal" },
		{ 't', 0, 1, 1, 1, "left tee" },
		{ 'u', 1, 0, 1, 1, "right tee" },
		{ 'v', 1, 1, 1, 0, "bottom tee" },
		{ 'w', 1, 1, 0, 1, "top tee" },
		{ 'x', 0, 0, 1, 1, "vertical" },
	};
	char buf[80];
	size_t i;
	int p;

	reset();
	for (i = 0; i < sizeof(box) / sizeof(box[0]); i++) {
		snprintf(buf, sizeof(buf), "\033[%d;1H\033(0%c\033(B",
		    (int)i + 1, box[i].final);
		out(buf);
		expect_int(box[i].name, lcdterm_cell((int)i, 0),
		    LT_GLYPH_FIRST + box[i].final - 0x5f);
		p = (panel_top + (int)i) % LT_ROWS;
		snprintf(buf, sizeof(buf), "%s left edge", box[i].name);
		expect_int(buf, (panel_bits[p][0][4] & 0x80) != 0, box[i].left);
		snprintf(buf, sizeof(buf), "%s right edge", box[i].name);
		expect_int(buf, (panel_bits[p][0][4] & 0x04) != 0, box[i].right);
		snprintf(buf, sizeof(buf), "%s top edge", box[i].name);
		expect_int(buf, (panel_bits[p][0][0] & 0x20) != 0, box[i].up);
		snprintf(buf, sizeof(buf), "%s bottom edge", box[i].name);
		expect_int(buf, (panel_bits[p][0][8] & 0x20) != 0, box[i].down);
	}
	expect_consistent("line drawing shapes");

	/* An upper-left corner, a rule and an upper-right corner. */
	reset();
	out("\033(0lqk\033(B");
	expect_int("corner meets rule", panel_bits[0][0][4] & 0x04, 0x04);
	expect_int("rule meets corner", panel_bits[0][1][4] & 0x80, 0x80);
	expect_int("rule spans the cell", panel_bits[0][1][4], 0xfc);
}

static void
test_glyph_bank(void)
{
	static const unsigned char blank[LT_GLYPH_ROWS];
	unsigned char bad[LT_GLYPH_ROWS], fill[LT_GLYPH_ROWS];
	const unsigned char *g;
	int i, j, row, col;

	/* An uploaded glyph is printed by its code and recorded by the panel. */
	reset();
	expect_int("upload accepted", lcdterm_setglyph(0xa5, tile_a), 0);
	lcdterm_putc(0xa5);
	expect_int("code stored", lcdterm_cell(0, 0), 0xa5);
	expect_bits("uploaded glyph drawn", 0, 0, tile_a);
	g = lcdterm_glyph(0xa5);
	expect_int("bank holds the rows", memcmp(g, tile_a, LT_GLYPH_ROWS), 0);
	out("\033[3;5H");
	lcdterm_putc(0xa5);
	expect_bits("same glyph elsewhere", 2, 4, tile_a);
	expect_consistent("glyph bank print");

	/* Redefinition redraws the cells already showing the code. */
	expect_int("redefinition accepted", lcdterm_setglyph(0xa5, tile_b), 0);
	expect_bits("redefined glyph, first cell", 0, 0, tile_b);
	expect_bits("redefined glyph, second cell", 2, 4, tile_b);
	expect_consistent("glyph redefinition");
	lcdterm_cursor(&row, &col);
	expect_int("redefinition keeps the cursor row", row, 2);
	expect_int("redefinition keeps the cursor column", col, 5);

	/* A second code does not disturb the first. */
	expect_int("second upload", lcdterm_setglyph(0xff, tile_a), 0);
	lcdterm_putc(0xff);
	expect_bits("second glyph", 2, 5, tile_a);
	expect_bits("first glyph untouched", 0, 0, tile_b);
	g = lcdterm_glyph(0xa6);
	expect_int("untouched bank code is not the uploaded one",
	    memcmp(g, tile_b, LT_GLYPH_ROWS) != 0, 1);
	expect_int("the ASCII range has no bank entry",
	    lcdterm_glyph('?') == NULL, 1);
	expect_int("below the bank", lcdterm_glyph(0x7f) == NULL, 1);
	expect_int("above the bank", lcdterm_glyph(0x100) == NULL, 1);

	/* Uploaded glyphs scroll with the ring like any other cell. */
	reset();
	expect_int("upload before scroll", lcdterm_setglyph(0xb0, tile_a), 0);
	out("\033[32;1H");
	lcdterm_putc(0xb0);
	for (i = 0; i < 3; i++)
		out("\r\n");
	expect_int("scrolled glyph code", lcdterm_cell(LT_ROWS - 4, 0), 0xb0);
	expect_bits("scrolled glyph rows", LT_ROWS - 4, 0, tile_a);
	expect_int("scroll moved the hardware start", panel_top, 3);
	expect_consistent("glyph scroll");

	/* The bounds of the bank are accepted. */
	reset();
	for (i = 0; i < LT_GLYPH_ROWS; i++)
		fill[i] = LT_GLYPH_MASK;
	expect_int("first bank code", lcdterm_setglyph(0x80, fill), 0);
	expect_int("last bank code", lcdterm_setglyph(0xff, fill), 0);

	/* Rejections change nothing. */
	reset();
	expect_int("setup upload", lcdterm_setglyph(0xc0, tile_a), 0);
	for (i = 0; i < LT_GLYPH_ROWS; i++) {
		for (j = 0; j < 2; j++) {
			memcpy(bad, tile_b, LT_GLYPH_ROWS);
			bad[i] |= (unsigned char)(1 << j);
			expect_int("row bit outside the six columns",
			    lcdterm_setglyph(0xc0, bad), -1);
			expect_int("rejected rows leave the bank", memcmp(
			    lcdterm_glyph(0xc0), tile_a, LT_GLYPH_ROWS), 0);
		}
	}
	expect_int("code 0x7f", lcdterm_setglyph(0x7f, tile_b), -1);
	expect_int("code 0x00", lcdterm_setglyph(0x00, tile_b), -1);
	expect_int("code 0x41", lcdterm_setglyph('A', tile_b), -1);
	expect_int("code 0x100", lcdterm_setglyph(0x100, tile_b), -1);
	expect_int("code -1", lcdterm_setglyph(-1, tile_b), -1);
	expect_int("the line-drawing code still serves", memcmp(
	    lcdterm_glyph(0x8d), blank, LT_GLYPH_ROWS) != 0, 1);

	/* A raw byte reaches the bank from 0xa0; C1 controls draw '?'. */
	reset();
	expect_int("upload for raw bytes", lcdterm_setglyph(0xa0, tile_a), 0);
	out("\x80\x9f\xa0\xa1\xff\x7f");
	expect_int("C1 0x80", lcdterm_cell(0, 0), '?');
	expect_int("C1 0x9f", lcdterm_cell(0, 1), '?');
	expect_int("raw 0xa0 is bank code 0xa0", lcdterm_cell(0, 2), 0xa0);
	expect_int("raw 0xa1 is bank code 0xa1", lcdterm_cell(0, 3), 0xa1);
	expect_int("raw 0xff is bank code 0xff", lcdterm_cell(0, 4), 0xff);
	lcdterm_cursor(&row, &col);
	expect_int("DEL draws nothing and does not advance", col, 5);
	expect_bits("raw 0xa0 draws the upload", 0, 2, tile_a);
	/* The default of every code the escape cannot reach is the '?' glyph. */
	expect_int("unreached codes look like '?'", memcmp(
	    lcdterm_glyph(0xa1), lcdterm_glyph(0xfe), LT_GLYPH_ROWS), 0);
	expect_int("and are not blank", memcmp(
	    lcdterm_glyph(0xa1), blank, LT_GLYPH_ROWS) != 0, 1);
	expect_consistent("raw bytes");

	/* The DEC set covers 0x5f-0x7e and nothing around it. */
	reset();
	out("\033(0^\x5f\x7e\x7f\033(B");
	expect_int("0x5e is not graphics", lcdterm_cell(0, 0), '^');
	expect_int("0x5f is bank code 0x80", lcdterm_cell(0, 1), 0x80);
	expect_int("0x7e is bank code 0x9f", lcdterm_cell(0, 2), 0x9f);
	lcdterm_cursor(&row, &col);
	expect_int("DEL under graphics draws nothing", col, 3);
	out("\033(0a\033(Ba");
	expect_int("checkerboard under graphics", lcdterm_cell(0, 3), 0x82);
	expect_int("ASCII after ESC ( B", lcdterm_cell(0, 4), 'a');

	/* Uploading over a line-drawing code changes the box character. */
	reset();
	out("\033(0q\033(B");
	expect_int("rule uploaded over", lcdterm_setglyph(0x92, tile_b), 0);
	expect_bits("the rule cell follows the upload", 0, 0, tile_b);
	expect_consistent("line drawing redefined");

	/*
	 * A soft reset keeps the bank; a full reset restores the defaults and
	 * the fault path leaves it alone, because its report is ASCII.
	 */
	reset();
	expect_int("upload before resets", lcdterm_setglyph(0x92, tile_a), 0);
	expect_int("upload of a text code", lcdterm_setglyph(0xb7, tile_a), 0);
	out("\033[!p");
	expect_int("DECSTR keeps the line-drawing upload", memcmp(
	    lcdterm_glyph(0x92), tile_a, LT_GLYPH_ROWS), 0);
	lcdterm_abort();
	expect_int("abort keeps the text-code upload", memcmp(
	    lcdterm_glyph(0xb7), tile_a, LT_GLYPH_ROWS), 0);
	out("\033c");
	expect_int("RIS restores the line-drawing code", memcmp(
	    lcdterm_glyph(0x92), tile_a, LT_GLYPH_ROWS) != 0, 1);
	expect_int("RIS restores the text code", memcmp(
	    lcdterm_glyph(0xb7), tile_a, LT_GLYPH_ROWS) != 0, 1);
	expect_int("RIS restores the horizontal rule", lcdterm_glyph(0x92)[4], 0xfc);
	expect_int("RIS restores the '?' glyph", memcmp(
	    lcdterm_glyph(0xb7), lcdterm_glyph(0xfe), LT_GLYPH_ROWS), 0);
	expect_consistent("resets");
}

static void
key(int state, int k, int app, int *ctrl, const char *want, const char *what)
{
	char buf[PICOKBD_MAXSEQ];
	int n = picokbd_translate(state, k, app, ctrl, buf);

	expect_bytes(what, buf, n, want);
}

/*
 * BEL (0x07) reaches the bell primitive once per byte, from the ground
 * state and inside a control sequence, and draws, moves and scrolls
 * nothing; the other C0 controls the terminal ignores stay silent.
 */
static void
test_bell(void)
{
	int row, col, i;

	reset();
	out("ab");
	out("\a");
	expect_int("one BEL rings once", nbells, 1);
	lcdterm_cursor(&row, &col);
	expect_int("BEL leaves the column", col, 2);
	expect_int("BEL leaves the row", row, 0);
	expect_line("BEL draws nothing", 0, "ab");
	out("\a\a");
	expect_int("BEL rings per byte", nbells, 3);

	reset();
	out("\033[3\aC");
	expect_int("BEL inside a CSI rings", nbells, 1);
	lcdterm_cursor(&row, &col);
	expect_int("the CSI completes after the BEL", col, 3);

	reset();
	for (i = 0; i < 0x20; i++) {
		if (i == 0x07 || i == '\b' || i == '\t' || i == '\n' ||
		    i == '\v' || i == '\f' || i == '\r' || i == 0x1b ||
		    i == 0x18 || i == 0x1a)
			continue;
		lcdterm_putc(i);
	}
	expect_int("other controls do not ring", nbells, 0);

	reset();
	out("\033[2;3H\a\033[6n");
	expect_int("BEL leaves a following report intact", nreplies, 6);
	expect_consistent("bell");
}

static void
test_keymap(void)
{
	int ctrl = 0;

	key(PICOKBD_PRESSED, 'a', 0, &ctrl, "a", "letter");
	key(PICOKBD_RELEASED, 'a', 0, &ctrl, "", "release types nothing");
	key(PICOKBD_HOLD, 'a', 0, &ctrl, "", "hold types nothing");
	key(PICOKBD_PRESSED, 0xa5, 0, &ctrl, "", "ctrl down");
	key(PICOKBD_PRESSED, 'c', 0, &ctrl, "\003", "ctrl-c");
	key(PICOKBD_PRESSED, '[', 0, &ctrl, "\033", "ctrl-[");
	key(PICOKBD_HOLD, 0xa5, 0, &ctrl, "", "ctrl hold");
	key(PICOKBD_PRESSED, 'd', 0, &ctrl, "\004", "ctrl-d while held");
	key(PICOKBD_RELEASED, 0xa5, 0, &ctrl, "", "ctrl up");
	key(PICOKBD_PRESSED, 'c', 0, &ctrl, "c", "plain c after ctrl");
	key(PICOKBD_PRESSED, 0x0a, 0, &ctrl, "\r", "enter");
	key(PICOKBD_PRESSED, 0x08, 0, &ctrl, "\177", "backspace");
	key(PICOKBD_PRESSED, 0xb1, 0, &ctrl, "\033", "escape");
	key(PICOKBD_PRESSED, 0xb5, 0, &ctrl, "\033[A", "up normal");
	key(PICOKBD_PRESSED, 0xb5, 1, &ctrl, "\033OA", "up application");
	key(PICOKBD_PRESSED, 0xb4, 0, &ctrl, "\033[D", "left");
	key(PICOKBD_PRESSED, 0xd2, 1, &ctrl, "\033OH", "home application");
	key(PICOKBD_PRESSED, 0xd4, 0, &ctrl, "\033[3~", "delete");
	key(PICOKBD_PRESSED, 0xd7, 0, &ctrl, "\033[6~", "page down");
	key(PICOKBD_PRESSED, 0x81, 0, &ctrl, "\033OP", "F1");
	key(PICOKBD_PRESSED, 0x85, 0, &ctrl, "\033[15~", "F5");
	key(PICOKBD_PRESSED, 0x89, 0, &ctrl, "\033[20~", "F9");
	key(PICOKBD_PRESSED, 0x90, 0, &ctrl, "\033[21~", "F10");
	key(PICOKBD_PRESSED, 0x91, 0, &ctrl, "", "power");
	key(PICOKBD_PRESSED, 0xa2, 0, &ctrl, "", "shift");
}

int
main(void)
{
	test_scroll();
	test_wrap();
	test_addressing_and_erase();
	test_insert_delete();
	test_region();
	test_attributes();
	test_save_restore_reports();
	test_cursor_overlay();
	test_review_cases();
	test_bell();
	test_line_drawing_shapes();
	test_glyph_bank();
	test_keymap();
	if (failures != 0) {
		fprintf(stderr, "lcdterm_test: %d failures\n", failures);
		return 1;
	}
	printf("lcdterm_test: ok\n");
	return 0;
}
