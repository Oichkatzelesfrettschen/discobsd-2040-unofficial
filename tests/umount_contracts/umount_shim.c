/*
 * The host side of the gate. It carries no tree header, so <stdio.h> here is
 * the host's and stderr is the host's; umount_shim.h states why the two sides
 * stay apart.
 */
#include "umount_shim.h"

#include <stdio.h>

unsigned ushim_checks;
unsigned ushim_failures;

void
ushim_fail(const char *file, int line, const char *what)
{
	ushim_failures++;
	fprintf(stderr, "FAIL %s:%d: %s\n", file, line, what);
}

void
ushim_note(const char *fmt, int a, int b)
{
	fprintf(stderr, "  ");
	fprintf(stderr, fmt, a, b);
	fprintf(stderr, "\n");
}

int
ushim_verdict(const char *suite)
{
	if (ushim_failures) {
		fprintf(stderr, "%s: %u of %u checks failed\n", suite,
		    ushim_failures, ushim_checks);
		return (1);
	}
	fprintf(stderr, "%s: %u checks passed\n", suite, ushim_checks);
	return (0);
}
