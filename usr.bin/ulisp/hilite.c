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
 * Classes of the characters of a line of Lisp for the terminal colors of the
 * line editor (unix.c), following the reader of ulisp.c: the delimiters of a
 * token, the strings with their escapes, the semicolon and nesting #| |#
 * comments of CLHS 2.4.4 and 2.4.8.19, the character and radix syntax of
 * CLHS 2.4.8, and the numbers nextitem() accepts; the # of #( #* #. and #nA
 * is a reader macro mark and what follows classifies as a token. A symbol that is
 * not a number, a keyword or an escape sequence is classified by the
 * interpreter's own builtin table (symclass), so the colors follow the
 * interpreter. A line is scanned from the state the lines before it left,
 * so a string or a block comment that continues over a line end stays one.
 */
#include <string.h>

#include "platform.h"

#define SPACE(c)	((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t')
#define DELIM(c)	(SPACE(c) || (c) == '(' || (c) == ')' || (c) == '"' || \
			    (c) == '#' || (c) == '\'' || (c) == ';')
#define TOKMAX		33	/* The reader keeps this many characters of a token. */

static int
digit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 16;
}

/* True when t, len characters, is a number nextitem() reads in base. */
static bool
number(const char *t, int len, int base)
{
#define T(i)	((i) < len ? t[i] : 0)
	int i = (base == 10 && (T(0) == '+' || T(0) == '-')) ? 1 : 0;
	int valid = (T(i) == '.') ? 0 : (digit(T(i)) < base) ? 1 : -1;
	bool exponent = false;

	for (; i < len; i++) {
		int c = t[i];

		if (base == 10 && c == '.' && !exponent)
			continue;
		if (base == 10 && (c == 'e' || c == 'E')) {
			exponent = true;
			valid = (valid == 1) ? 0 : -1;
		} else if (exponent && (c == '-' || c == '+'))
			continue;
		else
			valid = (digit(c) < base && valid != -1) ? 1 : -1;
	}
	return valid == 1;
}

/* Sets cls[0..n) to the class of each character of s, updating *st. */
void
hl_scan(struct hl_state *st, const char *s, int n, uint8_t *cls,
    int (*symclass)(const char *))
{
	int i = 0;

	while (i < n) {
		int c = (unsigned char)s[i], j = i + 1, k = HL_PLAIN;
		int c2 = (j < n) ? (unsigned char)s[j] : 0;

		if (st->block) {
			k = HL_COMMENT;
			if (c == '|' && c2 == '#') {
				st->block--;
				j++;
			} else if (c == '#' && c2 == '|') {
				st->block++;
				j++;
			}
		} else if (st->instring) {
			k = HL_STRING;
			for (j = i; j < n;) {
				c = s[j++];
				if (c == '\\' && j < n)
					j++;
				else if (c == '"') {
					st->instring = 0;
					break;
				}
			}
		} else if (SPACE(c)) {
			k = HL_PLAIN;
		} else if (c == ';') {
			k = HL_COMMENT;
			j = n;
		} else if (c == '(') {
			k = HL_PAREN;
			st->depth++;
		} else if (c == ')') {
			k = st->depth ? HL_PAREN : HL_ERROR;
			if (st->depth)
				st->depth--;
		} else if (c == '\'') {
			k = HL_QUOTE;
		} else if (c == '"') {
			k = HL_STRING;
			st->instring = 1;
		} else if (c == '#') {
			int base = 0;

			k = HL_QUOTE;
			if (c2 == '|') {
				k = HL_COMMENT;
				st->block = 1;
				j++;
			} else if (c2 == '\'') {
				j++;
			} else if (c2 == '\\') {
				k = HL_CHAR;
				j++;
				if (j < n && DELIM(s[j]))
					j++;
				else
					while (j < n && !DELIM(s[j]))
						j++;
			} else if (c2 != '(' && c2 != '*' && c2 != '.' &&
			    (c2 < '1' || c2 > '9')) {
				base = ((c2 | 0x20) == 'b') ? 2 :
				    ((c2 | 0x20) == 'o') ? 8 :
				    ((c2 | 0x20) == 'x') ? 16 : 0;
				k = HL_ERROR;
				if (base != 0) {
					int d = ++j;

					while (j < n && !DELIM(s[j]))
						j++;
					k = number(s + d, j - d, base) ?
					    HL_NUMBER : HL_PLAIN;
				}
			}
		} else {
			for (j = i; j < n && j - i < TOKMAX && !DELIM(s[j]);)
				j++;
			if (number(s + i, j - i, 10))
				k = HL_NUMBER;
			else if (c == ':')
				k = HL_KEYWORD;
			else {
				char token[TOKMAX + 1];

				memcpy(token, s + i, (size_t)(j - i));
				token[j - i] = '\0';
				k = symclass(token);
			}
		}
		memset(cls + i, k, (size_t)(j - i));
		i = j;
	}
}
