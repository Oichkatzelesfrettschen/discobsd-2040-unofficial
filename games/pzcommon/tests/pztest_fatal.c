/*
 * fatal() for the host rule tests: the collection's allocator calls it when
 * the system is out of memory. It lives in its own unit because the tests
 * compile with -fwhole-program, which would make a definition in the test
 * unit invisible to the support code.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void fatal(const char *fmt, ...);

void
fatal(const char *fmt, ...)
{
	printf("FAIL fatal: %s\n", fmt);
	exit(1);
}
