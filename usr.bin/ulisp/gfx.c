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
#include <string.h>

#include "platform.h"
#include "lcdfont.h"

#define CELL_W	6
#define CELL_H	10
/*
 * The Lisp functions pass any int. Every sum, difference and product of
 * them here is taken in coord, which holds two sums of ints and a product
 * with a panel-sized or bounded factor; a shape is clipped after that.
 * A span of two ints reaches 2^32, so the products that place a clipped line
 * or a triangle edge are formed by muldiv() in unsigned 64 bits. A line is
 * clipped to the panel before it is walked, a triangle is walked over the
 * panel rows alone, and a circle too large for the midpoint walk is drawn
 * one panel row at a time (bigarcs), so a shape costs at most a few
 * thousand steps wherever its corners lie.
 */
typedef long long coord;
#define CURSOR_LIMIT	(1LL << 40)

static coord cursor_x, cursor_y;
static int text_color = 0xffff, text_bg = GFX_TRANSPARENT, text_size = 1;
static bool text_wrap = true;

static void
fill(coord x, coord y, coord w, coord h, int color)
{
	coord x1 = x + w - 1, y1 = y + h - 1;

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
	dev_fill((int)x, (int)y, (int)x1, (int)y1, (uint16_t)color);
}

void
gfx_fillrect(int x, int y, int w, int h, int color)
{
	fill(x, y, w, h, color);
}

void
gfx_pixel(int x, int y, int color)
{
	fill(x, y, 1, 1, color);
}

/* The horizontal run x0..x1 in either order. */
static void
hspan(coord x0, coord x1, coord y, int color)
{
	if (x0 > x1) {
		coord t = x0;
		x0 = x1;
		x1 = t;
	}
	fill(x0, y, x1 - x0 + 1, 1, color);
}

/*
 * a * b / c for |a| and |b| below 2^32 and c != 0, rounded to the nearest
 * integer or truncated toward zero. The product of the magnitudes stays
 * below 2^64, so the unsigned arithmetic cannot overflow.
 */
static coord
muldiv(coord a, coord b, coord c, bool nearest)
{
	bool neg = ((a < 0) ^ (b < 0) ^ (c < 0)) != 0;
	unsigned long long ua = a < 0 ? -(unsigned long long)a : (unsigned long long)a;
	unsigned long long ub = b < 0 ? -(unsigned long long)b : (unsigned long long)b;
	unsigned long long uc = c < 0 ? -(unsigned long long)c : (unsigned long long)c;
	unsigned long long n = ua * ub, q = 0, r = 0;

	/*
	 * Bitwise long division: the library's 64-bit divide is 400 bytes the
	 * program has no other use for. uc stays far below 2^63 (a panel
	 * coordinate difference), so the shifted remainder cannot overflow.
	 */
	for (int i = 63; i >= 0; i--) {
		r = r << 1 | ((n >> i) & 1);
		if (r >= uc) {
			r -= uc;
			q |= 1ULL << i;
		}
	}

	if (nearest && 2 * r >= uc)
		q++;
	return neg ? -(coord)q : (coord)q;
}

/* The Cohen-Sutherland region of a point: one bit for each side of the panel it lies beyond. */
static int
region(coord x, coord y)
{
	return (x < 0) | (x >= GFX_WIDTH) << 1 | (y < 0) << 2 | (y >= GFX_HEIGHT) << 3;
}

/*
 * Moves the endpoints of a segment onto the panel along the segment.
 * Returns false when the segment lies wholly outside it. An endpoint is
 * rounded to the nearest pixel on the line, which can leave it a pixel off
 * the panel on the other axis; the next pass clips it again, and a segment
 * that does not settle in a few passes is not drawn.
 */
static bool
clipline(coord *x0, coord *y0, coord *x1, coord *y1)
{
	for (int pass = 0; pass < 16; pass++) {
		int c0 = region(*x0, *y0), c1 = region(*x1, *y1);
		coord *px, *py, qx, qy;
		int out;

		if ((c0 | c1) == 0)
			return true;
		if ((c0 & c1) != 0)
			return false;
		out = c0 != 0 ? c0 : c1;
		if (c0 != 0) {
			px = x0; py = y0; qx = *x1; qy = *y1;
		} else {
			px = x1; py = y1; qx = *x0; qy = *y0;
		}
		if (out & 3) {
			coord edge = (out & 1) ? 0 : GFX_WIDTH - 1;

			*py += muldiv(edge - *px, qy - *py, qx - *px, true);
			*px = edge;
		} else {
			coord edge = (out & 4) ? 0 : GFX_HEIGHT - 1;

			*px += muldiv(edge - *py, qx - *px, qy - *py, true);
			*py = edge;
		}
	}
	return false;
}

void
gfx_line(int ix0, int iy0, int ix1, int iy1, int color)
{
	coord x0 = ix0, y0 = iy0, x1 = ix1, y1 = iy1;
	coord dx, sx, dy, sy, err;

	if (!clipline(&x0, &y0, &x1, &y1))
		return;
	dx = x1 > x0 ? x1 - x0 : x0 - x1;
	sx = x0 < x1 ? 1 : -1;
	dy = y1 > y0 ? y0 - y1 : y1 - y0;
	sy = y0 < y1 ? 1 : -1;
	err = dx + dy;
	if (dy == 0) {
		hspan(x0, x1, y0, color);
		return;
	}
	if (dx == 0) {
		fill(x0, y0 < y1 ? y0 : y1, 1, -dy + 1, color);
		return;
	}
	/* The clipped segment lies on the panel, so it has at most a few hundred steps. */
	for (;;) {
		coord e2 = 2 * err;

		fill(x0, y0, 1, 1, color);
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
	fill(x, y, w, 1, color);
	fill(x, (coord)y + h - 1, w, 1, color);
	fill(x, y, 1, h, color);
	fill((coord)x + w - 1, y, 1, h, color);
}

/* The largest s with s * s <= n. */
static coord
isqrt(unsigned long long n)
{
	unsigned long long s = 0, bit = 1ULL << 62;

	while (bit > n)
		bit >>= 2;
	while (bit != 0) {
		if (n >= s + bit) {
			n -= s + bit;
			s = (s >> 1) + bit;
		} else {
			s >>= 1;
		}
		bit >>= 2;
	}
	return (coord)s;
}

/*
 * The arcs of arcs() for a radius too large to walk step by step: for each
 * panel row, the run of pixels between the circle's extent on that row and
 * on the next row toward the pole, so the outline stays connected; a
 * filled shape takes the whole span. Row j above y0 (or below y1) is j rows
 * from the pole line, and its extent is isqrt(r*r - j*j), exact in 64 bits.
 */
static void
bigarcs(coord x0, coord y0, coord x1, coord y1, coord r, int color, bool filled)
{
	for (int side = 0; side < 2; side++) {
		coord base = side == 0 ? y0 : y1;
		coord lo = side == 0 ? base - (GFX_HEIGHT - 1) : -base;
		coord hi = side == 0 ? base : GFX_HEIGHT - 1 - base;

		if (lo < 0)
			lo = 0;
		if (hi > r)
			hi = r;
		for (coord j = lo; j <= hi; j++) {
			coord y = side == 0 ? base - j : base + j;
			coord out = isqrt((unsigned long long)(r * r - j * j));
			coord in = j < r ? isqrt((unsigned long long)(r * r - (j + 1) * (j + 1))) : 0;

			if (filled) {
				hspan(x0 - out, x1 + out, y, color);
			} else {
				hspan(x0 - out, x0 - in, y, color);
				hspan(x1 + in, x1 + out, y, color);
			}
		}
	}
}

/*
 * The midpoint circle of radius r, as four quarter arcs centered on the
 * corners of the rectangle x0..x1, y0..y1: a circle when the rectangle is
 * one point, the corners of a rounded rectangle otherwise. A filled shape
 * draws the runs between the left and right arcs.
 */
static void
arcs(coord x0, coord y0, coord x1, coord y1, int r, int color, bool filled)
{
	int f, x = 0, y = r;

	if (r < 0)
		return;
	if (r > 2 * GFX_WIDTH) {
		bigarcs(x0, y0, x1, y1, r, color, filled);
		return;
	}
	f = 1 - r;
	for (;;) {
		if (filled) {
			hspan(x0 - x, x1 + x, y0 - y, color);
			hspan(x0 - x, x1 + x, y1 + y, color);
			hspan(x0 - y, x1 + y, y0 - x, color);
			hspan(x0 - y, x1 + y, y1 + x, color);
		} else {
			fill(x0 - x, y0 - y, 1, 1, color);
			fill(x1 + x, y0 - y, 1, 1, color);
			fill(x0 - x, y1 + y, 1, 1, color);
			fill(x1 + x, y1 + y, 1, 1, color);
			fill(x0 - y, y0 - x, 1, 1, color);
			fill(x1 + y, y0 - x, 1, 1, color);
			fill(x0 - y, y1 + x, 1, 1, color);
			fill(x1 + y, y1 + x, 1, 1, color);
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
static coord
corner(coord w, coord h, coord r)
{
	coord most = (w < h ? w : h) / 2;

	return r > most ? most : r;
}

void
gfx_roundrect(int ix, int iy, int iw, int ih, int ir, int color)
{
	coord x = ix, y = iy, w = iw, h = ih, r;

	if (w <= 0 || h <= 0)
		return;
	r = corner(w, h, ir);
	fill(x + r, y, w - 2 * r, 1, color);
	fill(x + r, y + h - 1, w - 2 * r, 1, color);
	fill(x, y + r, 1, h - 2 * r, color);
	fill(x + w - 1, y + r, 1, h - 2 * r, color);
	arcs(x + r, y + r, x + w - 1 - r, y + h - 1 - r, (int)r, color, false);
}

void
gfx_fillroundrect(int ix, int iy, int iw, int ih, int ir, int color)
{
	coord x = ix, y = iy, w = iw, h = ih, r;

	if (w <= 0 || h <= 0)
		return;
	r = corner(w, h, ir);
	fill(x, y + r, w, h - 2 * r, color);
	arcs(x + r, y + r, x + w - 1 - r, y + h - 1 - r, (int)r, color, true);
}

void
gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int color)
{
	gfx_line(x0, y0, x1, y1, color);
	gfx_line(x1, y1, x2, y2, color);
	gfx_line(x2, y2, x0, y0, color);
}

/* x where the edge (xa,ya)-(xb,yb) crosses row y, truncated toward xa; ya != yb. */
static coord
edge(coord xa, coord ya, coord xb, coord yb, coord y)
{
	return xa + muldiv(xb - xa, y - ya, yb - ya, false);
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
		coord xa = edge(x0, y0, x2, y2, y), xb;

		if (y < y1 || y1 == y2)
			xb = (y0 == y1) ? x1 : edge(x0, y0, x1, y1, y);
		else
			xb = edge(x1, y1, x2, y2, y);
		hspan(xa, xb, y, color);
	}
}

/* The bits of an opaque cell of size: rows of (w + 7) / 8 bytes. */
static uint8_t cellbits[256];

static void
glyph_bits(int c, int w, int h, int size)
{
	int stride = (w + 7) / 8;

	memset(cellbits, 0, (size_t)(stride * h));
	for (int r = 0; r < h && r / size < LCDFONT_HEIGHT; r++) {
		unsigned int bits = lcdfont[c - LCDFONT_FIRST][r / size];

		for (int col = 0; col < w; col++)
			if (bits & (0x80u >> (col / size)))
				cellbits[r * stride + col / 8] |=
				    (uint8_t)(0x80u >> (col % 8));
	}
}

static void
glyph(coord x, coord y, int c, int color, int bg, int size)
{
	coord w = (coord)CELL_W * size, h = (coord)CELL_H * size;

	if (size < 1 || c < LCDFONT_FIRST || c >= LCDFONT_FIRST + LCDFONT_NCHARS)
		return;
	if (bg != color && bg != GFX_TRANSPARENT) {
		/* A cell wholly on the panel goes out as one window. */
		if (x >= 0 && y >= 0 && x + w <= GFX_WIDTH && y + h <= GFX_HEIGHT &&
		    (w + 7) / 8 * h <= (coord)sizeof(cellbits)) {
			glyph_bits(c, (int)w, (int)h, size);
			dev_bits((int)x, (int)y, (int)w, (int)h, cellbits,
			    (uint16_t)color, (uint16_t)bg);
			return;
		}
		fill(x, y, w, h, bg);
	}
	for (int row = 0; row < LCDFONT_HEIGHT; row++) {
		unsigned int bits = lcdfont[c - LCDFONT_FIRST][row];

		/* One fill for each run of set pixels in the row. */
		for (int col = 0; col < CELL_W; col++) {
			int run = col;

			while (run < CELL_W && (bits & (0x80u >> run)) != 0)
				run++;
			if (run > col) {
				fill(x + (coord)col * size, y + (coord)row * size,
				    (coord)(run - col) * size, size, color);
				col = run;
			}
		}
	}
}

void
gfx_char(int x, int y, int c, int color, int bg, int size)
{
	glyph(x, y, c, color, bg, size);
}

/* The cursor stays within CURSOR_LIMIT however long the text runs. */
static coord
advance(coord at, coord by)
{
	at += by;
	return at > CURSOR_LIMIT ? CURSOR_LIMIT : at;
}

void
gfx_write(char c)
{
	if (c == '\n') {
		cursor_x = 0;
		cursor_y = advance(cursor_y, (coord)CELL_H * text_size);
		return;
	}
	if (c == '\r')
		return;
	if (text_wrap && cursor_x + (coord)CELL_W * text_size > GFX_WIDTH) {
		cursor_x = 0;
		cursor_y = advance(cursor_y, (coord)CELL_H * text_size);
	}
	glyph(cursor_x, cursor_y, c, text_color, text_bg, text_size);
	cursor_x = advance(cursor_x, (coord)CELL_W * text_size);
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
