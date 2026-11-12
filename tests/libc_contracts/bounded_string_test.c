#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef BOUNDED_STRING_NO_GUARD
#include <sys/mman.h>
#include <unistd.h>
#endif

size_t db_strnlen(const char *, size_t);
char *db_strndup(const char *, size_t);
char *db_stpcpy(char *, const char *);
char *db_stpncpy(char *, const char *, size_t);

static size_t allocation_size;
static unsigned int allocation_calls;
static int allocation_failure;
static unsigned long cases;

static void
require(int condition, const char *label)
{
	if (!condition) {
		fprintf(stderr, "bounded string mismatch: %s\n", label);
		exit(1);
	}
}

void *
bounded_alloc(size_t size)
{
	allocation_size = size;
	allocation_calls++;
	if (allocation_failure) {
		errno = ENOMEM;
		return NULL;
	}
	return malloc(size);
}

static void
matrix(void)
{
	unsigned char input[17], output[24];

	for (unsigned int byte = 1; byte <= 255; byte++) {
		for (size_t length = 0; length <= 16; length++) {
			for (size_t index = 0; index < length; index++)
				input[index] = (unsigned char)(1 + (byte + index - 1) % 255);
			input[length] = 0;
			memset(output, 0xa5, sizeof(output));
			errno = EDOM;
			require(db_stpcpy((char *)output + 1, (char *)input) ==
			    (char *)output + 1 + length, "stpcpy return");
			require(errno == EDOM, "stpcpy errno");
			for (size_t index = 0; index < sizeof(output); index++)
				require(output[index] == ((index >= 1 && index <= length + 1) ?
				    input[index - 1] : 0xa5), "stpcpy bytes and sentinels");
			require(db_strnlen((char *)input, SIZE_MAX) == length,
			    "strnlen large bound");
			for (size_t bound = 0; bound <= 20; bound++) {
				size_t extent = length < bound ? length : bound;
				memset(output, 0xa5, sizeof(output));
				errno = EDOM;
				require(db_strnlen((char *)input, bound) == extent,
				    "strnlen extent");
				require(errno == EDOM, "strnlen errno");
				require(db_stpncpy((char *)output + 1, (char *)input, bound) ==
				    (char *)output + 1 + extent, "stpncpy return");
				require(errno == EDOM, "stpncpy errno");
				for (size_t index = 0; index < sizeof(output); index++) {
					unsigned int expected = 0xa5;
					if (index >= 1 && index <= bound)
						expected = index <= length ? input[index - 1] : 0;
					require(output[index] == expected, "stpncpy padding and sentinels");
				}
				allocation_calls = 0;
				char *copy = db_strndup((char *)input, bound);
				require(copy != NULL, "strndup allocation");
				require(allocation_calls == 1 && allocation_size == extent + 1,
				    "strndup exact allocation extent");
				for (size_t index = 0; index <= extent; index++)
					require((unsigned char)copy[index] ==
					    (index < extent ? input[index] : 0), "strndup bytes");
				free(copy);
				cases++;
			}
		}
	}
	for (size_t bound = 0; bound <= 4; bound++) {
		allocation_failure = 1;
		allocation_calls = 0;
		errno = EDOM;
		require(db_strndup("abc", bound) == NULL && errno == ENOMEM &&
		    allocation_calls == 1, "strndup allocation failure");
	}
	allocation_failure = 0;
	allocation_calls = 0;
	char *copy = db_strndup("", SIZE_MAX);
	require(copy != NULL && copy[0] == 0 && allocation_size == 1 &&
	    allocation_calls == 1, "strndup large bound and empty input");
	free(copy);
}

#ifndef BOUNDED_STRING_NO_GUARD
static void
guarded(void)
{
	long page = sysconf(_SC_PAGESIZE);
	require(page > 32 && (size_t)page <= SIZE_MAX / 2, "guard page size");
	size_t bytes = (size_t)page * 2;
	char *source = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	char *destination = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	require(source != MAP_FAILED && destination != MAP_FAILED, "guard mappings");
	require(mprotect(source + page, (size_t)page, PROT_NONE) == 0 &&
	    mprotect(destination + page, (size_t)page, PROT_NONE) == 0, "guard protection");
	char *input = source + page - 32;
	char *output = destination + page - 32;
	memset(input, 'x', 32);
	require(db_strnlen(input, 32) == 32, "strnlen guard");
	char *copy = db_strndup(input, 32);
	require(copy != NULL && copy[32] == 0 && memcmp(copy, input, 32) == 0,
	    "strndup guard");
	free(copy);
	require(db_stpncpy(output, input, 32) == output + 32 &&
	    memcmp(output, input, 32) == 0, "stpncpy nonterminated guard");
	require(db_stpncpy(output, "x", 32) == output + 1, "stpncpy padded guard");
	for (size_t index = 1; index < 32; index++)
		require(output[index] == 0, "stpncpy guarded padding");
	input[31] = 0;
	require(db_stpcpy(output, input) == output + 31 &&
	    memcmp(output, input, 32) == 0, "stpcpy guard");
	require(db_strnlen(input + 31, SIZE_MAX) == 0, "strnlen early NUL guard");
	require(munmap(source, bytes) == 0 && munmap(destination, bytes) == 0,
	    "guard cleanup");
}
#endif

int
main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "matrix") == 0) {
		matrix();
		printf("bounded strings: %lu matrix cases; allocation failures and large bounds pass\n",
		    cases);
	} else if (strcmp(argv[1], "guard") == 0) {
#ifndef BOUNDED_STRING_NO_GUARD
		guarded();
		puts("bounded strings: protected source and destination boundaries pass");
#else
		return 2;
#endif
	} else {
		return 2;
	}
	return 0;
}
