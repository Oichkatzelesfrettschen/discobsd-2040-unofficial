#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <flash.h>

#define	TEST_DRIVE_COUNT	2U

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "flash_device_bounds: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	unsigned int minor_number;
	unsigned int unit;
	unsigned int partition;
	int expected;

	for (minor_number = 0; minor_number <= UCHAR_MAX; minor_number++) {
		unit = minor_number >> 3;
		partition = minor_number & 7U;
		expected = unit < TEST_DRIVE_COUNT &&
		    partition <= FLASH_PARTITION_COUNT;
		require(flash_indices_valid(unit, partition, TEST_DRIVE_COUNT) ==
		    expected, "index predicate disagrees with encoded minor bounds");
	}
	puts("flash_device_bounds: PASS (all encoded minor values)");
	return 0;
}
