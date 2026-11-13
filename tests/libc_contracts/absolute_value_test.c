#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int (*volatile int_absolute)(int) = abs;
static long (*volatile long_absolute)(long) = labs;
static unsigned checks;

static int
check_int(int value, int expected)
{
	int result = int_absolute(value);

	checks++;
	if (result != expected) {
		printf("FAIL abs(%d): %d != %d\n", value, result, expected);
		return 1;
	}
	return 0;
}

static int
check_long(long value, long expected)
{
	long result = long_absolute(value);

	checks++;
	if (result != expected) {
		printf("FAIL labs(%ld): %ld != %ld\n", value, result, expected);
		return 2;
	}
	return 0;
}

int
main(void)
{
	static const int integers[][2] = {
		{ 0, 0 }, { 1, 1 }, { -1, 1 },
		{ INT_MAX, INT_MAX }, { -INT_MAX, INT_MAX },
		{ INT_MIN + 1, -(INT_MIN + 1) }
	};
	static const long longs[][2] = {
		{ 0, 0 }, { 1, 1 }, { -1, 1 },
		{ LONG_MAX, LONG_MAX }, { -LONG_MAX, LONG_MAX },
		{ LONG_MIN + 1, -(LONG_MIN + 1) }
	};
	unsigned i;
	int value, status;

	_Static_assert(INT_MAX >= 2147483647, "the maintained target has at least 32-bit int");
	for (value = 0; value <= 32768; value++) {
		if ((status = check_int(value, value)) != 0)
			return status;
		if ((status = check_int(-value, value)) != 0)
			return status;
		if ((status = check_long(value, value)) != 0)
			return status;
		if ((status = check_long(-value, value)) != 0)
			return status;
	}
	for (i = 0; i < sizeof(integers) / sizeof(integers[0]); i++)
		if ((status = check_int(integers[i][0], integers[i][1])) != 0)
			return status;
	for (i = 0; i < sizeof(longs) / sizeof(longs[0]); i++)
		if ((status = check_long(longs[i][0], longs[i][1])) != 0)
			return status;
	printf("absolute values: %u checks passed\n", checks);
	return 0;
}
