/*
 * Interfaces the OpenBSD dc(1) and bc(1) sources use that this tree's libc
 * and headers spell differently or lack. extern.h includes this file ahead
 * of every other header, and bc's extern.h includes it as well.
 */

#ifndef DC_COMPAT_H
#define DC_COMPAT_H

#include <stdio.h>
#include <stdlib.h>

/* The OpenBSD attribute for a function that does not return. */
#define __dead		_Noreturn

/* Defined by lib/libc/stdlib/getopt.c, declared by no header. */
extern int		optreset;

/*
 * lib/libc has no asprintf. basprintf (mem.c) keeps its contract: the
 * formatted string is malloc'd into *strp, and the result is its length or
 * -1 on failure.
 */
int			basprintf(char **strp, const char *fmt, ...);

#endif /* DC_COMPAT_H */
