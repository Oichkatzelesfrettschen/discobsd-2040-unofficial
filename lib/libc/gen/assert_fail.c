#include <stdio.h>
#include <stdlib.h>

void
__discobsd_assert_fail(const char *expression, const char *file, int line,
    const char *function)
{
	fprintf(stderr, "Assertion failed: %s, file %s, line %d, function %s\n",
	    expression, file, line, function);
	abort();
}
