#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <machine/board.h>

extern const volatile struct board_desc *board_desc_test;
void board_desc_test_reset(void);

static unsigned int checks;

#define CHECK(condition, label) do { \
	checks++; \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s\n", label); \
		return 1; \
	} \
} while (0)

static unsigned int
crc32(const unsigned char *bytes, size_t length)
{
	unsigned int crc = 0xffffffffU;

	while (length-- != 0) {
		unsigned int bit;

		crc ^= *bytes++;
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
	}
	return crc ^ 0xffffffffU;
}

static struct board_desc
make_descriptor(unsigned int board, unsigned int major_number)
{
	struct board_desc descriptor = {
		BOARD_DESC_MAGIC, BOARD_DESC_VERSION, BOARD_DESC_LENGTH,
		(unsigned char)board, (unsigned char)major_number, 0, 0, 0,
	};

	descriptor.crc32 = crc32((const unsigned char *)&descriptor,
	    offsetof(struct board_desc, crc32));
	return descriptor;
}

static void
refresh_crc(struct board_desc *descriptor)
{
	descriptor->crc32 = crc32((const unsigned char *)descriptor,
	    offsetof(struct board_desc, crc32));
}

static int
expect_selection(const struct board_desc *descriptor, int expected_board,
    int expected_major, const char *label)
{
	int major_number, minor_number;

	board_desc_test = descriptor;
	board_desc_test_reset();
	CHECK(board_id() == expected_board, label);
	board_console(&major_number, &minor_number);
	CHECK(major_number == expected_major, label);
	CHECK(minor_number == 0, label);
	return 0;
}

int
main(void)
{
	struct board_desc fixture;
	struct board_desc erased;

	CHECK(sizeof(struct board_desc) == 16, "descriptor wire size");
	CHECK(offsetof(struct board_desc, crc32) == 12, "CRC field offset");

	fixture = make_descriptor(BOARD_ID_PICOCALC, 10);
	if (expect_selection(&fixture, BOARD_ID_PICOCALC, 10,
	    "valid PicoCalc descriptor") != 0)
		return 1;

	fixture = make_descriptor(BOARD_ID_PICOCALC, 10);
	fixture.length--;
	refresh_crc(&fixture);
	if (expect_selection(&fixture, BOARD_ID_PICO, 7,
	    "truncated descriptor") != 0)
		return 1;

	fixture = make_descriptor(99, 10);
	if (expect_selection(&fixture, BOARD_ID_PICO, 7,
	    "unknown board") != 0)
		return 1;

	fixture = make_descriptor(BOARD_ID_PICO, 10);
	if (expect_selection(&fixture, BOARD_ID_PICO, 7,
	    "bare Pico with panel console") != 0)
		return 1;

	fixture = make_descriptor(BOARD_ID_PICOCALC, 10);
	fixture.crc32 ^= 1;
	if (expect_selection(&fixture, BOARD_ID_PICO, 7,
	    "CRC mismatch") != 0)
		return 1;

	memset(&erased, 0xff, sizeof erased);
	if (expect_selection(&erased, BOARD_ID_PICO, 7,
	    "erased descriptor") != 0)
		return 1;

	fixture = make_descriptor(BOARD_ID_PICO, 7);
	fixture.magic ^= 1;
	refresh_crc(&fixture);
	if (expect_selection(&fixture, BOARD_ID_PICO, 7,
	    "bad magic") != 0)
		return 1;

	fixture = make_descriptor(BOARD_ID_PICOCALC, 10);
	fixture.version++;
	refresh_crc(&fixture);
	if (expect_selection(&fixture, BOARD_ID_PICO, 7,
	    "unknown version") != 0)
		return 1;

	printf("board descriptor reader: %u checks passed\n", checks);
	return 0;
}
