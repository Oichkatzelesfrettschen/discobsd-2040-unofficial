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
 * word: a clean-room word processor in the command model of Word 5.1
 * for UNIX (SPEC.md), with bounded static storage for the 192 KiB
 * RP2040 process window.
 */

#include "word.h"

#ifdef TERMIOS
#include <termios.h>
#else
#include <sgtty.h>
#endif

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
	KEY_NONE = 0,
	KEY_CTRL_K = 11,
	KEY_CTRL_L = 12,
	KEY_ENTER = 13,
	KEY_CTRL_Q = 17,
	KEY_CTRL_S = 19,
	KEY_CTRL_Z = 26,
	KEY_ESC = 27,
	KEY_BACKSPACE = 127,
	KEY_UP = 1000,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_SHIFT_UP,
	KEY_SHIFT_DOWN,
	KEY_SHIFT_LEFT,
	KEY_SHIFT_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_PGUP,
	KEY_PGDN,
	KEY_DEL,
	KEY_F4,
	KEY_F6,
	KEY_F9
};

enum word_mode {
	MODE_EDIT = 0,
	MODE_MENU,
	MODE_PROMPT_LOAD,
	MODE_PROMPT_SAVE,
	MODE_PROMPT_JUMP,
	MODE_PROMPT_SEARCH,
	MODE_PROMPT_REPL_FIND,
	MODE_PROMPT_REPL_WITH,
	MODE_CONFIRM_REPL,
	MODE_CONFIRM_QUIT,
	MODE_CONFIRM_CLEAR,
	MODE_HELP
};

struct menu_frame {
	enum word_menu_id id;
	uint8_t sel;
};

/* Static storage: bounded BSS (~69 KiB) for the 192 KiB RP2040 window. */
static char doc[WORD_DOC_CAP];
static size_t doc_len;
static size_t cursor;
static int has_anchor;
static size_t anchor;
static int dirty;

static char undo_doc[WORD_DOC_CAP];
static size_t undo_len;
static size_t undo_cursor;
static int undo_valid;

static char scrap[WORD_SCRAP_CAP];
static size_t scrap_len;

static char filename[WORD_PATH_MAX];
static char status_msg[WORD_MSG_MAX];

static char prompt_buf[WORD_PROMPT_MAX];
static size_t prompt_len;

static char search_text[WORD_PROMPT_MAX];
static char replace_text[WORD_PROMPT_MAX];
static int opt_case;
static int opt_whole;
static int opt_down = 1;
static int opt_confirm = 1;
static size_t repl_count;
static struct word_match cur_match;
static struct word_pattern cur_pat;

static enum word_lang lang = LANG_EN;
static enum word_mode mode = MODE_EDIT;
static struct menu_frame menu_stack[WORD_MENU_DEPTH];
static uint8_t menu_depth = 1;

static int scr_rows = 24;
static int scr_cols = 80;
static size_t top_line;
static int running = 1;

/* ======================= Terminal raw mode ======================= */

static int rawmode;
#ifdef TERMIOS
static struct termios orig_termios;
#else
static struct sgttyb orig_sgttyb;
static struct tchars orig_tchars;
static struct ltchars orig_ltchars;
#endif

static void
disable_raw_mode(int fd)
{
	if (!rawmode)
		return;
#ifdef TERMIOS
	tcsetattr(fd, TCSAFLUSH, &orig_termios);
#else
	ioctl(fd, TIOCSETP, &orig_sgttyb);
	ioctl(fd, TIOCSETC, &orig_tchars);
	ioctl(fd, TIOCSLTC, &orig_ltchars);
#endif
	rawmode = 0;
}

static void
at_exit_restore(void)
{
	disable_raw_mode(STDIN_FILENO);
	printf("\x1b[0m\x1b[?25h\x1b[%d;1H\n", scr_rows);
	fflush(stdout);
}

static void
query_winsize(int fd)
{
#ifdef TIOCGWINSZ
	struct winsize ws;
	if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 10 && ws.ws_col >= 24) {
		scr_rows = (int)ws.ws_row;
		scr_cols = (int)ws.ws_col;
	}
#else
	(void)fd;
#endif
}

static int
enable_raw_mode(int fd)
{
	if (rawmode)
		return 0;
	if (!isatty(fd)) {
		errno = ENOTTY;
		return -1;
	}
	atexit(at_exit_restore);

#ifdef TERMIOS
	struct termios raw;
	if (tcgetattr(fd, &orig_termios) == -1)
		return -1;
	raw = orig_termios;
	raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(tcflag_t)(OPOST);
	raw.c_cflag |= (tcflag_t)(CS8);
	raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
		return -1;
#else
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars ltc;

	if (ioctl(fd, TIOCGETP, &orig_sgttyb) == -1)
		return -1;
	if (ioctl(fd, TIOCGETC, &orig_tchars) == -1)
		return -1;
	if (ioctl(fd, TIOCGLTC, &orig_ltchars) == -1)
		return -1;

	sg = orig_sgttyb;
	sg.sg_flags &= ~(ECHO | CRMOD | XTABS | RAW);
	sg.sg_flags |= CBREAK;
	if (ioctl(fd, TIOCSETP, &sg) == -1)
		return -1;

	tc = orig_tchars;
	tc.t_intrc = -1;
	tc.t_quitc = -1;
	tc.t_startc = -1;
	tc.t_stopc = -1;
	tc.t_eofc = -1;
	ioctl(fd, TIOCSETC, &tc);

	ltc = orig_ltchars;
	ltc.t_suspc = -1;
	ltc.t_dsuspc = -1;
	ltc.t_rprntc = -1;
	ltc.t_flushc = -1;
	ltc.t_werasc = -1;
	ltc.t_lnextc = -1;
	ioctl(fd, TIOCSLTC, &ltc);
#endif
	rawmode = 1;
	query_winsize(fd);
	return 0;
}

static int
read_byte_nonblock(int fd, unsigned char *out)
{
	int avail = 0;

	if (ioctl(fd, FIONREAD, &avail) == 0 && avail > 0)
		return read(fd, out, 1) == 1 ? 1 : 0;
	return 0;
}

static int
read_key(int fd)
{
	unsigned char c = 0;
	ssize_t n;

	for (;;) {
		n = read(fd, &c, 1);
		if (n == 1)
			break;
		if (n == 0)
			return KEY_NONE;
		if (errno != EINTR)
			return KEY_NONE;
	}

	if (c == '\r' || c == '\n')
		return KEY_ENTER;
	if (c == 8 || c == 127)
		return KEY_BACKSPACE;
	if (c != KEY_ESC)
		return (int)c;

	/* Disambiguate bare ESC from CSI/SS3 escape sequences. */
	unsigned char seq[6];
	size_t slen = 0;
	if (!read_byte_nonblock(fd, &seq[0])) {
		/* Brief yield for slow serial links before deciding bare ESC. */
#ifdef TERMIOS
		struct termios t;
		if (tcgetattr(fd, &t) == 0) {
			t.c_cc[VMIN] = 0;
			t.c_cc[VTIME] = 1;
			tcsetattr(fd, TCSANOW, &t);
			n = read(fd, &seq[0], 1);
			t.c_cc[VMIN] = 1;
			t.c_cc[VTIME] = 0;
			tcsetattr(fd, TCSANOW, &t);
			if (n != 1)
				return KEY_ESC;
		} else {
			return KEY_ESC;
		}
#else
		return KEY_ESC;
#endif
	}
	slen = 1;

	if (seq[0] == 'O') {
		if (read_byte_nonblock(fd, &seq[1]) || read(fd, &seq[1], 1) == 1) {
			switch (seq[1]) {
			case 'H': return KEY_HOME;
			case 'F': return KEY_END;
			case 'S': return KEY_F4;
			default:  return KEY_ESC;
			}
		}
		return KEY_ESC;
	}

	if (seq[0] != '[')
		return KEY_ESC;

	while (slen < sizeof(seq)) {
		if (read(fd, &seq[slen], 1) != 1)
			break;
		unsigned char b = seq[slen++];
		if ((b >= 'A' && b <= 'Z') || b == '~')
			break;
	}

	if (slen == 2) {
		switch (seq[1]) {
		case 'A': return KEY_UP;
		case 'B': return KEY_DOWN;
		case 'C': return KEY_RIGHT;
		case 'D': return KEY_LEFT;
		case 'H': return KEY_HOME;
		case 'F': return KEY_END;
		default:  return KEY_ESC;
		}
	}

	if (slen == 3 && seq[2] == '~') {
		switch (seq[1]) {
		case '1': case '7': return KEY_HOME;
		case '3': return KEY_DEL;
		case '4': case '8': return KEY_END;
		case '5': return KEY_PGUP;
		case '6': return KEY_PGDN;
		default:  return KEY_ESC;
		}
	}

	if (slen == 4 && seq[3] == '~') {
		if (seq[1] == '1' && seq[2] == '4') return KEY_F4;
		if (seq[1] == '1' && seq[2] == '7') return KEY_F6;
		if (seq[1] == '2' && seq[2] == '0') return KEY_F9;
	}

	if (slen == 5 && seq[1] == '1' && seq[2] == ';' && seq[3] == '2') {
		switch (seq[4]) {
		case 'A': return KEY_SHIFT_UP;
		case 'B': return KEY_SHIFT_DOWN;
		case 'C': return KEY_SHIFT_RIGHT;
		case 'D': return KEY_SHIFT_LEFT;
		default:  return KEY_ESC;
		}
	}

	return KEY_ESC;
}

/* ======================= Document helpers ======================= */

static void
set_msg(const char *s)
{
	snprintf(status_msg, sizeof(status_msg), "%s", s);
}

static void
undo_snapshot(void)
{
	memcpy(undo_doc, doc, doc_len);
	undo_len = doc_len;
	undo_cursor = cursor;
	undo_valid = 1;
}

static void
do_undo(void)
{
	size_t i, mlen, tlen, tcur;

	if (!undo_valid) {
		set_msg(lang == LANG_DE ? "Nichts zum Rueckgaengigmachen"
		    : "Nothing to undo");
		return;
	}
	mlen = (doc_len > undo_len) ? doc_len : undo_len;
	for (i = 0; i < mlen; i++) {
		char t = doc[i];
		doc[i] = undo_doc[i];
		undo_doc[i] = t;
	}
	tlen = doc_len;
	doc_len = undo_len;
	undo_len = tlen;
	tcur = cursor;
	cursor = undo_cursor;
	undo_cursor = tcur;
	has_anchor = 0;
	dirty = 1;
	set_msg(lang == LANG_DE ? "Rueckgaengig ausgefuehrt" : "Undone");
}

static void
sel_bounds(size_t *lo, size_t *hi)
{
	if (!has_anchor) {
		*lo = cursor;
		*hi = cursor;
		return;
	}
	if (anchor <= cursor) {
		*lo = anchor;
		*hi = cursor;
	} else {
		*lo = cursor;
		*hi = anchor;
	}
}

static int
doc_insert_bytes(size_t pos, const char *data, size_t n)
{
	if (n == 0)
		return 1;
	if (pos > doc_len || doc_len + n > WORD_DOC_CAP) {
		set_msg(lang == LANG_DE ? "Dokumentpuffer voll"
		    : "Document buffer full");
		return 0;
	}
	memmove(doc + pos + n, doc + pos, doc_len - pos);
	memcpy(doc + pos, data, n);
	doc_len += n;
	dirty = 1;
	return 1;
}

static void
doc_delete_range(size_t lo, size_t hi)
{
	if (lo >= hi || hi > doc_len)
		return;
	memmove(doc + lo, doc + hi, doc_len - hi);
	doc_len -= (hi - lo);
	if (cursor > hi)
		cursor -= (hi - lo);
	else if (cursor > lo)
		cursor = lo;
	has_anchor = 0;
	dirty = 1;
}

static size_t
line_start_of(size_t pos)
{
	while (pos > 0 && doc[pos - 1] != '\n')
		pos--;
	return pos;
}

static size_t
line_end_of(size_t pos)
{
	while (pos < doc_len && doc[pos] != '\n')
		pos++;
	return pos;
}

static void
cursor_line_col(size_t pos, size_t *line_out, size_t *col_out)
{
	size_t line = 0;
	size_t col = 0;
	size_t i;

	for (i = 0; i < pos && i < doc_len; i++) {
		if (doc[i] == '\n') {
			line++;
			col = 0;
		} else {
			col++;
		}
	}
	*line_out = line;
	*col_out = col;
}

static size_t
offset_for_line(size_t target_line)
{
	size_t line = 0;
	size_t i;

	if (target_line == 0)
		return 0;
	for (i = 0; i < doc_len; i++) {
		if (doc[i] == '\n') {
			line++;
			if (line == target_line)
				return i + 1;
		}
	}
	return doc_len;
}

static void
move_vertical(int delta, int extend_sel)
{
	size_t line, col, target_line, ls, le, nlen;

	if (extend_sel && !has_anchor) {
		has_anchor = 1;
		anchor = cursor;
	}

	cursor_line_col(cursor, &line, &col);
	if (delta < 0) {
		size_t back = (size_t)(-delta);
		target_line = (line > back) ? (line - back) : 0;
	} else {
		target_line = line + (size_t)delta;
	}

	ls = offset_for_line(target_line);
	le = line_end_of(ls);
	nlen = le - ls;
	cursor = ls + (col < nlen ? col : nlen);
}

static void
move_horizontal(int delta, int extend_sel)
{
	if (extend_sel && !has_anchor) {
		has_anchor = 1;
		anchor = cursor;
	}

	if (delta < 0) {
		if (cursor > 0)
			cursor--;
	} else if (delta > 0) {
		if (cursor < doc_len)
			cursor++;
	}
}

/* ======================= File I/O ======================= */

static int
file_load(const char *path)
{
	int fd;
	ssize_t n;
	size_t total = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		snprintf(status_msg, sizeof(status_msg),
		    lang == LANG_DE ? "Laden fehlgeschlagen: %s"
		    : "Load failed: %s", strerror(errno));
		return 0;
	}
	undo_snapshot();
	while (total < WORD_DOC_CAP) {
		n = read(fd, doc + total, WORD_DOC_CAP - total);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			snprintf(status_msg, sizeof(status_msg),
			    "Read error: %s", strerror(errno));
			return 0;
		}
		if (n == 0)
			break;
		total += (size_t)n;
	}
	close(fd);
	doc_len = total;
	cursor = 0;
	top_line = 0;
	has_anchor = 0;
	dirty = 0;
	snprintf(filename, sizeof(filename), "%s", path);
	snprintf(status_msg, sizeof(status_msg),
	    lang == LANG_DE ? "Geladen: %s (%lu Bytes)"
	    : "Loaded: %s (%lu bytes)", path, (unsigned long)doc_len);
	return 1;
}

static int
file_save(const char *path)
{
	int fd;
	size_t written = 0;
	ssize_t n;

	if (path[0] == '\0') {
		set_msg(lang == LANG_DE ? "Kein Dateiname angegeben"
		    : "No filename specified");
		return 0;
	}
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		snprintf(status_msg, sizeof(status_msg),
		    lang == LANG_DE ? "Speichern fehlgeschlagen: %s"
		    : "Save failed: %s", strerror(errno));
		return 0;
	}
	while (written < doc_len) {
		n = write(fd, doc + written, doc_len - written);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			snprintf(status_msg, sizeof(status_msg),
			    "Write error: %s", strerror(errno));
			return 0;
		}
		written += (size_t)n;
	}
	if (close(fd) != 0) {
		snprintf(status_msg, sizeof(status_msg),
		    "Close error: %s", strerror(errno));
		return 0;
	}
	dirty = 0;
	snprintf(filename, sizeof(filename), "%s", path);
	snprintf(status_msg, sizeof(status_msg),
	    lang == LANG_DE ? "Gespeichert: %s (%lu Bytes)"
	    : "Saved: %s (%lu bytes)", path, (unsigned long)doc_len);
	return 1;
}

/* ======================= Scrap & Search/Replace ======================= */

static void
do_copy(void)
{
	size_t lo, hi, n;

	sel_bounds(&lo, &hi);
	if (lo == hi) {
		/* No selection: copy current character if present. */
		if (cursor < doc_len)
			hi = cursor + 1;
		else {
			set_msg(lang == LANG_DE ? "Keine Auswahl" : "No selection");
			return;
		}
	}
	n = hi - lo;
	if (n > WORD_SCRAP_CAP)
		n = WORD_SCRAP_CAP;
	memcpy(scrap, doc + lo, n);
	scrap_len = n;
	has_anchor = 0;
	snprintf(status_msg, sizeof(status_msg),
	    lang == LANG_DE ? "%lu Zeichen in Papierkorb kopiert"
	    : "Copied %lu chars to scrap", (unsigned long)n);
}

static void
do_delete_to_scrap(void)
{
	size_t lo, hi, n;

	sel_bounds(&lo, &hi);
	if (lo == hi) {
		if (cursor < doc_len)
			hi = cursor + 1;
		else {
			set_msg(lang == LANG_DE ? "Nichts zu loeschen"
			    : "Nothing to delete");
			return;
		}
	}
	undo_snapshot();
	n = hi - lo;
	if (n > WORD_SCRAP_CAP)
		n = WORD_SCRAP_CAP;
	memcpy(scrap, doc + lo, n);
	scrap_len = n;
	doc_delete_range(lo, hi);
	snprintf(status_msg, sizeof(status_msg),
	    lang == LANG_DE ? "%lu Zeichen in Papierkorb geloescht"
	    : "Deleted %lu chars to scrap", (unsigned long)n);
}

static void
do_insert_from_scrap(void)
{
	if (scrap_len == 0) {
		set_msg(lang == LANG_DE ? "Papierkorb ist leer"
		    : "Scrap is empty");
		return;
	}
	undo_snapshot();
	if (doc_insert_bytes(cursor, scrap, scrap_len)) {
		cursor += scrap_len;
		has_anchor = 0;
		snprintf(status_msg, sizeof(status_msg),
		    lang == LANG_DE ? "%lu Zeichen aus Papierkorb eingefuegt"
		    : "Inserted %lu chars from scrap", (unsigned long)scrap_len);
	}
}

static void
run_search(int advance_if_selected)
{
	size_t from = cursor;
	size_t lo, hi;

	if (search_text[0] == '\0') {
		set_msg(lang == LANG_DE ? "Kein Suchtext" : "No search text");
		return;
	}
	word_pat_compile(&cur_pat, search_text, opt_case, opt_whole);
	if (advance_if_selected && has_anchor) {
		sel_bounds(&lo, &hi);
		from = opt_down ? hi : lo;
	}
	if (word_pat_find(&cur_pat, doc, doc_len, from, opt_down, &cur_match)) {
		anchor = cur_match.start;
		cursor = cur_match.end;
		has_anchor = 1;
		set_msg(lang == LANG_DE ? "Suchtext gefunden (F4 weiter)"
		    : "Match found (F4 repeat)");
	} else {
		set_msg(lang == LANG_DE ? "Suchtext nicht gefunden"
		    : "Search text not found");
	}
}

static void
replace_step(size_t from)
{
	word_pat_compile(&cur_pat, search_text, opt_case, opt_whole);
	while (word_pat_find(&cur_pat, doc, doc_len, from, 1, &cur_match)) {
		anchor = cur_match.start;
		cursor = cur_match.end;
		has_anchor = 1;
		if (opt_confirm) {
			mode = MODE_CONFIRM_REPL;
			set_msg(lang == LANG_DE
			    ? "Ersetzen? (Y=Ja N=Nein A=Alle Esc=Abbruch)"
			    : "Replace? (Y=Yes N=No A=All Esc=Stop)");
			return;
		}
		size_t rlen = strlen(replace_text);
		doc_delete_range(cur_match.start, cur_match.end);
		if (!doc_insert_bytes(cur_match.start, replace_text, rlen)) {
			mode = MODE_EDIT;
			return;
		}
		cursor = cur_match.start + rlen;
		has_anchor = 0;
		repl_count++;
		from = cursor;
	}
	mode = MODE_EDIT;
	snprintf(status_msg, sizeof(status_msg),
	    lang == LANG_DE ? "%lu Ersetzungen ausgefuehrt"
	    : "%lu replacements made", (unsigned long)repl_count);
}

/* ======================= Screen rendering ======================= */

static void
render_screen(void)
{
	int view_rows = scr_rows - 4;
	size_t cur_line, cur_col, lo, hi;
	int r, c;
	size_t line_idx, pos;

	if (view_rows < 1)
		view_rows = 1;

	cursor_line_col(cursor, &cur_line, &cur_col);
	if (cur_line < top_line)
		top_line = cur_line;
	if (cur_line >= top_line + (size_t)view_rows)
		top_line = cur_line - (size_t)view_rows + 1;

	sel_bounds(&lo, &hi);

	/* Hide cursor while redrawing to avoid flicker. */
	fputs("\x1b[?25l\x1b[H", stdout);

	if (mode == MODE_HELP) {
		static const char *const help_lines[] = {
			"WORD 5.1 FOR DISCOBSD (CLEAN-ROOM C17 PORT)",
			"",
			"  Esc            Enter/exit COMMAND menu (or back out)",
			"  Arrow/Home/End Move cursor; Shift+Arrow or Ctrl-K selects",
			"  F6 / Ctrl-K    Toggle selection anchor (for Copy/Delete)",
			"  F4             Repeat last Search",
			"  F9 / Ctrl-L    Toggle English / Deutsch menus live",
			"  Ctrl-S         Save file   |  Ctrl-Q  Quit",
			"  Ctrl-Z         Undo last edit",
			"",
			"COMMAND MENU: Esc then press highlighted letter or Enter:",
			"  C Copy   D Delete (to scrap)   I Insert (from scrap)",
			"  S Search (? = 1 char, ^w = spaces, ^t = tab, ^p = newline)",
			"  R Replace (Y/N/A)   T Transfer (L Load, S Save, C Clear)",
			"  J Jump -> P Page/Line   U Undo   Q Quit",
			"",
			"Press Esc or Enter to return to document."
		};
		size_t nh = sizeof(help_lines) / sizeof(help_lines[0]);
		for (r = 0; r < view_rows; r++) {
			fputs("\x1b[0m\x1b[K", stdout);
			if ((size_t)r < nh)
				printf("%.*s", scr_cols, help_lines[r]);
			fputs("\r\n", stdout);
		}
	} else {
		pos = offset_for_line(top_line);
		for (r = 0; r < view_rows; r++) {
			int in_rev = 0;
			fputs("\x1b[0m\x1b[K", stdout);
			c = 0;
			while (pos < doc_len && doc[pos] != '\n') {
				int want_rev = (has_anchor && pos >= lo && pos < hi);
				if (want_rev != in_rev) {
					fputs(want_rev ? "\x1b[7m" : "\x1b[0m", stdout);
					in_rev = want_rev;
				}
				unsigned char ch = (unsigned char)doc[pos];
				if (ch == '\t') {
					int sp = 4 - (c % 4);
					while (sp-- > 0 && c < scr_cols) {
						putchar(' ');
						c++;
					}
				} else if (c < scr_cols) {
					putchar(isprint(ch) ? (int)ch : '.');
					c++;
				}
				pos++;
			}
			if (in_rev)
				fputs("\x1b[0m", stdout);
			if (pos < doc_len && doc[pos] == '\n') {
				pos++;
			} else if (pos == doc_len) {
				if (c + 2 <= scr_cols)
					fputs("\x1b[2m<>\x1b[0m", stdout);
				pos++; /* sentinel so <> draws only once */
			}
			fputs("\r\n", stdout);
		}
	}

	/* Row scr_rows - 3: COMMAND menu bar */
	{
		struct menu_frame *mf = &menu_stack[menu_depth - 1];
		const struct word_menu *m = word_menu_get(mf->id);
		const char *title = (lang == LANG_DE) ? m->de_title : m->en_title;
		int col = 0;
		uint8_t i;

		fputs("\x1b[0m\x1b[K", stdout);
		if (mode == MODE_MENU)
			fputs("\x1b[1m", stdout);
		printf("%s: ", title);
		col = (int)strlen(title) + 2;
		fputs("\x1b[0m", stdout);

		for (i = 0; i < m->count; i++) {
			const struct word_menu_item *it = word_menu_item_at(m, lang, i);
			const char *lbl = (lang == LANG_DE) ? it->de : it->en;
			int len = (int)strlen(lbl);
			if (col + len + 1 > scr_cols)
				break;
			if (mode == MODE_MENU && i == mf->sel)
				fputs("\x1b[7m", stdout);
			printf("%s", lbl);
			if (mode == MODE_MENU && i == mf->sel)
				fputs("\x1b[0m", stdout);
			putchar(' ');
			col += len + 1;
		}
		fputs("\r\n", stdout);
	}

	/* Row scr_rows - 2: Message / active prompt */
	fputs("\x1b[0m\x1b[K", stdout);
	switch (mode) {
	case MODE_PROMPT_LOAD:
		printf("%s %s", lang == LANG_DE ? "Datei laden:" : "Transfer Load:",
		    prompt_buf);
		break;
	case MODE_PROMPT_SAVE:
		printf("%s %s", lang == LANG_DE ? "Datei speichern:" : "Transfer Save:",
		    prompt_buf);
		break;
	case MODE_PROMPT_JUMP:
		printf("%s %s", lang == LANG_DE ? "Gehe zu Zeile:" : "Jump to line:",
		    prompt_buf);
		break;
	case MODE_PROMPT_SEARCH:
		printf("%s %s", lang == LANG_DE ? "Suchen:" : "Search text:",
		    prompt_buf);
		break;
	case MODE_PROMPT_REPL_FIND:
		printf("%s %s", lang == LANG_DE ? "Wechseln Text:" : "Replace text:",
		    prompt_buf);
		break;
	case MODE_PROMPT_REPL_WITH:
		printf("%s %s", lang == LANG_DE ? "Mit Text:" : "With text:",
		    prompt_buf);
		break;
	default:
		printf("%.*s", scr_cols, status_msg);
		break;
	}
	fputs("\r\n", stdout);

	/* Row scr_rows - 1: Contextual hint / options bar */
	fputs("\x1b[0m\x1b[K", stdout);
	if (mode == MODE_PROMPT_SEARCH || mode == MODE_PROMPT_REPL_FIND ||
	    mode == MODE_PROMPT_REPL_WITH) {
		printf("[F2/Tab] case:%s  whole:%s  dir:%s  confirm:%s",
		    opt_case ? "Yes" : "No",
		    opt_whole ? "Yes" : "No",
		    opt_down ? "Down" : "Up",
		    opt_confirm ? "Yes" : "No");
	} else {
		printf("%.*s", scr_cols,
		    lang == LANG_DE
		    ? "Esc=Befehl  F6/^K=Auswahl  F4=Weitersuchen  F9/^L=DE/EN  ^S=Speichern"
		    : "Esc=Command  F6/^K=Select  F4=Repeat  F9/^L=EN/DE  ^S=Save  ^Q=Quit");
	}
	fputs("\r\n", stdout);

	/* Row scr_rows: Status line */
	{
		size_t page = (cur_line / 54u) + 1u;
		const char *mname = (mode == MODE_MENU) ? "COMMAND"
		    : (mode == MODE_EDIT) ? "EDIT" : "PROMPT";
		char left[160];

		snprintf(left, sizeof(left),
		    "Pg %lu Ln %lu Col %lu | %.40s%s | %s | %s | Scrap:%lu",
		    (unsigned long)page,
		    (unsigned long)(cur_line + 1u),
		    (unsigned long)(cur_col + 1u),
		    filename[0] ? filename : "(unnamed)",
		    dirty ? "*" : "",
		    mname,
		    lang == LANG_DE ? "DE" : "EN",
		    (unsigned long)scrap_len);
		fputs("\x1b[7m\x1b[K", stdout);
		printf("%-*.*s", scr_cols, scr_cols, left);
		fputs("\x1b[0m", stdout);
	}

	/* Place hardware cursor */
	if (mode == MODE_EDIT) {
		int crow = (int)(cur_line - top_line) + 1;
		int ccol = (int)cur_col + 1;
		if (ccol > scr_cols)
			ccol = scr_cols;
		printf("\x1b[%d;%dH\x1b[?25h", crow, ccol);
	} else if (mode >= MODE_PROMPT_LOAD && mode <= MODE_PROMPT_REPL_WITH) {
		int ccol = (int)prompt_len + 17;
		if (ccol > scr_cols)
			ccol = scr_cols;
		printf("\x1b[%d;%dH\x1b[?25h", scr_rows - 2, ccol);
	} else {
		line_idx = (size_t)(scr_rows - 3);
		printf("\x1b[%lu;1H\x1b[?25h", (unsigned long)line_idx);
	}
	fflush(stdout);
}

/* ======================= Command dispatch ======================= */

static void
start_prompt(enum word_mode pm, const char *initial)
{
	mode = pm;
	snprintf(prompt_buf, sizeof(prompt_buf), "%s", initial ? initial : "");
	prompt_len = strlen(prompt_buf);
}

static void
execute_builtin(enum word_builtin b)
{
	switch (b) {
	case ACT_BACK:
		if (menu_depth > 1)
			menu_depth--;
		else
			mode = MODE_EDIT;
		break;
	case ACT_COPY:
		mode = MODE_EDIT;
		do_copy();
		break;
	case ACT_DELETE:
		mode = MODE_EDIT;
		do_delete_to_scrap();
		break;
	case ACT_INSERT:
		mode = MODE_EDIT;
		do_insert_from_scrap();
		break;
	case ACT_UNDO:
		mode = MODE_EDIT;
		do_undo();
		break;
	case ACT_LOAD:
		start_prompt(MODE_PROMPT_LOAD, filename);
		break;
	case ACT_SAVE:
		start_prompt(MODE_PROMPT_SAVE, filename);
		break;
	case ACT_CLEAR:
		if (dirty) {
			mode = MODE_CONFIRM_CLEAR;
			set_msg(lang == LANG_DE
			    ? "Aenderungen verwerfen? (Y/N)"
			    : "Discard unsaved changes? (Y/N)");
		} else {
			undo_snapshot();
			doc_len = 0;
			cursor = 0;
			top_line = 0;
			has_anchor = 0;
			filename[0] = '\0';
			dirty = 0;
			mode = MODE_EDIT;
			set_msg(lang == LANG_DE ? "Dokument geleert"
			    : "Document cleared");
		}
		break;
	case ACT_JUMP_LINE:
		start_prompt(MODE_PROMPT_JUMP, "");
		break;
	case ACT_SEARCH:
		start_prompt(MODE_PROMPT_SEARCH, search_text);
		break;
	case ACT_REPLACE:
		start_prompt(MODE_PROMPT_REPL_FIND, search_text);
		break;
	case ACT_HELP_BASICS:
		mode = MODE_HELP;
		break;
	case ACT_QUIT:
		if (dirty) {
			mode = MODE_CONFIRM_QUIT;
			set_msg(lang == LANG_DE
			    ? "Vor Beenden speichern? (Y=Ja N=Nein Esc=Abbruch)"
			    : "Save changes before quit? (Y=Yes N=No Esc=Cancel)");
		} else {
			running = 0;
		}
		break;
	default:
		mode = MODE_EDIT;
		break;
	}
}

static void
activate_menu_item(const struct word_menu_item *it)
{
	if (it->kind == KIND_SUB) {
		if (menu_depth < WORD_MENU_DEPTH) {
			menu_stack[menu_depth].id = (enum word_menu_id)it->target;
			menu_stack[menu_depth].sel = 0;
			menu_depth++;
		}
	} else if (it->kind == KIND_BUILTIN) {
		execute_builtin((enum word_builtin)it->target);
	} else {
		snprintf(status_msg, sizeof(status_msg),
		    lang == LANG_DE ? "%s: noch nicht implementiert"
		    : "%s: not yet implemented",
		    lang == LANG_DE ? it->de : it->en);
		mode = MODE_EDIT;
	}
}

static void
handle_prompt_key(int k)
{
	if (k == KEY_ESC) {
		mode = MODE_EDIT;
		set_msg(lang == LANG_DE ? "Abgebrochen" : "Cancelled");
		return;
	}
	if (k == '\t' && (mode == MODE_PROMPT_SEARCH ||
	    mode == MODE_PROMPT_REPL_FIND || mode == MODE_PROMPT_REPL_WITH)) {
		/* Cycle case/whole/dir flags with Tab during Search/Replace. */
		int st = (opt_case << 2) | (opt_whole << 1) | opt_down;
		st = (st + 1) & 7;
		opt_case = (st >> 2) & 1;
		opt_whole = (st >> 1) & 1;
		opt_down = st & 1;
		return;
	}
	if (k == KEY_BACKSPACE) {
		if (prompt_len > 0)
			prompt_buf[--prompt_len] = '\0';
		return;
	}
	if (k == KEY_ENTER) {
		switch (mode) {
		case MODE_PROMPT_LOAD:
			mode = MODE_EDIT;
			file_load(prompt_buf);
			break;
		case MODE_PROMPT_SAVE:
			mode = MODE_EDIT;
			file_save(prompt_buf);
			break;
		case MODE_PROMPT_JUMP: {
			long ln = strtol(prompt_buf, NULL, 10);
			if (ln < 1)
				ln = 1;
			cursor = offset_for_line((size_t)(ln - 1));
			has_anchor = 0;
			mode = MODE_EDIT;
			snprintf(status_msg, sizeof(status_msg),
			    lang == LANG_DE ? "Zeile %ld" : "Line %ld", ln);
			break;
		}
		case MODE_PROMPT_SEARCH:
			snprintf(search_text, sizeof(search_text), "%s", prompt_buf);
			mode = MODE_EDIT;
			run_search(0);
			break;
		case MODE_PROMPT_REPL_FIND:
			snprintf(search_text, sizeof(search_text), "%s", prompt_buf);
			start_prompt(MODE_PROMPT_REPL_WITH, replace_text);
			break;
		case MODE_PROMPT_REPL_WITH:
			snprintf(replace_text, sizeof(replace_text), "%s", prompt_buf);
			repl_count = 0;
			undo_snapshot();
			replace_step(cursor);
			break;
		default:
			mode = MODE_EDIT;
			break;
		}
		return;
	}
	if (k >= 32 && k < 127 && prompt_len + 1 < WORD_PROMPT_MAX) {
		prompt_buf[prompt_len++] = (char)k;
		prompt_buf[prompt_len] = '\0';
	}
}

static void
handle_key(int k)
{
	if (k == KEY_NONE)
		return;

	if (k == KEY_F9 || k == KEY_CTRL_L) {
		lang = (lang == LANG_EN) ? LANG_DE : LANG_EN;
		set_msg(lang == LANG_DE ? "Sprache: Deutsch" : "Language: English");
		return;
	}

	if (mode == MODE_HELP) {
		if (k == KEY_ESC || k == KEY_ENTER || k == 'q' || k == 'Q')
			mode = MODE_EDIT;
		return;
	}

	if (mode == MODE_CONFIRM_QUIT) {
		int up = toupper(k & 0xff);
		if (k == KEY_ESC) {
			mode = MODE_EDIT;
			set_msg("Cancelled");
		} else if (up == 'Y') {
			if (filename[0] != '\0') {
				if (file_save(filename))
					running = 0;
				else
					mode = MODE_EDIT;
			} else {
				start_prompt(MODE_PROMPT_SAVE, "");
			}
		} else if (up == 'N') {
			running = 0;
		}
		return;
	}

	if (mode == MODE_CONFIRM_CLEAR) {
		int up = toupper(k & 0xff);
		if (up == 'Y') {
			undo_snapshot();
			doc_len = 0;
			cursor = 0;
			top_line = 0;
			has_anchor = 0;
			filename[0] = '\0';
			dirty = 0;
			set_msg("Document cleared");
		}
		mode = MODE_EDIT;
		return;
	}

	if (mode == MODE_CONFIRM_REPL) {
		int up = toupper(k & 0xff);
		if (k == KEY_ESC) {
			mode = MODE_EDIT;
			snprintf(status_msg, sizeof(status_msg),
			    "%lu replacements made", (unsigned long)repl_count);
		} else if (up == 'Y' || up == 'A') {
			size_t rlen = strlen(replace_text);
			if (up == 'A')
				opt_confirm = 0;
			doc_delete_range(cur_match.start, cur_match.end);
			if (doc_insert_bytes(cur_match.start, replace_text, rlen)) {
				cursor = cur_match.start + rlen;
				has_anchor = 0;
				repl_count++;
				replace_step(cursor);
			} else {
				mode = MODE_EDIT;
			}
		} else if (up == 'N') {
			replace_step(cur_match.end);
		}
		return;
	}

	if (mode >= MODE_PROMPT_LOAD && mode <= MODE_PROMPT_REPL_WITH) {
		handle_prompt_key(k);
		return;
	}

	if (mode == MODE_MENU) {
		struct menu_frame *mf = &menu_stack[menu_depth - 1];
		const struct word_menu *m = word_menu_get(mf->id);
		if (k == KEY_ESC) {
			if (menu_depth > 1)
				menu_depth--;
			else
				mode = MODE_EDIT;
		} else if (k == KEY_LEFT) {
			mf->sel = (mf->sel == 0) ? (uint8_t)(m->count - 1)
			    : (uint8_t)(mf->sel - 1);
		} else if (k == KEY_RIGHT || k == ' ' || k == '\t') {
			mf->sel = (uint8_t)((mf->sel + 1) % m->count);
		} else if (k == KEY_ENTER) {
			const struct word_menu_item *it =
			    word_menu_item_at(m, lang, mf->sel);
			if (it != NULL)
				activate_menu_item(it);
		} else if (k >= 32 && k < 127) {
			int idx = word_menu_find_accel(m, lang, (char)k);
			if (idx >= 0) {
				mf->sel = (uint8_t)idx;
				activate_menu_item(word_menu_item_at(m, lang, mf->sel));
			}
		}
		return;
	}

	/* MODE_EDIT */
	switch (k) {
	case KEY_ESC:
		menu_depth = 1;
		menu_stack[0].id = MENU_MAIN;
		mode = MODE_MENU;
		break;
	case KEY_CTRL_S:
		if (filename[0] != '\0')
			file_save(filename);
		else
			start_prompt(MODE_PROMPT_SAVE, "");
		break;
	case KEY_CTRL_Q:
		execute_builtin(ACT_QUIT);
		break;
	case KEY_CTRL_Z:
		do_undo();
		break;
	case KEY_F4:
		run_search(1);
		break;
	case KEY_F6:
	case KEY_CTRL_K:
		if (has_anchor) {
			has_anchor = 0;
			set_msg(lang == LANG_DE ? "Auswahl aufgehoben"
			    : "Selection cleared");
		} else {
			has_anchor = 1;
			anchor = cursor;
			set_msg(lang == LANG_DE ? "Auswahl gestartet"
			    : "Selection anchor set");
		}
		break;
	case KEY_LEFT:        move_horizontal(-1, 0); break;
	case KEY_RIGHT:       move_horizontal(1, 0);  break;
	case KEY_UP:          move_vertical(-1, 0);   break;
	case KEY_DOWN:        move_vertical(1, 0);    break;
	case KEY_SHIFT_LEFT:  move_horizontal(-1, 1); break;
	case KEY_SHIFT_RIGHT: move_horizontal(1, 1);  break;
	case KEY_SHIFT_UP:    move_vertical(-1, 1);   break;
	case KEY_SHIFT_DOWN:  move_vertical(1, 1);    break;
	case KEY_HOME:        has_anchor = 0; cursor = line_start_of(cursor); break;
	case KEY_END:         has_anchor = 0; cursor = line_end_of(cursor);   break;
	case KEY_PGUP:        move_vertical(-(scr_rows - 5), 0); break;
	case KEY_PGDN:        move_vertical(scr_rows - 5, 0);    break;
	case KEY_BACKSPACE:
		if (has_anchor && anchor != cursor) {
			size_t lo, hi;
			undo_snapshot();
			sel_bounds(&lo, &hi);
			doc_delete_range(lo, hi);
		} else if (cursor > 0) {
			undo_snapshot();
			doc_delete_range(cursor - 1, cursor);
		}
		break;
	case KEY_DEL:
		if (has_anchor && anchor != cursor) {
			size_t lo, hi;
			undo_snapshot();
			sel_bounds(&lo, &hi);
			doc_delete_range(lo, hi);
		} else if (cursor < doc_len) {
			undo_snapshot();
			doc_delete_range(cursor, cursor + 1);
		}
		break;
	case KEY_ENTER: {
		char nl = '\n';
		undo_snapshot();
		if (has_anchor && anchor != cursor) {
			size_t lo, hi;
			sel_bounds(&lo, &hi);
			doc_delete_range(lo, hi);
		}
		if (doc_insert_bytes(cursor, &nl, 1))
			cursor++;
		break;
	}
	case '\t': {
		char tb = '\t';
		undo_snapshot();
		if (doc_insert_bytes(cursor, &tb, 1))
			cursor++;
		break;
	}
	default:
		if (k >= 32 && k < 127) {
			char ch = (char)k;
			undo_snapshot();
			if (has_anchor && anchor != cursor) {
				size_t lo, hi;
				sel_bounds(&lo, &hi);
				doc_delete_range(lo, hi);
			}
			if (doc_insert_bytes(cursor, &ch, 1))
				cursor++;
		}
		break;
	}
}

int
main(int argc, char **argv)
{
	const char *open_path = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
			i++;
			if (strcmp(argv[i], "de") == 0)
				lang = LANG_DE;
			else if (strcmp(argv[i], "en") == 0)
				lang = LANG_EN;
		} else if (strcmp(argv[i], "--help") == 0 ||
		    strcmp(argv[i], "-h") == 0) {
			puts("usage: word [--lang en|de] [file]");
			return 0;
		} else if (argv[i][0] != '-') {
			open_path = argv[i];
		}
	}

	if (open_path != NULL)
		file_load(open_path);
	else
		set_msg("Word 5.1 for DiscoBSD -- press Esc for COMMAND menu");

	if (enable_raw_mode(STDIN_FILENO) != 0) {
		perror("word: tty");
		return 1;
	}

	fputs("\x1b[2J", stdout);
	while (running) {
		render_screen();
		handle_key(read_key(STDIN_FILENO));
	}
	return 0;
}
