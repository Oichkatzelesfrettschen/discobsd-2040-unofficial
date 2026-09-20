/*-
 * Copyright (c) 1985, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by Dave Yost.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* The RP2040 process window keeps every input-dependent structure bounded:
 * one 4,096-byte line, 100 argument-backed symbols, and 64 iterative frames.
 * #elif requires expression selection and therefore returns an error. The
 * check-unifdef-contracts gate pins each bound and verdict. */
#define UNIFDEF_MAX_DEPTH 64
#define UNIFDEF_MAX_LINE 4096
#define UNIFDEF_MAX_SYMBOLS 100

#define UNIFDEF_UNCHANGED 0
#define UNIFDEF_CHANGED 1
#define UNIFDEF_ERROR 2

enum directive_kind {
	DIRECTIVE_PLAIN,
	DIRECTIVE_IF,
	DIRECTIVE_IFDEF,
	DIRECTIVE_IFNDEF,
	DIRECTIVE_ELIF,
	DIRECTIVE_ELSE,
	DIRECTIVE_ENDIF
};

enum frame_kind {
	FRAME_UNKNOWN,
	FRAME_KNOWN,
	FRAME_IGNORED
};

enum lexical_state {
	LEXICAL_CODE,
	LEXICAL_BLOCK_COMMENT,
	LEXICAL_LINE_COMMENT,
	LEXICAL_STRING,
	LEXICAL_CHARACTER
};

struct directive {
	enum directive_kind kind;
	size_t symbol_offset;
	size_t symbol_length;
	size_t prefix_length;
	size_t suffix_offset;
};

struct conditional_frame {
	signed char symbol_index;
	unsigned char kind;
	unsigned char parent_emits;
	unsigned char parent_scans;
	unsigned char condition_true;
	unsigned char saw_else;
};

static const char *symbol_names[UNIFDEF_MAX_SYMBOLS];
static unsigned char symbol_defined[UNIFDEF_MAX_SYMBOLS];
static unsigned char symbol_ignored[UNIFDEF_MAX_SYMBOLS];
static size_t symbol_count;

static struct conditional_frame conditional_stack[UNIFDEF_MAX_DEPTH];
static size_t conditional_depth;
static int content_emits;
static int content_scans;

static unsigned char line_buffer[UNIFDEF_MAX_LINE];
static enum lexical_state lexical_state;
static int blank_deleted_lines;
static int complement_output;
static int text_input;

void unifdef_reset(void);
int unifdef_add_symbol(const char *, int, int);
int unifdef_process(FILE *, FILE *, const char *);
int main(int, char **);

static int unifdef_error(const char *, unsigned long, const char *);
static int unifdef_usage(const char *);
static int symbol_name_valid(const char *);
static int symbol_lookup(const unsigned char *, size_t);
static int read_line(FILE *, size_t *);
static int directive_space(unsigned char);
static size_t skip_directive_spacing(const unsigned char *, size_t, size_t,
	int *);
static size_t directive_comment_suffix(const unsigned char *, size_t, size_t);
static int line_comment_continues(const unsigned char *, size_t);
static struct directive parse_directive(const unsigned char *, size_t,
	enum lexical_state);
static void scan_lexical_state(const unsigned char *, size_t, int);
static int emit_line(FILE *, const unsigned char *, size_t, int, int *);
static int push_conditional(enum frame_kind, int, int);
static int enter_else(void);
static void pop_conditional(void);

void
unifdef_reset(void)
{
	size_t symbol_index;

	for (symbol_index = 0; symbol_index < UNIFDEF_MAX_SYMBOLS;
	    symbol_index++) {
		symbol_names[symbol_index] = NULL;
		symbol_defined[symbol_index] = 0;
		symbol_ignored[symbol_index] = 0;
	}
	symbol_count = 0;
	conditional_depth = 0;
	content_emits = 1;
	content_scans = 1;
	lexical_state = LEXICAL_CODE;
	blank_deleted_lines = 0;
	complement_output = 0;
	text_input = 0;
}

static int
symbol_name_valid(const char *name)
{
	const unsigned char *cursor = (const unsigned char *)name;

	if (!(isalpha(*cursor) || *cursor == '_'))
		return 0;
	cursor++;
	while (*cursor != '\0') {
		if (!(isalnum(*cursor) || *cursor == '_'))
			return 0;
		cursor++;
	}
	return 1;
}

int
unifdef_add_symbol(const char *name, int is_defined, int is_ignored)
{
	size_t symbol_index;

	if (!symbol_name_valid(name))
		return -1;
	for (symbol_index = 0; symbol_index < symbol_count; symbol_index++) {
		if (strcmp(symbol_names[symbol_index], name) == 0)
			return 0;
	}
	if (symbol_count == UNIFDEF_MAX_SYMBOLS)
		return -1;
	symbol_names[symbol_count] = name;
	symbol_defined[symbol_count] = is_defined != 0;
	symbol_ignored[symbol_count] = is_ignored != 0;
	symbol_count++;
	return 0;
}

static int
symbol_lookup(const unsigned char *name, size_t name_length)
{
	size_t frame_index;
	size_t symbol_index;

	for (symbol_index = 0; symbol_index < symbol_count; symbol_index++) {
		if (strlen(symbol_names[symbol_index]) == name_length &&
		    memcmp(symbol_names[symbol_index], name, name_length) == 0)
			break;
	}
	if (symbol_index == symbol_count)
		return -1;
	/* The stack owns active-symbol state. Scanning it saves a second mutable
	 * table and prevents an error return from leaving a stale active bit. */
	for (frame_index = 0; frame_index < conditional_depth; frame_index++) {
		if (conditional_stack[frame_index].symbol_index ==
		    (signed char)symbol_index)
			return -1;
	}
	return (int)symbol_index;
}

static int
read_line(FILE *input, size_t *line_length)
{
	int character;
	size_t length = 0;

	while (length < sizeof(line_buffer)) {
		character = fgetc(input);
		if (character == EOF) {
			if (ferror(input))
				return -1;
			*line_length = length;
			return length == 0 ? 0 : 1;
		}
		line_buffer[length++] = (unsigned char)character;
		if (character == '\n') {
			*line_length = length;
			return 1;
		}
	}
	character = fgetc(input);
	if (character == EOF && !ferror(input)) {
		*line_length = length;
		return 1;
	}
	return ferror(input) ? -1 : -2;
}

static int
identifier_character(unsigned char character)
{
	return isalnum(character) || character == '_';
}

static size_t
skip_directive_spacing(const unsigned char *line, size_t line_length,
	size_t cursor, int *saw_comment)
{
	for (;;) {
		while (cursor < line_length && directive_space(line[cursor]))
			cursor++;
		if (text_input)
			return cursor;
		if (cursor + 1 >= line_length || line[cursor] != '/' ||
		    line[cursor + 1] != '*')
			return cursor;
		if (saw_comment != NULL)
			*saw_comment = 1;
		cursor += 2;
		while (cursor + 1 < line_length &&
		    !(line[cursor] == '*' && line[cursor + 1] == '/'))
			cursor++;
		if (cursor + 1 >= line_length)
			return line_length;
		cursor += 2;
	}
}

static int
directive_space(unsigned char character)
{
	return character == ' ' || character == '\t' || character == '\v' ||
	    character == '\f';
}

static size_t
directive_comment_suffix(const unsigned char *line, size_t line_length,
	size_t cursor)
{
	if (text_input)
		return line_length;
	while (cursor < line_length && directive_space(line[cursor]))
		cursor++;
	if (cursor + 1 < line_length && line[cursor] == '/' &&
	    (line[cursor + 1] == '*' || line[cursor + 1] == '/'))
		return cursor;
	return line_length;
}

static int
line_comment_continues(const unsigned char *line, size_t line_length)
{
	size_t cursor = line_length;

	if (cursor == 0 || line[cursor - 1] != '\n')
		return 0;
	cursor--;
	if (cursor != 0 && line[cursor - 1] == '\r')
		cursor--;
	return cursor != 0 && line[cursor - 1] == '\\';
}

static struct directive
parse_directive(const unsigned char *line, size_t line_length,
	enum lexical_state starting_state)
{
	static const struct {
		const char *name;
		enum directive_kind kind;
	} keywords[] = {
		{ "if", DIRECTIVE_IF },
		{ "ifdef", DIRECTIVE_IFDEF },
		{ "ifndef", DIRECTIVE_IFNDEF },
		{ "elif", DIRECTIVE_ELIF },
		{ "else", DIRECTIVE_ELSE },
		{ "endif", DIRECTIVE_ENDIF }
	};
	struct directive result = {
		DIRECTIVE_PLAIN, 0, 0, 0, line_length
	};
	size_t cursor = 0;
	size_t hash_offset;
	size_t keyword_start;
	size_t keyword_length;
	size_t keyword_index;
	int prefix_has_comment = 0;

	if (starting_state == LEXICAL_BLOCK_COMMENT) {
		while (cursor + 1 < line_length &&
		    !(line[cursor] == '*' && line[cursor + 1] == '/'))
			cursor++;
		if (cursor + 1 >= line_length)
			return result;
		cursor += 2;
		prefix_has_comment = 1;
	} else if (starting_state != LEXICAL_CODE) {
		return result;
	}
	cursor = skip_directive_spacing(line, line_length, cursor,
	    &prefix_has_comment);
	if (cursor == line_length || line[cursor] != '#')
		return result;
	hash_offset = cursor;
	cursor++;
	cursor = skip_directive_spacing(line, line_length, cursor, NULL);
	keyword_start = cursor;
	while (cursor < line_length && identifier_character(line[cursor]))
		cursor++;
	keyword_length = cursor - keyword_start;
	for (keyword_index = 0;
	    keyword_index < sizeof(keywords) / sizeof(keywords[0]);
	    keyword_index++) {
		if (strlen(keywords[keyword_index].name) == keyword_length &&
		    memcmp(keywords[keyword_index].name,
		    line + keyword_start, keyword_length) == 0) {
			result.kind = keywords[keyword_index].kind;
			break;
		}
	}
	if (result.kind != DIRECTIVE_PLAIN && prefix_has_comment) {
		result.prefix_length = hash_offset;
		while (result.prefix_length != 0 &&
		    directive_space(line[result.prefix_length - 1]))
			result.prefix_length--;
	}
	if (result.kind != DIRECTIVE_IFDEF &&
	    result.kind != DIRECTIVE_IFNDEF) {
		result.suffix_offset = directive_comment_suffix(line,
		    line_length, cursor);
		return result;
	}
	cursor = skip_directive_spacing(line, line_length, cursor, NULL);
	result.symbol_offset = cursor;
	if (cursor < line_length &&
	    (isalpha(line[cursor]) || line[cursor] == '_')) {
		cursor++;
		while (cursor < line_length && identifier_character(line[cursor]))
			cursor++;
		result.symbol_length = cursor - result.symbol_offset;
	}
	result.suffix_offset = directive_comment_suffix(line, line_length,
	    cursor);
	return result;
}

static void
scan_lexical_state(const unsigned char *line, size_t line_length,
	int continuation_only)
{
	size_t cursor = 0;

	if (text_input || (continuation_only && lexical_state == LEXICAL_CODE))
		return;
	while (cursor < line_length &&
	    (!continuation_only || lexical_state != LEXICAL_CODE)) {
		unsigned char character = line[cursor];
		unsigned char next = cursor + 1 < line_length ? line[cursor + 1] : 0;

		if (lexical_state == LEXICAL_LINE_COMMENT) {
			cursor = line_length;
			continue;
		}
		if (lexical_state == LEXICAL_BLOCK_COMMENT) {
			if (character == '*' && next == '/') {
				lexical_state = LEXICAL_CODE;
				cursor += 2;
			} else {
				cursor++;
			}
			continue;
		}
		if (lexical_state == LEXICAL_STRING ||
		    lexical_state == LEXICAL_CHARACTER) {
			unsigned char terminator = lexical_state == LEXICAL_STRING ?
			    '"' : '\'';

			if (character == '\\' && cursor + 1 < line_length) {
				cursor += 2;
				continue;
			}
			if (character == terminator)
				lexical_state = LEXICAL_CODE;
			cursor++;
			continue;
		}
		if (character == '/' && next == '*') {
			lexical_state = LEXICAL_BLOCK_COMMENT;
			cursor += 2;
		} else if (character == '/' && next == '/') {
			lexical_state = LEXICAL_LINE_COMMENT;
			cursor = line_length;
		} else if (character == '"') {
			lexical_state = LEXICAL_STRING;
			cursor++;
		} else if (character == '\'') {
			lexical_state = LEXICAL_CHARACTER;
			cursor++;
		} else {
			cursor++;
		}
	}
	if (lexical_state == LEXICAL_LINE_COMMENT &&
	    !line_comment_continues(line, line_length))
		lexical_state = LEXICAL_CODE;
}

static int
emit_line(FILE *output, const unsigned char *line, size_t line_length,
	int raw_keep, int *changed)
{
	int keep = complement_output ? !raw_keep : raw_keep;

	if (keep) {
		if (line_length != 0 &&
		    fwrite(line, 1, line_length, output) != line_length)
			return UNIFDEF_ERROR;
		return UNIFDEF_UNCHANGED;
	}
	if (blank_deleted_lines && line_length != 0 &&
	    line[line_length - 1] == '\n') {
		if (fputc('\n', output) == EOF)
			return UNIFDEF_ERROR;
		if (line_length != 1 || line[0] != '\n')
			*changed = 1;
	} else if (line_length != 0) {
		*changed = 1;
	}
	return UNIFDEF_UNCHANGED;
}

static int
push_conditional(enum frame_kind frame_kind, int symbol_index,
	int condition_true)
{
	struct conditional_frame *frame;

	if (conditional_depth == UNIFDEF_MAX_DEPTH)
		return -1;
	frame = &conditional_stack[conditional_depth++];
	frame->kind = (unsigned char)frame_kind;
	frame->symbol_index = (signed char)symbol_index;
	frame->parent_emits = (unsigned char)content_emits;
	frame->parent_scans = (unsigned char)content_scans;
	frame->condition_true = (unsigned char)(condition_true != 0);
	frame->saw_else = 0;
	if (frame_kind == FRAME_KNOWN)
		content_emits = content_emits && condition_true;
	else if (frame_kind == FRAME_IGNORED)
		content_scans = content_scans && condition_true;
	return 0;
}

static int
enter_else(void)
{
	struct conditional_frame *frame;

	if (conditional_depth == 0)
		return -1;
	frame = &conditional_stack[conditional_depth - 1];
	if (frame->saw_else)
		return -1;
	frame->saw_else = 1;
	if (frame->kind == FRAME_KNOWN)
		content_emits = frame->parent_emits && !frame->condition_true;
	else if (frame->kind == FRAME_IGNORED)
		content_scans = frame->parent_scans && !frame->condition_true;
	else {
		content_emits = frame->parent_emits;
		content_scans = frame->parent_scans;
	}
	return 0;
}

static void
pop_conditional(void)
{
	struct conditional_frame *frame =
	    &conditional_stack[conditional_depth - 1];

	content_emits = frame->parent_emits;
	content_scans = frame->parent_scans;
	conditional_depth--;
}

static int
unifdef_error(const char *source_name, unsigned long line_number,
	const char *message)
{
	if (line_number == 0)
		(void)fprintf(stderr, "unifdef: %s: %s\n", source_name, message);
	else
		(void)fprintf(stderr, "unifdef: %s:%lu: %s\n", source_name,
		    line_number, message);
	return UNIFDEF_ERROR;
}

int
unifdef_process(FILE *input, FILE *output, const char *source_name)
{
	unsigned long line_number = 0;
	int changed = 0;
	int read_result;
	size_t line_length;

	conditional_depth = 0;
	content_emits = 1;
	content_scans = 1;
	lexical_state = LEXICAL_CODE;
	while ((read_result = read_line(input, &line_length)) != 0) {
		struct directive directive;
		enum lexical_state starting_state;
		int emit_result;
		int line_emits;
		int line_scans;
		int raw_keep = 0;

		if (read_result < 0)
			return unifdef_error(source_name, line_number + 1,
			    read_result == -2 ? "line exceeds 4096 bytes" :
			    "input read error");
		line_number++;
		starting_state = lexical_state;
		line_emits = content_emits;
		line_scans = content_scans;
		directive = parse_directive(line_buffer, line_length,
		    starting_state);
		if (line_scans)
			scan_lexical_state(line_buffer, line_length, 0);
		else
			scan_lexical_state(line_buffer, line_length, 1);
		switch (directive.kind) {
		case DIRECTIVE_PLAIN:
			raw_keep = content_emits;
			break;

		case DIRECTIVE_IF:
			raw_keep = content_emits;
			if (push_conditional(FRAME_UNKNOWN, -1, 0) != 0)
				return unifdef_error(source_name, line_number,
				    "conditional nesting exceeds 64");
			break;

		case DIRECTIVE_IFDEF:
		case DIRECTIVE_IFNDEF: {
			int symbol_index;
			enum frame_kind frame_kind;
			int condition_true = 0;

			if (directive.symbol_length == 0)
				return unifdef_error(source_name, line_number,
				    "missing conditional symbol");
			symbol_index = symbol_lookup(
			    line_buffer + directive.symbol_offset,
			    directive.symbol_length);
			if (symbol_index < 0) {
				frame_kind = FRAME_UNKNOWN;
			} else {
				condition_true =
				    symbol_defined[(size_t)symbol_index];
				if (directive.kind == DIRECTIVE_IFNDEF)
					condition_true = !condition_true;
				frame_kind = symbol_ignored[(size_t)symbol_index] ?
				    FRAME_IGNORED : FRAME_KNOWN;
			}
			raw_keep = content_emits && frame_kind != FRAME_KNOWN;
			if (push_conditional(frame_kind, symbol_index,
			    condition_true) != 0)
				return unifdef_error(source_name, line_number,
				    "conditional nesting exceeds 64");
			break;
		}

		case DIRECTIVE_ELIF:
			return unifdef_error(source_name, line_number,
			    "unsupported #elif");

		case DIRECTIVE_ELSE: {
			struct conditional_frame *frame;

			if (conditional_depth == 0)
				return unifdef_error(source_name, line_number,
				    "inappropriate #else");
			frame = &conditional_stack[conditional_depth - 1];
			raw_keep = frame->parent_emits &&
			    frame->kind != FRAME_KNOWN;
			if (enter_else() != 0)
				return unifdef_error(source_name, line_number,
				    "duplicate #else");
			break;
		}

		case DIRECTIVE_ENDIF: {
			struct conditional_frame *frame;

			if (conditional_depth == 0)
				return unifdef_error(source_name, line_number,
				    "inappropriate #endif");
			frame = &conditional_stack[conditional_depth - 1];
			raw_keep = frame->parent_emits &&
			    frame->kind != FRAME_KNOWN;
			pop_conditional();
			break;
		}
		}
		if (!line_scans &&
		    (directive.kind == DIRECTIVE_ELSE ||
		    directive.kind == DIRECTIVE_ENDIF) &&
		    directive.suffix_offset < line_length)
			scan_lexical_state(line_buffer + directive.suffix_offset,
			    line_length - directive.suffix_offset, 0);
		if (!(complement_output ? !raw_keep : raw_keep) &&
		    !complement_output &&
		    ((line_emits && directive.prefix_length != 0) ||
		    (content_emits &&
		    directive.suffix_offset < line_length))) {
			int prefix_emitted = line_emits &&
			    directive.prefix_length != 0;
			int suffix_emitted = content_emits &&
			    directive.suffix_offset < line_length;

			if (prefix_emitted &&
			    fwrite(line_buffer, 1, directive.prefix_length,
			    output) != directive.prefix_length)
				return unifdef_error(source_name, line_number,
				    "output write error");
			if (suffix_emitted) {
				size_t suffix_length = line_length -
				    directive.suffix_offset;

				if (fwrite(line_buffer + directive.suffix_offset, 1,
				    suffix_length, output) != suffix_length)
					return unifdef_error(source_name,
					    line_number, "output write error");
			} else if (prefix_emitted &&
			    line_length != 0 &&
			    line_buffer[line_length - 1] == '\n' &&
			    fputc('\n', output) == EOF) {
				return unifdef_error(source_name, line_number,
				    "output write error");
			}
			changed = 1;
			continue;
		}
		emit_result = emit_line(output, line_buffer, line_length,
		    raw_keep, &changed);
		if (emit_result == UNIFDEF_ERROR)
			return unifdef_error(source_name, line_number,
			    "output write error");
	}
	if (lexical_state == LEXICAL_LINE_COMMENT)
		lexical_state = LEXICAL_CODE;
	if (conditional_depth != 0)
		return unifdef_error(source_name, line_number,
		    "premature EOF in conditional");
	if (!text_input && lexical_state != LEXICAL_CODE)
		return unifdef_error(source_name, line_number,
		    lexical_state == LEXICAL_BLOCK_COMMENT ?
		    "premature EOF in comment" :
		    "premature EOF in quoted text");
	if (fflush(output) == EOF)
		return unifdef_error(source_name, line_number, "output flush error");
	return changed ? UNIFDEF_CHANGED : UNIFDEF_UNCHANGED;
}

static int
unifdef_usage(const char *program_name)
{
	(void)fprintf(stderr,
	    "usage: %s [-l] [-t] [-c] [-Dsym] [-Usym] "
	    "[-iDsym] [-iUsym] [file]\n", program_name);
	return UNIFDEF_ERROR;
}

int
main(int argc, char **argv)
{
	const char *program_name = argv[0][0] != '\0' ? argv[0] : "unifdef";
	const char *input_name = "[stdin]";
	FILE *input = stdin;
	int argument_index;
	int result;

	unifdef_reset();
	for (argument_index = 1; argument_index < argc; argument_index++) {
		const char *argument = argv[argument_index];
		const char *symbol;
		int is_defined;
		int is_ignored = 0;

		if (argument[0] != '-')
			break;
		if (strcmp(argument, "-l") == 0) {
			blank_deleted_lines = 1;
			continue;
		}
		if (strcmp(argument, "-t") == 0) {
			text_input = 1;
			continue;
		}
		if (strcmp(argument, "-c") == 0) {
			complement_output = 1;
			continue;
		}
		if (argument[1] == 'i') {
			is_ignored = 1;
			argument++;
		}
		if ((argument[1] != 'D' && argument[1] != 'U') ||
		    argument[2] == '\0')
			return unifdef_usage(program_name);
		is_defined = argument[1] == 'D';
		symbol = argument + 2;
		if (unifdef_add_symbol(symbol, is_defined, is_ignored) != 0)
			return unifdef_error("arguments", 0,
			    symbol_count == UNIFDEF_MAX_SYMBOLS ?
			    "too many symbols" : "invalid symbol");
	}
	if (symbol_count == 0 || argc - argument_index > 1)
		return unifdef_usage(program_name);
	if (argument_index < argc) {
		input_name = argv[argument_index];
		input = fopen(input_name, "r");
		if (input == NULL)
			return unifdef_error(input_name, 0, "cannot open input");
	}
	result = unifdef_process(input, stdout, input_name);
	if (input != stdin && fclose(input) == EOF)
		result = unifdef_error(input_name, 0, "input close error");
	return result;
}
