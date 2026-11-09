#define NDEBUG
#include "assert.h"

static int
disabled(void)
{
	int count = 0;

	assert(++count);
	return count;
}

#undef NDEBUG
#include "assert.h"

int
main(void)
{
	int count = 0;

	assert(++count);
	return count != 1 || disabled() != 0;
}
