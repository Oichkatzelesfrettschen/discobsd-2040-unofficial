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

/*
 * word: a clean-room word processor in the command model of Word 5.1 for
 * UNIX (SPEC.md), with vi and nano key modes. This file holds the editor
 * core: the cursor and selection, the wrapped layout of paragraphs into
 * display rows, the screen composition, the command menu and its forms,
 * and the Word key mode. word_buf.c holds the document, word_term.c the
 * terminal and word_modes.c the vi and nano keys.
 */

#include "word.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAGE_LINES	54	/* a 9-inch text area at six lines per inch */

enum ui {
	U_EDIT = 0, U_MENU, U_FORM, U_CONF_REPL, U_CONF_QUIT, U_CONF_CLEAR,
	U_PICK, U_HELP
};

int keymode = KEYS_WORD, running = 1, scrap_lines;
woff_t ed_cur, ed_anchor;
int ed_sel;
char ed_file[WORD_PATH_MAX];

static int opt_wrap = 1, opt_hints = -1, opt_backup, tabw = 4;
static enum ui ui;
static int ext, ovr, pmod, goal = -1, quit_after, pick_form = -1;
static woff_t top, leftcol;
static woff_t scrap_ref, scrap_len;
static uint32_t saved_gen;
static char msg[WORD_MSG_MAX];
static int msg_err;

/* Logical line cache: the line number of li_off is li_line. */
static woff_t li_off, li_line;
static int li_valid;

struct mframe {
	uint8_t id, sel;
};
static struct mframe mstack[WORD_MENU_DEPTH];
static unsigned mdepth;

/* ---- messages ---- */

void
ed_msg(int err, const char *s)
{
	snprintf(msg, sizeof(msg), "%s", s);
	msg_err = err;
}

void
ed_msgf(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	msg_err = err;
}

static void
msg_errno(int e)
{
	ed_msgf(1, "%s", strerror(e));
}

/* ---- positions ---- */

static int
is_wc(int c)
{
	return c >= 0 && (isalnum(c) || c == '_');
}

woff_t
ed_bol(woff_t p)
{
	while (p > 0 && buf_at(p - 1) != '\n')
		p--;
	return p;
}

woff_t
ed_eol(woff_t p)
{
	int c;

	while ((c = buf_at(p)) >= 0 && c != '\n')
		p++;
	return p;
}

woff_t
ed_wordfwd(woff_t p)
{
	woff_t len = buf_len();

	if (is_wc(buf_at(p)))
		while (p < len && is_wc(buf_at(p)))
			p++;
	else if (p < len && !isspace(buf_at(p)))
		p++;
	while (p < len && isspace(buf_at(p)))
		p++;
	return p;
}

woff_t
ed_wordback(woff_t p)
{
	while (p > 0 && isspace(buf_at(p - 1)))
		p--;
	if (p > 0 && is_wc(buf_at(p - 1)))
		while (p > 0 && is_wc(buf_at(p - 1)))
			p--;
	else if (p > 0)
		p--;
	return p;
}

woff_t
ed_wordend(woff_t p)
{
	woff_t len = buf_len();

	if (p < len)
		p++;
	while (p < len && isspace(buf_at(p)))
		p++;
	while (p + 1 < len && is_wc(buf_at(p + 1)) && is_wc(buf_at(p)))
		p++;
	return p;
}

woff_t
ed_line_off(woff_t line)
{
	woff_t p = 0, len = buf_len();

	while (line > 0 && p < len) {
		if (buf_at(p) == '\n')
			line--;
		p++;
	}
	return p;
}

static woff_t
line_of(woff_t off)
{
	woff_t p = 0, n = 0;

	if (buf_lowmark() < li_off)
		li_valid = 0;
	if (li_valid && off < li_off) {
		/* Count back from the cached point rather than from byte 0. */
		for (p = off, n = li_line; p < li_off; p++)
			if (buf_at(p) == '\n')
				n--;
		li_off = off;
		li_line = n;
		return n;
	}
	if (li_valid && off >= li_off) {
		p = li_off;
		n = li_line;
	}
	for (; p < off; p++)
		if (buf_at(p) == '\n')
			n++;
	li_off = off;
	li_line = n;
	li_valid = 1;
	return n;
}

/* ---- layout: paragraphs wrapped into display rows ---- */

static int
cell_w(int c, int col)
{
	if (c == '\t')
		return tabw - col % tabw;
	return 1;
}

/* Start of the display row after the one at s; WOFF_END after the last. */
static woff_t
row_next(woff_t s)
{
	woff_t p = s, len = buf_len(), sp = WOFF_END;
	int col = 0, c, w;

	for (;;) {
		if (p >= len)
			return WOFF_END;
		c = buf_at(p);
		if (c == '\n')
			return p + 1;
		if (opt_wrap) {
			if (c == ' ' && col >= scr_cols)
				return p + 1;
			w = cell_w(c, col);
			if (col + w > scr_cols) {
				if (sp != WOFF_END)
					return sp + 1;
				return p > s ? p : p + 1;
			}
			if (c == ' ' || c == '\t')
				sp = p;
		} else
			w = cell_w(c, col);
		col += w;
		p++;
	}
}

static woff_t
row_start(woff_t off)
{
	woff_t r = ed_bol(off), n;

	for (;;) {
		n = row_next(r);
		if (n == WOFF_END || off < n)
			return r;
		r = n;
	}
}

static int
col_of(woff_t s, woff_t off)
{
	int col = 0;

	for (; s < off; s++)
		col += cell_w(buf_at(s), col);
	return col;
}

static woff_t
at_col(woff_t s, int want)
{
	woff_t n = row_next(s), last = n == WOFF_END ? buf_len() : n - 1;
	int col = 0, w;

	while (s < last) {
		w = cell_w(buf_at(s), col);
		if (col + w > want)
			break;
		col += w;
		s++;
	}
	return s;
}

void
ed_set(woff_t pos)
{
	woff_t len = buf_len();

	ed_cur = pos > len ? len : pos;
	goal = -1;
}

void
ed_vmove(int rows)
{
	woff_t rs = row_start(ed_cur), n;

	if (goal < 0)
		goal = col_of(rs, ed_cur);
	while (rows > 0) {
		n = row_next(rs);
		if (n == WOFF_END)
			break;
		rs = n;
		rows--;
	}
	while (rows < 0 && rs > 0) {
		rs = row_start(rs - 1);
		rows++;
	}
	ed_cur = at_col(rs, goal);
}

void
ed_set_wrap(int on)
{
	opt_wrap = on;
	leftcol = 0;
}

/* ---- screen geometry ---- */

static int
hints_on(void)
{
	return opt_hints < 0 ? keymode == KEYS_NANO : opt_hints;
}

static int
cmd_lines(void)
{
	return scr_cols >= 70 ? 2 : 3;
}

static int
doc_top(void)
{
	return keymode == KEYS_NANO ? 1 : 0;
}

int
ed_rows(void)
{
	int r = scr_rows - (hints_on() ? 2 : 0);

	if (keymode == KEYS_WORD)
		r -= cmd_lines() + 2;
	else
		r -= keymode == KEYS_NANO ? 2 : 2;
	return r < 1 ? 1 : r;
}

static int
visible(woff_t t, woff_t off, int rows)
{
	woff_t n;

	while (rows-- > 0) {
		n = row_next(t);
		if (n == WOFF_END || off < n)
			return 1;
		t = n;
	}
	return 0;
}

static void
scroll_to_cursor(void)
{
	int rows = ed_rows(), i;

	if (top > buf_len())
		top = buf_len();
	top = row_start(top);
	if (ed_cur < top)
		top = row_start(ed_cur);
	else if (!visible(top, ed_cur, rows)) {
		top = row_start(ed_cur);
		for (i = 1; i < rows && top > 0; i++)
			top = row_start(top - 1);
	}
	if (!opt_wrap) {
		int col = col_of(ed_bol(ed_cur), ed_cur);

		if ((woff_t)col < leftcol)
			leftcol = (woff_t)col;
		if ((woff_t)col >= leftcol + (woff_t)scr_cols)
			leftcol = (woff_t)(col - scr_cols + 1);
	}
}

/* ---- edits ---- */

int
ed_dirty(void)
{
	return buf_gen() != saved_gen;
}

static void
sel_range(woff_t *lo, woff_t *hi)
{
	if (!ed_sel) {
		*lo = *hi = ed_cur;
		return;
	}
	*lo = ed_anchor < ed_cur ? ed_anchor : ed_cur;
	*hi = ed_anchor < ed_cur ? ed_cur : ed_anchor;
}

int
ed_ins(const char *s, size_t n)
{
	int e = buf_insert(ed_cur, s, n);

	if (e != 0) {
		msg_errno(e);
		return -1;
	}
	ed_cur += (woff_t)n;
	goal = -1;
	return 0;
}

int
ed_del(woff_t lo, woff_t hi, int scrap, int append)
{
	woff_t ref;
	int e;

	if (hi <= lo)
		return 0;
	e = buf_delete(lo, hi - lo, &ref);
	if (e != 0) {
		msg_errno(e);
		return -1;
	}
	if (scrap) {
		if (append && scrap_len > 0 && scrap_ref + scrap_len == ref)
			scrap_len += hi - lo;
		else {
			scrap_ref = ref;
			scrap_len = hi - lo;
		}
	}
	ed_set(lo);
	ed_sel = 0;
	return 0;
}

void
ed_yank(woff_t lo, woff_t hi, int lines)
{
	woff_t ref;
	int e;

	if (hi <= lo)
		return;
	e = buf_copy(lo, hi - lo, &ref);
	if (e != 0) {
		msg_errno(e);
		return;
	}
	scrap_ref = ref;
	scrap_len = hi - lo;
	scrap_lines = lines;
}

void
ed_paste(woff_t at)
{
	int e;

	if (scrap_len == 0) {
		ed_msg(1, "The scrap is empty");
		return;
	}
	e = buf_insert_ref(at, scrap_ref, scrap_len);
	if (e != 0) {
		msg_errno(e);
		return;
	}
	ed_set(at + scrap_len);
	ed_sel = 0;
}

void
ed_undo(void)
{
	woff_t p = ed_cur;
	int r = buf_undo(&p);

	if (r <= 0) {
		ed_msg(r < 0, "Nothing to undo");
		return;
	}
	ed_set(p);
	ed_sel = 0;
}

void
ed_redo(void)
{
	woff_t p = ed_cur;
	int r = buf_redo(&p);

	if (r <= 0) {
		ed_msg(r < 0, "Nothing to redo");
		return;
	}
	ed_set(p);
	ed_sel = 0;
}

/* Typing at the cursor: a selection is replaced, overtype replaces one. */
static void
type_char(int c)
{
	char ch = (char)c;
	woff_t lo, hi;

	sel_range(&lo, &hi);
	if (lo < hi) {
		if (ed_del(lo, hi, 0, 0) != 0)
			return;
	} else if (ovr && c != '\n' && buf_at(ed_cur) >= 0 &&
	    buf_at(ed_cur) != '\n') {
		int e = buf_replace(ed_cur, 1, &ch, 1);

		if (e != 0) {
			msg_errno(e);
			return;
		}
		ed_cur++;
		goal = -1;
		ext = 0;
		return;
	}
	ed_ins(&ch, 1);
	ext = 0;
}

/* ---- files ---- */

int
ed_save(const char *path)
{
	int e;

	if (path[0] == '\0') {
		ed_msg(1, "No file name");
		return -1;
	}
	e = buf_save(path, opt_backup);
	if (e != 0) {
		msg_errno(e);
		return -1;
	}
	if (path != ed_file)
		snprintf(ed_file, sizeof(ed_file), "%s", path);
	saved_gen = buf_gen();
	ed_msgf(0, "Saved %s: %lu bytes",
	    path, (unsigned long)buf_len());
	return 0;
}

int
ed_load(const char *path)
{
	char p[WORD_PATH_MAX];
	int e;

	snprintf(p, sizeof(p), "%s", path);
	e = buf_load(p);
	if (e == ENOENT) {
		buf_clear();
		snprintf(ed_file, sizeof(ed_file), "%s", p);
		ed_msg(0, "New file");
	} else if (e != 0) {
		msg_errno(e);
		return -1;
	} else {
		snprintf(ed_file, sizeof(ed_file), "%s", p);
		ed_msgf(0, "Loaded %s: %lu bytes",
		    p, (unsigned long)buf_len());
	}
	saved_gen = buf_gen();
	ed_set(0);
	top = 0;
	ed_sel = 0;
	return 0;
}

void
ed_quit(int force)
{
	if (force || !ed_dirty()) {
		running = 0;
		return;
	}
	ui = U_CONF_QUIT;
	ed_msg(0, "Save changes? Y saves, N discards, Esc stays");
}

void
ed_info(void)
{
	ed_msgf(0, "\"%s\"%s %lu bytes, line %lu",
	    ed_file[0] ? ed_file : "(no name)",
	    ed_dirty() ? " *" : "", (unsigned long)buf_len(),
	    (unsigned long)line_of(ed_cur) + 1);
}

static void
word_count(void)
{
	woff_t p, len = buf_len(), words = 0, lines = 0;
	int c, in = 0;

	for (p = 0; p < len; p++) {
		c = buf_at(p);
		if (c == '\n')
			lines++;
		if (isspace(c))
			in = 0;
		else if (!in) {
			in = 1;
			words++;
		}
	}
	if (len > 0 && buf_at(len - 1) != '\n')
		lines++;
	ed_msgf(0, "%lu words, %lu lines, %lu characters", (unsigned long)words,
	    (unsigned long)lines, (unsigned long)len);
}

/* ---- search and replace ---- */

static char f_text[WORD_FIELD_MAX], f_with[WORD_FIELD_MAX];
static char f_name[WORD_PATH_MAX], f_cmd[WORD_FIELD_MAX], f_num[12];
static uint8_t s_up, s_case, s_whole, r_confirm = 1;
static uint8_t r_ask;		/* this Replace still asks before each match */
static unsigned long nrepl;
static struct word_pattern pat;

void
ed_search(const char *text, int down, int again)
{
	woff_t from = ed_cur, s, e, lo, hi;

	if (text != NULL && text != f_text)
		snprintf(f_text, sizeof(f_text), "%s", text);
	if (f_text[0] == '\0') {
		ed_msg(1, "No search text");
		return;
	}
	s_up = !down;
	word_pat_compile(&pat, f_text, s_case, s_whole);
	if (again && ed_sel) {
		sel_range(&lo, &hi);
		from = down ? hi : lo;
	} else if (again && down && from < buf_len())
		from++;
	if (!word_pat_find(&pat, from, down, &s, &e)) {
		ed_msg(1, "Search text not found");
		return;
	}
	ed_anchor = s;
	ed_set(e);
	ed_sel = 1;
	msg[0] = '\0';
}

/* A failed replacement leaves the text as it was and keeps its message. */
static void
replace_failed(void)
{
	ui = U_EDIT;
	ed_sel = 0;
	ed_msgf(1, "Replace failed; %lu replaced", nrepl);
}

static void
replace_from(woff_t from)
{
	woff_t s, e;
	size_t rl = strlen(f_with);

	while (word_pat_find(&pat, from, 1, &s, &e)) {
		if (r_ask) {
			ed_anchor = s;
			ed_set(e);
			ed_sel = 1;
			ui = U_CONF_REPL;
			ed_msg(0, "Replace? Y replaces, N skips, A all, Esc stops");
			return;
		}
		if (buf_replace(s, e - s, f_with, rl) != 0) {
			replace_failed();
			return;
		}
		nrepl++;
		from = s + (woff_t)rl;
		ed_set(from);
	}
	ui = U_EDIT;
	ed_sel = 0;
	ed_msgf(0, "%lu replacements", nrepl);
}

static void
replace_key(int k)
{
	woff_t s, e;
	int up = toupper(k & 0xff);

	sel_range(&s, &e);
	if (k == K_ESC) {
		ui = U_EDIT;
		ed_sel = 0;
		ed_msgf(0, "%lu replacements", nrepl);
	} else if (up == 'Y' || up == 'A') {
		if (up == 'A')
			r_ask = 0;
		if (buf_replace(s, e - s, f_with, strlen(f_with)) != 0) {
			replace_failed();
			return;
		}
		nrepl++;
		replace_from(s + (woff_t)strlen(f_with));
	} else if (up == 'N')
		replace_from(e > s ? e : s + 1);
}

/* ---- forms: a command's fields on the command lines ---- */

enum { T_TEXT, T_CHOICE };

struct field {
	const char *name;
	uint8_t type, nch, file;
	const char *choices;	/* choices separated by '|' */
	char *buf;
	size_t cap;
};

struct form {
	const char *name;
	uint8_t nf;
	struct field f[6];
};

#define TXT(e, b)	{ e, T_TEXT, 0, 0, NULL, b, sizeof(b) }
#define FIL(e, b)	{ e, T_TEXT, 0, 1, NULL, b, sizeof(b) }
#define CHO(e, n, ce)	{ e, T_CHOICE, n, 0, ce, NULL, 0 }
#define YESNO(e)	CHO(e, 2, "Yes|No")

static const struct form forms[F_NFORMS] = {
	[F_LOAD] = { "TRANSFER LOAD", 1,
	    { FIL("filename", f_name) } },
	[F_SAVE] = { "TRANSFER SAVE", 2,
	    { FIL("filename", f_name),
	      YESNO("backup") } },
	[F_MERGE] = { "TRANSFER MERGE", 1,
	    { FIL("filename", f_name) } },
	[F_FDELETE] = { "TRANSFER DELETE", 1,
	    { FIL("filename", f_name) } },
	[F_RENAME] = { "TRANSFER RENAME", 1,
	    { FIL("filename", f_name) } },
	[F_CHDIR] = { "TRANSFER OPTIONS", 1,
	    { TXT("setup", f_name) } },
	[F_SEARCH] = { "SEARCH", 4,
	    { TXT("text", f_text),
	      CHO("direction", 2, "Up|Down"),
	      YESNO("case"), YESNO("whole word") } },
	[F_REPLACE] = { "REPLACE", 5,
	    { TXT("text", f_text), TXT("with text", f_with),
	      YESNO("confirm"), YESNO("case"),
	      YESNO("whole word") } },
	[F_JUMP] = { "JUMP PAGE", 1,
	    { TXT("number", f_num) } },
	[F_LINE] = { "JUMP LINE", 1,
	    { TXT("number", f_num) } },
	[F_RUN] = { "LIBRARY RUN", 1,
	    { TXT("command", f_cmd) } },
	[F_OPTIONS] = { "OPTIONS", 5,
	    { CHO("keys", 3, "Word|vi|nano"),
	      YESNO("wrap"), YESNO("hints"),
	      YESNO("color"),
	      CHO("tab width", 2, "4|8") } }
};

static int fid, fcur, ffresh;
static uint8_t fval[6];

/* The n-th '|'-separated choice of list, copied into out. */
static const char *
choice(const char *list, int n, char *out, size_t sz)
{
	size_t i = 0;

	while (n > 0 && *list != '\0')
		if (*list++ == '|')
			n--;
	while (list[i] != '\0' && list[i] != '|' && i + 1 < sz) {
		out[i] = list[i];
		i++;
	}
	out[i] = '\0';
	return out;
}

void
ed_form(int id)
{
	const struct form *f = &forms[id];

	fid = id;
	fcur = 0;
	ffresh = 1;
	memset(fval, 0, sizeof(fval));
	switch (id) {
	case F_LOAD:
	case F_MERGE:
	case F_FDELETE:
		f_name[0] = '\0';
		break;
	case F_SAVE:
	case F_RENAME:
		snprintf(f_name, sizeof(f_name), "%s", ed_file);
		fval[1] = !opt_backup;
		break;
	case F_CHDIR:
		if (getcwd(f_name, sizeof(f_name)) == NULL)
			f_name[0] = '\0';
		break;
	case F_SEARCH:
		fval[1] = !s_up;
		fval[2] = !s_case;
		fval[3] = !s_whole;
		break;
	case F_REPLACE:
		fval[2] = !r_confirm;
		fval[3] = !s_case;
		fval[4] = !s_whole;
		break;
	case F_JUMP:
	case F_LINE:
		f_num[0] = '\0';
		break;
	case F_OPTIONS:
		fval[0] = (uint8_t)keymode;
		fval[1] = !opt_wrap;
		fval[2] = !hints_on();
		fval[3] = !scr_color;
		fval[4] = tabw == 8;
		break;
	default:
		break;
	}
	(void)f;
	ui = U_FORM;
	msg[0] = '\0';
}

static void
form_run(void)
{
	long n;

	ui = U_EDIT;
	switch (fid) {
	case F_LOAD:
		if (ed_dirty()) {
			ed_msg(1, "Unsaved edits: save or clear first");
			break;
		}
		ed_load(f_name);
		break;
	case F_SAVE:
		opt_backup = fval[1] == 0;
		if (ed_save(f_name) == 0 && quit_after)
			running = 0;
		quit_after = 0;
		break;
	case F_MERGE: {
		woff_t got = 0;
		int e = buf_merge(ed_cur, f_name, &got);

		if (e != 0)
			msg_errno(e);
		else
			ed_msgf(0, "Merged %lu bytes",
			    (unsigned long)got);
		break;
	}
	case F_FDELETE:
		if (unlink(f_name) != 0)
			msg_errno(errno);
		else
			ed_msg(0, "File deleted");
		break;
	case F_RENAME:
		if (ed_file[0] != '\0' && access(ed_file, F_OK) == 0 &&
		    rename(ed_file, f_name) != 0) {
			msg_errno(errno);
			break;
		}
		snprintf(ed_file, sizeof(ed_file), "%s", f_name);
		ed_msg(0, "Renamed");
		break;
	case F_CHDIR:
		if (chdir(f_name) != 0)
			msg_errno(errno);
		else
			ed_msgf(0, "%s", f_name);
		break;
	case F_SEARCH:
		s_case = fval[2] == 0;
		s_whole = fval[3] == 0;
		ed_search(f_text, fval[1] == 1, 0);
		break;
	case F_REPLACE:
		r_confirm = fval[2] == 0;
		s_case = fval[3] == 0;
		s_whole = fval[4] == 0;
		if (f_text[0] == '\0') {
			ed_msg(1, "No search text");
			break;
		}
		word_pat_compile(&pat, f_text, s_case, s_whole);
		nrepl = 0;
		r_ask = r_confirm;
		buf_mark();
		replace_from(ed_cur);
		break;
	case F_JUMP:
	case F_LINE:
		n = strtol(f_num, NULL, 10);
		if (n < 1)
			n = 1;
		else if (n > 1000000)
			n = 1000000;
		ed_set(ed_line_off(fid == F_JUMP ? (woff_t)(n - 1) * PAGE_LINES :
		    (woff_t)(n - 1)));
		ed_sel = 0;
		break;
	case F_RUN:
		tty_restore();
		if (system(f_cmd) < 0)
			perror("word");
		fputs("\nPress Enter to return to word.", stdout);
		fflush(stdout);
		{
			char line[8];

			if (read(STDIN_FILENO, line, sizeof(line)) < 0)
				line[0] = 0;
		}
		tty_raw();
		break;
	case F_OPTIONS:
		keymode = fval[0];
		opt_wrap = fval[1] == 0;
		leftcol = 0;
		opt_hints = fval[2] == 0;
		scr_color = fval[3] == 0;
		tabw = fval[4] ? 8 : 4;
		scr_invalidate();
		vi_reset();
		break;
	default:
		break;
	}
}

static void
form_key(int k)
{
	const struct field *fl = &forms[fid].f[fcur];
	int nf = forms[fid].nf;
	size_t n;

	if (k == K_ESC) {
		ui = U_EDIT;
		quit_after = 0;
		ed_msg(0, "Cancelled");
		return;
	}
	if (k == K_ENTER) {
		form_run();
		return;
	}
	if (k == K_TAB || k == K_DOWN) {
		fcur = (fcur + 1) % nf;
		ffresh = 1;
		return;
	}
	if (k == K_BTAB || k == K_UP) {
		fcur = (fcur + nf - 1) % nf;
		ffresh = 1;
		return;
	}
	if (fl->type == T_CHOICE) {
		char a[16];
		int i;

		if (k == ' ' || k == K_RIGHT)
			fval[fcur] = (uint8_t)((fval[fcur] + 1) % fl->nch);
		else if (k == K_LEFT)
			fval[fcur] = (uint8_t)((fval[fcur] + fl->nch - 1) % fl->nch);
		else if (k > ' ' && k < 0x7f)
			for (i = 0; i < fl->nch; i++)
				if (tolower((unsigned char)choice(fl->choices, i, a, sizeof(a))[0]) == tolower(k))
					fval[fcur] = (uint8_t)i;
		return;
	}
	if (k == K_F1 && fl->file) {
		if (pick_open(".") == 0) {
			pick_form = fid;
			ui = U_PICK;
		} else
			msg_errno(errno);
		return;
	}
	n = strlen(fl->buf);
	if (k == K_BS) {
		if (ffresh)
			n = 0;
		else if (n > 0)
			n--;
		fl->buf[n] = '\0';
		ffresh = 0;
		return;
	}
	if (k >= ' ' && k < 0x7f) {
		if (ffresh)
			n = 0;
		ffresh = 0;
		if (n + 1 < fl->cap) {
			fl->buf[n++] = (char)k;
			fl->buf[n] = '\0';
		}
	}
}

/* ---- the command menu ---- */

void
ed_menu(void)
{
	mdepth = 1;
	mstack[0].id = MENU_MAIN;
	mstack[0].sel = 0;
	ui = U_MENU;
	ed_msg(0, "Type a command's letter, or move and press Enter");
}

void
ed_help(void)
{
	ui = U_HELP;
}

void
ed_pick(void)
{
	if (ed_dirty()) {
		ed_msg(1, "Unsaved edits: save or clear first");
		return;
	}
	if (pick_open(".") != 0) {
		msg_errno(errno);
		return;
	}
	pick_form = -1;
	ui = U_PICK;
}

static void
do_clear(void)
{
	buf_clear();
	saved_gen = buf_gen();
	ed_file[0] = '\0';
	ed_set(0);
	top = 0;
	ed_sel = 0;
	ed_msg(0, "Document cleared");
}

static void
copy_sel(void)
{
	woff_t lo, hi;

	sel_range(&lo, &hi);
	if (lo == hi && buf_at(lo) >= 0)
		hi = lo + 1;
	ed_yank(lo, hi, 0);
	ed_sel = 0;
	ext = 0;
}

static void
delete_sel(int scrap)
{
	woff_t lo, hi;

	sel_range(&lo, &hi);
	if (lo == hi && buf_at(lo) >= 0)
		hi = lo + 1;
	buf_mark();
	ed_del(lo, hi, scrap, 0);
	if (scrap)
		scrap_lines = 0;
	ext = 0;
}

static void
builtin(int b)
{
	ui = U_EDIT;
	switch (b) {
	case ACT_BACK:
		if (mdepth > 1) {
			mdepth--;
			ui = U_MENU;
		}
		break;
	case ACT_COPY:
		copy_sel();
		break;
	case ACT_DELETE:
		delete_sel(1);
		break;
	case ACT_INSERT:
		buf_mark();
		ed_paste(ed_cur);
		break;
	case ACT_UNDO:
		ed_undo();
		break;
	case ACT_LOAD:
		ed_form(F_LOAD);
		break;
	case ACT_SAVE:
		ed_form(F_SAVE);
		break;
	case ACT_MERGE:
		ed_form(F_MERGE);
		break;
	case ACT_FDELETE:
		ed_form(F_FDELETE);
		break;
	case ACT_RENAME:
		ed_form(F_RENAME);
		break;
	case ACT_CHDIR:
		ed_form(F_CHDIR);
		break;
	case ACT_SEARCH:
		ed_form(F_SEARCH);
		break;
	case ACT_REPLACE:
		ed_form(F_REPLACE);
		break;
	case ACT_JUMP_PAGE:
		ed_form(F_JUMP);
		break;
	case ACT_RUN:
		ed_form(F_RUN);
		break;
	case ACT_OPTIONS:
		ed_form(F_OPTIONS);
		break;
	case ACT_PICK:
		ed_pick();
		break;
	case ACT_COUNT:
		word_count();
		break;
	case ACT_HELP:
		ed_help();
		break;
	case ACT_CLEAR:
		if (ed_dirty()) {
			ui = U_CONF_CLEAR;
			ed_msg(0, "Discard unsaved edits? Y or N");
		} else
			do_clear();
		break;
	case ACT_QUIT:
		ed_quit(0);
		break;
	default:
		break;
	}
}

static void
menu_key(int k)
{
	struct mframe *mf = &mstack[mdepth - 1];
	const struct word_menu *m = word_menu_get((enum word_menu_id)mf->id);
	const struct word_menu_item *it = NULL;
	int i;

	if (k == K_ESC) {
		ui = U_EDIT;
		msg[0] = '\0';
		return;
	}
	if (k == K_LEFT)
		mf->sel = (uint8_t)(mf->sel == 0 ? m->count - 1 : mf->sel - 1);
	else if (k == K_RIGHT || k == ' ' || k == K_TAB)
		mf->sel = (uint8_t)((mf->sel + 1) % m->count);
	else if (k == K_ENTER)
		it = word_menu_item_at(m, mf->sel);
	else if (k > ' ' && k < 0x7f && (i = word_menu_find_accel(m,
	    (char)k)) >= 0) {
		mf->sel = (uint8_t)i;
		it = word_menu_item_at(m, mf->sel);
	}
	if (it == NULL)
		return;
	if (it->kind == KIND_SUB && mdepth < WORD_MENU_DEPTH) {
		mstack[mdepth].id = it->target;
		mstack[mdepth].sel = 0;
		mdepth++;
	} else if (it->kind == KIND_BUILTIN)
		builtin(it->target);
	else {
		ui = U_EDIT;
		ed_msgf(1, "%s is not available in this version",
		    it->name);
	}
}

/* ---- the Word key mode ---- */

static void
select_word(int forward)
{
	woff_t lo, hi, s;

	sel_range(&lo, &hi);
	if (forward) {
		s = ed_sel ? ed_wordfwd(hi > lo ? hi - 1 : hi) : ed_cur;
		if (ed_sel && hi > lo && !is_wc(buf_at(hi - 1)))
			s = hi;
		while (is_wc(buf_at(s)) && s > 0 && is_wc(buf_at(s - 1)))
			s--;
	} else
		s = ed_wordback(lo);
	ed_anchor = s;
	ed_cur = ed_wordfwd(s);
	if (ed_cur == s && buf_at(s) >= 0)
		ed_cur = s + 1;
	ed_sel = 1;
}

static void
select_para(int forward)
{
	woff_t s = ed_bol(ed_cur), e;

	if (forward && ed_sel) {
		woff_t lo, hi;

		sel_range(&lo, &hi);
		s = hi;
	} else if (!forward && ed_sel && s > 0 && s == ed_anchor)
		s = ed_bol(s - 1);
	e = ed_eol(s);
	if (buf_at(e) == '\n')
		e++;
	ed_anchor = s;
	ed_cur = e;
	ed_sel = 1;
}

static int
alias_key(int k)
{
	int m = k & ~K_BASE, b = k & K_BASE;

	switch (b) {
	case KCTRL('x'): return K_DEL | m;
	case KCTRL('v'): return K_INS | m;
	case KCTRL('t'): return K_HOME | m;
	case KCTRL('e'): return K_END | m;
	case KCTRL('u'): return K_PGUP | m;
	case KCTRL('d'): return K_PGDN | m;
	default: return k;
	}
}

static int
move_key(int k)
{
	int b = k & K_BASE, c = k & KM_CTRL;
	woff_t p = ed_cur, len = buf_len();

	switch (b) {
	case K_LEFT:
		p = c ? ed_wordback(p) : (p > 0 ? p - 1 : 0);
		break;
	case K_RIGHT:
		p = c ? ed_wordfwd(p) : (p < len ? p + 1 : len);
		break;
	case K_UP:
		if (c) {
			p = ed_bol(p);
			if (p == ed_cur && p > 0)
				p = ed_bol(p - 1);
			break;
		}
		ed_vmove(-1);
		return 1;
	case K_DOWN:
		if (c) {
			p = ed_eol(p);
			if (p < len)
				p++;
			break;
		}
		ed_vmove(1);
		return 1;
	case K_HOME:
		p = c ? top : row_start(p);
		break;
	case K_END:
		if (c) {
			int i;

			p = top;
			for (i = 1; i < ed_rows(); i++) {
				woff_t n = row_next(p);

				if (n == WOFF_END)
					break;
				p = n;
			}
			break;
		} else {
			woff_t n = row_next(row_start(p));

			p = n == WOFF_END ? len : n - 1;
		}
		break;
	case K_PGUP:
		if (c)
			p = 0;
		else {
			ed_vmove(-(ed_rows() - 1));
			return 1;
		}
		break;
	case K_PGDN:
		if (c)
			p = len;
		else {
			ed_vmove(ed_rows() - 1);
			return 1;
		}
		break;
	default:
		return 0;
	}
	ed_set(p);
	return 1;
}

static void
word_key(int k)
{
	int b;

	if (pmod != 0 && k != K_ESC) {
		k |= pmod;
		pmod = 0;
	}
	k = alias_key(k);
	b = k & K_BASE;
	if (k == KCTRL('z')) {
		pmod = KM_SHIFT;
		ed_msg(0, "Shift");
		return;
	}
	if (k == KCTRL('a')) {
		pmod = KM_ALT;
		ed_msg(0, "Alt");
		return;
	}
	if (k == KCTRL('c')) {
		pmod = KM_CTRL;
		ed_msg(0, "Ctrl");
		return;
	}
	if (b >= K_UP && b <= K_PGDN && b != K_INS && b != K_DEL) {
		woff_t old = ed_cur;
		int extend = ext || (k & KM_SHIFT);

		if (extend && !ed_sel) {
			ed_anchor = old;
			ed_sel = 1;
		}
		move_key(k);
		if (!extend)
			ed_sel = 0;
		buf_mark();
		return;
	}
	switch (k) {
	case K_ESC:
		pmod = 0;
		ed_menu();
		return;
	case K_F6:
		ext = !ext;
		if (ext && !ed_sel) {
			ed_anchor = ed_cur;
			ed_sel = 1;
		}
		return;
	case K_F6 | KM_SHIFT:
		ed_msg(1, "Column selection is not available");
		return;
	case K_F7:
		select_word(0);
		return;
	case K_F8:
		select_word(1);
		return;
	case K_F9:
		select_para(0);
		return;
	case K_F10:
		select_para(1);
		return;
	case K_F9 | KM_SHIFT: {
		woff_t n;

		ed_anchor = row_start(ed_cur);
		n = row_next(ed_anchor);
		ed_cur = n == WOFF_END ? buf_len() : n;
		ed_sel = 1;
		return;
	}
	case K_F10 | KM_SHIFT:
		ed_anchor = 0;
		ed_cur = buf_len();
		ed_sel = 1;
		return;
	case K_DEL:
		delete_sel(1);
		return;
	case K_DEL | KM_SHIFT:
		delete_sel(0);
		return;
	case K_INS:
		buf_mark();
		ed_paste(ed_cur);
		return;
	case K_F3 | KM_ALT:
		copy_sel();
		return;
	case K_F4:
	case K_F4 | KM_SHIFT:
		ed_search(NULL, !s_up, 1);
		return;
	case K_F5:
		ovr = !ovr;
		return;
	case K_F1 | KM_SHIFT:
		ed_undo();
		return;
	case KCTRL('y'):
		ed_redo();
		return;
	case K_F10 | KM_CTRL:
		if (ed_file[0] != '\0')
			ed_save(ed_file);
		else
			ed_form(F_SAVE);
		return;
	case KCTRL('l'):
		scr_invalidate();
		return;
	case K_BS: {
		woff_t lo, hi;

		buf_mark();
		sel_range(&lo, &hi);
		if (lo < hi)
			ed_del(lo, hi, 0, 0);
		else if (ed_cur > 0)
			ed_del(ed_cur - 1, ed_cur, 0, 0);
		ext = 0;
		return;
	}
	case K_ENTER:
		type_char('\n');
		return;
	case K_TAB:
		type_char('\t');
		return;
	default:
		if (k >= ' ' && k < 0x7f) {
			type_char(k);
			return;
		}
		if ((k & ~K_BASE) && b >= ' ' && b < 0x7f) {
			ed_msg(1, "That key combination is not available");
			return;
		}
	}
}

/* ---- screen composition ---- */

static void
draw_doc(int row0, int rows)
{
	woff_t r = top, n, p, lo, hi, len = buf_len();
	int i, col, c, w, x, attr;

	sel_range(&lo, &hi);
	for (i = 0; i < rows; i++) {
		scr_fill(row0 + i, 0, scr_cols, 0);
		if (r == WOFF_END) {
			if (keymode == KEYS_VI)
				scr_put(row0 + i, 0, '~', A_CLS(C_MARK) | A_BOLD);
			continue;
		}
		n = row_next(r);
		col = 0;
		for (p = r; p < (n == WOFF_END ? len : n); p++) {
			c = buf_at(p);
			if (c == '\n')
				break;
			w = cell_w(c, col);
			attr = (ed_sel && p >= lo && p < hi) ?
			    A_REV | A_CLS(C_SEL) : 0;
			for (x = 0; x < w; x++, col++) {
				int sc = (int)((woff_t)col - (opt_wrap ? 0 : leftcol));

				if (!opt_wrap && (woff_t)col < leftcol)
					continue;
				scr_put(row0 + i, sc, c == '\t' ? ' ' :
				    (c < ' ' || c > '~' ? '?' : c), attr);
			}
		}
		if (ed_sel && p >= lo && p < hi && buf_at(p) == '\n')
			scr_put(row0 + i, col - (int)(opt_wrap ? 0 : leftcol), ' ',
			    A_REV | A_CLS(C_SEL));
		if (n == WOFF_END && keymode == KEYS_WORD)
			scr_put(row0 + i, col - (int)(opt_wrap ? 0 : leftcol),
			    G_DIAMOND, A_CLS(C_MARK));
		r = n;
	}
}

/* Place a token on the command lines, wrapping at a token boundary. */
static int crow, ccol, crow0, crows, cindent;

static void
cmd_put(const char *s, int attr)
{
	int n = (int)strlen(s);

	if (ccol > cindent && ccol + n > scr_cols) {
		crow++;
		ccol = cindent;
	}
	if (crow < crow0 + crows)
		scr_puts(crow, ccol, s, attr, scr_cols - ccol);
	ccol += n;
}

static void
draw_menu(void)
{
	const struct mframe *mf = &mstack[mdepth - 1];
	const struct word_menu *m = word_menu_get((enum word_menu_id)mf->id);
	char t[40];
	uint8_t i;

	snprintf(t, sizeof(t), "%s:", m->title);
	cindent = (int)strlen(t) + 1;
	cmd_put(t, A_BOLD | A_CLS(C_TITLE));
	ccol++;
	for (i = 0; i < m->count; i++) {
		const struct word_menu_item *it = word_menu_item_at(m, i);

		cmd_put(it->name, i == mf->sel ? A_REV | A_CLS(C_SEL) :
		    it->kind == KIND_TODO ? 0 : A_BOLD);
		ccol++;
	}
}

static int fcur_row, fcur_col;

static void
draw_form(void)
{
	const struct form *f = &forms[fid];
	char a[24], t[WORD_FIELD_MAX + 4];
	int i, j;

	cindent = 2;
	cmd_put(f->name, A_BOLD | A_CLS(C_TITLE));
	ccol++;
	for (i = 0; i < f->nf; i++) {
		const struct field *fl = &f->f[i];

		snprintf(t, sizeof(t), "%s:", fl->name);
		cmd_put(t, 0);
		if (fl->type == T_TEXT) {
			snprintf(t, sizeof(t), "%s", fl->buf);
			cmd_put(t, i == fcur ? A_UL | A_BOLD : A_UL);
			if (i == fcur) {
				fcur_row = crow;
				fcur_col = ccol;
			}
			if (t[0] == '\0') {
				cmd_put(" ", i == fcur ? A_UL | A_BOLD : A_UL);
				if (i == fcur) {
					fcur_row = crow;
					fcur_col = ccol - 1;
				}
			}
		} else {
			for (j = 0; j < fl->nch; j++) {
				choice(fl->choices, j, a, sizeof(a));
				if (j == fval[i]) {
					snprintf(t, sizeof(t), "(%s)", a);
					cmd_put(t, i == fcur ? A_REV | A_CLS(C_SEL) : A_BOLD);
					if (i == fcur) {
						fcur_row = crow;
						fcur_col = ccol - 1;
					}
				} else
					cmd_put(a, 0);
			}
		}
		ccol++;
	}
}

static void
scrap_preview(char *out, size_t sz)
{
	woff_t i;
	size_t k = 0;

	for (i = 0; i < scrap_len && k + 4 < sz && i < 8; i++) {
		int c = buf_ref_at(scrap_ref + i);

		out[k++] = c == '\n' ? '|' : (c < ' ' || c > '~' ? '.' : (char)c);
	}
	if (scrap_len > 8 && k + 3 < sz) {
		memcpy(out + k, "...", 3);
		k += 3;
	}
	out[k] = '\0';
}

static const char *const hints_word[2] = {
	"Esc Menu  F6 Extend  Del Cut  Ins Paste  F4 Find",
	"^Z Shift  ^A Alt  ^C Ctrl  ^Y Redo  F5 Overtype"
};
static const char *const hints_vi[2] = {
	"i Insert  v Select  dd Cut  yy Copy  p Paste  u Undo",
	":w Save  :q Quit  / Find  n Next  :menu  :help"
};
static const char *const hints_nano[2] = {
	"^G Help  ^O Write  ^W Where  ^K Cut  ^C Pos  ^Z Undo",
	"^X Exit  ^R Read  ^\\ Replace  ^U Paste  ^_ Line"
};

static void
draw_hints(int row)
{
	const char *const *h = keymode == KEYS_VI ? hints_vi :
	    keymode == KEYS_NANO ? hints_nano : hints_word;
	int i, c;

	/* An entry is a key, one space and its label; two spaces end it. */
	for (i = 0; i < 2; i++) {
		const char *s = h[i];
		int key = 1, sp = 0;

		scr_fill(row + i, 0, scr_cols, 0);
		for (c = 0; *s != '\0' && c < scr_cols; s++, c++) {
			if (*s == ' ') {
				sp++;
				key = 0;
			} else {
				if (sp >= 2)
					key = 1;
				sp = 0;
			}
			scr_put(row + i, c, (unsigned char)*s,
			    key && *s != ' ' ? A_REV | A_CLS(C_KEY) : 0);
		}
	}
}

static void
draw_status(int row)
{
	char left[WORD_PATH_MAX + 128], sp[16];
	woff_t rs = row_start(ed_cur), ln = line_of(ed_cur);

	scrap_preview(sp, sizeof(sp));
	snprintf(left, sizeof(left), "Pg%lu Li%lu Co%d {%s}%s%s %s%s",
	    (unsigned long)(ln / PAGE_LINES + 1), (unsigned long)(ln + 1),
	    col_of(rs, ed_cur) + 1, sp, ext ? " EXT" : "", ovr ? " OVR" : "",
	    ed_file[0] ? ed_file : "(no name)",
	    ed_dirty() ? "*" : "");
	scr_fill(row, 0, scr_cols, A_REV | A_CLS(C_BAR));
	scr_puts(row, 0, left, A_REV | A_CLS(C_BAR), scr_cols);
	if ((int)strlen(left) + 6 < scr_cols)
		scr_puts(row, scr_cols - 5, keymode == KEYS_VI ? "vi" :
		    keymode == KEYS_NANO ? "nano" : "Word", A_REV | A_CLS(C_BAR), 5);
}

static const char *const help_word[] = {
	"WORD KEYS",
	"Esc opens the COMMAND menu; a command's letter runs it.",
	"Arrows, Home End PgUp PgDn move; ^T ^E ^U ^D also.",
	"^Z then a key is Shift+key, ^A Alt+key, ^C Ctrl+key.",
	"Shift+arrow or F6 extends the selection.",
	"F7 F8 select a word, F9 F10 a paragraph,",
	"Shift+F9 the line, Shift+F10 the document.",
	"Del or ^X cuts to the scrap, Shift+Del deletes,",
	"Ins or ^V inserts the scrap, Alt+F3 copies.",
	"F4 finds again, F5 overtype, Shift+F1 undo, ^Y redo.",
	"Ctrl+F10 saves. In a form: Tab moves, Space picks,",
	"F1 lists files, Enter runs, Esc cancels.",
	"Options (Esc O) switches to the vi or nano keys.",
	NULL
};
static const char *const help_vi[] = {
	"VI KEYS",
	"h j k l 0 ^ $ w b e G gg move; ^D ^U ^F ^B scroll.",
	"i a I A o O insert; Esc returns to command mode.",
	"x X dd dw D C cw cc r J yy p P edit; u undo, ^R redo.",
	"v selects, then d x y or c. / ? n N search.",
	":w :q :q! :wq :x ZZ :e file :e! :f :N :set wrap",
	":set nowrap :menu (Word menu) :help. ^G file info.",
	NULL
};
static const char *const help_nano[] = {
	"NANO KEYS",
	"Type to insert. ^O write, ^X exit, ^R insert file.",
	"^W search, ^\\ replace, ^_ go to line, ^C position.",
	"^K cuts the line or selection; repeated cuts add up.",
	"^U pastes. ^^ sets the mark. ^A ^E line ends.",
	"^Y ^V page up and down. ^Z or Esc u undo, Esc e redo.",
	NULL
};

static void
draw_help(int row0, int rows)
{
	const char *const *h = keymode == KEYS_VI ? help_vi :
	    keymode == KEYS_NANO ? help_nano : help_word;
	int i;

	for (i = 0; i < rows; i++) {
		scr_fill(row0 + i, 0, scr_cols, 0);
		if (h[i] == NULL)
			break;
		scr_puts(row0 + i, 0, h[i], i == 0 ? A_BOLD | A_CLS(C_TITLE) : 0,
		    scr_cols);
	}
	for (; i < rows; i++)
		scr_fill(row0 + i, 0, scr_cols, 0);
	if (rows > 1)
		scr_puts(row0 + rows - 1, 0, "Press any key to return.", A_CLS(C_KEY), scr_cols);
}

static void
render(void)
{
	int rows = ed_rows(), r0 = doc_top(), row, cr = 0, cc = 0, bottom;

	scroll_to_cursor();
	scr_clear();
	if (keymode == KEYS_NANO) {
		char t[WORD_PATH_MAX + 32];

		snprintf(t, sizeof(t), "  word  %s%s", ed_file[0] ? ed_file :
		    "New Buffer", ed_dirty() ?
		    "  Modified" : "");
		scr_fill(0, 0, scr_cols, A_REV | A_CLS(C_BAR));
		scr_puts(0, 0, t, A_REV | A_CLS(C_BAR), scr_cols);
	}
	if (ui == U_PICK)
		pick_draw(r0, rows);
	else if (ui == U_HELP)
		draw_help(r0, rows);
	else
		draw_doc(r0, rows);
	row = r0 + rows;
	if (keymode == KEYS_WORD) {
		crow = crow0 = row;
		crows = cmd_lines();
		ccol = 0;
		if (ui == U_FORM)
			draw_form();
		else {
			if (ui != U_MENU)
				mdepth = 1, mstack[0].id = MENU_MAIN, mstack[0].sel = 0;
			draw_menu();
		}
		row += crows;
	}
	scr_puts(row, 0, msg, msg_err ? A_BOLD | A_CLS(C_ERR) : 0, scr_cols);
	if (keymode == KEYS_VI && vi_cmdline() != NULL) {
		scr_fill(row, 0, scr_cols, 0);
		scr_puts(row, 0, vi_cmdline(), 0, scr_cols);
		cr = row;
		cc = (int)strlen(vi_cmdline());
	} else if (keymode != KEYS_WORD && (ui == U_FORM || ui == U_MENU)) {
		/* The vi and nano modes show a menu or form over the last rows. */
		int i;

		crows = cmd_lines();
		crow = crow0 = r0 + rows - crows;
		ccol = 0;
		for (i = 0; i < crows; i++)
			scr_fill(crow0 + i, 0, scr_cols, 0);
		if (ui == U_FORM)
			draw_form();
		else
			draw_menu();
	}
	row++;
	if (hints_on()) {
		draw_hints(row);
		row += 2;
	}
	bottom = row;
	if (keymode != KEYS_NANO && bottom < scr_rows)
		draw_status(bottom);
	if (keymode == KEYS_VI && bottom < scr_rows && vi_state()[0] != '\0')
		scr_puts(bottom, scr_cols - 16, vi_state(),
		    A_REV | A_CLS(C_BAR) | A_BOLD, 10);
	if (ui == U_FORM) {
		cr = fcur_row;
		cc = fcur_col;
	} else if (ui == U_EDIT && !(keymode == KEYS_VI && vi_cmdline())) {
		woff_t rs = row_start(ed_cur), t = top;
		int i = 0;

		while (t != WOFF_END && t < rs && i < rows) {
			t = row_next(t);
			i++;
		}
		cr = r0 + i;
		cc = col_of(rs, ed_cur) - (int)(opt_wrap ? 0 : leftcol);
		if (cc >= scr_cols)
			cc = scr_cols - 1;
	} else if (ui == U_MENU) {
		cr = r0 + rows;
		cc = 0;
	}
	scr_flush(cr, cc);
}

/* ---- the main loop ---- */

static void
dispatch(int k)
{
	char path[WORD_PATH_MAX];
	int r;

	/* Text keys typed in a row form one undo group; any other key ends it. */
	if (ui != U_EDIT || !((k >= ' ' && k < 0x7f) || k == K_ENTER ||
	    k == K_TAB))
		buf_mark();
	switch (ui) {
	case U_MENU:
		menu_key(k);
		return;
	case U_FORM:
		form_key(k);
		return;
	case U_HELP:
		ui = U_EDIT;
		return;
	case U_PICK:
		r = pick_key(k, path, sizeof(path));
		if (r < 0) {
			ui = pick_form >= 0 ? U_FORM : U_EDIT;
			return;
		}
		if (r == 0)
			return;
		if (pick_form >= 0) {
			snprintf(f_name, sizeof(f_name), "%s", path);
			fid = pick_form;
			form_run();
		} else {
			ui = U_EDIT;
			ed_load(path);
		}
		return;
	case U_CONF_REPL:
		replace_key(k);
		return;
	case U_CONF_QUIT:
		r = toupper(k & 0xff);
		ui = U_EDIT;
		if (k == K_ESC)
			ed_msg(0, "Cancelled");
		else if (r == 'Y') {
			if (ed_file[0] != '\0') {
				if (ed_save(ed_file) == 0)
					running = 0;
			} else {
				quit_after = 1;
				ed_form(F_SAVE);
			}
		} else if (r == 'N')
			running = 0;
		else
			ui = U_CONF_QUIT;
		return;
	case U_CONF_CLEAR:
		r = toupper(k & 0xff);
		ui = U_EDIT;
		if (r == 'Y')
			do_clear();
		return;
	default:
		break;
	}
	if (keymode == KEYS_VI)
		vi_key(k);
	else if (keymode == KEYS_NANO)
		nano_key(k);
	else
		word_key(k);
}

static void
usage(void)
{
	fputs("usage: word [-k word|vi|nano] [-W] [-c] [file]\n",
	    stderr);
	exit(2);
}

static int
keys_named(const char *s)
{
	if (strcmp(s, "vi") == 0)
		return KEYS_VI;
	if (strcmp(s, "nano") == 0)
		return KEYS_NANO;
	if (strcmp(s, "word") == 0)
		return KEYS_WORD;
	return -1;
}

int
main(int argc, char **argv)
{
	const char *path = NULL, *env = getenv("WORDKEYS");
	int i, k;

	if (env != NULL && keys_named(env) >= 0)
		keymode = keys_named(env);
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
			if ((keymode = keys_named(argv[++i])) < 0)
				usage();
		} else if (strcmp(argv[i], "-W") == 0)
			opt_wrap = 0;
		else if (strcmp(argv[i], "-c") == 0)
			scr_color = 0;
		else if (argv[i][0] == '-')
			usage();
		else if (path == NULL)
			path = argv[i];
		else
			usage();
	}
	buf_init();
	saved_gen = buf_gen();
	if (path != NULL && strlen(path) >= WORD_PATH_MAX) {
		fputs("word: file name too long\n", stderr);
		return 1;
	}
	if (path != NULL && ed_load(path) != 0) {
		fprintf(stderr, "word: %s: %s\n", path, msg);
		return 1;
	}
	if (path == NULL)
		ed_msg(0, "Type text, or press Esc for the COMMAND menu");
	if (keymode != KEYS_WORD && path == NULL)
		msg[0] = '\0';
	tty_size();
	if (tty_raw() != 0) {
		perror("word: tty");
		return 1;
	}
	vi_reset();
	while (running) {
		render();
		k = key_read();
		if (k < 0)
			break;
		dispatch(k);
	}
	tty_restore();
	return 0;
}
