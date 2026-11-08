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
 * The PicoCalc's pixel device, /dev/lcdgfx; sys/lcdgfx.h is the interface.
 *
 * Open takes the panel from the text console through lcdcons_gfx(), which
 * stops lcdterm.c drawing while it keeps the cell store current, and leaves
 * the palette at the console's colors, RGB666 and a scroll area of 320 lines
 * from line 0. The last close, including the one process exit performs,
 * returns the panel to the text configuration and repaints the text. A
 * second open fails with EBUSY, so one program owns the panel.
 *
 * Every drawing command validates its whole payload before the first panel
 * write: a coordinate outside the 320x320 panel, or a shape that leaves it,
 * fails with EINVAL and draws nothing. Rectangles, lines and circles are
 * drawn through lcd_gfx_window(), lcd_gfx_byte() and lcd_gfx_end() of
 * st7365p.c, which keep chip select low from the window command to the last
 * pixel byte. A window opened by LCDG_WINDOW stays open between write(2)
 * calls; the panel has no other user while gfx_open is set, because
 * lcdterm.c draws nothing then and the console's fatal path ends the
 * transfer (lcdgfx_isopen()).
 *
 * Colors are RGB565. While the panel runs COLMOD 0x66 each component is
 * widened to eight bits by bit replication and the panel keeps the upper
 * six; under COLMOD 0x55 the two bytes go out unchanged, most significant
 * first.
 *
 * The tile atlas is a 4,096-byte array of 128 tiles of 32 bytes, cleared at
 * every open. LCDG_TILESET streams tiles into it through uiomove() with no
 * staging buffer. LCDG_TILEWIN streams tile numbers into a one-row buffer;
 * when a row of the block is complete, tile_row() opens one panel window
 * over the row and sends the pixel lines of its tiles interleaved, so a row
 * of n tiles costs one window setup and 64 n pixels, and the window is
 * closed before write(2) returns. Neither stream holds chip select between
 * write(2) calls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/lcdgfx.h>

#include <rp2040/dev/lcdterm.h>
#include <rp2040/dev/lcdgfx.h>

#define	LCDGFX_CHUNK	64		/* Bytes of pixel data per uiomove. */
#define	LCDGFX_BOTTOM	(LCDG_RAM_LINES - LCDG_HEIGHT)	/* Text bottom area. */

static int gfx_open;
static unsigned gfx_fmt;		/* COLMOD in force. */
static unsigned short gfx_pal[LCDG_NPAL];
static int scr_top, scr_bottom;		/* VSCRDEF fixed areas in force. */

/* The window of LCDG_WINDOW that write(2) is filling. */
static unsigned win_left;		/* Data bytes still wanted. */
static unsigned win_fmt, win_fg, win_bg;
static int win_w, win_col;

/* The tile atlas and the two streams that fill it and draw from it. */
static unsigned char atlas[LCDG_ATLAS_N * LCDG_TILE_BYTES];
static unsigned tset_pos, tset_left;	/* LCDG_TILESET: next byte, bytes wanted. */
static unsigned char tw_row[LCDG_GRID];	/* LCDG_TILEWIN: numbers of one row. */
static unsigned tw_fill, tw_left;	/* Numbers in tw_row, numbers wanted. */
static unsigned tw_x, tw_y, tw_w;	/* Column of the block, row and width. */

_Static_assert(LCDG_GRID * LCDG_TILE_W == LCDG_WIDTH &&
    LCDG_GRID * LCDG_TILE_H == LCDG_HEIGHT,
    "the tile grid covers the panel");
_Static_assert(LCDG_ATLAS_N <= 255 && LCDG_TILE_BYTES == LCDG_TILE_H * 4,
    "the atlas size fits LCDG_INFO and a tile is eight lines of four bytes");

static unsigned
rgb565(unsigned r, unsigned g, unsigned b)
{
	return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
}

/* Send one RGB565 pixel in the format the panel is in. */
static void
put_color(unsigned c)
{
	unsigned r = c >> 11, g = (c >> 5) & 0x3f, b = c & 0x1f;

	if (gfx_fmt == LCDG_COLMOD_RGB565) {
		lcd_gfx_byte(c >> 8);
		lcd_gfx_byte(c & 0xff);
		return;
	}
	lcd_gfx_byte(r << 3 | r >> 2);
	lcd_gfx_byte(g << 2 | g >> 4);
	lcd_gfx_byte(b << 3 | b >> 2);
}

_Static_assert(LCDG_WIDTH == LCDG_HEIGHT,
    "one bound checks an x and a y coordinate alike");

/* Every one of the n coordinates v[0..n-1] lies on the panel. */
static int
coords_ok(const unsigned short *v, int n)
{
	while (n-- > 0)
		if (*v++ >= LCDG_WIDTH)
			return 0;
	return 1;
}

static void
fill(int x0, int y0, int x1, int y1, unsigned color)
{
	unsigned n = (unsigned)(x1 - x0 + 1) * (unsigned)(y1 - y0 + 1);

	lcd_gfx_window(x0, y0, x1, y1);
	while (n-- > 0)
		put_color(color);
	lcd_gfx_end();
}

static void
plot(int x, int y, unsigned color)
{
	fill(x, y, x, y, color);
}

/* One window per side, so no pixel is drawn twice. */
static void
frame(int x0, int y0, int x1, int y1, unsigned color)
{
	fill(x0, y0, x1, y0, color);
	if (y1 != y0)
		fill(x0, y1, x1, y1, color);
	if (y1 - y0 > 1) {
		fill(x0, y0 + 1, x0, y1 - 1, color);
		if (x1 != x0)
			fill(x1, y0 + 1, x1, y1 - 1, color);
	}
}

/* Bresenham's line in all octants; an axis-parallel line is one window. */
static void
line(int x0, int y0, int x1, int y1, unsigned color)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0;	/* Minus the height. */
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, e2;

	if (x0 == x1 || y0 == y1) {
		fill(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
		    x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0, color);
		return;
	}
	for (;;) {
		plot(x0, y0, color);
		if (x0 == x1 && y0 == y1)
			return;
		e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/*
 * The midpoint circle: eight symmetric points per step of the first octant.
 * Bit 2 of the point number swaps the axes, bits 0 and 1 mirror them.
 */
static void
circle(int cx, int cy, int r, unsigned color)
{
	int x = r, y = 0, err = 1 - r, i, dx, dy;

	while (x >= y) {
		for (i = 0; i < 8; i++) {
			dx = i & 4 ? y : x;
			dy = i & 4 ? x : y;
			plot(cx + (i & 1 ? -dx : dx), cy + (i & 2 ? -dy : dy),
			    color);
		}
		y++;
		if (err < 0) {
			err += 2 * y + 1;
		} else {
			x--;
			err += 2 * (y - x) + 1;
		}
	}
}

/* Abandon a window, a tile upload or a tile block that still wants data. */
static void
window_end(void)
{
	tset_left = 0;
	tw_left = 0;
	if (win_left != 0) {
		win_left = 0;
		lcd_gfx_end();
	}
}

int
lcdgfx_open(void)
{
	int i, error;

	if (gfx_open)
		return EBUSY;
	error = lcdcons_gfx(1);
	if (error != 0)
		return error;
	gfx_open = 1;
	gfx_fmt = LCDG_COLMOD_RGB666;
	scr_top = 0;
	scr_bottom = LCDGFX_BOTTOM;
	win_left = 0;
	tset_left = 0;
	tw_left = 0;
	bzero(atlas, sizeof(atlas));
	for (i = 0; i < LCDG_NPAL; i++)
		gfx_pal[i] = rgb565(lcd_palette[i][0], lcd_palette[i][1],
		    lcd_palette[i][2]);
	lcd_gfx_scroll(0);
	return 0;
}

int
lcdgfx_close(void)
{
	window_end();
	gfx_open = 0;
	return lcdcons_gfx(0);
}

int
lcdgfx_isopen(void)
{
	return gfx_open;
}

/* Draw n bytes of window data. */
static void
window_data(const unsigned char *p, unsigned n)
{
	unsigned v;
	int k;

	for (; n > 0; p++, n--) {
		v = *p;
		switch (win_fmt) {
		case LCDG_WIN_RGB565:
			put_color(v | (unsigned)*++p << 8);
			n--;
			break;
		case LCDG_WIN_INDEX:
			put_color(gfx_pal[v & (LCDG_NPAL - 1)]);
			break;
		default:
			k = win_w - win_col < 8 ? win_w - win_col : 8;
			for (; k > 0; k--, v <<= 1)
				put_color(gfx_pal[v & 0x80 ? win_fg : win_bg]);
			win_col += 8;
			if (win_col >= win_w)
				win_col = 0;
			break;
		}
	}
}

/*
 * Draw the row of tiles in tw_row as one window. The panel window fills
 * left to right, top to bottom, so line l of every tile precedes line l + 1:
 * the loops run line, then tile, then the four bytes of the tile's line,
 * high nibble first.
 */
static void
tile_row(void)
{
	const unsigned char *p;
	unsigned line, i, b;

	lcd_gfx_window(tw_x * LCDG_TILE_W, tw_y * LCDG_TILE_H,
	    (tw_x + tw_w) * LCDG_TILE_W - 1, (tw_y + 1) * LCDG_TILE_H - 1);
	for (line = 0; line < LCDG_TILE_H; line++) {
		for (i = 0; i < tw_w; i++) {
			p = atlas + tw_row[i] * LCDG_TILE_BYTES + line * 4;
			for (b = 0; b < 4; b++) {
				put_color(gfx_pal[p[b] >> 4]);
				put_color(gfx_pal[p[b] & 15]);
			}
		}
	}
	lcd_gfx_end();
}

/* The bytes of a LCDG_TILESET upload go straight into the atlas. */
static int
tileset_write(struct uio *uio)
{
	unsigned n;
	int error;

	while (uio->uio_resid > 0 && tset_left > 0) {
		n = uio->uio_resid;
		if (n > tset_left)
			n = tset_left;
		error = uiomove((caddr_t)atlas + tset_pos, n, uio);
		if (error != 0)
			return error;
		tset_pos += n;
		tset_left -= n;
	}
	return 0;
}

/*
 * Tile numbers of a LCDG_TILEWIN block collect into tw_row and each
 * complete row is drawn. A number the atlas does not hold ends the block
 * before its row is drawn.
 */
static int
tilewin_write(struct uio *uio)
{
	unsigned n, i;
	int error;

	while (uio->uio_resid > 0 && tw_left > 0) {
		n = tw_w - tw_fill;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		error = uiomove((caddr_t)tw_row + tw_fill, n, uio);
		if (error != 0)
			return error;
		for (i = tw_fill; i < tw_fill + n; i++) {
			if (tw_row[i] >= LCDG_ATLAS_N) {
				tw_left = 0;
				return EINVAL;
			}
		}
		tw_fill += n;
		tw_left -= n;
		if (tw_fill == tw_w) {
			tile_row();
			tw_fill = 0;
			tw_y++;
		}
	}
	return 0;
}

int
lcdgfx_write(struct uio *uio)
{
	unsigned char buf[LCDGFX_CHUNK];
	unsigned n;
	int error;

	if (tset_left != 0)
		return tileset_write(uio);
	if (tw_left != 0)
		return tilewin_write(uio);
	if (win_left == 0)
		return EINVAL;
	if (win_fmt == LCDG_WIN_RGB565 && (uio->uio_resid & 1) != 0)
		return EINVAL;
	while (uio->uio_resid > 0 && win_left > 0) {
		n = uio->uio_resid;
		if (n > sizeof(buf))
			n = sizeof(buf);
		if (n > win_left)
			n = win_left;
		error = uiomove((caddr_t)buf, n, uio);
		if (error != 0)
			return error;
		win_left -= n;
		window_data(buf, n);
	}
	if (win_left == 0)
		lcd_gfx_end();
	return 0;
}

/* LCDG_WINDOW: validate, open the panel window and count the bytes wanted. */
static int
window_begin(const struct lcdg_window *w)
{
	if (!coords_ok(&w->x, 2) || w->w == 0 || w->h == 0 ||
	    w->x + w->w > LCDG_WIDTH || w->y + w->h > LCDG_HEIGHT ||
	    w->fmt > LCDG_WIN_BITS || w->fg >= LCDG_NPAL || w->bg >= LCDG_NPAL)
		return EINVAL;
	win_fmt = w->fmt;
	win_fg = w->fg;
	win_bg = w->bg;
	win_w = w->w;
	win_col = 0;
	win_left = (unsigned)w->w * (unsigned)w->h;
	if (win_fmt == LCDG_WIN_RGB565)
		win_left *= 2;
	else if (win_fmt == LCDG_WIN_BITS)
		win_left = (unsigned)((w->w + 7) >> 3) * (unsigned)w->h;
	lcd_gfx_window(w->x, w->y, w->x + w->w - 1, w->y + w->h - 1);
	return 0;
}

int
lcdgfx_ioctl(unsigned cmd, char *addr)
{
	const struct lcdg_rect *rc = (const struct lcdg_rect *)addr;
	const struct lcdg_line *ln = (const struct lcdg_line *)addr;
	const struct lcdg_pixel *px = (const struct lcdg_pixel *)addr;
	const struct lcdg_circle *ci = (const struct lcdg_circle *)addr;
	const struct lcdg_palette *pl = (const struct lcdg_palette *)addr;
	const struct lcdg_scroll *sc = (const struct lcdg_scroll *)addr;
	const struct lcdg_scrdef *sd = (const struct lcdg_scrdef *)addr;
	const struct lcdg_colmod *cm = (const struct lcdg_colmod *)addr;
	const struct lcdg_tileset *ts = (const struct lcdg_tileset *)addr;
	const struct lcdg_tilewin *tl = (const struct lcdg_tilewin *)addr;
	struct lcdg_info *in = (struct lcdg_info *)addr;

	window_end();
	switch (cmd) {
	case LCDG_FILL:
	case LCDG_FRAME:
		if (!coords_ok(&rc->x0, 4) || rc->x0 > rc->x1 || rc->y0 > rc->y1)
			return EINVAL;
		if (cmd == LCDG_FILL)
			fill(rc->x0, rc->y0, rc->x1, rc->y1, rc->color);
		else
			frame(rc->x0, rc->y0, rc->x1, rc->y1, rc->color);
		return 0;
	case LCDG_PIXEL:
		if (!coords_ok(&px->x, 2))
			return EINVAL;
		plot(px->x, px->y, px->color);
		return 0;
	case LCDG_LINE:
		if (!coords_ok(&ln->x0, 4))
			return EINVAL;
		line(ln->x0, ln->y0, ln->x1, ln->y1, ln->color);
		return 0;
	case LCDG_CIRCLE:
		if (!coords_ok(&ci->x, 2) || ci->r > ci->x || ci->r > ci->y ||
		    ci->x + ci->r >= LCDG_WIDTH || ci->y + ci->r >= LCDG_HEIGHT)
			return EINVAL;
		circle(ci->x, ci->y, ci->r, ci->color);
		return 0;
	case LCDG_WINDOW:
		return window_begin((const struct lcdg_window *)addr);
	case LCDG_PALETTE:
		if (pl->index >= LCDG_NPAL)
			return EINVAL;
		gfx_pal[pl->index] = (unsigned short)rgb565(pl->r, pl->g, pl->b);
		return 0;
	case LCDG_SCROLL:
		if (sc->line < scr_top || sc->line >= LCDG_RAM_LINES - scr_bottom)
			return EINVAL;
		lcd_gfx_scroll(sc->line);
		return 0;
	case LCDG_SCRDEF:
		if (sd->top + sd->bottom >= LCDG_RAM_LINES)
			return EINVAL;
		scr_top = sd->top;
		scr_bottom = sd->bottom;
		lcd_gfx_scrdef(scr_top, scr_bottom);
		lcd_gfx_scroll(scr_top);
		return 0;
	case LCDG_COLMOD:
		if (cm->fmt != LCDG_COLMOD_RGB666 && cm->fmt != LCDG_COLMOD_RGB565)
			return EINVAL;
		gfx_fmt = cm->fmt;
		lcd_gfx_colmod(gfx_fmt);
		return 0;
	case LCDG_INFO:
		in->w = LCDG_WIDTH;
		in->h = LCDG_HEIGHT;
		in->tiles = LCDG_ATLAS_N;
		in->bpp = gfx_fmt == LCDG_COLMOD_RGB565 ? 16 : 18;
		return 0;
	case LCDG_TILESET:
		if (ts->count == 0 || ts->first + ts->count > LCDG_ATLAS_N)
			return EINVAL;
		tset_pos = ts->first * LCDG_TILE_BYTES;
		tset_left = ts->count * LCDG_TILE_BYTES;
		return 0;
	case LCDG_TILEWIN:
		if (tl->tw == 0 || tl->th == 0 ||
		    tl->tx + tl->tw > LCDG_GRID || tl->ty + tl->th > LCDG_GRID)
			return EINVAL;
		tw_x = tl->tx;
		tw_y = tl->ty;
		tw_w = tl->tw;
		tw_fill = 0;
		tw_left = tl->tw * tl->th;
		return 0;
	}
	return ENOTTY;
}
