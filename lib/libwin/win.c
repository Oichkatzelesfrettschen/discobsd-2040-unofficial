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
 * libwin core: scene, damage list and strip renderer. See win.h for the
 * model. Rendering is the painter's algorithm into one strip: the desktop
 * color, then each window bottom to top (frame, title bar, client
 * background, client callback), every drawing call clipped to the strip and
 * to the part of the window it belongs to.
 */
#include <string.h>

#include "win.h"
#include "../../sys/arch/rp2040/dev/lcdfont.h"

#define GLYPH_W		6
#define PIXEL_MASK	0x0f

static int
clampi(int v)
{
	if (v < -WIN_LIMIT)
		return -WIN_LIMIT;
	if (v > WIN_LIMIT)
		return WIN_LIMIT;
	return v;
}

static int
min_i(int a, int b)
{
	return a < b ? a : b;
}

static int
max_i(int a, int b)
{
	return a > b ? a : b;
}

/* Rectangles */

static int
rect_empty(const struct win_rect *r)
{
	return r->x0 >= r->x1 || r->y0 >= r->y1;
}

/* a contains b. */
static int
rect_has(const struct win_rect *a, const struct win_rect *b)
{
	return a->x0 <= b->x0 && a->y0 <= b->y0 && a->x1 >= b->x1 &&
	    a->y1 >= b->y1;
}

static int
rect_overlap(const struct win_rect *a, const struct win_rect *b)
{
	return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 &&
	    b->y0 < a->y1;
}

/* Replace a by a joined with b when the union is a rectangle. */
static int
rect_join(struct win_rect *a, const struct win_rect *b)
{
	if (a->y0 == b->y0 && a->y1 == b->y1 && a->x0 <= b->x1 &&
	    b->x0 <= a->x1) {
		a->x0 = min_i(a->x0, b->x0);
		a->x1 = max_i(a->x1, b->x1);
		return 1;
	}
	if (a->x0 == b->x0 && a->x1 == b->x1 && a->y0 <= b->y1 &&
	    b->y0 <= a->y1) {
		a->y0 = min_i(a->y0, b->y0);
		a->y1 = max_i(a->y1, b->y1);
		return 1;
	}
	return 0;
}

/*
 * Cut b out of a when b crosses a from edge to edge and a keeps one
 * rectangle. a and b overlap and neither contains the other.
 */
static int
rect_trim(struct win_rect *a, const struct win_rect *b)
{
	if (b->y0 <= a->y0 && b->y1 >= a->y1) {
		if (b->x0 <= a->x0) {
			a->x0 = b->x1;
			return 1;
		}
		if (b->x1 >= a->x1) {
			a->x1 = b->x0;
			return 1;
		}
	} else if (b->x0 <= a->x0 && b->x1 >= a->x1) {
		if (b->y0 <= a->y0) {
			a->y0 = b->y1;
			return 1;
		}
		if (b->y1 >= a->y1) {
			a->y1 = b->y0;
			return 1;
		}
	}
	return 0;
}

/* Drawing context */

static void
ctx_clip(struct win_ctx *c, int x0, int y0, int x1, int y1)
{
	c->cx0 = max_i(x0, c->sx);
	c->cy0 = max_i(y0, c->sy);
	c->cx1 = min_i(x1, c->sx + c->sw);
	c->cy1 = min_i(y1, c->sy + c->sh);
}

/* Fill panel-pixel rectangle [x0, x1) x [y0, y1) inside the clip. */
static void
fill_abs(struct win_ctx *c, int x0, int y0, int x1, int y1, int color)
{
	int y;

	x0 = max_i(x0, c->cx0);
	y0 = max_i(y0, c->cy0);
	x1 = min_i(x1, c->cx1);
	y1 = min_i(y1, c->cy1);
	if (x0 >= x1 || y0 >= y1)
		return;
	for (y = y0; y < y1; y++)
		memset(c->buf + (y - c->sy) * c->sw + (x0 - c->sx),
		    color & PIXEL_MASK, (size_t)(x1 - x0));
}

void
win_fill(struct win_ctx *c, int x, int y, int w, int h, int color)
{
	if (w <= 0 || h <= 0)
		return;
	x = c->ox + clampi(x);
	y = c->oy + clampi(y);
	fill_abs(c, x, y, x + clampi(w), y + clampi(h), color);
}

void
win_hline(struct win_ctx *c, int x, int y, int len, int color)
{
	win_fill(c, x, y, len, 1, color);
}

void
win_vline(struct win_ctx *c, int x, int y, int len, int color)
{
	win_fill(c, x, y, 1, len, color);
}

void
win_text(struct win_ctx *c, int x, int y, const char *s, int color)
{
	const unsigned char *rows;
	unsigned ch;
	int gx, gy, row, col, px, py;

	gx = c->ox + clampi(x);
	gy = c->oy + clampi(y);
	if (gy >= c->cy1 || gy + LCDFONT_HEIGHT <= c->cy0)
		return;
	for (; *s != '\0'; s++, gx += GLYPH_W) {
		if (gx >= c->cx1)
			break;
		if (gx + GLYPH_W <= c->cx0)
			continue;
		ch = (unsigned char)*s;
		if (ch < LCDFONT_FIRST || ch >= LCDFONT_FIRST + LCDFONT_NCHARS)
			ch = '?';
		rows = lcdfont[ch - LCDFONT_FIRST];
		for (row = 0; row < LCDFONT_HEIGHT; row++) {
			py = gy + row;
			if (py < c->cy0 || py >= c->cy1)
				continue;
			for (col = 0; col < GLYPH_W; col++) {
				px = gx + col;
				if ((rows[row] & (0x80 >> col)) == 0 ||
				    px < c->cx0 || px >= c->cx1)
					continue;
				c->buf[(py - c->sy) * c->sw + (px - c->sx)] =
				    (unsigned char)(color & PIXEL_MASK);
			}
		}
	}
}

/* Damage list */

static void
damage_drop(struct win_scene *s, int i)
{
	s->ndamage--;
	s->damage[i] = s->damage[s->ndamage];
}

/*
 * Record [x0, x1) x [y0, y1), clipped to the panel. Every public call makes
 * room first (make_room), so the list has a free slot for each rectangle a
 * call adds, and the store below never finds it full.
 */
static void
damage_add(struct win_scene *s, int x0, int y0, int x1, int y1)
{
	struct win_rect n;
	struct win_rect *e;
	int i, again;

	n.x0 = max_i(x0, 0);
	n.y0 = max_i(y0, 0);
	n.x1 = min_i(x1, WIN_PANEL_W);
	n.y1 = min_i(y1, WIN_PANEL_H);
	if (rect_empty(&n))
		return;
	do {
		again = 0;
		for (i = 0; i < s->ndamage && !again; i++) {
			e = &s->damage[i];
			if (rect_has(e, &n))
				return;
			if (rect_has(&n, e) || rect_join(&n, e)) {
				damage_drop(s, i);
				again = 1;
			} else if (rect_overlap(&n, e) &&
			    (rect_trim(&n, e) || rect_trim(e, &n))) {
				again = 1;
			}
		}
	} while (again);
	s->damage[s->ndamage++] = n;
}

/*
 * Free n damage slots before anything changes. A backend failure here leaves
 * the scene and the list as they were, so the caller reports it and has
 * changed nothing. The worst case of each public call, in rectangles, is
 * WIN_DAMAGE_CALL (2):
 *	win_create		its window, the old focus title bar
 *	win_destroy, win_hide	the window, the new focus title bar
 *	win_show		the window (the new focus title is inside it)
 *	win_raise, win_lower	the window, one other title bar
 *	win_focus		two title bars
 *	win_move, win_resize, win_set_geometry	old and new rectangle
 *	win_set_title, win_invalidate		one
 * A compound operation of k calls reserves k * WIN_DAMAGE_CALL first
 * (win_reserve), so no inner call finds the list too full to take its own.
 */
static int
make_room(struct win_scene *s, int n)
{
	if (s->ndamage > WIN_DAMAGE_MAX - n)
		return win_flush(s);
	return 0;
}

int
win_reserve(struct win_scene *s, int n)
{
	if (n < 1 || n > WIN_DAMAGE_MAX)
		return -1;
	return make_room(s, n);
}

static void
window_rect(const struct win *w, struct win_rect *r)
{
	r->x0 = w->x;
	r->y0 = w->y;
	r->x1 = w->x + w->w;
	r->y1 = w->y + w->h;
}

static void
damage_rect(struct win_scene *s, const struct win_rect *r)
{
	damage_add(s, r->x0, r->y0, r->x1, r->y1);
}

static void
damage_window(struct win_scene *s, const struct win *w)
{
	struct win_rect r;

	window_rect(w, &r);
	damage_rect(s, &r);
}

/* A hidden window draws nothing, so a change to it is not damage. */
static void
damage_shown(struct win_scene *s, const struct win *w)
{
	if (!w->hidden)
		damage_window(s, w);
}

/* The title bar of the window in a slot. */
static void
damage_title(struct win_scene *s, int slot)
{
	const struct win *w;

	if (slot < 0)
		return;
	w = &s->wins[slot];
	damage_add(s, w->x + 1, w->y + 1, w->x + w->w - 1,
	    w->y + 1 + WIN_TITLE_H);
}

/* Focus a slot (or -1) and redraw the two title bars that change. */
static void
focus_set(struct win_scene *s, int slot)
{
	if (s->focus == slot)
		return;
	damage_title(s, s->focus);
	s->focus = slot;
	damage_title(s, slot);
}

/*
 * The shown window nearest to z index from, going down the stack and then
 * wrapping to the top, or -1.
 */
static int
focus_pick(const struct win_scene *s, int from)
{
	int k;

	for (k = from; k >= 0; k--)
		if (!s->wins[s->z[k]].hidden)
			return s->z[k];
	for (k = s->nwin - 1; k > from; k--)
		if (!s->wins[s->z[k]].hidden)
			return s->z[k];
	return -1;
}

/* Rendering */

static void
paint_window(struct win_ctx *c, const struct win *w, int focused)
{
	int tx0, ty0, tx1, ty1, cx0, cy0, cx1, cy1;

	if (w->hidden)
		return;
	if (w->x >= c->sx + c->sw || w->x + w->w <= c->sx ||
	    w->y >= c->sy + c->sh || w->y + w->h <= c->sy)
		return;
	c->ox = 0;
	c->oy = 0;
	ctx_clip(c, w->x, w->y, w->x + w->w, w->y + w->h);
	fill_abs(c, w->x, w->y, w->x + w->w, w->y + w->h, WIN_C_FRAME);

	tx0 = w->x + 1;
	ty0 = w->y + 1;
	tx1 = w->x + w->w - 1;
	ty1 = ty0 + WIN_TITLE_H;
	ctx_clip(c, tx0, ty0, tx1, ty1);
	fill_abs(c, tx0, ty0, tx1, ty1,
	    focused ? WIN_C_TITLE_BG : WIN_C_TITLE_IDLE);
	c->ox = tx0;
	c->oy = ty0;
	win_text(c, 2, 1, w->title, WIN_C_TITLE_FG);

	cx0 = w->x + 1;
	cy0 = ty1;
	cx1 = w->x + w->w - 1;
	cy1 = w->y + w->h - 1;
	ctx_clip(c, cx0, cy0, cx1, cy1);
	fill_abs(c, cx0, cy0, cx1, cy1, WIN_C_CLIENT);
	c->ox = cx0;
	c->oy = cy0;
	c->w = cx1 - cx0;
	c->h = cy1 - cy0;
	if (w->paint != NULL)
		w->paint(c, w->arg);
}

/* Render one damage rectangle, in strips of whole rows, and send each. */
static int
repaint(struct win_scene *s, const struct win_rect *r)
{
	struct win_ctx c;
	int w, rows, y, h, i;

	w = r->x1 - r->x0;
	rows = WIN_STRIP_BYTES / w;
	c.buf = s->buf + WIN_GUARD;
	for (y = r->y0; y < r->y1; y += h) {
		h = min_i(rows, r->y1 - y);
		c.sx = r->x0;
		c.sy = y;
		c.sw = w;
		c.sh = h;
		memset(c.buf, s->desktop & PIXEL_MASK, (size_t)(w * h));
		for (i = 0; i < s->nwin; i++)
			paint_window(&c, &s->wins[s->z[i]], s->z[i] == s->focus);
		if (s->be.rect == NULL ||
		    s->be.rect(s->be.self, r->x0, y, w, h, c.buf) < 0)
			return -1;
	}
	return 0;
}

int
win_flush(struct win_scene *s)
{
	struct win_rect pending[WIN_DAMAGE_MAX];
	int i, n;

	n = s->ndamage;
	memcpy(pending, s->damage, (size_t)n * sizeof(pending[0]));
	s->ndamage = 0;
	for (i = 0; i < n; i++) {
		if (repaint(s, &pending[i]) < 0) {
			memcpy(s->damage, &pending[i],
			    (size_t)(n - i) * sizeof(pending[0]));
			s->ndamage = n - i;
			return -1;
		}
	}
	return 0;
}

/*
 * Scene and windows. A call changes the scene first and records the damage
 * after, so a flush that the damage list forces in the middle of a call
 * already renders the new scene; every pixel the call changed is in the list
 * or is added before the call returns.
 */

int
win_scene_init(struct win_scene *s, const struct win_backend *be)
{
	if (s == NULL || be == NULL || be->rect == NULL)
		return -1;
	memset(s, 0, sizeof(*s));
	s->focus = -1;
	s->focus_follows_raise = 1;
	s->be = *be;
	s->desktop = WIN_C_DESKTOP;
	memset(s->buf, WIN_GUARD_BYTE, sizeof(s->buf));
	damage_add(s, 0, 0, WIN_PANEL_W, WIN_PANEL_H);
	return 0;
}

int
win_set_palette(struct win_scene *s, int index, int r, int g, int b)
{
	if (index < 0 || index >= WIN_NPAL || r < 0 || r > 255 || g < 0 ||
	    g > 255 || b < 0 || b > 255 || s->be.palette == NULL)
		return -1;
	return s->be.palette(s->be.self, index, r, g, b);
}

/* The slot of a window of this scene, or -1. Pointers compare by equality. */
static int
win_slot(const struct win_scene *s, const struct win *w)
{
	int i;

	for (i = 0; i < WIN_MAX; i++)
		if (&s->wins[i] == w)
			return s->wins[i].used ? i : -1;
	return -1;
}

static int
size_ok(int w, int h)
{
	return w >= WIN_MIN_W && w <= WIN_LIMIT && h >= WIN_MIN_H &&
	    h <= WIN_LIMIT;
}

static void
title_copy(struct win *w, const char *title)
{
	size_t i;

	for (i = 0; title != NULL && title[i] != '\0' && i < WIN_TITLE_MAX - 1;
	    i++)
		w->title[i] = title[i];
	w->title[i] = '\0';
}

struct win *
win_create(struct win_scene *s, int x, int y, int w, int h,
    const char *title, win_paint_fn paint, void *arg)
{
	struct win *win;
	int i;

	if (s->nwin >= WIN_MAX || !size_ok(w, h) || x != clampi(x) ||
	    y != clampi(y) || make_room(s, WIN_DAMAGE_CALL) < 0)
		return NULL;
	for (i = 0; s->wins[i].used; i++)
		;
	win = &s->wins[i];
	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	win->paint = paint;
	win->arg = arg;
	win->used = 1;
	win->hidden = 0;
	win->gen = ++s->next_gen;
	win->on_event = NULL;
	win->on_event_arg = NULL;
	title_copy(win, title);
	s->z[s->nwin++] = (unsigned char)i;
	damage_window(s, win);
	focus_set(s, i);
	return win;
}

int
win_destroy(struct win_scene *s, struct win *win)
{
	int i, j, p;

	j = win_slot(s, win);
	if (j < 0 || make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	for (i = 0; s->z[i] != j; i++)
		;
	p = i;
	for (; i + 1 < s->nwin; i++)
		s->z[i] = s->z[i + 1];
	s->nwin--;
	win->used = 0;
	damage_shown(s, win);
	if (s->focus == j) {
		s->focus = -1;
		focus_set(s, focus_pick(s, p - 1));
	}
	return 0;
}

int
win_move(struct win_scene *s, struct win *win, int x, int y)
{
	struct win_rect old;

	if (win_slot(s, win) < 0 || x != clampi(x) || y != clampi(y))
		return -1;
	if (x == win->x && y == win->y)
		return 0;
	if (win->hidden) {
		win->x = x;
		win->y = y;
		return 0;
	}
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	window_rect(win, &old);
	win->x = x;
	win->y = y;
	damage_rect(s, &old);
	damage_window(s, win);
	return 0;
}

int
win_resize(struct win_scene *s, struct win *win, int w, int h)
{
	struct win_rect old;

	if (win_slot(s, win) < 0 || !size_ok(w, h))
		return -1;
	if (w == win->w && h == win->h)
		return 0;
	if (win->hidden) {
		win->w = w;
		win->h = h;
		return 0;
	}
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	window_rect(win, &old);
	win->w = w;
	win->h = h;
	damage_rect(s, &old);
	damage_window(s, win);
	return 0;
}

int
win_set_geometry(struct win_scene *s, struct win *win, int x, int y, int w,
    int h)
{
	struct win_rect old;

	if (win_slot(s, win) < 0 || x != clampi(x) || y != clampi(y) ||
	    !size_ok(w, h))
		return -1;
	if (x == win->x && y == win->y && w == win->w && h == win->h)
		return 0;
	if (!win->hidden && make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	window_rect(win, &old);
	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	if (!win->hidden) {
		damage_rect(s, &old);
		damage_window(s, win);
	}
	return 0;
}

int
win_raise(struct win_scene *s, struct win *win)
{
	int i, j, follow;

	j = win_slot(s, win);
	if (j < 0)
		return -1;
	follow = s->focus_follows_raise && !win->hidden && s->focus != j;
	if (s->z[s->nwin - 1] == j && !follow)
		return 0;
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	if (s->z[s->nwin - 1] != j) {
		for (i = 0; s->z[i] != j; i++)
			;
		for (; i + 1 < s->nwin; i++)
			s->z[i] = s->z[i + 1];
		s->z[s->nwin - 1] = (unsigned char)j;
		damage_shown(s, win);
	}
	if (follow)
		focus_set(s, j);
	return 0;
}

/* Send a window to the bottom of the stack. */
int
win_lower(struct win_scene *s, struct win *win)
{
	int i, j;

	j = win_slot(s, win);
	if (j < 0)
		return -1;
	if (s->z[0] == j)
		return 0;
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	for (i = 0; s->z[i] != j; i++)
		;
	for (; i > 0; i--)
		s->z[i] = s->z[i - 1];
	s->z[0] = (unsigned char)j;
	damage_shown(s, win);
	if (s->focus_follows_raise && s->focus == j)
		focus_set(s, focus_pick(s, s->nwin - 1));
	return 0;
}

int
win_hide(struct win_scene *s, struct win *win)
{
	int i, j;

	j = win_slot(s, win);
	if (j < 0)
		return -1;
	if (win->hidden)
		return 0;
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	win->hidden = 1;
	damage_window(s, win);
	if (s->focus == j) {
		for (i = 0; s->z[i] != j; i++)
			;
		s->focus = -1;
		focus_set(s, focus_pick(s, i - 1));
	}
	return 0;
}

/* A shown window keeps its place in the stack and takes the focus if none has it. */
int
win_show(struct win_scene *s, struct win *win)
{
	int j;

	j = win_slot(s, win);
	if (j < 0)
		return -1;
	if (!win->hidden)
		return 0;
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	win->hidden = 0;
	damage_window(s, win);
	if (s->focus < 0)
		focus_set(s, j);
	return 0;
}

int
win_focus(struct win_scene *s, struct win *win)
{
	int j;

	if (win == NULL) {
		if (s->focus < 0)
			return 0;
		if (make_room(s, WIN_DAMAGE_CALL) < 0)
			return -1;
		focus_set(s, -1);
		return 0;
	}
	j = win_slot(s, win);
	if (j < 0 || win->hidden)
		return -1;
	if (s->focus == j)
		return 0;
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	focus_set(s, j);
	return 0;
}

struct win *
win_focused(struct win_scene *s)
{
	return s->focus < 0 ? NULL : &s->wins[s->focus];
}

int
win_is_live(const struct win_scene *s, const struct win *win)
{
	return win_slot(s, win) >= 0;
}

int
win_set_handler(struct win_scene *s, struct win *win, win_event_fn fn,
    void *arg)
{
	if (win_slot(s, win) < 0)
		return -1;
	win->on_event = fn;
	win->on_event_arg = arg;
	return 0;
}

int
win_set_title(struct win_scene *s, struct win *win, const char *title)
{
	int j = win_slot(s, win);

	if (j < 0)
		return -1;
	if (win->hidden) {
		title_copy(win, title);
		return 0;
	}
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	title_copy(win, title);
	damage_title(s, j);
	return 0;
}

int
win_invalidate(struct win_scene *s, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
		return 0;
	if (make_room(s, WIN_DAMAGE_CALL) < 0)
		return -1;
	x = clampi(x);
	y = clampi(y);
	damage_add(s, x, y, x + clampi(w), y + clampi(h));
	return 0;
}

int
win_invalidate_client(struct win_scene *s, struct win *win,
    int x, int y, int w, int h)
{
	if (win_slot(s, win) < 0)
		return -1;
	return win_invalidate(s, win->x + 1 + clampi(x),
	    win->y + WIN_TITLE_H + 1 + clampi(y), w, h);
}
