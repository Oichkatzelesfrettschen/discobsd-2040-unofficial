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
 * The character classes of hilite.c on a table of lines, each with the class
 * of every character: . plain, p parenthesis, s string, c comment, n number,
 * h character literal, q quote, k keyword, o special form, f function,
 * x constant, E error. A line with chain set starts from the state the line
 * before it left. The symbols of symclass() stand in for the builtin table of
 * ulisp.c, whose classes the interpreter and pty test check.
 *
 * usage: hilite-test
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "platform.h"

static int
symclass(const char *name)
{
	static const char *const functions[] = { "car", "cdr", "cons", "list", NULL };
	static const char *const specials[] = { "defun", "lambda", "let", "if", NULL };

	for (int i = 0; functions[i] != NULL; i++)
		if (strcasecmp(name, functions[i]) == 0)
			return HL_FUNCTION;
	for (int i = 0; specials[i] != NULL; i++)
		if (strcasecmp(name, specials[i]) == 0)
			return HL_SPECIAL;
	return (strcasecmp(name, "nil") == 0 || strcasecmp(name, "t") == 0) ?
	    HL_CONSTANT : HL_PLAIN;
}

static const struct {
	const char *name;
	int chain;
	const char *line;
	const char *classes;
} table[] = {
	{ "call", 0, "(car '(1 2))",
	    "pfff.qpn.npp" },
	{ "special and user symbol", 0, "(defun foo (x) nil)",
	    "pooooo.....p.p.xxxp" },
	{ "constants fold case", 0, "NIL t T",
	    "xxx.x.x" },
	{ "string holds a semicolon and parentheses", 0, "\"a;b (c) #| d\" 1",
	    "ssssssssssssss.n" },
	{ "string escapes", 0, "\"a\\\"b\" \"c\\\\\" 2",
	    "ssssss.sssss.n" },
	{ "line comment after a form", 0, "(car x) ; (x \"y\" #| z",
	    "pfff..p.ccccccccccccc" },
	{ "semicolon ends a token", 0, "a;b c",
	    ".cccc" },
	{ "semicolon inside block comment", 0, "#| ; |# 1",
	    "ccccccc.n" },
	{ "block comment inside a line comment", 0, "; #| not open",
	    "ccccccccccccc" },
	{ "a stray block comment closer is an error", 1, "x |#",
	    "...E" },
	{ "block comment closes", 0, "#| a |# b",
	    "ccccccc.." },
	{ "block comments nest", 0, "#| a #| b |# c |# d",
	    "ccccccccccccccccc.." },
	{ "block comment opened inside a list", 0, "(1 #| ) |# 2)",
	    "pn.ccccccc.np" },
	{ "block comment left open", 0, "(a #| open",
	    "p..ccccccc" },
	{ "block comment continues", 1, "still |# x #| ",
	    "cccccccc...ccc" },
	{ "block comment nesting continues", 1, "#| inner ",
	    "ccccccccc" },
	{ "block comment closes at the second close", 1, "|# |# y",
	    "ccccc.." },
	{ "string left open", 0, "(a \"open (",
	    "p..sssssss" },
	{ "string continues", 1, "rest ; \" b)",
	    "ssssssss..p" },
	{ "character literals", 0, "#\\; #\\( #\\) #\\\" #\\# #\\' #\\a",
	    "hhh.hhh.hhh.hhh.hhh.hhh.hhh" },
	{ "character names", 0, "#\\Space #\\newline)",
	    "hhhhhhh.hhhhhhhhhE" },
	{ "character literal then a paren", 0, "(#\\a)",
	    "phhhp" },
	{ "symbols that look numeric", 0, "1+ - + 1- -x +a a1",
	    ".................." },
	{ "integers", 0, "1 -1 +1 007 2147483647",
	    "n.nn.nn.nnn.nnnnnnnnnn" },
	{ "floats", 0, ".5 -.5 +.5 1. 1.5 -.5e-3 1.5E3",
	    "nn.nnn.nnn.nn.nnn.nnnnnn.nnnnn" },
	{ "exponents", 0, "1e5 1E5 1e+5 1e-5 1e e5 1e5x 1e5.2",
	    "nnn.nnn.nnnn.nnnn................." },
	{ "not numbers", 0, ". .. -. 1/2 12abc 1+2",
	    "....................." },
	{ "consing dot", 0, "(a . b)",
	    "p.....p" },
	{ "radix numbers", 0, "#x1F #xff #b101 #o17 #X1f",
	    "nnnn.nnnn.nnnnn.nnnn.nnnn" },
	{ "radix with a bad digit", 0, "#xG #b102 #x #o8",
	    "................" },
	{ "radix takes no sign", 0, "#x-1",
	    "...." },
	{ "unknown sharpsign", 0, "#+sbcl #<",
	    "E......E." },
	{ "keywords", 0, ":key :initial-element : a:b",
	    "kkkk.kkkkkkkkkkkkkkkk.k...." },
	{ "quotes", 0, "'a '(b) #'car #'(lambda (x) x)",
	    "q..qp.p.qqfff.qqpoooooo.p.p..p" },
	{ "vertical bar is a symbol character", 0, "a|b |a b|",
	    "........." },
	{ "unbalanced closers", 0, ") ()))",
	    "E.ppEE" },
	{ "closer after the open list of an earlier line", 1, "(a",
	    "p." },
	{ "...is balanced here", 1, "))",
	    "pE" },
	{ "delimiters split tokens", 0, "a'b\"c\"d(e)f",
	    ".q.sss.p.p." },
	{ "whitespace", 0, "\ta\t b\n",
	    "......" },
	{ "vector and array marks", 0, "#(1 2) #2A()",
	    "qpn.np.q..pp" },
	{ "hash breaks a token", 0, "a#x1 b",
	    ".nnn.." },
};

static char
letter(int k)
{
	return ".pscnhqkofxE"[k];
}

int
main(void)
{
	struct hl_state state;
	int failures = 0;
	const int count = (int)(sizeof table / sizeof table[0]);

	memset(&state, 0, sizeof state);
	for (int t = 0; t < count; t++) {
		uint8_t cls[256];
		char got[256];
		int n = (int)strlen(table[t].line);

		if (n != (int)strlen(table[t].classes)) {
			printf("hilite: %s: table row has %d characters and %d classes\n",
			    table[t].name, n, (int)strlen(table[t].classes));
			failures++;
			continue;
		}
		if (!table[t].chain)
			memset(&state, 0, sizeof state);
		hl_scan(&state, table[t].line, n, cls, symclass);
		for (int i = 0; i < n; i++)
			got[i] = letter(cls[i]);
		got[n] = '\0';
		if (strcmp(got, table[t].classes) != 0) {
			printf("hilite: %s\n  line %s\n  want %s\n  got  %s\n",
			    table[t].name, table[t].line, table[t].classes, got);
			failures++;
		}
	}
	if (failures == 0)
		printf("hilite: %d lines classify as the table says\n", count);
	return failures != 0;
}
