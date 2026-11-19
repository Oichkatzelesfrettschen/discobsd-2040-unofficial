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
 * Shapes and text for the uLisp graphics functions, drawn as clipped
 * rectangle fills through dev_fill(). The kernel's pixel device rejects a
 * shape that leaves the panel, so every primitive is clipped here; a
 * single device operation keeps the display interface to one ioctl.
 */
#include "platform.h"
#include "lcdfont.h"

#define CELL_W	6
#define CELL_H	10

static int cursor_x, cursor_y;
static int text_color = 0xffff, text_bg = GFX_TRANSPARENT, text_size = 1;
static bool text_wrap = true;

void
gfx_fillrect(int x, int y, int w, int h, int color)
{
	int x1 = x + w - 1, y1 = y + h - 1;

	if (w <= 0 || h <= 0 || x >= GFX_WIDTH || y >= GFX_HEIGHT ||
	    x1 < 0 || y1 < 0)
		return;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 >= GFX_WIDTH)
		x1 = GFX_WIDTH - 1;
	if (y1 >= GFX_HEIGHT)
		y1 = GFX_HEIGHT - 1;
	dev_fill(x, y, x1, y1, (uint16_t)color);
}

void
gfx_pixel(int x, int y, int color)
{
	gfx_fillrect(x, y, 1, 1, color);
}

/* The horizontal run x0..x1 in either order. */
static void
hspan(int x0, int x1, int y, int color)
{
	if (x0 > x1) {
		int t = x0;
		x0 = x1;
		x1 = t;
	}
	gfx_fillrect(x0, y, x1 - x0 + 1, 1, color);
}

void
gfx_line(int x0, int y0, int x1, int y1, int color)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	if (dy == 0) {
		hspan(x0, x1, y0, color);
		return;
	}
	if (dx == 0) {
		gfx_fillrect(x0, y0 < y1 ? y0 : y1, 1, -dy + 1, color);
		return;
	}
	/* Each step is one pixel; a line far off the panel is not walked. */
	if (dx > 4 * GFX_WIDTH || -dy > 4 * GFX_HEIGHT)
		return;
	for (;;) {
		int e2 = 2 * err;

		gfx_pixel(x0, y0, color);
		if (x0 == x1 && y0 == y1)
			break;
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

void
gfx_rect(int x, int y, int w, int h, int color)
{
	gfx_fillrect(x, y, w, 1, color);
	gfx_fillrect(x, y + h - 1, w, 1, color);
	gfx_fillrect(x, y, 1, h, color);
	gfx_fillrect(x + w - 1, y, 1, h, color);
}

/*
 * The midpoint circle of radius r, as four quarter arcs centered on the
 * corners of the rectangle x0..x1, y0..y1: a circle when the rectangle is
 * one point, the corners of a rounded rectangle otherwise. A filled shape
 * draws the runs between the left and right arcs.
 */
static void
arcs(int x0, int y0, int x1, int y1, int r, int color, bool filled)
{
	int f = 1 - r, x = 0, y = r;

	if (r < 0 || r > 2 * GFX_WIDTH)
		return;
	for (;;) {
		if (filled) {
			hspan(x0 - x, x1 + x, y0 - y, color);
			hspan(x0 - x, x1 + x, y1 + y, color);
			hspan(x0 - y, x1 + y, y0 - x, color);
			hspan(x0 - y, x1 + y, y1 + x, color);
		} else {
			gfx_pixel(x0 - x, y0 - y, color);
			gfx_pixel(x1 + x, y0 - y, color);
			gfx_pixel(x0 - x, y1 + y, color);
			gfx_pixel(x1 + x, y1 + y, color);
			gfx_pixel(x0 - y, y0 - x, color);
			gfx_pixel(x1 + y, y0 - x, color);
			gfx_pixel(x0 - y, y1 + x, color);
			gfx_pixel(x1 + y, y1 + x, color);
		}
		if (x >= y)
			break;
		if (f >= 0) {
			y--;
			f -= 2 * y;
		}
		x++;
		f += 2 * x + 1;
	}
}

void
gfx_circle(int x, int y, int r, int color)
{
	arcs(x, y, x, y, r, color, false);
}

void
gfx_fillcircle(int x, int y, int r, int color)
{
	arcs(x, y, x, y, r, color, true);
}

/* The radius a rounded rectangle of w by h can carry. */
static int
corner(int w, int h, int r)
{
	int most = (w < h ? w : h) / 2;

	return r > most ? most : r;
}

void
gfx_roundrect(int x, int y, int w, int h, int r, int color)
{
	if (w <= 0 || h <= 0)
		return;
	r = corner(w, h, r);
	gfx_fillrect(x + r, y, w - 2 * r, 1, color);
	gfx_fillrect(x + r, y + h - 1, w - 2 * r, 1, color);
	gfx_fillrect(x, y + r, 1, h - 2 * r, color);
	gfx_fillrect(x + w - 1, y + r, 1, h - 2 * r, color);
	arcs(x + r, y + r, x + w - 1 - r, y + h - 1 - r, r, color, false);
}

void
gfx_fillroundrect(int x, int y, int w, int h, int r, int color)
{
	if (w <= 0 || h <= 0)
		return;
	r = corner(w, h, r);
	gfx_fillrect(x, y + r, w, h - 2 * r, color);
	arcs(x + r, y + r, x + w - 1 - r, y + h - 1 - r, r, color, true);
}

void
gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int color)
{
	gfx_line(x0, y0, x1, y1, color);
	gfx_line(x1, y1, x2, y2, color);
	gfx_line(x2, y2, x0, y0, color);
}

/* x where the edge (xa,ya)-(xb,yb) crosses row y; ya != yb. */
static int
edge(int xa, int ya, int xb, int yb, int y)
{
	return xa + (int)((long long)(xb - xa) * (y - ya) / (yb - ya));
}

void
gfx_filltriangle(int x0, int y0, int x1, int y1, int x2, int y2, int color)
{
	int t;

#define SWAP(a, b)	(t = a, a = b, b = t)
	if (y0 > y1) {
		SWAP(y0, y1);
		SWAP(x0, x1);
	}
	if (y1 > y2) {
		SWAP(y1, y2);
		SWAP(x1, x2);
	}
	if (y0 > y1) {
		SWAP(y0, y1);
		SWAP(x0, x1);
	}
#undef SWAP
	if (y0 == y2) {		/* Every corner on one row. */
		int lo = x0 < x1 ? x0 : x1, hi = x0 > x1 ? x0 : x1;

		hspan(lo < x2 ? lo : x2, hi > x2 ? hi : x2, y0, color);
		return;
	}
	for (int y = y0 < 0 ? 0 : y0; y <= y2 && y < GFX_HEIGHT; y++) {
		int xa = edge(x0, y0, x2, y2, y), xb;

		if (y < y1 || y1 == y2)
			xb = (y0 == y1) ? x1 : edge(x0, y0, x1, y1, y);
		else
			xb = edge(x1, y1, x2, y2, y);
		hspan(xa, xb, y, color);
	}
}

void
gfx_char(int x, int y, int c, int color, int bg, int size)
{
	if (size < 1 || c < LCDFONT_FIRST || c >= LCDFONT_FIRST + LCDFONT_NCHARS)
		return;
	if (bg != color && bg != GFX_TRANSPARENT)
		gfx_fillrect(x, y, CELL_W * size, CELL_H * size, bg);
	for (int row = 0; row < LCDFONT_HEIGHT; row++) {
		unsigned int bits = lcdfont[c - LCDFONT_FIRST][row];

		/* One fill for each run of set pixels in the row. */
		for (int col = 0; col < CELL_W; col++) {
			int run = col;

			while (run < CELL_W && (bits & (0x80u >> run)) != 0)
				run++;
			if (run > col) {
				gfx_fillrect(x + col * size, y + row * size,
				    (run - col) * size, size, color);
				col = run;
			}
		}
	}
}

void
gfx_write(char c)
{
	if (c == '\n') {
		cursor_x = 0;
		cursor_y += CELL_H * text_size;
		return;
	}
	if (c == '\r')
		return;
	if (text_wrap && cursor_x + CELL_W * text_size > GFX_WIDTH) {
		cursor_x = 0;
		cursor_y += CELL_H * text_size;
	}
	gfx_char(cursor_x, cursor_y, c, text_color, text_bg, text_size);
	cursor_x += CELL_W * text_size;
}

void
gfx_setcursor(int x, int y)
{
	cursor_x = x;
	cursor_y = y;
}

void
gfx_settextcolor(int color, int bg)
{
	text_color = color;
	text_bg = bg;
}

void
gfx_settextsize(int size)
{
	text_size = size < 1 ? 1 : size;
}

void
gfx_settextwrap(bool wrap)
{
	text_wrap = wrap;
}

void
gfx_fillscreen(int color)
{
	gfx_fillrect(0, 0, GFX_WIDTH, GFX_HEIGHT, color);
}

int
gfx_readpixel(int x, int y)
{
	if (x < 0 || y < 0 || x >= GFX_WIDTH || y >= GFX_HEIGHT)
		return 0;
	return dev_readpixel(x, y);
}
