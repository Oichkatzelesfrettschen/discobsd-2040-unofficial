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
 * word: shared declarations of the clean-room word processor (SPEC.md).
 *
 * Every capacity below is a fixed static size. The document itself has no
 * capacity: word_buf.c keeps it as a piece table over the loaded file and
 * an append-only add stream that spills to an unlinked temporary file.
 */

#ifndef WORD_H
#define WORD_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t woff_t;		/* byte offset in the document */

#define WOFF_END	((woff_t)0xffffffffu)	/* past the last display row */

#define WORD_PATH_MAX	128u
#define WORD_FIELD_MAX	96u
#define WORD_MSG_MAX	96u
#define WORD_PAT_MAX	WORD_FIELD_MAX	/* an atom takes at least one character */
#define WORD_MENU_DEPTH	4u
#define WORD_ROWS_MAX	48
#define WORD_COLS_MAX	120

/* ---- word_buf.c: the document store ---- */

/*
 * Character and paragraph properties (SPEC.md, Formatting). Distances are
 * in twips, 1/1440 inch; the console shows a column as 1/10 inch.
 */
#define WORD_NCHP	64
#define WORD_NPAP	32

enum {
	CH_BOLD = 0x01, CH_ITALIC = 0x02, CH_UNDER = 0x04, CH_STRIKE = 0x08,
	CH_UPPER = 0x10, CH_SMALLCAPS = 0x20, CH_DUNDER = 0x40,
	CH_HIDDEN = 0x80
};
enum { POS_NORMAL = 0, POS_SUPER, POS_SUB };
enum { AL_LEFT = 0, AL_CENTER, AL_RIGHT, AL_JUSTIFY };
enum { PA_KEEP = 0x01, PA_FOLLOW = 0x02, PA_SIDE = 0x04 };

struct word_chp {
	uint8_t flags, pos, font, size;	/* size in half points; 0 default */
	uint8_t color, pad[3];
};

struct word_pap {
	uint8_t align, keep;
	int16_t left, first, right;	/* twips */
	uint16_t spacing, before, after;	/* twips; spacing 0 is single */
};

#define TWIPS_PER_COL	144

int	buf_init(void);
int	buf_load(const char *path);	/* 0, or an errno value */
void	buf_clear(void);
woff_t	buf_len(void);
int	buf_at(woff_t off);		/* byte, or -1 past the end */
int	buf_ref_at(woff_t ref);		/* byte of the add stream */
int	buf_insert(woff_t pos, const char *s, size_t n);
int	buf_insert_ref(woff_t pos, woff_t ref, woff_t n);
int	buf_insert_props(woff_t pos, woff_t ref, woff_t n, unsigned chp,
	    unsigned pap);
void	buf_props_for(woff_t pos, unsigned *chp, unsigned *pap);
int	buf_insert_fmt(woff_t pos, const char *s, size_t n, int chp);
int	buf_append_props(const char *s, size_t n, unsigned chp, unsigned pap);
void	buf_loaded(void);		/* end of a load: no undo history */

/* ---- word_doc.c: Word 5.1 document files ---- */

int	doc_probe(const char *path);	/* 1 Word document, 0 other, -1 error */
int	doc_load(const char *path);	/* 0, or an errno value */
int	doc_save(const char *path, int backup);
int	doc_formatted(void);		/* 1 when any property is not default */
int	buf_props(woff_t off);		/* pap << 8 | chp, or -1 */
const struct word_chp *buf_chp(unsigned i);
const struct word_pap *buf_pap(unsigned i);
int	buf_chp_index(const struct word_chp *c);	/* -1: table full */
int	buf_pap_index(const struct word_pap *p);
int	buf_restyle(woff_t lo, woff_t hi, int which,
	    int (*fn)(unsigned old, int which, void *ctx), void *ctx);
int	buf_delete(woff_t pos, woff_t n, woff_t *ref);
int	buf_replace(woff_t pos, woff_t n, const char *s, size_t len);
int	buf_copy(woff_t pos, woff_t n, woff_t *ref);
int	buf_merge(woff_t pos, const char *path, woff_t *n);
int	buf_save(const char *path, int backup);
void	buf_mark(void);			/* start a new undo group */
int	buf_undo(woff_t *pos);
int	buf_redo(woff_t *pos);
uint32_t buf_gen(void);			/* changes with every edit */
woff_t	buf_lowmark(void);		/* lowest offset edited since last call */
unsigned buf_pieces(void);
woff_t	buf_spilled(void);

/* ---- word_find.c: the search pattern ---- */

enum word_atom_kind {
	ATOM_CH = 0,
	ATOM_ANY,
	ATOM_WHITERUN,
	ATOM_BREAK
};

struct word_atom {
	uint8_t kind;
	char ch;
};

struct word_pattern {
	struct word_atom atoms[WORD_PAT_MAX];
	size_t len;
	int match_case;
	int whole_word;
};

void	word_pat_compile(struct word_pattern *p, const char *text,
	    int match_case, int whole_word);
int	word_pat_find(const struct word_pattern *p, woff_t from, int down,
	    woff_t *start, woff_t *end);

/* ---- word_term.c: terminal, keys and the screen ---- */

enum {
	K_NONE = 0,
	K_TAB = 9,
	K_ENTER = 13,
	K_ESC = 27,
	K_BS = 127,
	K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END,
	K_PGUP, K_PGDN, K_INS, K_DEL, K_BTAB,
	K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10,
	K_F11, K_F12,
	KM_SHIFT = 0x1000,
	KM_CTRL = 0x2000,
	KM_ALT = 0x4000,
	K_BASE = 0x0fff
};

#define KCTRL(c)	((c) & 0x1f)

/* Cell attributes: three flag bits and a color class in bits 3-5. */
#define A_REV	0x01
#define A_BOLD	0x02
#define A_UL	0x04
#define A_CLS(n)	((n) << 3)
enum { C_TEXT, C_BAR, C_KEY, C_TITLE, C_ERR, C_MARK, C_SEL, C_ITAL, C_STRIKE,
	C_HIDE, C_SUPER };

/* Cell characters below 0x20 select the DEC line-drawing set. */
enum { G_DIAMOND = 1, G_HLINE, G_VLINE, G_UL, G_UR, G_LL, G_LR };

extern int	scr_rows, scr_cols, scr_color;

int	tty_raw(void);
void	tty_restore(void);
void	tty_size(void);
int	key_read(void);
void	scr_clear(void);
void	scr_put(int row, int col, int ch, int attr);
int	scr_puts(int row, int col, const char *s, int attr, int max);
void	scr_fill(int row, int col, int n, int attr);
void	scr_flush(int crow, int ccol);
void	scr_invalidate(void);

/* ---- word_menus.c: the command menu tree ---- */

enum word_menu_id {
	MENU_MAIN = 0,
	MENU_FORMAT,
	MENU_GALLERY,
	MENU_HELP,
	MENU_JUMP,
	MENU_LIBRARY,
	MENU_PRINT,
	MENU_TRANSFER,
	MENU_WINDOW,
	MENU_DOC_RETRIEVAL,
	MENU_SPELL,
	MENU_THESAURUS,
	MENU_POSITION,
	MENU_VIEW,
	MENU_COUNT
};

enum word_builtin {
	ACT_NONE = 0,
	ACT_SAVE,
	ACT_LOAD,
	ACT_CLEAR,
	ACT_JUMP_PAGE,
	ACT_QUIT,
	ACT_BACK,
	ACT_COPY,
	ACT_DELETE,
	ACT_INSERT,
	ACT_SEARCH,
	ACT_REPLACE,
	ACT_UNDO,
	ACT_HELP,
	ACT_OPTIONS,
	ACT_MERGE,
	ACT_FDELETE,
	ACT_RENAME,
	ACT_CHDIR,
	ACT_RUN,
	ACT_PICK,
	ACT_COUNT,
	ACT_FCHAR,
	ACT_FPARA
};

enum word_act_kind {
	KIND_TODO = 0,
	KIND_SUB,
	KIND_BUILTIN
};

struct word_menu_item {
	const char *name;
	char accel;
	uint8_t kind;
	uint8_t target;		/* enum word_menu_id or enum word_builtin */
};

struct word_menu {
	const char *title;
	const struct word_menu_item *items;
	uint8_t count;
};

const struct word_menu *word_menu_get(enum word_menu_id id);
const struct word_menu_item *word_menu_item_at(const struct word_menu *m,
	    uint8_t idx);
int	word_menu_find_accel(const struct word_menu *m, char ch);

/* ---- word.c: the editor core that word_modes.c drives ---- */

enum { KEYS_WORD = 0, KEYS_VI, KEYS_NANO };
enum { F_LOAD = 0, F_SAVE, F_MERGE, F_FDELETE, F_RENAME, F_CHDIR, F_SEARCH,
	F_REPLACE, F_JUMP, F_LINE, F_RUN, F_OPTIONS, F_FCHAR, F_FPARA, F_NFORMS };

extern int keymode, running, scrap_lines;
extern woff_t ed_cur, ed_anchor;
extern int ed_sel;
extern char ed_file[WORD_PATH_MAX];

void	ed_msg(int err, const char *s);
void	ed_msgf(int err, const char *fmt, ...);
void	ed_set(woff_t pos);
woff_t	ed_bol(woff_t pos);
woff_t	ed_eol(woff_t pos);
woff_t	ed_wordfwd(woff_t pos);
woff_t	ed_wordback(woff_t pos);
woff_t	ed_wordend(woff_t pos);
woff_t	ed_line_off(woff_t line);
void	ed_vmove(int rows);
int	ed_rows(void);
int	ed_ins(const char *s, size_t n);
int	ed_del(woff_t lo, woff_t hi, int scrap, int append);
void	ed_yank(woff_t lo, woff_t hi, int lines);
void	ed_paste(woff_t at);
void	ed_undo(void);
void	ed_redo(void);
int	ed_dirty(void);
int	ed_save(const char *path);
int	ed_load(const char *path);
void	ed_quit(int force);
void	ed_form(int id);
void	ed_menu(void);
void	ed_search(const char *text, int down, int again);
void	ed_help(void);
void	ed_info(void);
void	ed_pick(void);
void	ed_set_wrap(int on);

/* ---- word_modes.c: the vi and nano key modes ---- */

void	vi_key(int k);
void	vi_reset(void);
const char *vi_cmdline(void);
const char *vi_state(void);
void	nano_key(int k);

/* ---- word_pick.c: the file picker ---- */

int	pick_open(const char *dir);
void	pick_draw(int top, int rows);
int	pick_key(int k, char *out, size_t outsz);	/* 1 chosen, -1 cancel */

#endif /* WORD_H */
