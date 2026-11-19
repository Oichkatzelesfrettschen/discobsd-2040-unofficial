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
 * word_menus.c: the command menu tree (SPEC.md).
 */

#include "word.h"

#include <ctype.h>

static const struct word_menu_item main_items[] = {
	{ "Copy", 'C', KIND_BUILTIN, ACT_COPY },
	{ "Delete", 'D', KIND_BUILTIN, ACT_DELETE },
	{ "Format", 'F', KIND_SUB,     MENU_FORMAT },
	{ "Gallery", 'G', KIND_SUB,     MENU_GALLERY },
	{ "Help", 'H', KIND_SUB,     MENU_HELP },
	{ "Insert", 'I', KIND_BUILTIN, ACT_INSERT },
	{ "Jump", 'J', KIND_SUB,     MENU_JUMP },
	{ "Library", 'L', KIND_SUB,     MENU_LIBRARY },
	{ "Options", 'O', KIND_BUILTIN, ACT_OPTIONS },
	{ "Print", 'P', KIND_SUB,     MENU_PRINT },
	{ "Quit", 'Q', KIND_BUILTIN, ACT_QUIT },
	{ "Replace", 'R', KIND_BUILTIN, ACT_REPLACE },
	{ "Search", 'S', KIND_BUILTIN, ACT_SEARCH },
	{ "Transfer", 'T', KIND_SUB,     MENU_TRANSFER },
	{ "Undo", 'U', KIND_BUILTIN, ACT_UNDO },
	{ "Window", 'W', KIND_SUB,     MENU_WINDOW }
};

static const struct word_menu_item format_items[] = {
	{ "Character", 'C', KIND_BUILTIN, ACT_FCHAR },
	{ "Paragraph", 'P', KIND_BUILTIN, ACT_FPARA },
	{ "Tab", 'T', KIND_TODO, 0 },
	{ "Border", 'B', KIND_TODO, 0 },
	{ "Footnote", 'F', KIND_TODO, 0 },
	{ "Division", 'D', KIND_TODO, 0 },
	{ "Running-head", 'R', KIND_TODO, 0 },
	{ "Stylesheet", 'S', KIND_TODO, 0 },
	{ "Search", 'E', KIND_TODO, 0 },
	{ "Replace", 'L', KIND_TODO, 0 },
	{ "Revision-marks", 'M', KIND_TODO, 0 },
	{ "Position", 'O', KIND_SUB,  MENU_POSITION },
	{ "Annotation", 'A', KIND_TODO, 0 },
	{ "Bookmark", 'K', KIND_TODO, 0 }
};

static const struct word_menu_item gallery_items[] = {
	{ "Copy", 'C', KIND_TODO,    0 },
	{ "Delete", 'D', KIND_TODO,    0 },
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK },
	{ "Format", 'F', KIND_TODO,    0 },
	{ "Help", 'H', KIND_TODO,    0 },
	{ "Insert", 'I', KIND_TODO,    0 },
	{ "Name", 'N', KIND_TODO,    0 },
	{ "Print", 'P', KIND_TODO,    0 },
	{ "Transfer", 'T', KIND_TODO,    0 },
	{ "Undo", 'U', KIND_TODO,    0 }
};

static const struct word_menu_item help_items[] = {
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK },
	{ "Basics", 'B', KIND_BUILTIN, ACT_HELP },
	{ "Next", 'N', KIND_TODO,    0 },
	{ "Previous", 'P', KIND_TODO,    0 },
	{ "Index", 'I', KIND_TODO,    0 },
	{ "Keyboard", 'K', KIND_BUILTIN, ACT_HELP },
	{ "Mouse", 'M', KIND_TODO,    0 }
};

static const struct word_menu_item jump_items[] = {
	{ "Page", 'P', KIND_BUILTIN, ACT_JUMP_PAGE },
	{ "Footnote", 'F', KIND_TODO,    0 },
	{ "Annotation", 'A', KIND_TODO,    0 },
	{ "Bookmark", 'K', KIND_TODO,    0 }
};

static const struct word_menu_item library_items[] = {
	{ "Autosort", 'A', KIND_TODO, 0 },
	{ "Count", 'C', KIND_BUILTIN, ACT_COUNT },
	{ "Document-retrieval", 'D', KIND_BUILTIN, ACT_PICK },
	{ "Hyphenate", 'H', KIND_TODO, 0 },
	{ "Index", 'I', KIND_TODO, 0 },
	{ "Link", 'L', KIND_TODO, 0 },
	{ "Number", 'N', KIND_TODO, 0 },
	{ "Run", 'R', KIND_BUILTIN, ACT_RUN },
	{ "Spell", 'S', KIND_SUB,  MENU_SPELL },
	{ "Table", 'T', KIND_TODO, 0 },
	{ "Thesaurus", 'E', KIND_SUB,  MENU_THESAURUS }
};

static const struct word_menu_item print_items[] = {
	{ "Printer", 'P', KIND_TODO, 0 },
	{ "Direct", 'D', KIND_TODO, 0 },
	{ "File", 'F', KIND_TODO, 0 },
	{ "Glossary", 'G',KIND_TODO, 0 },
	{ "Merge", 'M', KIND_TODO, 0 },
	{ "Options", 'O', KIND_TODO, 0 },
	{ "PreView", 'V', KIND_SUB,  MENU_VIEW },
	{ "Queue", 'Q',KIND_TODO,0 },
	{ "Repaginate", 'R',KIND_TODO,0 }
};

static const struct word_menu_item transfer_items[] = {
	{ "Load", 'L', KIND_BUILTIN, ACT_LOAD },
	{ "Save", 'S', KIND_BUILTIN, ACT_SAVE },
	{ "Clear", 'C', KIND_BUILTIN, ACT_CLEAR },
	{ "Delete", 'D', KIND_BUILTIN, ACT_FDELETE },
	{ "Merge", 'M', KIND_BUILTIN, ACT_MERGE },
	{ "Options", 'O', KIND_BUILTIN, ACT_CHDIR },
	{ "Glossary", 'G',KIND_TODO,    0 },
	{ "Rename", 'R', KIND_BUILTIN, ACT_RENAME }
};

static const struct word_menu_item window_items[] = {
	{ "Split", 'S', KIND_TODO, 0 },
	{ "Close", 'C', KIND_TODO, 0 },
	{ "Move", 'M',KIND_TODO, 0 },
	{ "Options", 'O', KIND_TODO, 0 }
};

static const struct word_menu_item docret_items[] = {
	{ "Query", 'Q', KIND_TODO,    0 },
	{ "Load", 'L', KIND_BUILTIN, ACT_LOAD },
	{ "Print", 'P', KIND_TODO,    0 },
	{ "Delete", 'D', KIND_TODO,    0 },
	{ "Update", 'U',KIND_TODO,  0 },
	{ "View", 'V', KIND_TODO,    0 },
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item spell_items[] = {
	{ "Proof", 'P', KIND_TODO,    0 },
	{ "Options", 'O', KIND_TODO,    0 },
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item thesaurus_items[] = {
	{ "Replace", 'R', KIND_TODO,    0 },
	{ "Lookup", 'L',KIND_TODO,  0 },
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item position_items[] = {
	{ "Paragraph", 'P', KIND_TODO,    0 },
	{ "Division", 'D', KIND_TODO,    0 },
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item view_items[] = {
	{ "Print", 'P', KIND_TODO,    0 },
	{ "Jump", 'J', KIND_BUILTIN, ACT_JUMP_PAGE },
	{ "Options", 'O', KIND_TODO,    0 },
	{ "Exit", 'E', KIND_BUILTIN, ACT_BACK }
};

#define MENU_ENTRY(title, arr) \
	{ (title), (arr), (uint8_t)(sizeof(arr) / sizeof((arr)[0])) }

static const struct word_menu menus[MENU_COUNT] = {
	[MENU_MAIN]          = MENU_ENTRY("COMMAND",  main_items),
	[MENU_FORMAT]        = MENU_ENTRY("FORMAT",   format_items),
	[MENU_GALLERY]       = MENU_ENTRY("GALLERY",  gallery_items),
	[MENU_HELP]          = MENU_ENTRY("HELP",     help_items),
	[MENU_JUMP]          = MENU_ENTRY("JUMP",     jump_items),
	[MENU_LIBRARY]       = MENU_ENTRY("LIBRARY",  library_items),
	[MENU_PRINT]         = MENU_ENTRY("PRINT",    print_items),
	[MENU_TRANSFER]      = MENU_ENTRY("TRANSFER", transfer_items),
	[MENU_WINDOW]        = MENU_ENTRY("WINDOW",   window_items),
	[MENU_DOC_RETRIEVAL] = MENU_ENTRY("DOC-RET",  docret_items),
	[MENU_SPELL]         = MENU_ENTRY("SPELL",    spell_items),
	[MENU_THESAURUS]     = MENU_ENTRY("THESAUR",  thesaurus_items),
	[MENU_POSITION]      = MENU_ENTRY("POSITION", position_items),
	[MENU_VIEW]          = MENU_ENTRY("PREVIEW",  view_items)
};

const struct word_menu *
word_menu_get(enum word_menu_id id)
{
	if ((unsigned)id >= MENU_COUNT)
		return &menus[MENU_MAIN];
	return &menus[id];
}

const struct word_menu_item *
word_menu_item_at(const struct word_menu *m, uint8_t idx)
{
	if (idx >= m->count)
		return NULL;
	return &m->items[idx];
}

int
word_menu_find_accel(const struct word_menu *m, char ch)
{
	int up = toupper((unsigned char)ch);
	uint8_t i;

	for (i = 0; i < m->count; i++)
		if (toupper((unsigned char)m->items[i].accel) == up)
			return (int)i;
	return -1;
}
