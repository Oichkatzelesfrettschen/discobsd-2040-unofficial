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
 * word_find.c: the search pattern compiler and matcher (SPEC.md, Command
 * menu: search text).
 */

#include "word.h"

#include <ctype.h>

static int
is_word_char(unsigned char c)
{
	return isalnum(c) || c == '_';
}

static int
chars_equal(unsigned char a, unsigned char b, int match_case)
{
	if (match_case)
		return a == b;
	return tolower(a) == tolower(b);
}

void
word_pat_compile(struct word_pattern *p, const char *text,
    int match_case, int whole_word)
{
	size_t i = 0;

	p->len = 0;
	p->match_case = match_case ? 1 : 0;
	p->whole_word = whole_word ? 1 : 0;

	while (text[i] != '\0' && p->len < WORD_PAT_MAX) {
		char c = text[i++];
		if (c == '?') {
			p->atoms[p->len].kind = ATOM_ANY;
			p->atoms[p->len].ch = 0;
			p->len++;
		} else if (c == '^') {
			char n = text[i];
			if (n == '\0') {
				p->atoms[p->len].kind = ATOM_CH;
				p->atoms[p->len].ch = '^';
				p->len++;
			} else {
				i++;
				switch (n) {
				case 'w':
				case 'W':
					p->atoms[p->len].kind = ATOM_WHITERUN;
					p->atoms[p->len].ch = 0;
					p->len++;
					break;
				case 's':
				case 'S':
					p->atoms[p->len].kind = ATOM_CH;
					p->atoms[p->len].ch = ' ';
					p->len++;
					break;
				case 't':
				case 'T':
					p->atoms[p->len].kind = ATOM_CH;
					p->atoms[p->len].ch = '\t';
					p->len++;
					break;
				case '-':
					p->atoms[p->len].kind = ATOM_CH;
					p->atoms[p->len].ch = '-';
					p->len++;
					break;
				case 'p':
				case 'P':
				case 'n':
				case 'N':
				case 'd':
				case 'D':
					p->atoms[p->len].kind = ATOM_BREAK;
					p->atoms[p->len].ch = '\n';
					p->len++;
					break;
				default:
					p->atoms[p->len].kind = ATOM_CH;
					p->atoms[p->len].ch = '^';
					p->len++;
					if (p->len < WORD_PAT_MAX) {
						p->atoms[p->len].kind = ATOM_CH;
						p->atoms[p->len].ch = n;
						p->len++;
					}
					break;
				}
			}
		} else {
			p->atoms[p->len].kind = ATOM_CH;
			p->atoms[p->len].ch = c;
			p->len++;
		}
	}
}

static int
match_at(const struct word_pattern *p, const char *doc, size_t doc_len,
    size_t at, size_t *end_out)
{
	size_t pos = at;
	size_t k;

	for (k = 0; k < p->len; k++) {
		const struct word_atom *a = &p->atoms[k];
		switch (a->kind) {
		case ATOM_CH:
			if (pos >= doc_len ||
			    !chars_equal((unsigned char)doc[pos],
			    (unsigned char)a->ch, p->match_case))
				return 0;
			pos++;
			break;
		case ATOM_ANY:
			if (pos >= doc_len || doc[pos] == '\n')
				return 0;
			pos++;
			break;
		case ATOM_BREAK:
			if (pos >= doc_len || doc[pos] != '\n')
				return 0;
			pos++;
			break;
		case ATOM_WHITERUN: {
			size_t start = pos;
			while (pos < doc_len &&
			    (doc[pos] == ' ' || doc[pos] == '\t'))
				pos++;
			if (pos == start)
				return 0;
			break;
		}
		default:
			return 0;
		}
	}

	if (p->whole_word) {
		if (at > 0 && is_word_char((unsigned char)doc[at - 1]))
			return 0;
		if (pos < doc_len && is_word_char((unsigned char)doc[pos]))
			return 0;
	}

	*end_out = pos;
	return 1;
}

int
word_pat_find(const struct word_pattern *p, const char *doc, size_t doc_len,
    size_t from, int dir_down, struct word_match *out)
{
	size_t pos;
	size_t end;

	if (p->len == 0)
		return 0;

	if (dir_down) {
		if (from > doc_len)
			return 0;
		for (pos = from; pos <= doc_len; pos++) {
			if (match_at(p, doc, doc_len, pos, &end)) {
				out->start = pos;
				out->end = end;
				return 1;
			}
		}
		return 0;
	}

	if (from == 0)
		return 0;
	pos = (from > doc_len) ? doc_len : from - 1;
	for (;;) {
		if (match_at(p, doc, doc_len, pos, &end)) {
			out->start = pos;
			out->end = end;
			return 1;
		}
		if (pos == 0)
			break;
		pos--;
	}
	return 0;
}
