/* See LICENSE file for copyright and license details. */
/* LICENSE: usr.bin/look/LICENSE (Caldera ancient-Unix grant, BSD-style). */
/*
 * Binary-search a sorted file for lines starting with a given string.
 * Origin: Seventh Edition Unix, usr/src/cmd/look.c on the
 * titor-special_torrent uv7swre.zip / unix_v7_rl.dsk V7 RL02 image
 * (UNIX-Source-Code/uv7swre.zip). Rewritten here with ANSI prototypes
 * for Smaller C, which rejects the original's K&R parameter lists;
 * the search algorithm, option letters and default dictionary path
 * are unchanged from the V7 source.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *dfile;
static char *filenam = "/usr/dict/words";

static int fold;
static int dict;
static int tab;
static char entry[250];
static char word[250];
static char key[50];

static int compare(char *s, char *t);
static int getword(char *w);
static void canon(char *old, char *new);

int
main(int argc, char **argv)
{
	int c;
	long top, bot, mid;

	while (argc >= 2 && *argv[1] == '-') {
		for (;;) {
			switch (*++argv[1]) {
			case 'd':
				dict++;
				continue;
			case 'f':
				fold++;
				continue;
			case 't':
				tab = argv[1][1];
				if (tab)
					++argv[1];
				continue;
			case 0:
				break;
			default:
				continue;
			}
			break;
		}
		argc--;
		argv++;
	}
	if (argc <= 1)
		return 0;
	if (argc == 2) {
		fold++;
		dict++;
	} else
		filenam = argv[2];
	dfile = fopen(filenam, "r");
	if (dfile == NULL) {
		fprintf(stderr, "look: can't open %s\n", filenam);
		exit(2);
	}
	canon(argv[1], key);
	bot = 0;
	fseek(dfile, 0L, 2);
	top = ftell(dfile);
	for (;;) {
		mid = (top + bot) / 2;
		fseek(dfile, mid, 0);
		do {
			c = getc(dfile);
			mid++;
		} while (c != EOF && c != '\n');
		if (!getword(entry))
			break;
		canon(entry, word);
		switch (compare(key, word)) {
		case -2:
		case -1:
		case 0:
			if (top <= mid)
				break;
			top = mid;
			continue;
		case 1:
		case 2:
			bot = mid;
			continue;
		}
		break;
	}
	fseek(dfile, bot, 0);
	while (ftell(dfile) < top) {
		if (!getword(entry))
			return 0;
		canon(entry, word);
		switch (compare(key, word)) {
		case -2:
			return 0;
		case -1:
		case 0:
			puts(entry);
			break;
		case 1:
		case 2:
			continue;
		}
		break;
	}
	while (getword(entry)) {
		canon(entry, word);
		switch (compare(key, word)) {
		case -1:
		case 0:
			puts(entry);
			continue;
		}
		break;
	}
	return 0;
}

static int
compare(char *s, char *t)
{
	for (; *s == *t; s++, t++)
		if (*s == 0)
			return 0;
	return *s == 0 ? -1 :
	       *t == 0 ? 1 :
	       *s < *t ? -2 : 2;
}

static int
getword(char *w)
{
	int c;

	for (;;) {
		c = getc(dfile);
		if (c == EOF)
			return 0;
		if (c == '\n')
			break;
		*w++ = c;
	}
	*w = 0;
	return 1;
}

static void
canon(char *old, char *new)
{
	int c;

	for (;;) {
		*new = c = *old++;
		if (c == 0 || c == tab) {
			*new = 0;
			break;
		}
		if (dict) {
			if (!isalnum(c))
				continue;
		}
		if (fold) {
			if (isupper(c))
				*new += 'a' - 'A';
		}
		new++;
	}
}
