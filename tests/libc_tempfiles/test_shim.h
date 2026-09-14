#include <stdio.h>
#include <stdlib.h>

int test_mkstemp(char *);
int test_unlink(const char *);
FILE *test_fdopen(int, const char *);
int test_close(int);
char *test_getenv(const char *);
char *test_mktemp(char *);
void *test_malloc(size_t);
void test_free(void *);
