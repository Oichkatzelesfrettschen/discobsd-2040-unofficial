#include "assert.h"

static int enabled_count;

static int
enabled(void)
{
	return (assert(++enabled_count), enabled_count);
}

#define NDEBUG
#include "assert.h"

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
