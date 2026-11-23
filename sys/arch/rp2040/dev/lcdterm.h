/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
 * instead of redrawing. The file compiles without kernel headers, so the host
 * test in tests/picocalc links it against recording primitives.
 *
 * Cell codes 0x20-0x7e are ASCII, drawn from lcdfont.c. Codes 0x80-0xff
 * select a glyph of the RAM bank that LCDIOC_SETGLYPH (sys/lcdcons.h)
 * rewrites: a text cell and a tile cell share the store, the attributes,
 * the scroll ring and the cursor.
 *
 * lcdterm_gfx(1) makes the terminal stop drawing so a pixel program can own
 * the panel (see sys/lcdgfx.h); lcdterm_gfx(0) restores the text
 * configuration and calls lcdterm_repaint(), which also runs after a fatal
 * fault interrupted a pixel program. The caller of either holds the drawing
 * flag of lcdcons.c.
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

/*
 * Glyph bank: codes LT_GLYPH_FIRST to LT_GLYPH_FIRST + LT_GLYPH_N - 1, nine
 * pixel rows each with the leftmost column in bit 7. The six cell columns
 * occupy bits 7-2 (LT_GLYPH_MASK); a bank glyph's tenth cell row repeats its
 * ninth, so a tile fills the cell and line drawing joins across rows.
 */
#define	LT_GLYPH_FIRST	0x80
#define	LT_GLYPH_N	128
#define	LT_GLYPH_ROWS	9
#define	LT_GLYPH_MASK	0xfc

/* Panel primitives, supplied by st7365p.c or by the host test. */
void	lcd_draw_cell(int prow, int col, int ch, unsigned attr);
void	lcd_fill_cells(int prow, int col, int n, unsigned attr);
void	lcd_set_top(int prow);

/*
 * Pixel-level panel primitives of the /dev/lcdgfx driver (lcdgfx.c), from
 * the same source. lcd_gfx_window() selects the panel and opens a write
 * window over columns x0..x1 and lines y0..y1; each lcd_gfx_byte() then
 * sends one byte of pixel data in the current COLMOD format, and
 * lcd_gfx_end() deselects the panel. lcd_gfx_scroll() programs VSCSAD,
 * lcd_gfx_scrdef() VSCRDEF with `top` and `bottom` fixed lines, and
 * lcd_gfx_colmod() COLMOD. lcd_gfx_reset() returns the panel to the text
 * configuration: COLMOD 0x55, VSCRDEF 0/320/160 and the two pixel columns
 * right of the last cell cleared. lcd_palette holds the console's sixteen
 * colors, red, green and blue as 8-bit values.
 */
void	lcd_gfx_window(int x0, int y0, int x1, int y1);
void	lcd_gfx_byte(unsigned b);
void	lcd_gfx_end(void);
void	lcd_gfx_scroll(int line);
void	lcd_gfx_scrdef(int top, int bottom);
void	lcd_gfx_colmod(unsigned fmt);
void	lcd_gfx_reset(void);
extern const unsigned char lcd_palette[16][3];

/*
 * The primitives of lcdgfx.c's LCD_GFX_EXT commands. lcd_gfx_read_begin()
 * lowers the SPI clock to the panel's read rate and lcd_gfx_read_end()
 * restores the write rate; between them lcd_gfx_read() stores len reply
 * bytes of a memory read of n pixels of line y from column x, and lcd_gfx_scanline() returns the line the controller
 * is scanning. lcd_gfx_micros() is a free-running microsecond count.
 * lcd_gfx_power() moves the panel to an LCDG_PWR_* mode.
 */
void	lcd_gfx_read_begin(void);
void	lcd_gfx_read_end(void);
void	lcd_gfx_read(int x, int y, int n, unsigned char *raw, unsigned len);
int	lcd_gfx_scanline(void);
unsigned lcd_gfx_micros(void);
void	lcd_gfx_power(unsigned mode);
/* The LCDG_DITHER sequence up to its pixels; the caller restores COLMOD. */
void	lcd_gfx_dither(int on);

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
void	lcdterm_repaint(void);
void	lcdterm_gfx(int on);
void	lcdterm_putc(int c);
void	lcdterm_cursor_show(void);
int	lcdterm_app_cursor(void);

/*
 * lcdterm_glyph() copies the LT_GLYPH_ROWS rows of bank code into rows and
 * returns 1, or returns 0 and leaves rows alone for a code outside the bank. lcdterm_setglyph() replaces one bank glyph and
 * redraws the cells that show it; it returns -1 and leaves the screen unchanged when
 * code is outside the bank or a row sets a bit outside LT_GLYPH_MASK. The
 * caller holds the drawing flag of lcdcons.c.
 */
int	lcdterm_glyph(int code, unsigned char rows[LT_GLYPH_ROWS]);
int	lcdterm_setglyph(int code, const unsigned char *rows);

/* Read access for the host test. */
int	lcdterm_cell(int row, int col);
unsigned lcdterm_attr(int row, int col);
int	lcdterm_top(void);
int	lcdterm_gfx_top(void);	/* LCD_GFX_EXT: top at lcdterm_gfx(1). */
void	lcdterm_cursor(int *row, int *col);

/*
 * LCD_VT, the second console of lcdcons.c. lcdterm_exchange() swaps the
 * screen state of the console on the panel (cells, attributes, scroll ring,
 * cursor, rendition, modes and parser) with that of the frozen console, word
 * by word and in place, and forgets the cursor overlay; the caller then
 * calls lcdterm_repaint(). Both consoles start as a reset terminal.
 * lcdterm_marker() draws the unseen-output marker over the top right cell
 * without storing it, so the next repaint of that cell removes it.
 */
void	lcdterm_exchange(void);
void	lcdterm_marker(void);

#ifdef LCDTERM_HOST_TEST
/* The host test compares whole screen states: console 0 is the live one. */
const void *lcdterm_state(int frozen, unsigned *size);
#endif

#endif /* !_LCDTERM_H */
