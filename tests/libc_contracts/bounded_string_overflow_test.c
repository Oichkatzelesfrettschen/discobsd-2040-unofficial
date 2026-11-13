#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

char *db_strndup(const char *, size_t);
static unsigned int allocation_calls;

size_t
db_strnlen(const char *source, size_t bound)
{
	(void)source;
	(void)bound;
	return SIZE_MAX;
}

void *
bounded_alloc(size_t size)
{
	(void)size;
	allocation_calls++;
	errno = ENOMEM;
	return NULL;
}

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	errno = EDOM;
	if (db_strndup("", SIZE_MAX) != NULL || errno != ENOMEM || allocation_calls != 0) {
		fputs("bounded string mismatch: allocation extent wraps\n", stderr);
		return 1;
	}
	puts("bounded strings: synthetic length overflow rejects before allocation");
	return 0;
}
