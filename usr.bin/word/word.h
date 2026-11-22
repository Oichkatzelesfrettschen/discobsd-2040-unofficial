/*
 * word -- clean-room Word 5.1 command-menu TUI editor for DiscoBSD/rp2040.
 *
 * Copyright (c) 2026 First Principalists
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef WORD_H
#define WORD_H

#include <stddef.h>
#include <stdint.h>

/* Bounded static capacities sized for the 192 KiB RP2040 OMAGIC process window. */
#define WORD_DOC_CAP     32768u
#define WORD_SCRAP_CAP    4096u
#define WORD_PATH_MAX      128u
#define WORD_PROMPT_MAX    128u
#define WORD_MSG_MAX       128u
#define WORD_PAT_MAX        64u
#define WORD_MENU_DEPTH      8u

enum word_lang {
	LANG_EN = 0,
	LANG_DE = 1
};

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
	ACT_JUMP_LINE,
	ACT_QUIT,
	ACT_BACK,
	ACT_COPY,
	ACT_DELETE,
	ACT_INSERT,
	ACT_SEARCH,
	ACT_REPLACE,
	ACT_UNDO,
	ACT_HELP_BASICS
};

enum word_act_kind {
	KIND_TODO = 0,
	KIND_SUB,
	KIND_BUILTIN
};

struct word_menu_item {
	const char *en;
	const char *de;
	char accel;
	uint8_t kind;
	uint8_t target; /* enum word_menu_id or enum word_builtin */
};

struct word_menu {
	const char *en_title;
	const char *de_title;
	const struct word_menu_item *items;
	uint8_t count;
	const uint8_t *de_order; /* NULL means same order as English */
};

/* word_menus.c */
const struct word_menu *word_menu_get(enum word_menu_id id);
const struct word_menu_item *word_menu_item_at(const struct word_menu *m,
    enum word_lang lang, uint8_t vis_idx);
int word_menu_find_accel(const struct word_menu *m, enum word_lang lang, char ch);

/* word_find.c */
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

struct word_match {
	size_t start;
	size_t end;
};

void word_pat_compile(struct word_pattern *p, const char *text,
    int match_case, int whole_word);
int word_pat_find(const struct word_pattern *p, const char *doc, size_t doc_len,
    size_t from, int dir_down, struct word_match *out);

#endif /* WORD_H */
