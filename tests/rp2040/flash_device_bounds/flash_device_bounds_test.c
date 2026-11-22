#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/*
 * The identity helpers: a reply of all zeros or all ones is not a chip, the
 * capacity byte names the fixed layout only at 15h, and the serial string
 * is the unique ID in hex with the suffix the host tools match.
 */
static void
check_identity(void)
{
	static const unsigned char w25q16[FLASH_JEDEC_BYTES] =
	    { 0xef, 0x40, 0x15 };
	static const unsigned char uid[FLASH_UNIQUE_BYTES] =
	    { 0xe6, 0x61, 0x41, 0x03, 0xe7, 0x0a, 0x9b, 0xf0 };
	unsigned char fill[FLASH_UNIQUE_BYTES];
	char serial[FLASH_SERIAL_CHARS + 1];
	unsigned int code;

	memset(fill, 0x00, sizeof(fill));
	require(!flash_id_plausible(fill, FLASH_JEDEC_BYTES),
	    "an all-zero JEDEC reply is accepted");
	require(!flash_id_plausible(fill, FLASH_UNIQUE_BYTES),
	    "an all-zero unique ID is accepted");
	memset(fill, 0xff, sizeof(fill));
	require(!flash_id_plausible(fill, FLASH_JEDEC_BYTES),
	    "an all-ones JEDEC reply is accepted");
	fill[3] = 0x00;
	require(flash_id_plausible(fill, FLASH_UNIQUE_BYTES),
	    "a mixed unique ID is refused");
	require(flash_id_plausible(w25q16, FLASH_JEDEC_BYTES),
	    "the W25Q16JV JEDEC ID is refused");

	for (code = 0; code <= UCHAR_MAX; code++)
		require(flash_capacity_matches(code) == (code == 0x15),
		    "capacity predicate disagrees with the 2 MB layout");

	memset(serial, 'X', sizeof(serial));
	flash_serial_string(serial, uid);
	require(strcmp(serial, "e6614103e70a9bf0-rp2040") == 0,
	    "serial string is not the unique ID and suffix");
	require(strlen(serial) + 1 == FLASH_SERIAL_CHARS,
	    "serial string does not fill its declared size");
	require(serial[FLASH_SERIAL_CHARS] == 'X',
	    "serial string writes past its declared size");
	memset(serial, 'X', sizeof(serial));
	flash_serial_string(serial, (const unsigned char *)0);
	require(strcmp(serial, "rp2040") == 0,
	    "fallback serial string is not rp2040");
	puts("flash_device_bounds: PASS (chip identity helpers)");
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
	check_identity();
	return 0;
}
