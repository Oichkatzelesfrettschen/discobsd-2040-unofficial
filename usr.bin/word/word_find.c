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
 * menu: search text). The matcher reads the document through buf_at(), so
 * it searches a document of any size. There is no repetition operator.
 */

#include "word.h"

#include <ctype.h>

static int
is_word_char(int c)
{
	return c >= 0 && (isalnum(c) || c == '_');
}

static int
chars_equal(int a, int b, int match_case)
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
				case '^':
				case '?':
					p->atoms[p->len].kind = ATOM_CH;
					p->atoms[p->len].ch = n;
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
match_at(const struct word_pattern *p, woff_t at, woff_t *end_out)
{
	woff_t pos = at;
	size_t k;
	int c;

	for (k = 0; k < p->len; k++) {
		const struct word_atom *a = &p->atoms[k];

		c = buf_at(pos);
		switch (a->kind) {
		case ATOM_CH:
			if (c < 0 || !chars_equal(c, (unsigned char)a->ch,
			    p->match_case))
				return 0;
			pos++;
			break;
		case ATOM_ANY:
			if (c < 0 || c == '\n')
				return 0;
			pos++;
			break;
		case ATOM_BREAK:
			if (c != '\n')
				return 0;
			pos++;
			break;
		case ATOM_WHITERUN:
			if (c != ' ' && c != '\t')
				return 0;
			while ((c = buf_at(pos)) == ' ' || c == '\t')
				pos++;
			break;
		default:
			return 0;
		}
	}
	if (p->whole_word) {
		if (at > 0 && is_word_char(buf_at(at - 1)))
			return 0;
		if (is_word_char(buf_at(pos)))
			return 0;
	}
	*end_out = pos;
	return 1;
}

int
word_pat_find(const struct word_pattern *p, woff_t from, int down,
    woff_t *start, woff_t *end)
{
	woff_t len = buf_len(), pos;

	if (p->len == 0)
		return 0;
	if (down) {
		for (pos = from; pos < len; pos++)
			if (match_at(p, pos, end)) {
				*start = pos;
				return 1;
			}
		return 0;
	}
	if (from == 0)
		return 0;
	pos = from > len ? len : from;
	while (pos-- > 0)
		if (match_at(p, pos, end)) {
			*start = pos;
			return 1;
		}
	return 0;
}
