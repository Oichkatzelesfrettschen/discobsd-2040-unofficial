/*
 * The process environment the makefs forks expect from usr.sbin/makefs:
 * NetBSD <util.h>'s allocation wrappers and makefs's timestamp override.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>

#include "makefs.h"
#include "util.h"

struct stat stampst;

static void *
checked(void *p)
{
	if (p == NULL) {
		fputs("msdosfs: out of memory\n", stderr);
		exit(1);
	}
	return p;
}

void *
ecalloc(size_t n, size_t size)
{
	return checked(calloc(n, size));
}

void *
emalloc(size_t size)
{
	return checked(malloc(size));
}

void *
erealloc(void *p, size_t size)
{
	return checked(realloc(p, size));
}
