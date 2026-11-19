/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Prototype core of a purpose-built layer: 4-bit shadow, damage bands,
 * rectangle-list clipping, z-ordered windows with title bars, 6x9 text.
 * Measured for size only; not a product. */
#include <stdint.h>
#include <string.h>
#define W 320
#define H 320
#define NWIN 8
#define MAXCLIP 24
struct rect { int16_t x, y, w, h; };
struct win {
	struct rect r;
	const char *title;
	uint8_t shown;
	void (*paint)(struct win *, const struct rect *);
};
extern const unsigned char lcdfont[95][9];
extern int panel_window(int x, int y, int w, int h);	/* ioctl LCDG_WINDOW */
extern int panel_write(const unsigned char *p, int n);	/* write(2) */

static uint8_t fb[W * H / 2];
static int16_t dminx[H / 8], dmaxx[H / 8];
static struct win *zorder[NWIN];
static int nwin, focus;
static struct rect clip = { 0, 0, W, H };

static void damage(int x, int y, int w, int h)
{
	int b, x1 = x + w;
	for (b = y / 8; b <= (y + h - 1) / 8 && b < H / 8; b++) {
		if (dmaxx[b] <= dminx[b]) { dminx[b] = x; dmaxx[b] = x1; }
		else { if (x < dminx[b]) dminx[b] = x; if (x1 > dmaxx[b]) dmaxx[b] = x1; }
	}
}
static void put(int x, int y, int c)
{
	uint8_t *p = &fb[(y * W + x) >> 1];
	if (x & 1) *p = (*p & 0xf0) | c; else *p = (*p & 0x0f) | (c << 4);
}
static void fillrect(int x, int y, int w, int h, int c)
{
	int x1 = x + w, y1 = y + h, i, j;
	if (x < clip.x) x = clip.x;
	if (y < clip.y) y = clip.y;
	if (x1 > clip.x + clip.w) x1 = clip.x + clip.w;
	if (y1 > clip.y + clip.h) y1 = clip.y + clip.h;
	if (x1 <= x || y1 <= y) return;
	for (j = y; j < y1; j++) for (i = x; i < x1; i++) put(i, j, c);
	damage(x, y, x1 - x, y1 - y);
}
static void text(int x, int y, const char *s, int fg, int bg)
{
	int r, c;
	for (; *s; s++, x += 6) {
		const unsigned char *g = lcdfont[(*s - 32) % 95];
		fillrect(x, y, 6, 9, bg);
		for (r = 0; r < 9; r++) for (c = 0; c < 6; c++)
			if ((g[r] << c) & 128 && x + c >= clip.x && x + c < clip.x + clip.w &&
			    y + r >= clip.y && y + r < clip.y + clip.h) put(x + c, y + r, fg);
		damage(x, y, 6, 9);
	}
}
/* r minus o, up to four rectangles. */
static int subtract(const struct rect *r, const struct rect *o, struct rect *out)
{
	int n = 0, x1 = r->x + r->w, y1 = r->y + r->h, ox1 = o->x + o->w, oy1 = o->y + o->h;
	if (o->x >= x1 || ox1 <= r->x || o->y >= y1 || oy1 <= r->y) { out[0] = *r; return 1; }
	if (o->y > r->y) { out[n].x = r->x; out[n].y = r->y; out[n].w = r->w; out[n++].h = o->y - r->y; }
	if (oy1 < y1) { out[n].x = r->x; out[n].y = oy1; out[n].w = r->w; out[n++].h = y1 - oy1; }
	{
		int ty = o->y > r->y ? o->y : r->y, by = oy1 < y1 ? oy1 : y1;
		if (o->x > r->x) { out[n].x = r->x; out[n].y = ty; out[n].w = o->x - r->x; out[n++].h = by - ty; }
		if (ox1 < x1) { out[n].x = ox1; out[n].y = ty; out[n].w = x1 - ox1; out[n++].h = by - ty; }
	}
	return n;
}
/* Visible part of window i: its rectangle minus every window above it. */
static int visible(int i, struct rect *v)
{
	struct rect tmp[MAXCLIP];
	int n = 1, j, k, m;
	v[0] = zorder[i]->r;
	for (j = i + 1; j < nwin; j++) {
		m = 0;
		for (k = 0; k < n && m + 4 <= MAXCLIP; k++) m += subtract(&v[k], &zorder[j]->r, tmp + m);
		memcpy(v, tmp, m * sizeof(tmp[0]));
		n = m;
	}
	return n;
}
static void paintwin(int i)
{
	struct rect v[MAXCLIP];
	struct win *w = zorder[i];
	int n = visible(i, v), k;
	for (k = 0; k < n; k++) {
		clip = v[k];
		fillrect(w->r.x, w->r.y, w->r.w, 12, i == nwin - 1 ? 1 : 8);
		text(w->r.x + 2, w->r.y + 1, w->title, 15, i == nwin - 1 ? 1 : 8);
		fillrect(w->r.x, w->r.y + 12, w->r.w, w->r.h - 12, 7);
		if (w->paint) w->paint(w, &v[k]);
	}
	clip.x = 0; clip.y = 0; clip.w = W; clip.h = H;
}
void gui_paint_all(void)
{
	int i;
	for (i = 0; i < nwin; i++) if (zorder[i]->shown) paintwin(i);
}
void gui_raise(struct win *w)
{
	int i, j;
	for (i = 0; i < nwin && zorder[i] != w; i++) ;
	if (i == nwin) { if (nwin == NWIN) return; zorder[nwin++] = w; i = nwin - 1; }
	for (j = i; j < nwin - 1; j++) zorder[j] = zorder[j + 1];
	zorder[nwin - 1] = w;
	focus = nwin - 1;
	gui_paint_all();
}
void gui_move(struct win *w, int dx, int dy)
{
	w->r.x += dx; w->r.y += dy;
	fillrect(0, 0, W, H, 0);
	gui_paint_all();
}
void gui_flush(void)
{
	unsigned char row[W];
	int b, y, x, x0, x1;
	for (b = 0; b < H / 8; b++) {
		if (dmaxx[b] <= dminx[b]) continue;
		x0 = dminx[b]; x1 = dmaxx[b];
		panel_window(x0, b * 8, x1 - x0, 8);
		for (y = b * 8; y < b * 8 + 8; y++) {
			for (x = x0; x < x1; x++) {
				uint8_t v = fb[(y * W + x) >> 1];
				row[x - x0] = (x & 1) ? v & 15 : v >> 4;
			}
			panel_write(row, x1 - x0);
		}
		dmaxx[b] = dminx[b] = 0;
	}
}
