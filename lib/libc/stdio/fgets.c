/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>

char *
fgets(char *s, int n, FILE *iop)
{
	int c = EOF;
	char *cs;

	cs = s;
	while (--n > 0 && (c = getc(iop)) != EOF) {
		*cs++ = (char) c;
		if (c == '\n')
			break;
	}
	if (c == EOF && cs == s)
		return (NULL);
	*cs++ = '\0';
	return (s);
}
