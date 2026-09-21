/*
 * The host side of the gate. It carries no tree header, so <stdio.h> here is
 * the host's and stderr is the host's; libc_shim.h states why the two sides
 * stay apart.
 */
#include "libc_shim.h"

#include <stdio.h>
#include <stdlib.h>

unsigned lsys_checks;
unsigned lsys_failures;

void *
lsys_alloc(unsigned nbytes)
{
	void *p = malloc(nbytes);

	if (p == NULL) {
		fprintf(stderr, "FAIL out of memory\n");
		exit(1);
	}
	return (p);
}

void
lsys_free(void *p)
{
	free(p);
}

void
lsys_fail(const char *file, int line, const char *what)
{
	lsys_failures++;
	fprintf(stderr, "FAIL %s:%d: %s\n", file, line, what);
}

void
lsys_note(const char *msg)
{
	fprintf(stderr, "  %s\n", msg);
}

int
lsys_verdict(const char *suite)
{
	if (lsys_failures) {
		fprintf(stderr, "%s: %u of %u checks failed\n", suite,
		    lsys_failures, lsys_checks);
		return (1);
	}
	fprintf(stderr, "%s: %u checks passed\n", suite, lsys_checks);
	return (0);
}
