/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <string.h>

/*
 * Sys5 compat routine
 */
void *
memchr(const void *vs, int c, size_t n)
{
	const unsigned char *s = vs;
	unsigned char value = (unsigned char)c;

	while (n-- > 0)
		if (*s++ == value)
			return (void *)(s - 1);
	return NULL;
}
