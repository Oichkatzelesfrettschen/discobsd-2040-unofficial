/* See LICENSE file for copyright and license details. */
/* LICENSE: usr.bin/deroff/LICENSE (Caldera ancient-Unix grant, BSD-style). */
/*
 * Strip troff, eqn and tbl markup from a document, leaving running
 * text (or, with -w, one word per line). Origin: Seventh Edition
 * Unix, usr/src/cmd/deroff.c on the titor-special_torrent
 * uv7swre.zip / unix_v7_rl.dsk V7 RL02 image
 * (UNIX-Source-Code/uv7swre.zip), version 1.02, 24 July 1978.
 * Rewritten here with ANSI prototypes and forward declarations for
 * Smaller C, which rejects the original's K&R parameter lists and
 * needs a function declared before its first use; the character
 * classification, macro/eqn/tbl skipping and backslash-construction
 * logic are unchanged from the V7 source.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deroff command -- strip troff, eqn, and Tbl sequences from
   a file. Has one flag argument, -w, to cause output one word per
   line rather than in the original format. Deroff follows .so and
   .nx commands, removes contents of macro definitions, equations
   (both .EQ ... .EN and $...$), Tbl command sequences, and Troff
   backslash constructions.

   All input is through the C macro; the most recently read character
   is in c. */

static int eof(void);
static int skeqn(void);

#define C ( (c = getc(infile)) == EOF ? eof() : ((c == ldelim) && (filesp == files) ? skeqn() : c) )
#define C1 ( (c = getc(infile)) == EOF ? eof() : c)
#define SKIP while (C != '\n')

#define YES 1
#define NO 0

#define NOCHAR -2
#define SPECIAL 0
#define APOS 1
#define DIGIT 2
#define LETTER 3

static int wordflag = NO;
static int inmacro = NO;
static int intable = NO;

static char chars[128]; /* SPECIAL, APOS, DIGIT, or LETTER */

static char line[512];
static char *lp;

static int c;
static int ldelim = NOCHAR;
static int rdelim = NOCHAR;

static int argc;
static char **argv;

static char fname[50];
static FILE *files[15];
static FILE **filesp;
static FILE *infile;

static FILE *opn(char *p);
static void getfname(void);
static void fatal(char *s, char *p);
static void work(void);
static void regline(int macline);
static void putmac(char *s);
static void putwords(int macline);
static void comline(void);
static void macro(void);
static void tbl(void);
static void eqn(void);
static void backsl(void);
static char *copys(char *s);

int
main(int ac, char **av)
{
	int i;
	char *p;
	static char onechar[2] = "X";

	argc = ac - 1;
	argv = av + 1;

	while (argc > 0 && argv[0][0] == '-' && argv[0][1] != '\0') {
		for (p = argv[0] + 1; *p; ++p)
			switch (*p) {
			case 'w':
				wordflag = YES;
				break;
			default:
				onechar[0] = *p;
				fatal("Invalid flag %s\n", onechar);
			}
		--argc;
		++argv;
	}

	if (argc == 0)
		infile = stdin;
	else {
		infile = opn(argv[0]);
		--argc;
		++argv;
	}

	files[0] = infile;
	filesp = &files[0];

	for (i = 'a'; i <= 'z'; ++i)
		chars[i] = LETTER;
	for (i = 'A'; i <= 'Z'; ++i)
		chars[i] = LETTER;
	for (i = '0'; i <= '9'; ++i)
		chars[i] = DIGIT;
	chars['\''] = APOS;
	chars['&'] = APOS;

	work();
	return 0;
}

static int
skeqn(void)
{
	while ((c = getc(infile)) != rdelim) {
		if (c == EOF) {
			c = eof();
		} else if (c == '"') {
			while ((c = getc(infile)) != '"') {
				if (c == EOF) {
					c = eof();
				} else if (c == '\\') {
					if ((c = getc(infile)) == EOF)
						c = eof();
				}
			}
		}
	}
	c = ' ';
	return c;
}

static FILE *
opn(char *p)
{
	FILE *fd;

	if (p[0] == '-' && p[1] == '\0')
		fd = stdin;
	else if ((fd = fopen(p, "r")) == NULL)
		fatal("Cannot open file %s\n", p);

	return fd;
}

static int
eof(void)
{
	if (infile != stdin)
		fclose(infile);
	if (filesp > files)
		infile = *--filesp;
	else if (argc > 0) {
		infile = opn(argv[0]);
		--argc;
		++argv;
	} else
		exit(0);

	return C;
}

static void
getfname(void)
{
	char *p;
	struct chain { struct chain *nextp; char *datap; } *chainblock;
	struct chain *q;
	static struct chain *namechain = NULL;

	while (C == ' ')
		;

	for (p = fname; (*p = c) != '\n' && c != ' ' && c != '\t' && c != '\\'; ++p)
		C;
	*p = '\0';
	while (c != '\n')
		C;

	/* see if this name has already been used */

	for (q = namechain; q; q = q->nextp)
		if (!strcmp(fname, q->datap)) {
			fname[0] = '\0';
			return;
		}

	q = (struct chain *) calloc(1, sizeof(*chainblock));
	q->nextp = namechain;
	q->datap = copys(fname);
	namechain = q;
}

static void
fatal(char *s, char *p)
{
	fprintf(stderr, "Deroff: ");
	fprintf(stderr, s, p);
	exit(1);
}

static void
work(void)
{
	for (;;) {
		if (C == '.' || c == '\'')
			comline();
		else
			regline(NO);
	}
}

static void
regline(int macline)
{
	line[0] = c;
	lp = line;
	for (;;) {
		if (c == '\\') {
			*lp = ' ';
			backsl();
		}
		if (c == '\n')
			break;
		if (intable && c == 'T') {
			*++lp = C;
			if (c == '{' || c == '}') {
				lp[-1] = ' ';
				*lp = C;
			}
		} else
			*++lp = C;
	}

	*lp = '\0';

	if (line[0] != '\0') {
		if (wordflag)
			putwords(macline);
		else if (macline)
			putmac(line);
		else
			puts(line);
	}
}

static void
putmac(char *s)
{
	char *t;

	while (*s) {
		while (*s == ' ' || *s == '\t')
			putchar(*s++);
		for (t = s; *t != ' ' && *t != '\t' && *t != '\0'; ++t)
			;
		if (t > s + 2 && chars[(int) s[0]] == LETTER && chars[(int) s[1]] == LETTER)
			while (s < t)
				putchar(*s++);
		else
			s = t;
	}
	putchar('\n');
}

static void
putwords(int macline) /* break into words for -w option */
{
	char *p, *p1;
	int i, nlet;

	for (p1 = line; ;) {
		/* skip initial specials ampersands and apostrophes */
		while (chars[(int) *p1] < DIGIT)
			if (*p1++ == '\0')
				return;
		nlet = 0;
		for (p = p1; (i = chars[(int) *p]) != SPECIAL; ++p)
			if (i == LETTER)
				++nlet;

		if ((!macline && nlet > 1) /* MDM definition of word */
		    || (macline && nlet > 2 && chars[(int) p1[0]] == LETTER
			&& chars[(int) p1[1]] == LETTER)) {
			/* delete trailing ampersands and apostrophes */
			while (p[-1] == '\'' || p[-1] == '&')
				--p;
			while (p1 < p)
				putchar(*p1++);
			putchar('\n');
		} else
			p1 = p;
	}
}

static void
comline(void)
{
	int c1, c2;

	while (C == ' ' || c == '\t')
		;
	if ((c1 = c) == '\n')
		return;
	c2 = C;
	if (c1 == '.' && c2 != '.')
		inmacro = NO;
	if (c2 == '\n')
		return;

	if (c1 == 'E' && c2 == 'Q' && filesp == files)
		eqn();
	else if (c1 == 'T' && (c2 == 'S' || c2 == 'C' || c2 == '&') && filesp == files)
		tbl();
	else if (c1 == 'T' && c2 == 'E')
		intable = NO;
	else if (!inmacro && c1 == 'd' && c2 == 'e')
		macro();
	else if (!inmacro && c1 == 'i' && c2 == 'g')
		macro();
	else if (!inmacro && c1 == 'a' && c2 == 'm')
		macro();
	else if (c1 == 's' && c2 == 'o') {
		getfname();
		if (fname[0])
			infile = *++filesp = opn(fname);
	} else if (c1 == 'n' && c2 == 'x') {
		getfname();
		if (fname[0] == '\0')
			exit(0);
		if (infile != stdin)
			fclose(infile);
		infile = *filesp = opn(fname);
	} else if (c1 == 'h' && c2 == 'w') {
		SKIP;
	} else {
		if (c1 == '.' && c2 == '.')
			while (C == '.')
				;
		++inmacro;
		regline(YES);
		--inmacro;
	}
}

static void
macro(void)
{
	/* look for .. */
	do {
		SKIP;
	} while (C != '.' || C != '.' || C == '.');
	SKIP;
	inmacro = YES;
}

static void
tbl(void)
{
	while (C != '.')
		;
	SKIP;
	intable = YES;
}

static void
eqn(void)
{
	int c1, c2;

	SKIP;

	for (;;) {
		if (C == '.' || c == '\'') {
			while (C == ' ' || c == '\t')
				;
			if (c == 'E' && C == 'N') {
				SKIP;
				return;
			}
		} else if (c == 'd') { /* look for delim */
			if (C == 'e' && C == 'l')
				if (C == 'i' && C == 'm') {
					while (C1 == ' ')
						;
					if ((c1 = c) == '\n' || (c2 = C1) == '\n'
					    || (c1 == 'o' && c2 == 'f' && C1 == 'f')) {
						ldelim = NOCHAR;
						rdelim = NOCHAR;
					} else {
						ldelim = c1;
						rdelim = c2;
					}
				}
		}

		if (c != '\n')
			SKIP;
	}
}

static void
backsl(void) /* skip over a complete backslash construction */
{
	int bdelim;

sw:
	switch (C) {
	case '"':
		SKIP;
		return;
	case 's':
		if (C == '\\')
			backsl();
		else {
			while (C >= '0' && c <= '9')
				;
			ungetc(c, infile);
			c = '0';
		}
		--lp;
		return;

	case 'f':
	case 'n':
	case '*':
		if (C != '(')
			return;
		/* FALLTHROUGH */

	case '(':
		if (C != '\n')
			C;
		return;

	case '$':
		C; /* discard argument number */
		return;

	case 'b':
	case 'x':
	case 'v':
	case 'h':
	case 'w':
	case 'o':
	case 'l':
	case 'L':
		if ((bdelim = C) == '\n')
			return;
		while (C != '\n' && c != bdelim)
			if (c == '\\')
				backsl();
		return;

	case '\\':
		if (inmacro)
			goto sw;
		/* FALLTHROUGH */
	default:
		return;
	}
}

static char *
copys(char *s)
{
	char *t, *t0;

	if ((t0 = t = calloc(strlen(s) + 1, sizeof(*t))) == NULL)
		fatal("Cannot allocate memory", (char *) NULL);

	while ((*t++ = *s++))
		;
	return t0;
}
