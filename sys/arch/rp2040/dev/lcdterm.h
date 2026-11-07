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
 * Character-cell terminal for a pixel panel without a frame buffer.
 *
 * lcdterm.c interprets the escape sequences of the xterm-basic termcap
 * entry that /etc/ttys names for the console, keeps one character and
 * one attribute byte per cell, and draws through the three panel
 * primitives below. The cell store is a ring of physical rows: logical
 * row r lives in physical row (top + r) % LT_ROWS, and a full-screen
 * scroll advances top and moves the panel's hardware scroll start
 * instead of redrawing. The file includes no kernel header, so the host
 * test in tests/picocalc links it against recording primitives.
 */

#ifndef _LCDTERM_H
#define _LCDTERM_H

#define	LT_CELL_W	6		/* Pixels per cell, horizontally. */
#define	LT_CELL_H	10		/* Pixels per cell, vertically. */
#define	LT_COLS		53		/* 320 / 6, two pixels to spare. */
#define	LT_ROWS		32		/* 320 / 10. */

/*
 * Cell attribute byte: foreground color index 0-15 in bits 0-3, where
 * 8-15 are the bright variants of 0-7 that bold or SGR 90-97 selects;
 * background color index 0-7 in bits 4-6, so SGR 100-107 selects the
 * base background; underline in bit 7. Reverse video is resolved into
 * the stored colors when the cell is written.
 */
#define	LT_ATTR_FG(a)		((a) & 0x0f)
#define	LT_ATTR_BG(a)		(((a) >> 4) & 0x07)
#define	LT_ATTR_UL		0x80
#define	LT_ATTR(fg, bg, ul)	(((fg) & 0x0f) | (((bg) & 0x07) << 4) | \
				    ((ul) ? LT_ATTR_UL : 0))
#define	LT_ATTR_DEFAULT		LT_ATTR(7, 0, 0)

/* Panel primitives, supplied by st7365p.c or by the host test. */
void	lcd_draw_cell(int prow, int col, int ch, unsigned attr);
void	lcd_fill_cells(int prow, int col, int n, unsigned attr);
void	lcd_set_top(int prow);

/* Sound the bell for BEL (0x07): supplied by picoaudio.c or the host test. */
void	lcd_bell(void);

/*
 * Bytes the terminal answers with (cursor-position and device-attribute
 * reports), supplied by lcdcons.c, which feeds them to the tty input
 * queue as though typed.
 */
void	lcdterm_reply(const char *s, int n);

void	lcdterm_init(void);
void	lcdterm_abort(void);
void	lcdterm_putc(int c);
void	lcdterm_cursor_show(void);
int	lcdterm_app_cursor(void);

/* Read access for the host test. */
int	lcdterm_cell(int row, int col);
unsigned lcdterm_attr(int row, int col);
int	lcdterm_top(void);
void	lcdterm_cursor(int *row, int *col);

#endif /* !_LCDTERM_H */
