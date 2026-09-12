/*
 * See LICENSE file for copyright and license details.
 * Trimmed from sbase's libutil/ealloc.c: the enmalloc/enstrdup
 * indirection through an exit status is collapsed since every caller
 * here always exits 1 on allocation failure.
 */
#include <stdlib.h>
#include <string.h>

#include "compat.h"

void *
emalloc(size_t size)
{
	void *p;

	p = malloc(size);
	if (!p)
		eprintf("malloc: out of memory\n");
	return p;
}

char *
estrdup(const char *s)
{
	char *p;

	p = strdup(s);
	if (!p)
		eprintf("strdup: out of memory\n");
	return p;
}
