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
 * word_menus.c: the command menu tree in English and German (SPEC.md).
 */

#include "word.h"

#include <ctype.h>

static const struct word_menu_item main_items[] = {
	{ "Copy",     "Kopie",       'C', KIND_BUILTIN, ACT_COPY },
	{ "Delete",   "Loeschen",    'D', KIND_BUILTIN, ACT_DELETE },
	{ "Format",   "Format",      'F', KIND_SUB,     MENU_FORMAT },
	{ "Gallery",  "Muster",      'G', KIND_SUB,     MENU_GALLERY },
	{ "Help",     "Hilfe",       'H', KIND_SUB,     MENU_HELP },
	{ "Insert",   "Einfuegen",   'I', KIND_BUILTIN, ACT_INSERT },
	{ "Jump",     "Gehezu",      'J', KIND_SUB,     MENU_JUMP },
	{ "Library",  "Bibliothek",  'L', KIND_SUB,     MENU_LIBRARY },
	{ "Options",  "Zusaetze",    'O', KIND_TODO,    0 },
	{ "Print",    "Druck",       'P', KIND_SUB,     MENU_PRINT },
	{ "Quit",     "Quitt",       'Q', KIND_BUILTIN, ACT_QUIT },
	{ "Replace",  "Wechseln",    'R', KIND_BUILTIN, ACT_REPLACE },
	{ "Search",   "Suchen",      'S', KIND_BUILTIN, ACT_SEARCH },
	{ "Transfer", "Uebertragen", 'T', KIND_SUB,     MENU_TRANSFER },
	{ "Undo",     "Rueckgaengig",'U', KIND_BUILTIN, ACT_UNDO },
	{ "Window",   "Ausschnitt",  'W', KIND_SUB,     MENU_WINDOW }
};

/* German alphabetical order of the 16 top-level commands. */
static const uint8_t main_de_order[16] = {
	15, 7, 9, 5, 2, 6, 4, 0, 1, 3, 10, 14, 12, 13, 11, 8
};

static const struct word_menu_item format_items[] = {
	{ "Character",      "Zeichen",        'C', KIND_TODO, 0 },
	{ "Paragraph",      "Absatz",         'P', KIND_TODO, 0 },
	{ "Tab",            "Tabulator",      'T', KIND_TODO, 0 },
	{ "Border",         "Rahmen",         'B', KIND_TODO, 0 },
	{ "Footnote",       "Fussnote",       'F', KIND_TODO, 0 },
	{ "Division",       "Bereich",        'D', KIND_TODO, 0 },
	{ "Running-head",   "Kopf-/Fusszeile",'R', KIND_TODO, 0 },
	{ "Stylesheet",     "Druckformat",    'S', KIND_TODO, 0 },
	{ "Search",         "Suchen",         'E', KIND_TODO, 0 },
	{ "Replace",        "Wechseln",       'L', KIND_TODO, 0 },
	{ "Revision-marks", "Ueberarbeitung", 'M', KIND_TODO, 0 },
	{ "Position",       "Position",       'O', KIND_SUB,  MENU_POSITION },
	{ "Annotation",     "Anmerkung",      'A', KIND_TODO, 0 },
	{ "Bookmark",       "Textmarke",      'K', KIND_TODO, 0 }
};

static const struct word_menu_item gallery_items[] = {
	{ "Copy",     "Kopie",       'C', KIND_TODO,    0 },
	{ "Delete",   "Loeschen",    'D', KIND_TODO,    0 },
	{ "Exit",     "Beenden",     'E', KIND_BUILTIN, ACT_BACK },
	{ "Format",   "Format",      'F', KIND_TODO,    0 },
	{ "Help",     "Hilfe",       'H', KIND_TODO,    0 },
	{ "Insert",   "Einfuegen",   'I', KIND_TODO,    0 },
	{ "Name",     "Name",        'N', KIND_TODO,    0 },
	{ "Print",    "Druck",       'P', KIND_TODO,    0 },
	{ "Transfer", "Uebertragen", 'T', KIND_TODO,    0 },
	{ "Undo",     "Rueckgaengig",'U', KIND_TODO,    0 }
};

static const struct word_menu_item help_items[] = {
	{ "Exit",     "Wiederaufnahme",    'E', KIND_BUILTIN, ACT_BACK },
	{ "Basics",   "Grundbegriffe",     'B', KIND_BUILTIN, ACT_HELP_BASICS },
	{ "Next",     "Naechste-seite",    'N', KIND_TODO,    0 },
	{ "Previous", "Vorhergehende",     'P', KIND_TODO,    0 },
	{ "Index",    "Register",          'I', KIND_TODO,    0 },
	{ "Keyboard", "Tastatur",          'K', KIND_BUILTIN, ACT_HELP_BASICS },
	{ "Mouse",    "Maus",              'M', KIND_TODO,    0 }
};

static const struct word_menu_item jump_items[] = {
	{ "Page",       "Bildschirmseite", 'P', KIND_BUILTIN, ACT_JUMP_LINE },
	{ "Footnote",   "Fussnote",        'F', KIND_TODO,    0 },
	{ "Annotation", "Anmerkung",       'A', KIND_TODO,    0 },
	{ "Bookmark",   "Textmarke",       'K', KIND_TODO,    0 }
};

static const struct word_menu_item library_items[] = {
	{ "Autosort",           "Sortieren",      'A', KIND_TODO, 0 },
	{ "Document-retrieval", "Datei-manager",  'D', KIND_SUB,  MENU_DOC_RETRIEVAL },
	{ "Hyphenate",          "Trennhilfe",     'H', KIND_TODO, 0 },
	{ "Index",              "Index",          'I', KIND_TODO, 0 },
	{ "Link",               "Verknuepfen",    'L', KIND_TODO, 0 },
	{ "Number",             "Numerieren",     'N', KIND_TODO, 0 },
	{ "Run",                "Betriebssystem", 'R', KIND_TODO, 0 },
	{ "Spell",              "Rechtschreibung",'S', KIND_SUB,  MENU_SPELL },
	{ "Table",              "Verzeichnis",    'T', KIND_TODO, 0 },
	{ "Thesaurus",          "Thesaurus",      'E', KIND_SUB,  MENU_THESAURUS }
};

static const struct word_menu_item print_items[] = {
	{ "Printer",  "Drucker",    'P', KIND_TODO, 0 },
	{ "Direct",   "Direkt",     'D', KIND_TODO, 0 },
	{ "File",     "Datei",      'F', KIND_TODO, 0 },
	{ "Glossary", "Textbaustein",'G',KIND_TODO, 0 },
	{ "Merge",    "Serienbrief",'M', KIND_TODO, 0 },
	{ "Options",  "Optionen",   'O', KIND_TODO, 0 },
	{ "PreView",  "Vorschau",   'V', KIND_SUB,  MENU_VIEW },
	{ "Queue",    "Warteschlange",'Q',KIND_TODO,0 },
	{ "Repaginate","Seitenumbruch",'R',KIND_TODO,0 }
};

static const struct word_menu_item transfer_items[] = {
	{ "Load",     "Laden",      'L', KIND_BUILTIN, ACT_LOAD },
	{ "Save",     "Speichern",  'S', KIND_BUILTIN, ACT_SAVE },
	{ "Clear",    "Loeschen",   'C', KIND_BUILTIN, ACT_CLEAR },
	{ "Delete",   "Entfernen",  'D', KIND_TODO,    0 },
	{ "Merge",    "Einfuegen",  'M', KIND_TODO,    0 },
	{ "Options",  "Optionen",   'O', KIND_TODO,    0 },
	{ "Glossary", "Textbaustein",'G',KIND_TODO,    0 },
	{ "Rename",   "Umbenennen", 'R', KIND_TODO,    0 }
};

static const struct word_menu_item window_items[] = {
	{ "Split", "Teilen",    'S', KIND_TODO, 0 },
	{ "Close", "Schliessen",'C', KIND_TODO, 0 },
	{ "Move",  "Verschieben",'M',KIND_TODO, 0 },
	{ "Options","Optionen", 'O', KIND_TODO, 0 }
};

static const struct word_menu_item docret_items[] = {
	{ "Query",  "Abfrage",   'Q', KIND_TODO,    0 },
	{ "Load",   "Laden",     'L', KIND_BUILTIN, ACT_LOAD },
	{ "Print",  "Drucken",   'P', KIND_TODO,    0 },
	{ "Delete", "Loeschen",  'D', KIND_TODO,    0 },
	{ "Update", "Aktualisieren",'U',KIND_TODO,  0 },
	{ "View",   "Ansicht",   'V', KIND_TODO,    0 },
	{ "Exit",   "Beenden",   'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item spell_items[] = {
	{ "Proof",   "Pruefen",     'P', KIND_TODO,    0 },
	{ "Options", "Optionen",    'O', KIND_TODO,    0 },
	{ "Exit",    "Beenden",     'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item thesaurus_items[] = {
	{ "Replace", "Ersetzen", 'R', KIND_TODO,    0 },
	{ "Lookup",  "Nachschlagen",'L',KIND_TODO,  0 },
	{ "Exit",    "Beenden",  'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item position_items[] = {
	{ "Paragraph", "Absatz",  'P', KIND_TODO,    0 },
	{ "Division",  "Bereich", 'D', KIND_TODO,    0 },
	{ "Exit",      "Beenden", 'E', KIND_BUILTIN, ACT_BACK }
};

static const struct word_menu_item view_items[] = {
	{ "Print",   "Drucken",  'P', KIND_TODO,    0 },
	{ "Jump",    "Gehezu",   'J', KIND_BUILTIN, ACT_JUMP_LINE },
	{ "Options", "Optionen", 'O', KIND_TODO,    0 },
	{ "Exit",    "Beenden",  'E', KIND_BUILTIN, ACT_BACK }
};

#define MENU_ENTRY(en, de, arr, ord) \
	{ (en), (de), (arr), (uint8_t)(sizeof(arr) / sizeof((arr)[0])), (ord) }

static const struct word_menu menus[MENU_COUNT] = {
	[MENU_MAIN]          = MENU_ENTRY("COMMAND", "BEFEHL",     main_items,      main_de_order),
	[MENU_FORMAT]        = MENU_ENTRY("FORMAT",  "FORMAT",     format_items,    NULL),
	[MENU_GALLERY]       = MENU_ENTRY("GALLERY", "MUSTER",     gallery_items,   NULL),
	[MENU_HELP]          = MENU_ENTRY("HELP",    "HILFE",      help_items,      NULL),
	[MENU_JUMP]          = MENU_ENTRY("JUMP",    "GEHEZU",     jump_items,      NULL),
	[MENU_LIBRARY]       = MENU_ENTRY("LIBRARY", "BIBLIOTHEK", library_items,   NULL),
	[MENU_PRINT]         = MENU_ENTRY("PRINT",   "DRUCK",      print_items,     NULL),
	[MENU_TRANSFER]      = MENU_ENTRY("TRANSFER","UEBERTRAGEN",transfer_items,  NULL),
	[MENU_WINDOW]        = MENU_ENTRY("WINDOW",  "AUSSCHNITT", window_items,    NULL),
	[MENU_DOC_RETRIEVAL] = MENU_ENTRY("DOC-RET", "DATEI-MGR",  docret_items,    NULL),
	[MENU_SPELL]         = MENU_ENTRY("SPELL",   "RECHTSCHR",  spell_items,     NULL),
	[MENU_THESAURUS]     = MENU_ENTRY("THESAUR", "THESAURUS",  thesaurus_items, NULL),
	[MENU_POSITION]      = MENU_ENTRY("POSITION","POSITION",   position_items,  NULL),
	[MENU_VIEW]          = MENU_ENTRY("PREVIEW", "VORSCHAU",   view_items,      NULL)
};

const struct word_menu *
word_menu_get(enum word_menu_id id)
{
	if ((unsigned)id >= MENU_COUNT)
		return &menus[MENU_MAIN];
	return &menus[id];
}

const struct word_menu_item *
word_menu_item_at(const struct word_menu *m, enum word_lang lang, uint8_t vis_idx)
{
	uint8_t raw_idx;

	if (vis_idx >= m->count)
		return NULL;
	raw_idx = (lang == LANG_DE && m->de_order != NULL)
	    ? m->de_order[vis_idx]
	    : vis_idx;
	return &m->items[raw_idx];
}

int
word_menu_find_accel(const struct word_menu *m, enum word_lang lang, char ch)
{
	int up = toupper((unsigned char)ch);
	uint8_t i;

	for (i = 0; i < m->count; i++) {
		const struct word_menu_item *it = word_menu_item_at(m, lang, i);
		char want = (lang == LANG_DE) ? it->de[0] : it->accel;
		if (toupper((unsigned char)want) == up)
			return (int)i;
	}
	return -1;
}
