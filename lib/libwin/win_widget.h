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
 * libwin widgets: a retained-state toolkit on the scene (win.h) and input
 * (win_input.h) layers. A form is the paint callback and event handler of
 * one window; it owns up to WGT_MAX controls that the caller allocates and
 * keeps alive while the form lives. Every pixel is repainted from control
 * state, so a menu or dialog needs no saved screen under it.
 *
 * Retained state and damage. Each operation compares the state it is about
 * to store with the state last drawn and invalidates only what differs: a
 * changed text cell, a list row, a thumb, a check box. A state set that
 * changes nothing invalidates nothing and writes no byte. A field's text
 * cell is exactly one glyph cell, so one changed character is one
 * glyph-cell rectangle (6 by 10 pixels).
 *
 * Atomicity. An operation validates its arguments, reserves damage slots
 * with win_reserve() (two per rectangle it can add), asks the activation
 * hook for its veto, and only then changes state and invalidates. A backend
 * failure in the reservation returns -1 with every control, the focus and
 * the scene unchanged; the same key can be sent again. The key handler
 * returns -1 to win_input_event() on such a failure. Control constructors
 * and wgt_add() change no scene state and cannot fail at the backend.
 *
 * Keys (the PicoCalc keyboard has no Shift event, so Shift-Tab does not
 * exist; the same keys work on the raw and the tty source). Press and
 * repeat edit and navigate; only a press activates; a release does
 * nothing, so a held Enter closes a dialog once and never reaches the
 * control beneath it.
 *	Tab			next control
 *	F5			previous control (the reverse of Tab)
 *	Ctrl-letter		the control whose text marks that letter with '&':
 *				focus it; a button also activates, a check box
 *				toggles, a menu bar opens that pull-down, a label
 *				focuses the next control after it. The letters h,
 *				i, j and m (Backspace, Tab and Enter on the tty)
 *				and w (the window manager's close) are refused.
 *	Enter or Space		activate a button; Space toggles a check box;
 *				Enter on a field or list reports the activation
 *	text field		printable keys insert at the cursor, Backspace and
 *				Delete remove, Left, Right, Home and End move
 *	list			Up, Down, Page Up, Page Down, Home and End move
 *				the selection; the thumb follows the top row
 *	menu bar		Left and Right pick a title, Down or Enter opens
 *				it; in a pull-down Up and Down move, Enter runs
 *				the entry, a letter alone runs the entry marked
 *				with '&', Esc closes
 *	dialog			Esc or a close request cancels; Enter on a control
 *				that did not take it presses OK
 * Traversal visits the shown, enabled controls that take the focus (button,
 * check box, field, list, menu bar) in the order they were added, wraps,
 * and ends at once when none qualifies.
 *
 * Pixel geometry. Coordinates are client pixels of the window; a control
 * covers x, y, w, h. A glyph cell is 6 by 10 pixels: nine glyph rows and a
 * row for the mnemonic underline. Colors are palette indexes: BG 7 (the
 * client color), FG 0, DIM 8, FACE 15, HI 4, HIFG 15. Inside a control
 * (ox, oy) is its top left pixel, text is drawn in FG (DIM when disabled)
 * and the mnemonic letter is underlined by a one-row line of 6 pixels at
 * row 9 of its cell. Nothing a control draws leaves its rectangle, except a
 * menu bar's open pull-down, painted last.
 *	label		text at (0, 0)
 *	status		row 0 a line of DIM, text at (2, 2); h is 14
 *	button		h 14; frame FG; face FACE (HI when focused) inside;
 *			text centered at x = (w - 6 * n) / 2, y 2, in HIFG
 *			when focused
 *	check box	h 12; box frame FG at (0, 1) 9 by 9, inside FACE, a
 *			5 by 5 mark at (2, 3) in FG when checked; text at
 *			(14, 1); focused: a HI rectangle at (12, 0) of
 *			6 * n + 4 by 12 behind the text, text in HIFG
 *	text field	h 14; frame FG (HI when focused), inside FACE (BG when
 *			disabled); cell k at x = 2 + 6 * k, y 2, k = 0 ..
 *			(w - 4) / 6 - 1, showing text[scroll + k]; when
 *			focused the cursor cell (scroll + k == cursor) is HI
 *			with its glyph in HIFG, also the blank cell after the
 *			last character. The scroll keeps the cursor in view
 *			with the fewest changes: it moves left to the cursor
 *			or right just far enough that the cursor is the last
 *			visible cell
 *	list		h 2 + 10 * rows; frame FG, inside FACE; the scroll bar
 *			is the 7 columns w - 8 .. w - 2, rows 1 .. h - 2, in DIM
 *			with a FACE thumb of height max(4, (h - 2) * rows /
 *			count) at y 1 + (h - 2 - thumb) * top / (count - rows)
 *			(the whole track when count <= rows); row r shows item
 *			top + r at (3, 1 + 10 * r) with its background (1, 1 +
 *			10 * r, w - 9, 10); the selected row is HI (DIM when the
 *			list is not focused) with text in HIFG
 *	menu bar	h 12; FACE with a DIM line at row 11; title i at x_i
 *			(x_0 = 0, x_i+1 = x_i + 6 * n_i + 12), text at x_i + 6,
 *			y 1; the title picked (focused, or open) is HI over
 *			(x_i, 0) of 6 * n_i + 12 by 11 with text in HIFG. An open
 *			pull-down is at (x_i, 12) of 6 * m + 12 by 2 + 10 * k
 *			(m the longest entry, k the count): frame FG, inside
 *			FACE, entry j at (6, 13 + 10 * j) over a (1, 13 + 10 *
 *			j) row of 6 * m + 10 by 10, the chosen row HI with HIFG
 *			text
 * A pull-down must fit inside the client area when the bar is added; a
 * window made smaller afterward clips it.
 */
#ifndef _WIN_WIDGET_H_
#define _WIN_WIDGET_H_

#include "win.h"
#include "win_input.h"

#define WGT_MAX		12		/* Controls in one form. */
#define WGT_TEXT_MAX	40		/* Text bytes including the NUL. */
#define WGT_FIELD_VIS	53		/* Most text cells a field shows. */

#define WGT_CELL_W	6
#define WGT_CELL_H	10

/* Control kinds. */
#define WGT_LABEL	1
#define WGT_STATUS	2
#define WGT_BUTTON	3
#define WGT_CHECK	4
#define WGT_FIELD	5
#define WGT_LIST	6
#define WGT_MENUBAR	7

#define WGT_F_HIDDEN	1
#define WGT_F_DISABLED	2

/* Control heights. */
#define WGT_LABEL_H	10
#define WGT_STATUS_H	14
#define WGT_BUTTON_H	14
#define WGT_CHECK_H	12
#define WGT_FIELD_H	14
#define WGT_MENUBAR_H	12
#define WGT_LIST_ROW_H	10
#define WGT_SCROLL_W	7

/* Palette indexes (the console colors the device starts with). */
#define WGT_C_BG	7
#define WGT_C_FG	0
#define WGT_C_DIM	8
#define WGT_C_FACE	15
#define WGT_C_HI	4
#define WGT_C_HIFG	15

struct wgt_form;

struct wgt {
	unsigned char kind;
	unsigned char flags;		/* WGT_F_* */
	unsigned char id;		/* The caller's identifier, reported on activation. */
	char mnem;			/* Lower case letter after the '&' of the text, or 0. */
	short x, y, w, h;
};

struct wgt_label {			/* WGT_LABEL and WGT_STATUS */
	struct wgt head;
	char text[WGT_TEXT_MAX];
};

struct wgt_button {
	struct wgt head;
	char text[WGT_TEXT_MAX];
};

struct wgt_check {
	struct wgt head;
	unsigned char checked;
	char text[WGT_TEXT_MAX];
};

struct wgt_field {
	struct wgt head;
	char *buf;			/* Caller's storage of cap bytes. */
	unsigned char cap;		/* Includes the NUL; the text holds cap - 1. */
	unsigned char len, cur, scroll;
	unsigned char vis;		/* Cells shown. */
};

struct wgt_list {
	struct wgt head;
	const char *const *items;
	unsigned char n, sel, top, rows;
};

struct wgt_entry {
	const char *text;		/* May mark one letter with '&'. */
	unsigned char id;
};

struct wgt_menu {
	const char *title;		/* May mark one letter with '&'. */
	const struct wgt_entry *entries;
	unsigned char n;
};

struct wgt_menubar {
	struct wgt head;
	const struct wgt_menu *menus;
	unsigned char n;
	unsigned char cur;		/* Title the bar points at. */
	unsigned char entry;		/* Entry the pull-down points at. */
	unsigned char open;		/* Nonzero while the pull-down shows. */
};

/*
 * Called when a control is activated: a button (value 0), a check box (the
 * value it will take), a field's Enter (0), a list's Enter (the selected
 * item), a menu entry (0; the entry's id). The hook runs before the control
 * changes state and may veto it by returning -1, which the handler reports.
 */
typedef int (*wgt_act_fn)(struct wgt_form *f, struct wgt *w, int id, int value);

struct wgt_form {
	struct win_scene *s;
	struct win *win;
	struct wgt *item[WGT_MAX];
	short cw, ch;			/* Client area size. */
	unsigned char n;
	signed char focus;		/* Index of the focused control, or -1. */
	wgt_act_fn act;
	void *arg;
};

/* Constructors fill the structure; none of them touches a scene. */
int wgt_label_init(struct wgt_label *w, int x, int y, int cw, const char *text);
int wgt_status_init(struct wgt_label *w, int x, int y, int cw, const char *text);
int wgt_button_init(struct wgt_button *w, int id, int x, int y, int cw,
    const char *text);
int wgt_check_init(struct wgt_check *w, int id, int x, int y, int cw,
    const char *text, int checked);
/* buf holds cap bytes (cap at most 255); its text, if any, is kept. */
int wgt_field_init(struct wgt_field *w, int id, int x, int y, int cw, char *buf,
    int cap);
/* rows items are visible (h = 2 + 10 * rows); items and n may be set later. */
int wgt_list_init(struct wgt_list *w, int id, int x, int y, int cw, int rows,
    const char *const *items, int n);
int wgt_menubar_init(struct wgt_menubar *w, int id, int x, int y, int cw,
    const struct wgt_menu *menus, int n);

/*
 * A form is the paint function and the event handler of its window:
 *	win = win_create(s, ..., "title", wgt_form_paint, &form);
 *	wgt_form_init(&form, s, win, act, arg);
 * wgt_form_init() installs the handler. wgt_add() appends a control, fails
 * on a full form, a control outside the client area, a mnemonic another
 * control holds or an open menu that does not fit, and gives the first
 * control that takes the focus the form's focus. It records no damage: add
 * every control before the first flush, or call wgt_form_refresh().
 */
void wgt_form_paint(struct win_ctx *c, void *arg);
int wgt_form_init(struct wgt_form *f, struct win_scene *s, struct win *win,
    wgt_act_fn act, void *arg);
int wgt_add(struct wgt_form *f, struct wgt *w);
int wgt_form_refresh(struct wgt_form *f);

/* The key handler of the form (a win_event_fn); returns 1, 0 or -1. */
int wgt_form_event(struct win_scene *s, struct win *win,
    const struct win_event *ev, void *arg);

/* Operations: 0 or -1 (invalid argument, backend failure; nothing changed). */
int wgt_focus(struct wgt_form *f, struct wgt *w);
int wgt_traverse(struct wgt_form *f, int forward);
int wgt_set_text(struct wgt_form *f, struct wgt *w, const char *text);
int wgt_set_enabled(struct wgt_form *f, struct wgt *w, int on);
int wgt_set_shown(struct wgt_form *f, struct wgt *w, int on);
int wgt_check_set(struct wgt_form *f, struct wgt_check *w, int on);
int wgt_field_set_text(struct wgt_form *f, struct wgt_field *w, const char *text);
int wgt_field_insert(struct wgt_form *f, struct wgt_field *w, int ch);
int wgt_field_delete(struct wgt_form *f, struct wgt_field *w, int forward);
int wgt_field_move(struct wgt_form *f, struct wgt_field *w, int pos);
int wgt_list_set(struct wgt_form *f, struct wgt_list *w,
    const char *const *items, int n);
int wgt_list_select(struct wgt_form *f, struct wgt_list *w, int index);
int wgt_menu_open(struct wgt_form *f, struct wgt_menubar *w, int title);
int wgt_menu_close(struct wgt_form *f, struct wgt_menubar *w);

/* Reads. */
int wgt_is_focusable(const struct wgt *w);
struct wgt *wgt_focused(const struct wgt_form *f);
int wgt_field_cursor(const struct wgt_field *w);
int wgt_list_selected(const struct wgt_list *w);	/* -1 for an empty list */

/*
 * Modal dialog and message box. A dialog is a window with a form, an OK
 * button and optionally a Cancel button (52 by 14, at y = client height - 20;
 * with Cancel, OK at x = client width / 2 - 56 and Cancel at client width /
 * 2 + 4, alone OK at (client width - 52) / 2; ids 1 and 2 are reserved),
 * added after the caller's controls
 * (so Tab starts on the caller's first control, or on OK with none). It
 * takes the focus, makes itself modal in the input state (the window-cycle
 * keys stop at it), and result changes from WGT_RES_NONE to WGT_RES_OK or
 * WGT_RES_CANCEL when the dialog closes itself, which also destroys its
 * window and returns the focus to the window that held it, if that is
 * still live and shown. A caller polls result; there is no blocking call,
 * so no dialog runs inside an event handler. A dialog without Cancel
 * reports WGT_RES_OK for Esc.
 */
#define WGT_RES_NONE	0
#define WGT_RES_OK	1
#define WGT_RES_CANCEL	2

#define WGT_ID_OK	1
#define WGT_ID_CANCEL	2

struct wgt_dialog {
	struct wgt_form form;
	struct wgt_button ok, cancel;
	struct wgt_label msg;
	struct win_input *in;
	struct win *opener;
	unsigned opener_gen;
	wgt_act_fn act;			/* Hook for the caller's own controls. */
	void *arg;
	int has_cancel;
	int built;			/* The buttons are in the form. */
	int w, h;
	char title[WIN_TITLE_MAX];
	int result;
};

/*
 * A dialog of window size w by h (at least 124 by 60); add controls with
 * wgt_add(&d->form, ...) before wgt_dialog_open(). A refused open leaves the
 * scene as it was and can be repeated.
 */
int wgt_dialog_init(struct wgt_dialog *d, struct win_input *in, const char *title,
    int w, int h, int with_cancel);
/* Create the window at (x, y); fails whole, with the scene unchanged. */
int wgt_dialog_open(struct wgt_dialog *d, struct win_scene *s, int x, int y);
/* Message box: one line of text centered on the panel, OK and optional Cancel. */
int wgt_msgbox_open(struct wgt_dialog *d, struct win_scene *s,
    struct win_input *in, const char *title, const char *text, int with_cancel);
/* Close with a result (a program ends a dialog early with this); 0 or -1. */
int wgt_dialog_finish(struct wgt_dialog *d, int result);

#endif /* !_WIN_WIDGET_H_ */
