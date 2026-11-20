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
 * libwin: a strip compositor for the PicoCalc's 320x320 panel.
 *
 * A scene holds up to WIN_MAX rectangular windows in z-order. Each window
 * has a one-pixel frame, a title bar of WIN_TITLE_H rows and a client area
 * that a paint callback fills through a clipped drawing context. The scene
 * keeps no copy of the screen. Changes accumulate as a short list of
 * damage rectangles; win_flush() renders the scene back to front into a
 * strip buffer of WIN_STRIP_BYTES bytes, one damage rectangle in strips of
 * whole rows, and hands each strip to a backend as one rectangle. A pixel
 * is one byte whose low four bits select a palette entry, the layout of
 * LCDG_WIN_INDEX in sys/lcdgfx.h.
 *
 * Window geometry, in panel pixels: a window at (x, y) of size w by h
 * occupies x .. x + w - 1 and y .. y + h - 1. The frame is the outer ring.
 * The title bar is the WIN_TITLE_H rows inside the frame, and the client
 * area is the remainder: (x + 1, y + WIN_TITLE_H + 1), w - 2 by
 * h - WIN_TITLE_H - 2 pixels. A window may lie partly or wholly off the
 * panel; only panel pixels are rendered.
 *
 * Damage policy. win_invalidate() and every call that changes pixels add
 * their rectangle, clipped to the panel, to a list of WIN_DAMAGE_MAX
 * rectangles. A rectangle inside an existing one is dropped, one that
 * contains existing ones replaces them, two that join into a rectangle are
 * merged, and a partial overlap is trimmed where the remainder is itself a
 * rectangle, so the list covers exactly the pixels invalidated and few
 * pixels twice. A rectangle is never widened.
 *
 * Failure rule. A call adds at most two rectangles, so it first flushes the
 * list when fewer than two slots are free, before it changes the scene.
 * When that flush fails at the backend the call returns -1 (NULL from
 * win_create) having changed nothing: the caller holds no new window, the
 * existing windows, their order and geometry are as before, and the
 * rectangle of an invalidate is not recorded. The unflushed rectangles,
 * including the one the backend refused, stay in the list, so a later
 * win_flush() or call retries them and writes only invalidated pixels.
 * A failing win_flush() returns -1 and keeps the same rectangles. A call
 * that returns 0 has recorded all of its damage. Pixels a failed flush had
 * already sent stay on the panel.
 *
 * Paint callbacks draw only through the context and do not call the scene
 * functions. Calls return 0, or -1 for an invalid argument, a full scene
 * or a backend failure; the functions that return a window return NULL.
 * Coordinates and sizes stay within WIN_LIMIT in magnitude.
 */
#ifndef _WIN_H_
#define _WIN_H_

#include <stddef.h>

#define WIN_PANEL_W	320
#define WIN_PANEL_H	320
#define WIN_MAX		8
#define WIN_DAMAGE_MAX	8
#define WIN_STRIP_BYTES	5120		/* 16 rows of a full-width strip. */
#define WIN_GUARD	8		/* Guard bytes on each side of the strip. */
#define WIN_GUARD_BYTE	0xa5
#define WIN_TITLE_MAX	24		/* Title bytes including the NUL. */
#define WIN_TITLE_H	11		/* Title bar rows: 9 glyph rows and a pad each side. */
#define WIN_MIN_W	8
#define WIN_MIN_H	(WIN_TITLE_H + 3)
#define WIN_LIMIT	4096
#define WIN_NPAL	16

/* Palette indexes the library draws with. */
#define WIN_C_DESKTOP	6
#define WIN_C_FRAME	0
#define WIN_C_TITLE_BG	4
#define WIN_C_TITLE_FG	15
#define WIN_C_CLIENT	7

struct win_ctx {
	unsigned char *buf;		/* Strip pixels, one byte each. */
	int sx, sy, sw, sh;		/* Strip rectangle in panel pixels. */
	int ox, oy;			/* Panel position of drawing point (0, 0). */
	int cx0, cy0, cx1, cy1;		/* Clip, panel pixels, upper bounds excluded. */
	int w, h;			/* Size of the drawing area (the client area). */
};

typedef void (*win_paint_fn)(struct win_ctx *ctx, void *arg);

/* Where finished rectangles go. px holds w * h bytes, row by row. */
struct win_backend {
	int (*rect)(void *self, int x, int y, int w, int h,
	    const unsigned char *px);
	int (*palette)(void *self, int index, int r, int g, int b);
	void *self;
};

struct win_rect {
	int x0, y0, x1, y1;		/* Upper bounds excluded. */
};

struct win {
	int x, y, w, h;
	win_paint_fn paint;
	void *arg;
	char title[WIN_TITLE_MAX];
	int used;
};

struct win_scene {
	struct win wins[WIN_MAX];
	unsigned char z[WIN_MAX];	/* Indexes into wins, bottom first. */
	int nwin;
	struct win_rect damage[WIN_DAMAGE_MAX];
	int ndamage;
	int desktop;			/* Palette index behind the windows. */
	struct win_backend be;
	unsigned char buf[WIN_GUARD + WIN_STRIP_BYTES + WIN_GUARD];
};

/* Scene: the whole panel starts damaged, so the first flush paints it. */
int win_scene_init(struct win_scene *s, const struct win_backend *be);
int win_set_palette(struct win_scene *s, int index, int r, int g, int b);
int win_flush(struct win_scene *s);

/* Windows. A new window is on top. */
struct win *win_create(struct win_scene *s, int x, int y, int w, int h,
    const char *title, win_paint_fn paint, void *arg);
int win_destroy(struct win_scene *s, struct win *win);
int win_move(struct win_scene *s, struct win *win, int x, int y);
int win_resize(struct win_scene *s, struct win *win, int w, int h);
int win_raise(struct win_scene *s, struct win *win);
int win_set_title(struct win_scene *s, struct win *win, const char *title);

/* Damage: a rectangle in panel pixels, or in client pixels of one window. */
int win_invalidate(struct win_scene *s, int x, int y, int w, int h);
int win_invalidate_client(struct win_scene *s, struct win *win,
    int x, int y, int w, int h);

/* Drawing context, for paint callbacks. (x, y) is relative to the client area. */
void win_fill(struct win_ctx *c, int x, int y, int w, int h, int color);
void win_hline(struct win_ctx *c, int x, int y, int len, int color);
void win_vline(struct win_ctx *c, int x, int y, int len, int color);
void win_text(struct win_ctx *c, int x, int y, const char *s, int color);

/* The /dev/lcdgfx backend: LCDG_WINDOW with LCDG_WIN_INDEX and LCDG_PALETTE. */
struct win_lcdgfx {
	int fd;
};
void win_lcdgfx_init(struct win_backend *be, struct win_lcdgfx *st, int fd);

#endif /* !_WIN_H_ */
