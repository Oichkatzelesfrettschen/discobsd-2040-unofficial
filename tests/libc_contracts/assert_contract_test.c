#include "assert.h"

static int enabled_count;

static int
enabled(void)
{
	return (assert(++enabled_count), enabled_count);
}

static_assert(sizeof(enabled_count) == sizeof(int), "enabled state");

#define NDEBUG
#include "assert.h"

/* C17 7.2p3 keeps static_assert defined while NDEBUG disables assert. */
static_assert(sizeof(char) == 1, "disabled state");

static int
disabled(void)
{
	int count = 0;

	assert(++count);
	return (assert(++count), count);
}

#undef NDEBUG
#include "assert.h"

int
main(int argc, char **argv)
{
	int count = 0;

	static_assert(sizeof(count) >= 2, "block scope");
	(void)argv;
	if (argc > 1)
		assert(count != 0);
	assert(enabled() == 1);
	assert(disabled() == 0);
	(void)(count ? (assert(count), 1) : (assert(!count), 0));
	for (; count < 1; assert(++count))
		;
	return count != 1;
}
