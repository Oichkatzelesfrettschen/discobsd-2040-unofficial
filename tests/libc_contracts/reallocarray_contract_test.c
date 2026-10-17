#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void *db_reallocarray(void *, size_t, size_t);

static void *expected_pointer;
static void *returned_pointer;
static size_t requested_size;
static int realloc_calls;
static int realloc_errno;

void *
array_test_realloc(void *pointer, size_t size)
{
	expected_pointer = pointer;
	requested_size = size;
	realloc_calls++;
	errno = realloc_errno;
	return returned_pointer;
}

static int
check(int condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "reallocarray contract: %s\n", message);
	return 1;
}

int
main(void)
{
	unsigned char original[8];
	unsigned char replacement[8];
	void *result;
	int failures = 0;

	memset(original, 0x5a, sizeof(original));
	returned_pointer = replacement;
	realloc_errno = 0;
	errno = 0;
	result = db_reallocarray(original, 3, 7);
	failures += check(result == replacement, "a successful allocation returns realloc's result");
	failures += check(realloc_calls == 1 && expected_pointer == original &&
	    requested_size == 21, "the element count and size multiply once");

	realloc_calls = 0;
	errno = 0;
	result = db_reallocarray(original, SIZE_MAX, 2);
	failures += check(result == NULL && errno == ENOMEM,
	    "an overflowing product fails with ENOMEM");
	failures += check(realloc_calls == 0,
	    "an overflowing product reaches no allocator");
	failures += check(original[0] == 0x5a && original[7] == 0x5a,
	    "overflow leaves the original allocation untouched");

	realloc_calls = 0;
	returned_pointer = NULL;
	realloc_errno = EAGAIN;
	errno = 0;
	result = db_reallocarray(original, 4, 5);
	failures += check(result == NULL && errno == EAGAIN,
	    "an allocation failure preserves realloc's errno");
	failures += check(realloc_calls == 1 && expected_pointer == original &&
	    requested_size == 20, "an allocation failure receives the original pointer");
	failures += check(original[0] == 0x5a && original[7] == 0x5a,
	    "an allocation failure leaves the original allocation untouched");

	realloc_calls = 0;
	returned_pointer = replacement;
	realloc_errno = 0;
	result = db_reallocarray(original, 0, SIZE_MAX);
	failures += check(result == replacement && realloc_calls == 1 &&
	    requested_size == 0, "a zero element count delegates a zero-size request");

	if (failures == 0)
		printf("reallocarray contract tests passed\n");
	return failures != 0;
}
