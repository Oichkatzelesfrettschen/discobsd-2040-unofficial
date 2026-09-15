/*
 * Host test for the production raw-swap programming loop. The NOR model
 * enforces the RP2040 ROM alignments and refuses every 0-to-1 transition.
 */

#include <rp2040/dev/flash.h>
#include <rp2040/dev/flash_swap.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BYTES	(32U * 1024U)
#define TEST_SECTORS	(TEST_BYTES / FLASH_SECTOR_BYTES)

static unsigned char modeled_flash[TEST_BYTES];
static unsigned int erase_count[TEST_SECTORS];
static unsigned int program_calls;
static int fail_read, fail_erase, fail_program;
static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
	} \
} while (0)

static int
modeled_read(unsigned int offset, unsigned char *data, unsigned int length)
{
	unsigned int relative;

	if (fail_read) {
		fail_read = 0;
		return -1;
	}
	if (offset < FLASH_SWAP_OFFSET ||
	    offset - FLASH_SWAP_OFFSET > TEST_BYTES - length)
		return -1;
	relative = offset - FLASH_SWAP_OFFSET;
	memcpy(data, modeled_flash + relative, length);
	return 0;
}

static int
modeled_erase(unsigned int offset, unsigned int length)
{
	unsigned int relative;

	if (fail_erase) {
		fail_erase = 0;
		return -1;
	}
	if (offset < FLASH_SWAP_OFFSET ||
	    offset - FLASH_SWAP_OFFSET > TEST_BYTES - length ||
	    length != FLASH_SECTOR_BYTES ||
	    (offset & (FLASH_SECTOR_BYTES - 1)) != 0)
		return -1;
	relative = offset - FLASH_SWAP_OFFSET;
	memset(modeled_flash + relative, 0xff, length);
	erase_count[relative / FLASH_SECTOR_BYTES]++;
	return 0;
}

static int
modeled_program(unsigned int offset, const unsigned char *data,
    unsigned int length)
{
	unsigned int index, relative;

	if (fail_program) {
		fail_program = 0;
		return -1;
	}
	if (offset < FLASH_SWAP_OFFSET ||
	    offset - FLASH_SWAP_OFFSET > TEST_BYTES - length ||
	    length != FLASH_PROG_BYTES ||
	    (offset & (FLASH_PROG_BYTES - 1)) != 0)
		return -1;
	relative = offset - FLASH_SWAP_OFFSET;
	for (index = 0; index < length; index++)
		if ((modeled_flash[relative + index] & data[index]) != data[index])
			return -1;
	for (index = 0; index < length; index++)
		modeled_flash[relative + index] &= data[index];
	program_calls++;
	return 0;
}

static const struct flash_swap_ops modeled_ops = {
	modeled_read,
	modeled_erase,
	modeled_program,
};

static void
fill(unsigned char *data, unsigned int length, unsigned int seed)
{
	unsigned int index;

	for (index = 0; index < length; index++)
		data[index] = (unsigned char)(seed + index * 29U + index / 7U);
}

static void
check_bytes(unsigned int offset, const unsigned char *expected,
    unsigned int length)
{
	CHECK(memcmp(modeled_flash + offset, expected, length) == 0);
}

static void
check_erased(unsigned int offset, unsigned int length)
{
	unsigned int index;

	for (index = 0; index < length; index++)
		CHECK(modeled_flash[offset + index] == 0xff);
}

static void
program_image(unsigned int base, const unsigned char *data,
    unsigned int data_length, const unsigned char *stack,
    unsigned int stack_length, const unsigned char *uarea,
    unsigned int uarea_length, unsigned char *scratch)
{
	unsigned int stack_offset, uarea_offset;

	stack_offset = (data_length + FLASH_UNIT_BYTES - 1) &
	    ~(FLASH_UNIT_BYTES - 1);
	uarea_offset = stack_offset +
	    ((stack_length + FLASH_UNIT_BYTES - 1) &
	    ~(FLASH_UNIT_BYTES - 1));
	CHECK(flash_swap_append(&modeled_ops, FLASH_SWAP_OFFSET + base,
	    data, data_length, scratch) == 0);
	CHECK(flash_swap_append(&modeled_ops,
	    FLASH_SWAP_OFFSET + base + stack_offset,
	    stack, stack_length, scratch) == 0);
	CHECK(flash_swap_append(&modeled_ops,
	    FLASH_SWAP_OFFSET + base + uarea_offset,
	    uarea, uarea_length, scratch) == 0);
	check_bytes(base, data, data_length);
	check_erased(base + data_length, stack_offset - data_length);
	check_bytes(base + stack_offset, stack, stack_length);
	check_erased(base + stack_offset + stack_length,
	    uarea_offset - stack_offset - stack_length);
	check_bytes(base + uarea_offset, uarea, uarea_length);
}

int
main(void)
{
	unsigned char data[5000], stack[333], uarea[3072];
	unsigned char replacement[17];
	unsigned char rewrite[2 * FLASH_UNIT_BYTES];
	unsigned char sector_before[FLASH_SECTOR_BYTES];
	unsigned char scratch[FLASH_PROG_BYTES];
	unsigned int calls_before, destination_erases_before;
	unsigned int scratch_erases_before, temp_block;

	memset(modeled_flash, 0, sizeof modeled_flash);
	fill(data, sizeof data, 11);
	fill(stack, sizeof stack, 37);
	fill(uarea, sizeof uarea, 71);
	program_image(0, data, sizeof data, stack, sizeof stack,
	    uarea, sizeof uarea, scratch);
	CHECK(erase_count[0] == 1);
	CHECK(erase_count[1] == 1);
	CHECK(erase_count[2] == 1);

	/* The next aligned image cannot erase or change the preceding image. */
	fill(replacement, sizeof replacement, 101);
	program_image(3 * FLASH_SECTOR_BYTES, replacement, sizeof replacement,
	    stack, 1, uarea, sizeof uarea, scratch);
	check_bytes(0, data, sizeof data);
	CHECK(erase_count[3] == 1);
	CHECK(erase_count[4] == 1);

	/* Empty leading pieces let the u area erase the first image sector. */
	program_image(5 * FLASH_SECTOR_BYTES, replacement, 0, stack, 0,
	    uarea, sizeof uarea, scratch);
	CHECK(erase_count[5] == 1);

	/* Reusing the first run erases its sectors and accepts new 0-to-1 bits. */
	program_image(0, replacement, sizeof replacement, stack, 1,
	    uarea, sizeof uarea, scratch);
	check_bytes(0, replacement, sizeof replacement);
	CHECK(erase_count[0] == 2);
	CHECK(erase_count[1] == 2);

	/* Invalid geometry and ROM-operation failures propagate without loops. */
	calls_before = program_calls;
	CHECK(flash_swap_append(&modeled_ops, FLASH_SWAP_OFFSET + 1,
	    replacement, sizeof replacement, scratch) == -1);
	CHECK(program_calls == calls_before);
	fail_erase = 1;
	CHECK(flash_swap_append(&modeled_ops, FLASH_SWAP_OFFSET +
	    5 * FLASH_SECTOR_BYTES, replacement, sizeof replacement,
	    scratch) == -1);

	/*
	 * A temporary extent initialized in strict block order uses append mode:
	 * one destination erase, sixteen programs, and no scratch-sector wear.
	 * A later duplicate block uses the rewrite path and preserves its peers.
	 */
	fill(sector_before, sizeof sector_before, 53);
	scratch_erases_before = erase_count[0];
	destination_erases_before = erase_count[6];
	calls_before = program_calls;
	for (temp_block = 0; temp_block < 4; temp_block++)
		CHECK(flash_swap_append(&modeled_ops, FLASH_SWAP_OFFSET +
		    6 * FLASH_SECTOR_BYTES + temp_block * FLASH_UNIT_BYTES,
		    sector_before + temp_block * FLASH_UNIT_BYTES,
		    FLASH_UNIT_BYTES, scratch) == 0);
	CHECK(erase_count[0] == scratch_erases_before);
	CHECK(erase_count[6] == destination_erases_before + 1);
	CHECK(program_calls == calls_before + 16);
	check_bytes(6 * FLASH_SECTOR_BYTES, sector_before,
	    sizeof sector_before);
	fill(rewrite, FLASH_UNIT_BYTES, 199);
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    6 * FLASH_SECTOR_BYTES + FLASH_UNIT_BYTES, rewrite,
	    FLASH_UNIT_BYTES, FLASH_SWAP_SCRATCH_OFFSET, scratch) == 0);
	check_bytes(6 * FLASH_SECTOR_BYTES, sector_before, FLASH_UNIT_BYTES);
	check_bytes(6 * FLASH_SECTOR_BYTES + FLASH_UNIT_BYTES, rewrite,
	    FLASH_UNIT_BYTES);
	check_bytes(6 * FLASH_SECTOR_BYTES + 2 * FLASH_UNIT_BYTES,
	    sector_before + 2 * FLASH_UNIT_BYTES, 2 * FLASH_UNIT_BYTES);

	/* Ordinary swap clients can rewrite one block without changing neighbors. */
	destination_erases_before = erase_count[6];
	fill(modeled_flash + 6 * FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES, 149);
	memcpy(sector_before, modeled_flash + 6 * FLASH_SECTOR_BYTES,
	    sizeof sector_before);
	fill(rewrite, sizeof rewrite, 211);
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    6 * FLASH_SECTOR_BYTES + FLASH_UNIT_BYTES, rewrite,
	    FLASH_UNIT_BYTES, FLASH_SWAP_SCRATCH_OFFSET, scratch) == 0);
	check_bytes(6 * FLASH_SECTOR_BYTES, sector_before, FLASH_UNIT_BYTES);
	check_bytes(6 * FLASH_SECTOR_BYTES + FLASH_UNIT_BYTES, rewrite,
	    FLASH_UNIT_BYTES);
	check_bytes(6 * FLASH_SECTOR_BYTES + 2 * FLASH_UNIT_BYTES,
	    sector_before + 2 * FLASH_UNIT_BYTES, 2 * FLASH_UNIT_BYTES);
	CHECK(erase_count[0] >= 1);
	CHECK(erase_count[6] == destination_erases_before + 1);

	/* A rewrite can cross a sector and reports each operation failure. */
	fill(rewrite, sizeof rewrite, 17);
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    7 * FLASH_SECTOR_BYTES - FLASH_UNIT_BYTES, rewrite,
	    sizeof rewrite, FLASH_SWAP_SCRATCH_OFFSET, scratch) == 0);
	fail_read = 1;
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    6 * FLASH_SECTOR_BYTES, rewrite, FLASH_UNIT_BYTES,
	    FLASH_SWAP_SCRATCH_OFFSET, scratch) == -1);
	fail_erase = 1;
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    6 * FLASH_SECTOR_BYTES, rewrite, FLASH_UNIT_BYTES,
	    FLASH_SWAP_SCRATCH_OFFSET, scratch) == -1);
	fail_program = 1;
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    6 * FLASH_SECTOR_BYTES, rewrite, FLASH_UNIT_BYTES,
	    FLASH_SWAP_SCRATCH_OFFSET, scratch) == -1);
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_SCRATCH_OFFSET,
	    rewrite, FLASH_UNIT_BYTES, FLASH_SWAP_SCRATCH_OFFSET,
	    scratch) == -1);
	CHECK(flash_swap_rewrite(&modeled_ops, FLASH_SWAP_OFFSET +
	    6 * FLASH_SECTOR_BYTES, replacement, sizeof replacement,
	    FLASH_SWAP_SCRATCH_OFFSET, scratch) == -1);
	fail_program = 1;
	CHECK(flash_swap_append(&modeled_ops, FLASH_SWAP_OFFSET +
	    5 * FLASH_SECTOR_BYTES, replacement, sizeof replacement,
	    scratch) == -1);

	if (failures != 0) {
		printf("flash swap: %d failures\n", failures);
		return 1;
	}
	printf("flash swap: image appends and block rewrites preserve bytes with a %u-byte scratch page\n",
	    (unsigned int)sizeof scratch);
	return 0;
}
