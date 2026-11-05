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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcdterm.h"
#include "picokbd_map.h"

static unsigned char panel_ch[LT_ROWS][LT_COLS];
static unsigned char panel_at[LT_ROWS][LT_COLS];
static int panel_top;
static char replies[64];
static int nreplies;
static int failures;

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	if (prow < 0 || prow >= LT_ROWS || col < 0 || col >= LT_COLS) {
		fprintf(stderr, "draw outside panel: %d,%d\n", prow, col);
		exit(2);
	}
	panel_ch[prow][col] = (unsigned char)ch;
	panel_at[prow][col] = (unsigned char)attr;
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
		panel_at[prow][col++] = (unsigned char)attr;
	}
}

void
lcd_set_top(int prow)
{
	panel_top = prow;
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
		}
	}
}

static void
reset(void)
{
	memset(panel_ch, 0, sizeof(panel_ch));
	memset(panel_at, 0, sizeof(panel_at));
	lcdterm_init();
	nreplies = 0;
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
	expect_line("line drawing", 0, "RVNUB+-+");
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
	expect_line("ESC ( after CSI ! p", 0, "-");

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

static void
key(int state, int k, int app, int *ctrl, const char *want, const char *what)
{
	char buf[PICOKBD_MAXSEQ];
	int n = picokbd_translate(state, k, app, ctrl, buf);

	expect_bytes(what, buf, n, want);
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
	test_keymap();
	if (failures != 0) {
		fprintf(stderr, "lcdterm_test: %d failures\n", failures);
		return 1;
	}
	printf("lcdterm_test: ok\n");
	return 0;
}
