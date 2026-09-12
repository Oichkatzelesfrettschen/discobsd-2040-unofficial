/*
 * md -- render Markdown to an 80-column ANSI terminal. A one-pass line
 * classifier drives block structure (headings, list items, block
 * quotes, fenced code, horizontal rules) and an inline pass renders
 * emphasis and inline code within a line. Color is SGR, sharing kilo's
 * palette: headings bold cyan, inline code green, list bullets yellow.
 * Text stays 7-bit ASCII, so the byte and column counts the tty layer
 * keeps never desync.
 *
 * usage: md [file ...]   (no file, or "-", reads standard input)
 */
#include <stdio.h>
#include <string.h>

#define WIDTH	72

/* Inline emphasis and code within one line's content. `code` prints
 * green until the closing backtick; bold and italic runs
 * toggle the SGR attribute. A closing backtick restores the default
 * foreground rather than a full reset, so it does not clear a bold or
 * italic run that spans it. */
static void
inln(const char *s)
{
	int bold = 0, ital = 0, code = 0;

	while (*s) {
		if (s[0] == '`') {
			code = !code;
			fputs(code ? "\033[32m" : "\033[39m", stdout);
			s++;
			continue;
		}
		if (!code && s[0] == '*' && s[1] == '*') {
			bold = !bold;
			fputs(bold ? "\033[1m" : "\033[22m", stdout);
			s += 2;
			continue;
		}
		if (!code && (s[0] == '*' || s[0] == '_')) {
			ital = !ital;
			fputs(ital ? "\033[4m" : "\033[24m", stdout);
			s++;
			continue;
		}
		putchar(*s++);
	}
	fputs("\033[0m", stdout);
}

static int
isrule(const char *s)
{
	int c = *s, n = 0;

	if (c != '-' && c != '*' && c != '_')
		return 0;
	for (; *s; s++) {
		if (*s == c)
			n++;
		else if (*s != ' ')
			return 0;
	}
	return n >= 3;
}

static void
render(FILE *fp)
{
	char line[1024];
	int incode = 0;

	while (fgets(line, sizeof(line), fp)) {
		char *s = line;
		int n;

		line[strcspn(line, "\r\n")] = '\0';

		if (strncmp(s, "```", 3) == 0) {
			incode = !incode;
			continue;
		}
		if (incode) {
			printf("    \033[32m%s\033[0m\n", s);
			continue;
		}
		if (s[0] == '\0') {
			putchar('\n');
			continue;
		}
		if (isrule(s)) {
			fputs("\033[2m", stdout);
			for (n = 0; n < WIDTH; n++)
				putchar('-');
			fputs("\033[0m\n", stdout);
			continue;
		}
		if (s[0] == '#') {
			for (n = 0; *s == '#'; s++)
				n++;
			while (*s == ' ')
				s++;
			printf("\033[1;36m%s\033[0m\n", s);
			if (n == 1) {
				int i, w = (int)strlen(s);
				fputs("\033[36m", stdout);
				for (i = 0; i < w && i < WIDTH; i++)
					putchar('=');
				fputs("\033[0m\n", stdout);
			}
			continue;
		}
		if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
			fputs("  \033[33m*\033[0m ", stdout);
			inln(s + 2);
			putchar('\n');
			continue;
		}
		if (s[0] == '>' ) {
			s++;
			if (*s == ' ')
				s++;
			fputs("\033[2m| \033[0m", stdout);
			inln(s);
			putchar('\n');
			continue;
		}
		inln(s);
		putchar('\n');
	}
}

int
main(int argc, char **argv)
{
	int i, rc = 0;
	FILE *fp;

	if (argc < 2) {
		render(stdin);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-") == 0) {
			render(stdin);
			continue;
		}
		if ((fp = fopen(argv[i], "r")) == NULL) {
			fprintf(stderr, "md: %s: cannot open\n", argv[i]);
			rc = 1;
			continue;
		}
		render(fp);
		fclose(fp);
	}
	return rc;
}
