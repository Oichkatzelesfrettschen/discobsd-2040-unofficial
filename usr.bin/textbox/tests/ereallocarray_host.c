/*
 * Host-only stand-in for ../reallocarray.c: the host libc already
 * carries reallocarray(3) (glibc under _DEFAULT_SOURCE), so this
 * gives ereallocarray() alone instead of redefining reallocarray()
 * and colliding with libc's copy at link time. The test runner
 * compiles this with -D_DEFAULT_SOURCE already on the command line.
 */
#include <stdlib.h>

#include "compat.h"

void *
ereallocarray(void *optr, size_t nmemb, size_t size)
{
	void *p;

	if (!(p = reallocarray(optr, nmemb, size)))
		eprintf("reallocarray: out of memory\n");
	return p;
}
