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

	/*
	 * The pointer advances after the compare, so the match returns it
	 * without a second register: seven Thumb-1 instructions a byte
	 * under GCC 13 and 16, where a post-increment in the compare costs
	 * GCC 13 an eighth.
	 */
	for (; n != 0; n--, s++)
		if (*s == value)
			return (void *)s;
	return NULL;
}
