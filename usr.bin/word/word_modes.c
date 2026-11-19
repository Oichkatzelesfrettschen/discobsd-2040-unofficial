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
 * word_modes.c: the vi and nano key modes (SPEC.md, Modern modes). Both
 * drive the editor core of word.c, so they share its document, scrap,
 * undo groups, forms and file picker. vi follows the commands of this
 * system's vi (usr.bin/stevie) and adds visual selection, redo and :set;
 * nano binds its control keys to the same core commands.
 */

#include "word.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { VI_NORMAL = 0, VI_INSERT, VI_VISUAL, VI_CMD };

static int vst, vcount, vop, vpend;
static char vcmd[WORD_FIELD_MAX];
static size_t vcmdlen;
static char vpat[WORD_FIELD_MAX];
static int vdown = 1;

void
vi_reset(void)
{
	vst = VI_NORMAL;
	vcount = vop = vpend = 0;
	ed_sel = 0;
}

const char *
vi_cmdline(void)
{
	return vst == VI_CMD ? vcmd : NULL;
}

const char *
vi_state(void)
{
	if (keymode != KEYS_VI)
		return "";
	if (vst == VI_INSERT)
		return "-- INSERT --";
	if (vst == VI_VISUAL)
		return "-- VISUAL --";
	return "";
}

static woff_t
first_nonblank(woff_t p)
{
	int c;

	p = ed_bol(p);
	while ((c = buf_at(p)) == ' ' || c == '\t')
		p++;
	return p;
}

/* The end of n lines from the line holding p, past the last newline. */
static woff_t
lines_end(woff_t p, int n)
{
	woff_t len = buf_len();

	while (n-- > 0) {
		p = ed_eol(p);
		if (p < len)
			p++;
		else
			break;
	}
	return p;
}

static void
del_lines(woff_t lo, woff_t hi, int yank_only)
{
	woff_t len;

	ed_yank(lo, hi, 1);
	if (yank_only)
		return;
	ed_del(lo, hi, 0, 0);
	len = buf_len();
	if (lo > 0 && lo == len && buf_at(lo - 1) == '\n') {
		ed_del(lo - 1, lo, 0, 0);
		lo = ed_bol(lo - 1);
	}
	ed_set(first_nonblank(lo > buf_len() ? buf_len() : lo));
}

static void
paste_lines(int after)
{
	woff_t at = after ? lines_end(ed_cur, 1) : ed_bol(ed_cur);
	woff_t len = buf_len();
	int nl_end = after && at == len && (len == 0 || buf_at(len - 1) != '\n');

	if (nl_end) {
		ed_set(at);
		ed_ins("\n", 1);
		at = buf_len();
	}
	ed_paste(at);
	ed_set(at);
}

static void
enter_insert(void)
{
	buf_mark();
	vst = VI_INSERT;
}

/* A motion key moves *p; *lines marks a linewise motion. */
static int
motion(int k, int n, woff_t *p, int *lines)
{
	woff_t q = *p, len = buf_len();
	int i;

	*lines = 0;
	switch (k) {
	case 'h':
	case K_LEFT:
	case K_BS:
		for (i = 0; i < n && q > ed_bol(*p); i++)
			q--;
		break;
	case 'l':
	case K_RIGHT:
	case ' ': {
		/* The cursor rests on the last character; an operator reaches past it. */
		woff_t e = ed_eol(*p), lim = vop || e == ed_bol(*p) ? e : e - 1;

		for (i = 0; i < n && q < lim; i++)
			q++;
		break;
	}
	case 'w':
		for (i = 0; i < n; i++)
			q = ed_wordfwd(q);
		break;
	case 'b':
		for (i = 0; i < n; i++)
			q = ed_wordback(q);
		break;
	case 'e':
		for (i = 0; i < n; i++)
			q = ed_wordend(q);
		break;
	case '0':
	case K_HOME:
		q = ed_bol(q);
		break;
	case '^':
		q = first_nonblank(q);
		break;
	case '$':
	case K_END:
		q = ed_eol(q);
		if (!vop && q > ed_bol(q))
			q--;
		break;
	case 'G':
		q = vcount > 0 ? ed_line_off((woff_t)vcount - 1) :
		    ed_bol(len);
		if (q == len && q > 0 && buf_at(q - 1) == '\n')
			q = ed_bol(q - 1);
		q = first_nonblank(q);
		*lines = 1;
		break;
	case 'j':
	case K_DOWN:
	case K_ENTER:
	case '+':
		for (i = 0; i < n && ed_eol(q) < len; i++)
			q = ed_eol(q) + 1;
		*lines = 1;
		break;
	case 'k':
	case K_UP:
	case '-':
		for (i = 0; i < n && ed_bol(q) > 0; i++)
			q = ed_bol(ed_bol(q) - 1);
		*lines = 1;
		break;
	default:
		return 0;
	}
	*p = q;
	return 1;
}

static void
apply_op(int op, woff_t a, woff_t b, int lines)
{
	woff_t lo = a < b ? a : b, hi = a < b ? b : a;

	buf_mark();
	if (lines) {
		lo = ed_bol(lo);
		hi = lines_end(hi, 1);
		if (op == 'c') {
			woff_t e = hi > lo && buf_at(hi - 1) == '\n' ? hi - 1 : hi;

			ed_yank(lo, e, 0);
			ed_del(lo, e, 0, 0);
			enter_insert();
		} else
			del_lines(lo, hi, op == 'y');
		return;
	}
	if (op == 'y') {
		ed_yank(lo, hi, 0);
		ed_set(lo);
		return;
	}
	ed_yank(lo, hi, 0);
	ed_del(lo, hi, 0, 0);
	if (op == 'c')
		enter_insert();
}

static void
ex_command(const char *c)
{
	char word[16];
	const char *arg;
	size_t n = 0;

	while (*c == ' ')
		c++;
	while (c[n] != '\0' && c[n] != ' ' && n + 1 < sizeof(word)) {
		word[n] = c[n];
		n++;
	}
	word[n] = '\0';
	arg = c + n;
	while (*arg == ' ')
		arg++;
	if (isdigit((unsigned char)word[0])) {
		ed_set(first_nonblank(ed_line_off((woff_t)atol(word) - 1)));
	} else if (strcmp(word, "$") == 0) {
		ed_set(first_nonblank(ed_bol(buf_len())));
	} else if (strcmp(word, "w") == 0) {
		ed_save(arg[0] ? arg : ed_file);
	} else if (strcmp(word, "wq") == 0 || strcmp(word, "x") == 0) {
		if (strcmp(word, "x") == 0 && !ed_dirty())
			running = 0;
		else if (ed_save(arg[0] ? arg : ed_file) == 0)
			running = 0;
	} else if (strcmp(word, "q") == 0) {
		if (ed_dirty())
			ed_msg(1, "No write since last change (:q! overrides)",
			    "Nicht gespeichert (:q! erzwingt)");
		else
			running = 0;
	} else if (strcmp(word, "q!") == 0) {
		ed_quit(1);
	} else if (strcmp(word, "e") == 0 || strcmp(word, "e!") == 0) {
		if (word[1] != '!' && ed_dirty())
			ed_msg(1, "No write since last change (:e! overrides)",
			    "Nicht gespeichert (:e! erzwingt)");
		else if (arg[0] != '\0')
			ed_load(arg);
		else if (word[1] == '!' && ed_file[0] != '\0') {
			char p[WORD_PATH_MAX];

			strcpy(p, ed_file);
			ed_load(p);
		} else if (word[1] != '!') {
			ed_pick();
		}
	} else if (strcmp(word, "f") == 0) {
		if (arg[0] != '\0' && strlen(arg) < WORD_PATH_MAX)
			strcpy(ed_file, arg);
		ed_info();
	} else if (strcmp(word, "set") == 0) {
		if (strcmp(arg, "wrap") == 0)
			ed_set_wrap(1);
		else if (strcmp(arg, "nowrap") == 0)
			ed_set_wrap(0);
		else
			ed_msg(1, "Unknown option", "Unbekannte Option");
	} else if (strcmp(word, "menu") == 0) {
		ed_menu();
	} else if (strcmp(word, "help") == 0) {
		ed_help();
	} else if (word[0] != '\0')
		ed_msg(1, "Not an editor command", "Kein Editorbefehl");
}

static void
cmd_key(int k)
{
	if (k == K_ESC) {
		vst = VI_NORMAL;
		return;
	}
	if (k == K_BS) {
		if (vcmdlen <= 1) {
			vst = VI_NORMAL;
			return;
		}
		vcmd[--vcmdlen] = '\0';
		return;
	}
	if (k == K_ENTER) {
		vst = VI_NORMAL;
		if (vcmd[0] == ':')
			ex_command(vcmd + 1);
		else {
			if (vcmd[1] != '\0')
				strcpy(vpat, vcmd + 1);
			vdown = vcmd[0] == '/';
			ed_sel = 0;
			ed_search(vpat, vdown, 1);
			if (ed_sel) {
				ed_set(ed_anchor);
				ed_sel = 0;
			}
		}
		return;
	}
	if (k >= ' ' && k < 0x7f && vcmdlen + 1 < sizeof(vcmd)) {
		vcmd[vcmdlen++] = (char)k;
		vcmd[vcmdlen] = '\0';
	}
}

static void
insert_key(int k)
{
	char c;

	if (k == K_ESC) {
		vst = VI_NORMAL;
		if (ed_cur > ed_bol(ed_cur))
			ed_set(ed_cur - 1);
		return;
	}
	if (k == K_BS) {
		if (ed_cur > 0)
			ed_del(ed_cur - 1, ed_cur, 0, 0);
		return;
	}
	if (k == K_DEL) {
		if (buf_at(ed_cur) >= 0)
			ed_del(ed_cur, ed_cur + 1, 0, 0);
		return;
	}
	if (k == K_ENTER || k == K_TAB || (k >= ' ' && k < 0x7f)) {
		c = k == K_ENTER ? '\n' : (char)k;
		ed_ins(&c, 1);
		return;
	}
	if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) {
		woff_t p = ed_cur;
		int l;

		buf_mark();
		if (k == K_UP || k == K_DOWN)
			ed_vmove(k == K_UP ? -1 : 1);
		else if (motion(k, 1, &p, &l))
			ed_set(p);
	}
}

void
vi_key(int k)
{
	int n, lines;
	woff_t p;

	if (vst == VI_CMD) {
		cmd_key(k);
		return;
	}
	if (vst == VI_INSERT) {
		insert_key(k);
		return;
	}
	if (vpend == 'r') {
		vpend = 0;
		if (k >= ' ' && k < 0x7f && buf_at(ed_cur) >= 0 &&
		    buf_at(ed_cur) != '\n') {
			char c = (char)k;

			buf_mark();
			ed_del(ed_cur, ed_cur + 1, 0, 0);
			ed_ins(&c, 1);
			ed_set(ed_cur - 1);
		}
		return;
	}
	if (vpend == 'g' || vpend == 'Z') {
		int pend = vpend;

		vpend = 0;
		if (pend == 'g' && k == 'g') {
			ed_set(first_nonblank(vcount > 0 ?
			    ed_line_off((woff_t)vcount - 1) : 0));
			vcount = 0;
		} else if (pend == 'Z' && k == 'Z')
			ex_command("x");
		return;
	}
	if (k >= '1' && k <= '9' && vcount < 100000) {
		vcount = vcount * 10 + (k - '0');
		return;
	}
	if (k == '0' && vcount > 0) {
		vcount *= 10;
		return;
	}
	n = vcount > 0 ? vcount : 1;
	p = ed_cur;
	if (vop == 'c' && k == 'w') {
		/* cw changes to the end of the word, as ce does. */
		vop = 0;
		vcount = 0;
		while (n-- > 0)
			p = ed_wordend(p);
		apply_op('c', ed_cur, p + 1, 0);
		return;
	}
	if (vop == 0 && (k == 'j' || k == 'k' || k == K_DOWN || k == K_UP)) {
		/* Without an operator j and k keep the column, by display row. */
		ed_vmove(k == 'j' || k == K_DOWN ? n : -n);
		vcount = 0;
		return;
	}
	if (motion(k, n, &p, &lines)) {
		if (vop != 0) {
			int op = vop;

			vop = 0;
			if (k == 'e')
				p++;
			apply_op(op, ed_cur, p, lines);
		} else
			ed_set(p);
		vcount = 0;
		return;
	}
	if (vst == VI_VISUAL) {
		woff_t lo = ed_anchor < ed_cur ? ed_anchor : ed_cur;
		woff_t hi = (ed_anchor < ed_cur ? ed_cur : ed_anchor) + 1;

		if (hi > buf_len())
			hi = buf_len();
		vcount = 0;
		switch (k) {
		case K_ESC:
		case 'v':
			vst = VI_NORMAL;
			ed_sel = 0;
			return;
		case 'd':
		case 'x':
		case 'y':
		case 'c':
			vst = VI_NORMAL;
			ed_sel = 0;
			apply_op(k == 'x' ? 'd' : k, lo, hi, 0);
			return;
		default:
			return;
		}
	}
	if (vop != 0) {
		int op = vop;

		vop = 0;
		vcount = 0;
		if (k == op)
			apply_op(op, ed_cur, lines_end(ed_cur, n) > ed_cur ?
			    lines_end(ed_cur, n) - 1 : ed_cur, 1);
		return;
	}
	vcount = 0;
	switch (k) {
	case 'd':
	case 'c':
	case 'y':
		vop = k;
		return;
	case 'g':
	case 'Z':
	case 'r':
		vpend = k;
		vcount = n > 1 ? n : 0;
		return;
	case 'x':
		apply_op('d', ed_cur, ed_cur + (woff_t)n > ed_eol(ed_cur) ?
		    ed_eol(ed_cur) : ed_cur + (woff_t)n, 0);
		return;
	case 'X':
		apply_op('d', ed_cur > ed_bol(ed_cur) + (woff_t)n ?
		    ed_cur - (woff_t)n : ed_bol(ed_cur), ed_cur, 0);
		return;
	case 'D':
		apply_op('d', ed_cur, ed_eol(ed_cur), 0);
		return;
	case 'C':
		apply_op('c', ed_cur, ed_eol(ed_cur), 0);
		return;
	case 'Y':
		apply_op('y', ed_cur, ed_cur, 1);
		return;
	case 'J':
		p = ed_eol(ed_cur);
		if (buf_at(p) == '\n') {
			woff_t q = p + 1;
			int c;

			while ((c = buf_at(q)) == ' ' || c == '\t')
				q++;
			buf_mark();
			ed_del(p, q, 0, 0);
			ed_ins(" ", 1);
			ed_set(p);
		}
		return;
	case 'p':
	case 'P':
		buf_mark();
		if (scrap_lines)
			paste_lines(k == 'p');
		else
			ed_paste(k == 'p' && buf_at(ed_cur) >= 0 &&
			    buf_at(ed_cur) != '\n' ? ed_cur + 1 : ed_cur);
		return;
	case 'i':
		enter_insert();
		return;
	case 'a':
		if (buf_at(ed_cur) >= 0 && buf_at(ed_cur) != '\n')
			ed_set(ed_cur + 1);
		enter_insert();
		return;
	case 'I':
		ed_set(first_nonblank(ed_cur));
		enter_insert();
		return;
	case 'A':
		ed_set(ed_eol(ed_cur));
		enter_insert();
		return;
	case 'o':
		enter_insert();
		ed_set(ed_eol(ed_cur));
		ed_ins("\n", 1);
		return;
	case 'O':
		enter_insert();
		ed_set(ed_bol(ed_cur));
		ed_ins("\n", 1);
		ed_set(ed_cur - 1);
		return;
	case 'v':
		vst = VI_VISUAL;
		ed_anchor = ed_cur;
		ed_sel = 1;
		return;
	case 'u':
		ed_undo();
		return;
	case KCTRL('r'):
		ed_redo();
		return;
	case ':':
	case '/':
	case '?':
		vst = VI_CMD;
		vcmd[0] = (char)k;
		vcmd[1] = '\0';
		vcmdlen = 1;
		return;
	case 'n':
	case 'N':
		ed_sel = 0;
		ed_search(vpat, k == 'n' ? vdown : !vdown, 1);
		if (ed_sel) {
			ed_set(ed_anchor);
			ed_sel = 0;
		}
		return;
	case KCTRL('d'):
		ed_vmove(ed_rows() / 2);
		return;
	case KCTRL('u'):
		ed_vmove(-ed_rows() / 2);
		return;
	case KCTRL('f'):
	case K_PGDN:
		ed_vmove(ed_rows() - 1);
		return;
	case KCTRL('b'):
	case K_PGUP:
		ed_vmove(-(ed_rows() - 1));
		return;
	case KCTRL('g'):
		ed_info();
		return;
	case KCTRL('l'):
		scr_invalidate();
		return;
	default:
		return;
	}
}

/* ---- nano ---- */

static int nano_cut_run;

void
nano_key(int k)
{
	int cut_run = nano_cut_run;
	woff_t lo, hi;

	nano_cut_run = 0;
	if (ed_sel) {
		lo = ed_anchor < ed_cur ? ed_anchor : ed_cur;
		hi = ed_anchor < ed_cur ? ed_cur : ed_anchor;
	} else
		lo = hi = ed_cur;
	switch (k) {
	case KCTRL('g'):
		ed_help();
		return;
	case KCTRL('x'):
		ed_quit(0);
		return;
	case KCTRL('o'):
		ed_form(F_SAVE);
		return;
	case KCTRL('r'):
		ed_form(F_MERGE);
		return;
	case KCTRL('t'):
		ed_pick();
		return;
	case KCTRL('w'):
		ed_form(F_SEARCH);
		return;
	case 0x1c:
		ed_form(F_REPLACE);
		return;
	case 0x1f:
		ed_form(F_LINE);
		return;
	case KCTRL('c'):
		ed_info();
		return;
	case 0x1e:
		ed_sel = !ed_sel;
		ed_anchor = ed_cur;
		ed_msg(0, ed_sel ? "Mark set" : "Mark unset",
		    ed_sel ? "Markierung gesetzt" : "Markierung entfernt");
		return;
	case KCTRL('k'):
		if (lo == hi) {
			lo = ed_bol(ed_cur);
			hi = lines_end(ed_cur, 1);
		}
		if (!cut_run)
			buf_mark();
		ed_del(lo, hi, 1, cut_run);
		scrap_lines = 0;
		nano_cut_run = 1;
		return;
	case KCTRL('u'):
		buf_mark();
		ed_paste(ed_cur);
		return;
	case KCTRL('z'):
	case KM_ALT | 'u':
		ed_undo();
		return;
	case KM_ALT | 'e':
		ed_redo();
		return;
	case KCTRL('a'):
	case K_HOME:
		ed_set(ed_bol(ed_cur));
		return;
	case KCTRL('e'):
	case K_END:
		ed_set(ed_eol(ed_cur));
		return;
	case KCTRL('y'):
	case K_PGUP:
		ed_vmove(-(ed_rows() - 1));
		return;
	case KCTRL('v'):
	case K_PGDN:
		ed_vmove(ed_rows() - 1);
		return;
	case K_UP:
		ed_vmove(-1);
		buf_mark();
		return;
	case K_DOWN:
		ed_vmove(1);
		buf_mark();
		return;
	case K_LEFT:
		if (ed_cur > 0)
			ed_set(ed_cur - 1);
		buf_mark();
		return;
	case K_RIGHT:
		if (buf_at(ed_cur) >= 0)
			ed_set(ed_cur + 1);
		buf_mark();
		return;
	case K_BS:
		buf_mark();
		if (lo < hi)
			ed_del(lo, hi, 0, 0);
		else if (ed_cur > 0)
			ed_del(ed_cur - 1, ed_cur, 0, 0);
		return;
	case KCTRL('d'):
	case K_DEL:
		buf_mark();
		if (buf_at(ed_cur) >= 0)
			ed_del(ed_cur, ed_cur + 1, 0, 0);
		return;
	case KCTRL('l'):
		scr_invalidate();
		return;
	default:
		break;
	}
	if (k == K_ENTER || k == K_TAB || (k >= ' ' && k < 0x7f)) {
		char ch = k == K_ENTER ? '\n' : (char)k;

		ed_sel = 0;
		ed_ins(&ch, 1);
	}
}
