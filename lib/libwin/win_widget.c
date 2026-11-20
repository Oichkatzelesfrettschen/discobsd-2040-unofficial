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
 * libwin widgets: controls, form, key handling and dialog. See win_widget.h
 * for the model, the keys and the pixel geometry. Every operation builds the
 * list of rectangles it changes from the state it is about to store and the
 * state last drawn, reserves two damage slots per rectangle, runs the
 * activation hook (which may veto), then stores the state and invalidates.
 * The reservation is the only step that can fail at the backend, so a
 * failure leaves every control and the scene as they were.
 */
#include <string.h>

#include "win_widget.h"

#define RL_MAX		4
#define POP_PAD		12
#define TITLE_PAD	12

/* A list of client rectangles to invalidate. */
struct rect_list {
	int n;
	int x[RL_MAX], y[RL_MAX], w[RL_MAX], h[RL_MAX];
};

static int
max_i(int a, int b)
{
	return a > b ? a : b;
}

static int
min_i(int a, int b)
{
	return a < b ? a : b;
}

static void
rl_add(struct rect_list *l, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0 || l->n >= RL_MAX)
		return;
	l->x[l->n] = x;
	l->y[l->n] = y;
	l->w[l->n] = w;
	l->h[l->n] = h;
	l->n++;
}

static void
rl_widget(struct rect_list *l, const struct wgt *w)
{
	if ((w->flags & WGT_F_HIDDEN) == 0)
		rl_add(l, w->x, w->y, w->w, w->h);
}

/* Reserve the damage slots the list needs; the only step that can fail. */
static int
rl_reserve(const struct wgt_form *f, const struct rect_list *l)
{
	if (l->n == 0)
		return 0;
	return win_reserve(f->s, WIN_DAMAGE_CALL * l->n);
}

/* Invalidate the list; the slots were reserved. */
static void
rl_commit(const struct wgt_form *f, const struct rect_list *l)
{
	int i;

	if (f->win == NULL || !win_is_live(f->s, f->win))
		return;
	for (i = 0; i < l->n; i++)
		(void)win_invalidate_client(f->s, f->win, l->x[i], l->y[i],
		    l->w[i], l->h[i]);
}

/* Text with '&' marks */

static int
text_cells(const char *t)
{
	int n = 0;

	for (; *t != '\0'; t++)
		if (*t != '&')
			n++;
	return n;
}

static int
letter_ok(int c)
{
	return c >= 'a' && c <= 'z' && strchr("hijmw", c) == NULL;
}

/*
 * The mnemonic of a text: 0 when it has no '&', -1 when the '&' does not
 * precede a usable letter, else the lower case letter.
 */
static int
mnem_of(const char *t)
{
	int c;

	for (; *t != '\0'; t++) {
		if (*t != '&')
			continue;
		c = (unsigned char)t[1];
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		return letter_ok(c) ? c : -1;
	}
	return 0;
}

static int
text_set(char *dst, const char *src)
{
	size_t n;

	if (src == NULL)
		return -1;
	n = strlen(src);
	if (n >= WGT_TEXT_MAX)
		return -1;
	memcpy(dst, src, n + 1);
	return 0;
}

/* Clip */

struct clip_save {
	int x0, y0, x1, y1;
};

static void
clip_push(struct win_ctx *c, struct clip_save *sv, int x, int y, int w, int h)
{
	sv->x0 = c->cx0;
	sv->y0 = c->cy0;
	sv->x1 = c->cx1;
	sv->y1 = c->cy1;
	c->cx0 = max_i(c->cx0, c->ox + x);
	c->cy0 = max_i(c->cy0, c->oy + y);
	c->cx1 = min_i(c->cx1, c->ox + x + w);
	c->cy1 = min_i(c->cy1, c->oy + y + h);
}

static void
clip_pop(struct win_ctx *c, const struct clip_save *sv)
{
	c->cx0 = sv->x0;
	c->cy0 = sv->y0;
	c->cx1 = sv->x1;
	c->cy1 = sv->y1;
}

/* Drawing, relative to a control's top left pixel */

static void
draw_box(struct win_ctx *c, const struct wgt *w, int x, int y, int cw, int ch,
    int color)
{
	win_fill(c, w->x + x, w->y + y, cw, ch, color);
}

/* Text with the mnemonic underlined; '&' is never drawn. */
static void
draw_marked(struct win_ctx *c, int x, int y, const char *t, int color)
{
	char one[2];
	int cell = 0, first = 1, pend = 0;

	one[1] = '\0';
	for (; *t != '\0'; t++) {
		if (*t == '&') {
			if (first)
				pend = 1;
			first = 0;
			continue;
		}
		one[0] = *t;
		win_text(c, x + cell * WGT_CELL_W, y, one, color);
		if (pend)
			win_hline(c, x + cell * WGT_CELL_W, y + 9, WGT_CELL_W, color);
		pend = 0;
		cell++;
	}
}

static int
is_focused(const struct wgt_form *f, const struct wgt *w)
{
	return f->focus >= 0 && f->item[f->focus] == w;
}

static int
text_color(const struct wgt *w)
{
	return (w->flags & WGT_F_DISABLED) != 0 ? WGT_C_DIM : WGT_C_FG;
}

/* Frame in color, inside in another. */
static void
draw_frame(struct win_ctx *c, const struct wgt *w, int frame, int inside)
{
	draw_box(c, w, 0, 0, w->w, w->h, frame);
	draw_box(c, w, 1, 1, w->w - 2, w->h - 2, inside);
}

static void
paint_label(struct win_ctx *c, const struct wgt_label *l)
{
	draw_marked(c, l->head.x, l->head.y, l->text, text_color(&l->head));
}

static void
paint_status(struct win_ctx *c, const struct wgt_label *l)
{
	draw_box(c, &l->head, 0, 0, l->head.w, 1, WGT_C_DIM);
	draw_marked(c, l->head.x + 2, l->head.y + 2, l->text,
	    text_color(&l->head));
}

static void
paint_button(struct win_ctx *c, const struct wgt_form *f,
    const struct wgt_button *b)
{
	int foc = is_focused(f, &b->head);
	int tx = (b->head.w - WGT_CELL_W * text_cells(b->text)) / 2;

	draw_frame(c, &b->head, WGT_C_FG, foc ? WGT_C_HI : WGT_C_FACE);
	draw_marked(c, b->head.x + tx, b->head.y + 2, b->text,
	    foc ? WGT_C_HIFG : text_color(&b->head));
}

static void
paint_check(struct win_ctx *c, const struct wgt_form *f,
    const struct wgt_check *k)
{
	int foc = is_focused(f, &k->head);

	draw_box(c, &k->head, 0, 1, 9, 9, WGT_C_FG);
	draw_box(c, &k->head, 1, 2, 7, 7, WGT_C_FACE);
	if (k->checked)
		draw_box(c, &k->head, 2, 3, 5, 5, WGT_C_FG);
	if (foc)
		draw_box(c, &k->head, 12, 0,
		    WGT_CELL_W * text_cells(k->text) + 4, WGT_CHECK_H, WGT_C_HI);
	draw_marked(c, k->head.x + 14, k->head.y + 1, k->text,
	    foc ? WGT_C_HIFG : text_color(&k->head));
}

static void
paint_field(struct win_ctx *c, const struct wgt_form *f,
    const struct wgt_field *fl)
{
	const struct wgt *w = &fl->head;
	int foc = is_focused(f, w);
	int k, i, cur, color;
	char one[2];

	draw_frame(c, w, foc ? WGT_C_HI : WGT_C_FG,
	    (w->flags & WGT_F_DISABLED) != 0 ? WGT_C_BG : WGT_C_FACE);
	one[1] = '\0';
	for (k = 0; k < fl->vis; k++) {
		i = fl->scroll + k;
		cur = foc && i == fl->cur;
		color = cur ? WGT_C_HIFG : text_color(w);
		if (cur)
			draw_box(c, w, 2 + WGT_CELL_W * k, 2, WGT_CELL_W,
			    WGT_CELL_H, WGT_C_HI);
		if (i < fl->len) {
			one[0] = fl->buf[i];
			win_text(c, w->x + 2 + WGT_CELL_W * k, w->y + 2, one,
			    color);
		}
	}
}

/* Width of a list row: between the frame and the scroll bar. */
static int
list_row_w(const struct wgt *w)
{
	return w->w - WGT_SCROLL_W - 2;
}

/* Thumb of a list in track pixels: height and top. */
static void
list_thumb(const struct wgt_list *l, int *ty, int *th)
{
	int track = l->head.h - 2;

	if (l->n <= l->rows) {
		*ty = 0;
		*th = track;
		return;
	}
	*th = max_i(4, track * l->rows / l->n);
	*ty = (track - *th) * l->top / (l->n - l->rows);
}

static void
paint_list(struct win_ctx *c, const struct wgt_form *f, const struct wgt_list *l)
{
	const struct wgt *w = &l->head;
	struct clip_save sv;
	int foc = is_focused(f, w);
	int r, i, ty, th;

	draw_frame(c, w, WGT_C_FG, WGT_C_FACE);
	list_thumb(l, &ty, &th);
	draw_box(c, w, w->w - 1 - WGT_SCROLL_W, 1, WGT_SCROLL_W, w->h - 2,
	    WGT_C_DIM);
	draw_box(c, w, w->w - 1 - WGT_SCROLL_W, 1 + ty, WGT_SCROLL_W, th,
	    WGT_C_FACE);
	for (r = 0; r < l->rows; r++) {
		i = l->top + r;
		if (i >= l->n)
			break;
		clip_push(c, &sv, w->x + 1, w->y + 1 + WGT_LIST_ROW_H * r,
		    list_row_w(w), WGT_LIST_ROW_H);
		if (i == l->sel)
			draw_box(c, w, 1, 1 + WGT_LIST_ROW_H * r, list_row_w(w),
			    WGT_LIST_ROW_H, foc ? WGT_C_HI : WGT_C_DIM);
		win_text(c, w->x + 3, w->y + 1 + WGT_LIST_ROW_H * r,
		    l->items[i], i == l->sel ? WGT_C_HIFG : text_color(w));
		clip_pop(c, &sv);
	}
}

/* Menu layout */

static int
title_w(const struct wgt_menu *m)
{
	return WGT_CELL_W * text_cells(m->title) + TITLE_PAD;
}

static int
title_x(const struct wgt_menubar *b, int i)
{
	int k, x = 0;

	for (k = 0; k < i; k++)
		x += title_w(&b->menus[k]);
	return x;
}

static int
popup_cells(const struct wgt_menu *m)
{
	int j, n = 0;

	for (j = 0; j < m->n; j++)
		n = max_i(n, text_cells(m->entries[j].text));
	return n;
}

/* Pull-down of title i, in client pixels. */
static void
popup_rect(const struct wgt_menubar *b, int i, int *x, int *y, int *w, int *h)
{
	*x = b->head.x + title_x(b, i);
	*y = b->head.y + WGT_MENUBAR_H;
	*w = WGT_CELL_W * popup_cells(&b->menus[i]) + POP_PAD;
	*h = 2 + WGT_LIST_ROW_H * b->menus[i].n;
}

static void
paint_menubar(struct win_ctx *c, const struct wgt_form *f,
    const struct wgt_menubar *b)
{
	const struct wgt *w = &b->head;
	int foc = is_focused(f, w);
	int i, x, hi;

	draw_box(c, w, 0, 0, w->w, WGT_MENUBAR_H, WGT_C_FACE);
	draw_box(c, w, 0, WGT_MENUBAR_H - 1, w->w, 1, WGT_C_DIM);
	for (i = 0; i < b->n; i++) {
		x = title_x(b, i);
		hi = i == b->cur && (foc || b->open);
		if (hi)
			draw_box(c, w, x, 0, title_w(&b->menus[i]),
			    WGT_MENUBAR_H - 1, WGT_C_HI);
		draw_marked(c, w->x + x + 6, w->y + 1, b->menus[i].title,
		    hi ? WGT_C_HIFG : text_color(w));
	}
}

static void
paint_popup(struct win_ctx *c, const struct wgt_menubar *b)
{
	const struct wgt_menu *m = &b->menus[b->cur];
	struct clip_save sv;
	int x, y, w, h, j;

	popup_rect(b, b->cur, &x, &y, &w, &h);
	clip_push(c, &sv, x, y, w, h);
	win_fill(c, x, y, w, h, WGT_C_FG);
	win_fill(c, x + 1, y + 1, w - 2, h - 2, WGT_C_FACE);
	for (j = 0; j < m->n; j++) {
		if (j == b->entry)
			win_fill(c, x + 1, y + 1 + WGT_LIST_ROW_H * j, w - 2,
			    WGT_LIST_ROW_H, WGT_C_HI);
		draw_marked(c, x + 6, y + 1 + WGT_LIST_ROW_H * j,
		    m->entries[j].text, j == b->entry ? WGT_C_HIFG : WGT_C_FG);
	}
	clip_pop(c, &sv);
}

static void
paint_one(struct win_ctx *c, const struct wgt_form *f, const struct wgt *w)
{
	switch (w->kind) {
	case WGT_LABEL:
		paint_label(c, (const struct wgt_label *)w);
		break;
	case WGT_STATUS:
		paint_status(c, (const struct wgt_label *)w);
		break;
	case WGT_BUTTON:
		paint_button(c, f, (const struct wgt_button *)w);
		break;
	case WGT_CHECK:
		paint_check(c, f, (const struct wgt_check *)w);
		break;
	case WGT_FIELD:
		paint_field(c, f, (const struct wgt_field *)w);
		break;
	case WGT_LIST:
		paint_list(c, f, (const struct wgt_list *)w);
		break;
	default:
		paint_menubar(c, f, (const struct wgt_menubar *)w);
		break;
	}
}

void
wgt_form_paint(struct win_ctx *c, void *arg)
{
	const struct wgt_form *f = arg;
	const struct wgt *w;
	struct clip_save sv;
	int i;

	for (i = 0; i < f->n; i++) {
		w = f->item[i];
		if ((w->flags & WGT_F_HIDDEN) != 0)
			continue;
		clip_push(c, &sv, w->x, w->y, w->w, w->h);
		if (c->cx0 < c->cx1 && c->cy0 < c->cy1)
			paint_one(c, f, w);
		clip_pop(c, &sv);
	}
	/* An open pull-down is an overlay: repainted from state, saved nowhere. */
	for (i = 0; i < f->n; i++) {
		w = f->item[i];
		if (w->kind == WGT_MENUBAR && (w->flags & WGT_F_HIDDEN) == 0 &&
		    ((const struct wgt_menubar *)w)->open != 0)
			paint_popup(c, (const struct wgt_menubar *)w);
	}
}

/* Constructors */

static void
head_set(struct wgt *h, int kind, int id, int x, int y, int w, int ht)
{
	h->kind = (unsigned char)kind;
	h->flags = 0;
	h->id = (unsigned char)id;
	h->mnem = 0;
	h->x = (short)x;
	h->y = (short)y;
	h->w = (short)w;
	h->h = (short)ht;
}

static int
geom_ok(int x, int y, int w)
{
	return x >= 0 && y >= 0 && x <= WIN_LIMIT && y <= WIN_LIMIT && w >= 1 &&
	    w <= WIN_LIMIT;
}

static int
label_init(struct wgt_label *w, int kind, int x, int y, int cw, int h,
    const char *text)
{
	int m;

	if (!geom_ok(x, y, cw) || text_set(w->text, text) < 0)
		return -1;
	/* Only a label carries a mnemonic; a status line shows its text. */
	m = kind == WGT_LABEL ? mnem_of(text) : 0;
	if (m < 0)
		return -1;
	head_set(&w->head, kind, 0, x, y, cw, h);
	w->head.mnem = (char)m;
	return 0;
}

int
wgt_label_init(struct wgt_label *w, int x, int y, int cw, const char *text)
{
	return label_init(w, WGT_LABEL, x, y, cw, WGT_LABEL_H, text);
}

int
wgt_status_init(struct wgt_label *w, int x, int y, int cw, const char *text)
{
	return label_init(w, WGT_STATUS, x, y, cw, WGT_STATUS_H, text);
}

int
wgt_button_init(struct wgt_button *w, int id, int x, int y, int cw,
    const char *text)
{
	int m;

	if (!geom_ok(x, y, cw) || cw < 4 || text_set(w->text, text) < 0 ||
	    (m = mnem_of(text)) < 0)
		return -1;
	head_set(&w->head, WGT_BUTTON, id, x, y, cw, WGT_BUTTON_H);
	w->head.mnem = (char)m;
	return 0;
}

int
wgt_check_init(struct wgt_check *w, int id, int x, int y, int cw,
    const char *text, int checked)
{
	int m;

	if (!geom_ok(x, y, cw) || cw < 14 || text_set(w->text, text) < 0 ||
	    (m = mnem_of(text)) < 0)
		return -1;
	head_set(&w->head, WGT_CHECK, id, x, y, cw, WGT_CHECK_H);
	w->head.mnem = (char)m;
	w->checked = checked != 0;
	return 0;
}

/* The scroll that keeps the cursor in view with the least movement. */
static unsigned char
field_scroll(const struct wgt_field *w, int cur, int scroll)
{
	if (cur < scroll)
		scroll = cur;
	if (cur >= scroll + w->vis)
		scroll = cur - w->vis + 1;
	return (unsigned char)scroll;
}

static int
text_printable(const char *t, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if ((unsigned char)t[i] < 0x20 || (unsigned char)t[i] > 0x7e)
			return 0;
	return 1;
}

int
wgt_field_init(struct wgt_field *w, int id, int x, int y, int cw, char *buf,
    int cap)
{
	size_t n;
	int vis;

	vis = (cw - 4) / WGT_CELL_W;
	if (!geom_ok(x, y, cw) || buf == NULL || cap < 2 || cap > 255 ||
	    vis < 1 || vis > WGT_FIELD_VIS)
		return -1;
	for (n = 0; n < (size_t)cap && buf[n] != '\0'; n++)
		continue;
	if (n >= (size_t)cap || !text_printable(buf, n))
		return -1;
	head_set(&w->head, WGT_FIELD, id, x, y, cw, WGT_FIELD_H);
	w->buf = buf;
	w->cap = (unsigned char)cap;
	w->len = (unsigned char)n;
	w->cur = (unsigned char)n;
	w->vis = (unsigned char)vis;
	w->scroll = 0;
	w->scroll = field_scroll(w, w->cur, 0);
	return 0;
}

int
wgt_list_init(struct wgt_list *w, int id, int x, int y, int cw, int rows,
    const char *const *items, int n)
{
	if (!geom_ok(x, y, cw) || cw < 2 + WGT_SCROLL_W + 1 + 6 || rows < 1 ||
	    rows > 20 || n < 0 || n > 255 || (n > 0 && items == NULL))
		return -1;
	head_set(&w->head, WGT_LIST, id, x, y, cw,
	    2 + WGT_LIST_ROW_H * rows);
	w->items = items;
	w->n = (unsigned char)n;
	w->sel = 0;
	w->top = 0;
	w->rows = (unsigned char)rows;
	return 0;
}

int
wgt_menubar_init(struct wgt_menubar *w, int id, int x, int y, int cw,
    const struct wgt_menu *menus, int n)
{
	int i, j, k, a, b;

	if (!geom_ok(x, y, cw) || menus == NULL || n < 1 || n > 8)
		return -1;
	for (i = 0; i < n; i++) {
		if (menus[i].title == NULL || menus[i].entries == NULL ||
		    menus[i].n < 1 || (a = mnem_of(menus[i].title)) < 0)
			return -1;
		for (k = 0; k < i; k++)
			if (a != 0 && mnem_of(menus[k].title) == a)
				return -1;
		for (j = 0; j < menus[i].n; j++) {
			if (menus[i].entries[j].text == NULL ||
			    (a = mnem_of(menus[i].entries[j].text)) < 0)
				return -1;
			for (k = 0; k < j; k++) {
				b = mnem_of(menus[i].entries[k].text);
				if (a != 0 && a == b)
					return -1;
			}
		}
	}
	head_set(&w->head, WGT_MENUBAR, id, x, y, cw, WGT_MENUBAR_H);
	w->menus = menus;
	w->n = (unsigned char)n;
	w->cur = 0;
	w->entry = 0;
	w->open = 0;
	return 0;
}

/* Form */

int
wgt_is_focusable(const struct wgt *w)
{
	return w->kind >= WGT_BUTTON && (w->flags & (WGT_F_HIDDEN | WGT_F_DISABLED)) == 0;
}

struct wgt *
wgt_focused(const struct wgt_form *f)
{
	return f->focus >= 0 ? f->item[f->focus] : NULL;
}

static void
form_reset(struct wgt_form *f, struct win_scene *s, struct win *win, int cw,
    int ch, wgt_act_fn act, void *arg)
{
	memset(f, 0, sizeof(*f));
	f->s = s;
	f->win = win;
	f->cw = (short)cw;
	f->ch = (short)ch;
	f->focus = -1;
	f->act = act;
	f->arg = arg;
}

int
wgt_form_init(struct wgt_form *f, struct win_scene *s, struct win *win,
    wgt_act_fn act, void *arg)
{
	if (f == NULL || s == NULL || !win_is_live(s, win))
		return -1;
	form_reset(f, s, win, win->w - 2, win->h - WIN_TITLE_H - 2, act, arg);
	return win_set_handler(s, win, wgt_form_event, f);
}

static int
index_of(const struct wgt_form *f, const struct wgt *w)
{
	int i;

	for (i = 0; i < f->n; i++)
		if (f->item[i] == w)
			return i;
	return -1;
}

/* Whether another control already holds the mnemonic letter. */
static int
mnem_taken(const struct wgt_form *f, const struct wgt *self, int letter)
{
	const struct wgt_menubar *b;
	int i, k;

	if (letter == 0)
		return 0;
	for (i = 0; i < f->n; i++) {
		if (f->item[i] == self)
			continue;
		if (f->item[i]->mnem == letter)
			return 1;
		if (f->item[i]->kind == WGT_MENUBAR) {
			b = (const struct wgt_menubar *)f->item[i];
			for (k = 0; k < b->n; k++)
				if (mnem_of(b->menus[k].title) == letter)
					return 1;
		}
	}
	return 0;
}

static int
bar_fits(const struct wgt_form *f, const struct wgt_menubar *b)
{
	int i, x, y, w, h;

	for (i = 0; i < b->n; i++) {
		popup_rect(b, i, &x, &y, &w, &h);
		if (x + w > f->cw || y + h > f->ch)
			return 0;
	}
	return 1;
}

int
wgt_add(struct wgt_form *f, struct wgt *w)
{
	const struct wgt_menubar *b;
	int i, k;

	if (f == NULL || w == NULL || f->n >= WGT_MAX || index_of(f, w) >= 0 ||
	    w->kind < WGT_LABEL || w->kind > WGT_MENUBAR || w->x < 0 ||
	    w->y < 0 || w->x + w->w > f->cw || w->y + w->h > f->ch)
		return -1;
	if (mnem_taken(f, w, w->mnem))
		return -1;
	if (w->kind == WGT_MENUBAR) {
		b = (const struct wgt_menubar *)w;
		for (i = 0; i < f->n; i++)
			if (f->item[i]->kind == WGT_MENUBAR)
				return -1;
		if (!bar_fits(f, b))
			return -1;
		for (k = 0; k < b->n; k++)
			if (mnem_taken(f, w, mnem_of(b->menus[k].title)))
				return -1;
	}
	f->item[f->n++] = w;
	if (f->focus < 0 && wgt_is_focusable(w))
		f->focus = (signed char)(f->n - 1);
	return 0;
}

int
wgt_form_refresh(struct wgt_form *f)
{
	struct rect_list l;

	l.n = 0;
	rl_add(&l, 0, 0, f->cw, f->ch);
	if (rl_reserve(f, &l) < 0)
		return -1;
	rl_commit(f, &l);
	return 0;
}

/* Focus */

/* Index of the next focusable control from from in a direction, or -1. */
static int
next_focusable(const struct wgt_form *f, int from, int dir)
{
	int step, i;

	if (f->n == 0)
		return -1;
	for (step = 1; step <= f->n; step++) {
		i = from < 0 ? (dir > 0 ? step - 1 : f->n - step) :
		    ((from + dir * step) % f->n + f->n) % f->n;
		if (wgt_is_focusable(f->item[i]))
			return i;
	}
	return -1;
}

static const struct wgt_menubar *
open_bar(const struct wgt_form *f)
{
	const struct wgt *w = wgt_focused(f);

	if (w != NULL && w->kind == WGT_MENUBAR &&
	    ((const struct wgt_menubar *)w)->open != 0)
		return (const struct wgt_menubar *)w;
	return NULL;
}

/* Rectangles that moving the focus to index to repaints. */
static void
focus_plan(const struct wgt_form *f, int to, struct rect_list *l)
{
	const struct wgt_menubar *b;
	int x, y, w, h;

	if (to == f->focus)
		return;
	if (f->focus >= 0) {
		rl_widget(l, f->item[f->focus]);
		b = open_bar(f);
		if (b != NULL) {
			popup_rect(b, b->cur, &x, &y, &w, &h);
			rl_add(l, x, y, w, h);
		}
	}
	if (to >= 0)
		rl_widget(l, f->item[to]);
}

static void
focus_apply(struct wgt_form *f, int to)
{
	struct wgt_menubar *b = (struct wgt_menubar *)open_bar(f);

	if (to == f->focus)
		return;
	if (b != NULL)
		b->open = 0;
	f->focus = (signed char)to;
}

int
wgt_focus(struct wgt_form *f, struct wgt *w)
{
	struct rect_list l;
	int i = index_of(f, w);

	if (i < 0 || !wgt_is_focusable(w))
		return -1;
	if (i == f->focus)
		return 0;
	l.n = 0;
	focus_plan(f, i, &l);
	if (rl_reserve(f, &l) < 0)
		return -1;
	focus_apply(f, i);
	rl_commit(f, &l);
	return 0;
}

int
wgt_traverse(struct wgt_form *f, int forward)
{
	struct rect_list l;
	int i = next_focusable(f, f->focus, forward ? 1 : -1);

	if (i < 0 || i == f->focus)
		return 0;
	l.n = 0;
	focus_plan(f, i, &l);
	if (rl_reserve(f, &l) < 0)
		return -1;
	focus_apply(f, i);
	rl_commit(f, &l);
	return 0;
}

/* Visibility and enabling */

static int
set_flag(struct wgt_form *f, struct wgt *w, int flag, int set)
{
	struct rect_list l;
	struct wgt_menubar *b = NULL;
	int x, y, pw, ph, i = index_of(f, w), to = -1, lose;
	unsigned char nf, was;

	if (i < 0)
		return -1;
	was = w->flags;
	nf = (unsigned char)(set ? (was | flag) : (was & ~flag));
	if (nf == was)
		return 0;
	lose = i == f->focus && (nf & (WGT_F_HIDDEN | WGT_F_DISABLED)) != 0;
	l.n = 0;
	/* The control's rectangle repaints, or erases what it showed. */
	rl_add(&l, w->x, w->y, w->w, w->h);
	if (w->kind == WGT_MENUBAR && ((struct wgt_menubar *)w)->open != 0) {
		b = (struct wgt_menubar *)w;
		popup_rect(b, b->cur, &x, &y, &pw, &ph);
		rl_add(&l, x, y, pw, ph);
	}
	if (lose) {
		w->flags = nf;
		to = next_focusable(f, i, 1);
		w->flags = was;
		if (to >= 0)
			rl_widget(&l, f->item[to]);
	}
	if (rl_reserve(f, &l) < 0)
		return -1;
	w->flags = nf;
	if (b != NULL && (nf & (WGT_F_HIDDEN | WGT_F_DISABLED)) != 0)
		b->open = 0;
	if (lose)
		f->focus = (signed char)to;
	rl_commit(f, &l);
	return 0;
}

int
wgt_set_enabled(struct wgt_form *f, struct wgt *w, int on)
{
	return set_flag(f, w, WGT_F_DISABLED, !on);
}

int
wgt_set_shown(struct wgt_form *f, struct wgt *w, int on)
{
	return set_flag(f, w, WGT_F_HIDDEN, !on);
}

/* Text of label, status, button and check box */

static char *
text_of(struct wgt *w)
{
	switch (w->kind) {
	case WGT_LABEL:
	case WGT_STATUS:
		return ((struct wgt_label *)w)->text;
	case WGT_BUTTON:
		return ((struct wgt_button *)w)->text;
	case WGT_CHECK:
		return ((struct wgt_check *)w)->text;
	default:
		return NULL;
	}
}

/* The cells a marked text shows, and which of them is underlined (or -1). */
static int
disp_cells(const char *t, char *out, int *ul)
{
	int n = 0, first = 1, pend = 0;

	*ul = -1;
	for (; *t != '\0'; t++) {
		if (*t == '&') {
			pend = first;
			first = 0;
			continue;
		}
		if (pend) {
			*ul = n;
			pend = 0;
		}
		out[n++] = *t;
	}
	return n;
}

int
wgt_set_text(struct wgt_form *f, struct wgt *w, const char *text)
{
	struct rect_list l;
	char *old = text_of(w);
	char co[WGT_TEXT_MAX], cn[WGT_TEXT_MAX];
	int m, k, lo = -1, hi = -1, ox, oy, no, nn, uo, un, top;
	size_t n;

	if (old == NULL || index_of(f, w) < 0 || text == NULL)
		return -1;
	n = strlen(text);
	m = w->kind == WGT_STATUS ? 0 : mnem_of(text);
	if (n >= WGT_TEXT_MAX || m < 0 || mnem_taken(f, w, m))
		return -1;
	if (strcmp(old, text) == 0)
		return 0;
	no = disp_cells(old, co, &uo);
	nn = disp_cells(text, cn, &un);
	top = no > nn ? no : nn;
	for (k = 0; k < top; k++) {
		if ((k < no ? co[k] : 0) == (k < nn ? cn[k] : 0) &&
		    (k == uo) == (k == un))
			continue;
		if (lo < 0)
			lo = k;
		hi = k;
	}
	l.n = 0;
	switch (w->kind) {
	case WGT_STATUS:
		ox = 2;
		oy = 2;
		break;
	case WGT_CHECK:
		ox = 14;
		oy = 1;
		break;
	case WGT_BUTTON:
		ox = (w->w - WGT_CELL_W * no) / 2;
		oy = 2;
		break;
	default:
		ox = 0;
		oy = 0;
		break;
	}
	if (lo < 0)
		;	/* Only the marks moved off the shown cells. */
	else if (w->kind == WGT_BUTTON && no != nn)
		rl_add(&l, w->x + 1, w->y + 1, w->w - 2, w->h - 2);
	else if (w->kind == WGT_CHECK && no != nn && is_focused(f, w))
		rl_add(&l, w->x + 12, w->y, WGT_CELL_W * top + 4, w->h);
	else
		rl_add(&l, w->x + ox + WGT_CELL_W * lo, w->y + oy,
		    WGT_CELL_W * (hi - lo + 1), WGT_CELL_H);
	if (rl_reserve(f, &l) < 0)
		return -1;
	memcpy(old, text, n + 1);
	w->mnem = (char)m;
	if ((w->flags & WGT_F_HIDDEN) == 0)
		rl_commit(f, &l);
	return 0;
}

/* Check box */

static void
check_box_rect(struct rect_list *l, const struct wgt_check *k)
{
	rl_add(l, k->head.x, k->head.y + 1, 9, 9);
}

int
wgt_check_set(struct wgt_form *f, struct wgt_check *w, int on)
{
	struct rect_list l;

	if (index_of(f, &w->head) < 0 || w->head.kind != WGT_CHECK)
		return -1;
	on = on != 0;
	if (w->checked == on)
		return 0;
	l.n = 0;
	check_box_rect(&l, w);
	if (rl_reserve(f, &l) < 0)
		return -1;
	w->checked = (unsigned char)on;
	if ((w->head.flags & WGT_F_HIDDEN) == 0)
		rl_commit(f, &l);
	return 0;
}

/* Text field */

/* What each visible cell shows: the character, plus 0x80 on the cursor cell. */
static void
field_cells(const struct wgt_form *f, const struct wgt_field *w,
    unsigned char *out)
{
	int k, i, foc = is_focused(f, &w->head);

	for (k = 0; k < w->vis; k++) {
		i = w->scroll + k;
		out[k] = i < w->len ? (unsigned char)w->buf[i] : ' ';
		if (foc && i == w->cur)
			out[k] |= 0x80;
	}
}

/* Invalidate the one run of cells that differs from the snapshot. */
static void
field_commit(const struct wgt_form *f, const struct wgt_field *w,
    const unsigned char *old)
{
	unsigned char now[WGT_FIELD_VIS];
	struct rect_list l;
	int k, lo = -1, hi = -1;

	field_cells(f, w, now);
	for (k = 0; k < w->vis; k++) {
		if (now[k] == old[k])
			continue;
		if (lo < 0)
			lo = k;
		hi = k;
	}
	if (lo < 0 || (w->head.flags & WGT_F_HIDDEN) != 0)
		return;
	l.n = 0;
	rl_add(&l, w->head.x + 2 + WGT_CELL_W * lo, w->head.y + 2,
	    WGT_CELL_W * (hi - lo + 1), WGT_CELL_H);
	rl_commit(f, &l);
}

static int
field_ok(const struct wgt_form *f, const struct wgt_field *w)
{
	return index_of(f, &w->head) >= 0 && w->head.kind == WGT_FIELD;
}

/* One run of cells is at most one rectangle. */
static int
field_reserve(const struct wgt_form *f)
{
	return win_reserve(f->s, WIN_DAMAGE_CALL);
}

int
wgt_field_set_text(struct wgt_form *f, struct wgt_field *w, const char *text)
{
	unsigned char old[WGT_FIELD_VIS];
	size_t n;

	if (!field_ok(f, w) || text == NULL)
		return -1;
	n = strlen(text);
	if (n > (size_t)w->cap - 1 || !text_printable(text, n))
		return -1;
	if (n == w->len && memcmp(w->buf, text, n) == 0 && w->cur == w->len)
		return 0;
	if (field_reserve(f) < 0)
		return -1;
	field_cells(f, w, old);
	memcpy(w->buf, text, n + 1);
	w->len = (unsigned char)n;
	w->cur = (unsigned char)n;
	w->scroll = field_scroll(w, w->cur, w->scroll);
	field_commit(f, w, old);
	return 0;
}

int
wgt_field_insert(struct wgt_form *f, struct wgt_field *w, int ch)
{
	unsigned char old[WGT_FIELD_VIS];

	if (!field_ok(f, w) || ch < 0x20 || ch > 0x7e || w->len >= w->cap - 1)
		return -1;
	if (field_reserve(f) < 0)
		return -1;
	field_cells(f, w, old);
	memmove(w->buf + w->cur + 1, w->buf + w->cur,
	    (size_t)(w->len - w->cur) + 1);
	w->buf[w->cur] = (char)ch;
	w->len++;
	w->cur++;
	w->scroll = field_scroll(w, w->cur, w->scroll);
	field_commit(f, w, old);
	return 0;
}

int
wgt_field_delete(struct wgt_form *f, struct wgt_field *w, int forward)
{
	unsigned char old[WGT_FIELD_VIS];
	int at;

	if (!field_ok(f, w))
		return -1;
	if (forward ? w->cur >= w->len : w->cur == 0)
		return 0;
	if (field_reserve(f) < 0)
		return -1;
	field_cells(f, w, old);
	at = forward ? w->cur : w->cur - 1;
	memmove(w->buf + at, w->buf + at + 1, (size_t)(w->len - at));
	w->len--;
	w->cur = (unsigned char)at;
	w->scroll = field_scroll(w, w->cur, w->scroll);
	field_commit(f, w, old);
	return 0;
}

int
wgt_field_move(struct wgt_form *f, struct wgt_field *w, int pos)
{
	unsigned char old[WGT_FIELD_VIS];

	if (!field_ok(f, w))
		return -1;
	pos = pos < 0 ? 0 : pos > w->len ? w->len : pos;
	if (pos == w->cur)
		return 0;
	if (field_reserve(f) < 0)
		return -1;
	field_cells(f, w, old);
	w->cur = (unsigned char)pos;
	w->scroll = field_scroll(w, w->cur, w->scroll);
	field_commit(f, w, old);
	return 0;
}

int
wgt_field_cursor(const struct wgt_field *w)
{
	return w->cur;
}

/* List */

static void
list_row_rect(struct rect_list *l, const struct wgt_list *w, int index)
{
	int r = index - w->top;

	if (r < 0 || r >= w->rows)
		return;
	rl_add(l, w->head.x + 1, w->head.y + 1 + WGT_LIST_ROW_H * r,
	    list_row_w(&w->head), WGT_LIST_ROW_H);
}

int
wgt_list_selected(const struct wgt_list *w)
{
	return w->n == 0 ? -1 : w->sel;
}

int
wgt_list_select(struct wgt_form *f, struct wgt_list *w, int index)
{
	struct rect_list l;
	int top;

	if (index_of(f, &w->head) < 0 || w->head.kind != WGT_LIST || index < 0 ||
	    index >= w->n)
		return -1;
	top = w->top;
	if (index < top)
		top = index;
	if (index >= top + w->rows)
		top = index - w->rows + 1;
	if (index == w->sel && top == w->top)
		return 0;
	l.n = 0;
	if (top != w->top) {
		rl_add(&l, w->head.x + 1, w->head.y + 1, w->head.w - 2,
		    w->head.h - 2);
	} else {
		list_row_rect(&l, w, w->sel);
		list_row_rect(&l, w, index);
	}
	if (rl_reserve(f, &l) < 0)
		return -1;
	w->sel = (unsigned char)index;
	w->top = (unsigned char)top;
	if ((w->head.flags & WGT_F_HIDDEN) == 0)
		rl_commit(f, &l);
	return 0;
}

int
wgt_list_set(struct wgt_form *f, struct wgt_list *w, const char *const *items,
    int n)
{
	struct rect_list l;

	if (index_of(f, &w->head) < 0 || w->head.kind != WGT_LIST || n < 0 ||
	    n > 255 || (n > 0 && items == NULL))
		return -1;
	if (items == w->items && n == w->n && w->sel == 0 && w->top == 0)
		return 0;
	l.n = 0;
	rl_widget(&l, &w->head);
	if (rl_reserve(f, &l) < 0)
		return -1;
	w->items = items;
	w->n = (unsigned char)n;
	w->sel = 0;
	w->top = 0;
	rl_commit(f, &l);
	return 0;
}

/* Menu bar */

/* A title's own rectangle in the bar. */
static void
title_rect(struct rect_list *l, const struct wgt_menubar *b, int i)
{
	rl_add(l, b->head.x + title_x(b, i), b->head.y, title_w(&b->menus[i]),
	    WGT_MENUBAR_H - 1);
}

static void
popup_row(struct rect_list *l, const struct wgt_menubar *b, int i, int j)
{
	int x, y, w, h;

	popup_rect(b, i, &x, &y, &w, &h);
	rl_add(l, x + 1, y + 1 + WGT_LIST_ROW_H * j, w - 2, WGT_LIST_ROW_H);
}

/*
 * Rectangles that a change of title, open state and entry repaints; titles
 * of the bar are left out when the bar repaints whole anyway.
 */
static void
menu_plan(const struct wgt_menubar *b, int ncur, int nopen, int nentry,
    int titles, struct rect_list *l)
{
	int x, y, w, h;

	if (b->open && (!nopen || ncur != b->cur)) {
		popup_rect(b, b->cur, &x, &y, &w, &h);
		rl_add(l, x, y, w, h);
	} else if (b->open && nentry != b->entry) {
		popup_row(l, b, b->cur, b->entry);
		popup_row(l, b, b->cur, nentry);
	}
	if (nopen && (!b->open || ncur != b->cur)) {
		popup_rect(b, ncur, &x, &y, &w, &h);
		rl_add(l, x, y, w, h);
	}
	if (titles && (b->open != nopen || ncur != b->cur)) {
		title_rect(l, b, b->cur);
		if (ncur != b->cur)
			title_rect(l, b, ncur);
	}
}

static void
menu_apply(struct wgt_menubar *b, int ncur, int nopen, int nentry)
{
	b->cur = (unsigned char)ncur;
	b->open = (unsigned char)nopen;
	b->entry = (unsigned char)nentry;
}

static int
menu_set(struct wgt_form *f, struct wgt_menubar *b, int ncur, int nopen,
    int nentry)
{
	struct rect_list l;

	l.n = 0;
	menu_plan(b, ncur, nopen, nentry, 1, &l);
	if (b->cur == ncur && b->open == nopen && b->entry == nentry)
		return 0;
	if (rl_reserve(f, &l) < 0)
		return -1;
	menu_apply(b, ncur, nopen, nentry);
	if ((b->head.flags & WGT_F_HIDDEN) == 0)
		rl_commit(f, &l);
	return 0;
}

int
wgt_menu_close(struct wgt_form *f, struct wgt_menubar *w)
{
	if (index_of(f, &w->head) < 0 || w->head.kind != WGT_MENUBAR)
		return -1;
	return menu_set(f, w, w->cur, 0, 0);
}

/* Focus the bar when it is not focused and open title t. */
static int
menu_open_at(struct wgt_form *f, struct wgt_menubar *w, int t)
{
	struct rect_list l;
	int i = index_of(f, &w->head);

	l.n = 0;
	focus_plan(f, i, &l);
	menu_plan(w, t, 1, 0, i == f->focus, &l);
	if (i != f->focus || w->cur != t || !w->open || w->entry != 0) {
		if (rl_reserve(f, &l) < 0)
			return -1;
		focus_apply(f, i);
		menu_apply(w, t, 1, 0);
		rl_commit(f, &l);
	}
	return 0;
}

int
wgt_menu_open(struct wgt_form *f, struct wgt_menubar *w, int title)
{
	if (index_of(f, &w->head) < 0 || w->head.kind != WGT_MENUBAR ||
	    !wgt_is_focusable(&w->head) || title < 0 || title >= w->n)
		return -1;
	return menu_open_at(f, w, title);
}

/* Run entry j of the open menu: veto, close, report. */
static int
menu_run(struct wgt_form *f, struct wgt_menubar *b, int j)
{
	struct rect_list l;
	int id = b->menus[b->cur].entries[j].id;

	l.n = 0;
	menu_plan(b, b->cur, 0, 0, 1, &l);
	if (rl_reserve(f, &l) < 0)
		return -1;
	if (f->act != NULL && f->act(f, &b->head, id, 0) < 0)
		return -1;
	menu_apply(b, b->cur, 0, 0);
	if ((b->head.flags & WGT_F_HIDDEN) == 0)
		rl_commit(f, &l);
	return 1;
}


static int
lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}

/* Keys of a focused menu bar; 1 consumed, 0 not, -1 failed. */
static int
menu_key(struct wgt_form *f, struct wgt_menubar *b, const struct win_event *ev)
{
	const struct wgt_menu *m = &b->menus[b->cur];
	int k = ev->key, j, r;
	int press = ev->kind == WIN_EV_PRESS;

	if (!b->open) {
		if (k == WIN_KEY_LEFT || k == WIN_KEY_RIGHT)
			r = menu_set(f, b, (b->cur + (k == WIN_KEY_RIGHT ? 1 : b->n - 1)) %
			    b->n, 0, 0);
		else if (k == WIN_KEY_DOWN || k == WIN_KEY_ENTER)
			r = press || k == WIN_KEY_DOWN ?
			    menu_set(f, b, b->cur, 1, 0) : 0;
		else
			return 0;
		return r < 0 ? -1 : 1;
	}
	if (k == WIN_KEY_ESC)
		r = press ? menu_set(f, b, b->cur, 0, 0) : 0;
	else if (k == WIN_KEY_LEFT || k == WIN_KEY_RIGHT)
		r = menu_set(f, b, (b->cur + (k == WIN_KEY_RIGHT ? 1 : b->n - 1)) %
		    b->n, 1, 0);
	else if (k == WIN_KEY_UP || k == WIN_KEY_DOWN)
		r = menu_set(f, b, b->cur, 1,
		    k == WIN_KEY_UP ? (b->entry > 0 ? b->entry - 1 : 0) :
		    (b->entry + 1 < m->n ? b->entry + 1 : b->entry));
	else if (k == WIN_KEY_HOME || k == WIN_KEY_END)
		r = menu_set(f, b, b->cur, 1, k == WIN_KEY_HOME ? 0 : m->n - 1);
	else if (k == WIN_KEY_ENTER)
		r = press ? menu_run(f, b, b->entry) : 0;
	else {
		r = 0;
		if (press && k >= 0x20 && k <= 0x7e)
			for (j = 0; j < m->n; j++)
				if (mnem_of(m->entries[j].text) == lower(k)) {
					r = menu_run(f, b, j);
					break;
				}
	}
	/* An open pull-down takes every key. */
	return r < 0 ? -1 : 1;
}

/* A control that takes the focus on a mnemonic: focus and perhaps act. */
static int
mnem_focus(struct wgt_form *f, int i)
{
	struct rect_list l;

	l.n = 0;
	focus_plan(f, i, &l);
	if (rl_reserve(f, &l) < 0)
		return -1;
	focus_apply(f, i);
	rl_commit(f, &l);
	return 1;
}

static int
mnem_button(struct wgt_form *f, int i)
{
	struct rect_list l;
	struct wgt *w = f->item[i];

	l.n = 0;
	focus_plan(f, i, &l);
	if (rl_reserve(f, &l) < 0)
		return -1;
	if (f->act != NULL && f->act(f, w, w->id, 0) < 0)
		return -1;
	focus_apply(f, i);
	rl_commit(f, &l);
	return 1;
}

/* Toggle a check box: veto, then store; plan holds what else repaints. */
static int
check_toggle(struct wgt_form *f, struct wgt_check *k, int focus_to)
{
	struct rect_list l;
	int on = !k->checked;

	l.n = 0;
	if (focus_to >= 0)
		focus_plan(f, focus_to, &l);
	check_box_rect(&l, k);
	if (rl_reserve(f, &l) < 0)
		return -1;
	if (f->act != NULL && f->act(f, &k->head, k->head.id, on) < 0)
		return -1;
	if (focus_to >= 0)
		focus_apply(f, focus_to);
	k->checked = (unsigned char)on;
	rl_commit(f, &l);
	return 1;
}

static int
key_mnemonic(struct wgt_form *f, int letter)
{
	struct wgt *w;
	struct wgt_menubar *b;
	int i, k, t;

	for (i = 0; i < f->n; i++) {
		w = f->item[i];
		if ((w->flags & (WGT_F_HIDDEN | WGT_F_DISABLED)) != 0)
			continue;
		if (w->kind == WGT_MENUBAR) {
			b = (struct wgt_menubar *)w;
			for (k = 0; k < b->n; k++)
				if (mnem_of(b->menus[k].title) == letter)
					return menu_open_at(f, b, k) < 0 ? -1 : 1;
			continue;
		}
		if (w->mnem != letter)
			continue;
		switch (w->kind) {
		case WGT_LABEL:
			t = next_focusable(f, i, 1);
			return t < 0 ? 1 : mnem_focus(f, t);
		case WGT_BUTTON:
			return mnem_button(f, i);
		case WGT_CHECK:
			return check_toggle(f, (struct wgt_check *)w, i);
		default:
			return mnem_focus(f, i);
		}
	}
	return 0;
}

static int
field_key(struct wgt_form *f, struct wgt_field *w, const struct win_event *ev)
{
	int k = ev->key, r;

	if (k == WIN_KEY_ENTER) {
		r = ev->kind == WIN_EV_PRESS && f->act != NULL ?
		    f->act(f, &w->head, w->head.id, 0) : 0;
		return r < 0 ? -1 : r > 0 ? 0 : 1;
	}
	if (k >= 0x20 && k <= 0x7e)
		r = w->len < w->cap - 1 ? wgt_field_insert(f, w, k) : 0;
	else if (k == WIN_KEY_BACKSPACE)
		r = wgt_field_delete(f, w, 0);
	else if (k == WIN_KEY_DELETE)
		r = wgt_field_delete(f, w, 1);
	else if (k == WIN_KEY_LEFT)
		r = wgt_field_move(f, w, w->cur - 1);
	else if (k == WIN_KEY_RIGHT)
		r = wgt_field_move(f, w, w->cur + 1);
	else if (k == WIN_KEY_HOME)
		r = wgt_field_move(f, w, 0);
	else if (k == WIN_KEY_END)
		r = wgt_field_move(f, w, w->len);
	else
		return 0;
	return r < 0 ? -1 : 1;
}

static int
list_key(struct wgt_form *f, struct wgt_list *w, const struct win_event *ev)
{
	int k = ev->key, r, to = w->sel;

	if (k == WIN_KEY_ENTER) {
		r = ev->kind == WIN_EV_PRESS && w->n > 0 && f->act != NULL ?
		    f->act(f, &w->head, w->head.id, w->sel) : 0;
		return r < 0 ? -1 : r > 0 ? 0 : 1;
	}
	if (w->n == 0)
		return 0;
	if (k == WIN_KEY_UP)
		to = w->sel - 1;
	else if (k == WIN_KEY_DOWN)
		to = w->sel + 1;
	else if (k == WIN_KEY_PAGE_UP)
		to = w->sel - w->rows;
	else if (k == WIN_KEY_PAGE_DOWN)
		to = w->sel + w->rows;
	else if (k == WIN_KEY_HOME)
		to = 0;
	else if (k == WIN_KEY_END)
		to = w->n - 1;
	else
		return 0;
	to = to < 0 ? 0 : to >= w->n ? w->n - 1 : to;
	return wgt_list_select(f, w, to) < 0 ? -1 : 1;
}

int
wgt_form_event(struct win_scene *s, struct win *win, const struct win_event *ev,
    void *arg)
{
	struct wgt_form *f = arg;
	struct wgt *w;
	int r;

	(void)s;
	(void)win;
	if (ev->kind != WIN_EV_PRESS && ev->kind != WIN_EV_REPEAT)
		return 0;
	if ((ev->mods & WIN_MOD_CTRL) != 0) {
		if (ev->kind != WIN_EV_PRESS || ev->key < 'a' || ev->key > 'z')
			return 0;
		return key_mnemonic(f, ev->key);
	}
	if (ev->key == WIN_KEY_TAB || ev->key == WIN_KEY_F1 + 4) {
		r = wgt_traverse(f, ev->key == WIN_KEY_TAB);
		return r < 0 ? -1 : 1;
	}
	w = wgt_focused(f);
	if (w == NULL)
		return 0;
	switch (w->kind) {
	case WGT_BUTTON:
		if (ev->key != WIN_KEY_ENTER && ev->key != ' ')
			return 0;
		if (ev->kind != WIN_EV_PRESS)
			return 1;
		r = f->act != NULL ? f->act(f, w, w->id, 0) : 0;
		return r < 0 ? -1 : 1;
	case WGT_CHECK:
		if (ev->key != ' ')
			return 0;
		if (ev->kind != WIN_EV_PRESS)
			return 1;
		return check_toggle(f, (struct wgt_check *)w, -1);
	case WGT_FIELD:
		return field_key(f, (struct wgt_field *)w, ev);
	case WGT_LIST:
		return list_key(f, (struct wgt_list *)w, ev);
	case WGT_MENUBAR:
		return menu_key(f, (struct wgt_menubar *)w, ev);
	default:
		return 0;
	}
}

/* Dialog */

#define DLG_BTN_W	52
#define DLG_BTN_GAP	8

int
wgt_dialog_finish(struct wgt_dialog *d, int result)
{
	struct win_scene *s = d->form.s;
	struct win *win = d->form.win;

	if (result != WGT_RES_OK && result != WGT_RES_CANCEL)
		return -1;
	if (d->result != WGT_RES_NONE || win == NULL)
		return 0;
	/* Destroy and the focus hand-back are two calls of two rectangles each. */
	if (win_is_live(s, win)) {
		if (win_reserve(s, 2 * WIN_DAMAGE_CALL) < 0)
			return -1;
		if (win_destroy(s, win) < 0)
			return -1;
		if (d->opener != NULL && win_is_live(s, d->opener) &&
		    d->opener->gen == d->opener_gen && !d->opener->hidden &&
		    win_focused(s) != d->opener)
			(void)win_focus(s, d->opener);
	}
	win_input_set_modal(d->in, NULL);
	d->form.win = NULL;
	d->result = result;
	return 0;
}

static int
dlg_act(struct wgt_form *f, struct wgt *w, int id, int value)
{
	struct wgt_dialog *d = f->arg;

	if (w->kind == WGT_BUTTON && id == WGT_ID_OK)
		return wgt_dialog_finish(d, WGT_RES_OK);
	if (w->kind == WGT_BUTTON && id == WGT_ID_CANCEL && d->has_cancel)
		return wgt_dialog_finish(d, WGT_RES_CANCEL);
	if (d->act != NULL)
		return d->act(f, w, id, value);
	/* No hook for a field or list: leave Enter to the dialog's default OK. */
	return w->kind == WGT_FIELD || w->kind == WGT_LIST ? WGT_ACT_PASS : 0;
}

static int
dlg_event(struct win_scene *s, struct win *win, const struct win_event *ev,
    void *arg)
{
	struct wgt_dialog *d = arg;
	int r, cancel = d->has_cancel ? WGT_RES_CANCEL : WGT_RES_OK;

	if (ev->kind == WIN_EV_CLOSE) {
		r = wgt_dialog_finish(d, cancel);
		return r < 0 ? -1 : 1;
	}
	r = wgt_form_event(s, win, ev, &d->form);
	if (r != 0 || ev->kind != WIN_EV_PRESS || (ev->mods & WIN_MOD_CTRL) != 0)
		return r;
	if (ev->key == WIN_KEY_ESC)
		r = wgt_dialog_finish(d, cancel);
	else if (ev->key == WIN_KEY_ENTER)
		r = wgt_dialog_finish(d, WGT_RES_OK);
	else
		return 0;
	return r < 0 ? -1 : 1;
}

int
wgt_dialog_init(struct wgt_dialog *d, struct win_input *in, const char *title,
    int w, int h, int with_cancel)
{
	if (d == NULL || in == NULL || title == NULL ||
	    strlen(title) >= WIN_TITLE_MAX || w < 124 || w > WIN_PANEL_W ||
	    h < 60 || h > WIN_PANEL_H)
		return -1;
	memset(d, 0, sizeof(*d));
	form_reset(&d->form, NULL, NULL, w - 2, h - WIN_TITLE_H - 2, dlg_act, d);
	d->in = in;
	d->w = w;
	d->h = h;
	d->has_cancel = with_cancel != 0;
	memcpy(d->title, title, strlen(title) + 1);
	d->result = WGT_RES_NONE;
	return 0;
}

int
wgt_dialog_open(struct wgt_dialog *d, struct win_scene *s, int x, int y)
{
	struct win *win;
	int cw = d->form.cw, by = d->form.ch - WGT_BUTTON_H - 6, ox;

	if (s == NULL || d->form.win != NULL || d->result != WGT_RES_NONE)
		return -1;
	d->form.s = s;
	ox = d->has_cancel ? cw / 2 - DLG_BTN_W - DLG_BTN_GAP / 2 :
	    (cw - DLG_BTN_W) / 2;
	if (!d->built) {
		if (wgt_button_init(&d->ok, WGT_ID_OK, ox, by, DLG_BTN_W, "&OK") < 0 ||
		    wgt_add(&d->form, &d->ok.head) < 0)
			return -1;
		if (d->has_cancel &&
		    (wgt_button_init(&d->cancel, WGT_ID_CANCEL,
		    ox + DLG_BTN_W + DLG_BTN_GAP, by, DLG_BTN_W, "&Cancel") < 0 ||
		    wgt_add(&d->form, &d->cancel.head) < 0))
			return -1;
		d->built = 1;
	}
	d->opener = win_focused(s);
	d->opener_gen = d->opener != NULL ? d->opener->gen : 0;
	win = win_create(s, x, y, d->w, d->h, d->title, wgt_form_paint, &d->form);
	if (win == NULL)
		return -1;
	d->form.win = win;
	(void)win_set_handler(s, win, dlg_event, d);
	win_input_set_modal(d->in, win);
	return 0;
}

int
wgt_msgbox_open(struct wgt_dialog *d, struct win_scene *s, struct win_input *in,
    const char *title, const char *text, int with_cancel)
{
	int w, h = 70;

	if (text == NULL || strlen(text) >= WGT_TEXT_MAX)
		return -1;
	w = max_i(130, WGT_CELL_W * (int)strlen(text) + 16 + 2);
	if (w > WIN_PANEL_W - 4)
		w = WIN_PANEL_W - 4;
	if (wgt_dialog_init(d, in, title, w, h, with_cancel) < 0 ||
	    wgt_label_init(&d->msg, 8, 10, w - 2 - 16, text) < 0 ||
	    wgt_add(&d->form, &d->msg.head) < 0)
		return -1;
	return wgt_dialog_open(d, s, (WIN_PANEL_W - w) / 2, (WIN_PANEL_H - h) / 2);
}
