/*
 * Host gate for the PicoCalc loader's flash limit: the stamp decode and the
 * erase/program bound in sys/arch/rp2040/dev/loader_stamp.h, and the raw
 * swap writer of sys/arch/rp2040/dev/flash_swap.c driven through that bound
 * against a NOR model of the whole chip. The oracle computes every bound in
 * 64-bit arithmetic, independently of the header's difference form.
 */

#include <rp2040/dev/flash.h>
#include <rp2040/dev/flash_swap.h>
#include <rp2040/dev/loader_stamp.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define	LOADER_LIMIT	0x101fc000UL	/* pelrun/uf2loader 2.5 flash_end. */
#define	LOADER_END	(LOADER_LIMIT - FLASH_XIP_BASE)

static int failures;

#define	CHECK(condition) do { \
	if (!(condition)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
	} \
} while (0)

/* The admission rule, stated on 64-bit sums that cannot wrap. */
static int
oracle_admitted(uint64_t offset, uint64_t len, uint64_t end)
{
	return len != 0 && offset >= FLASH_FS_OFFSET && offset + len <= end;
}

/*
 * Compare the header with the oracle over offsets and lengths at one
 * granularity, and count any admitted operation that reaches the end.
 */
static void
sweep(uint32_t end, uint32_t step, uint32_t max_len, const char *label)
{
	uint64_t offset, len;
	unsigned long mismatches = 0, beyond = 0;
	int admitted;

	for (offset = 0; offset <= FLASH_TOTAL_BYTES + 2 * FLASH_BLOCK_BYTES;
	    offset += step) {
		for (len = step; len <= max_len; len += step) {
			admitted = flash_write_admitted((uint32_t)offset,
			    (uint32_t)len, end);
			if (admitted != oracle_admitted(offset, len, end))
				mismatches++;
			if (admitted && offset + len > end)
				beyond++;
		}
	}
	if (mismatches != 0 || beyond != 0) {
		failures++;
		printf("FAIL sweep %s end %#lx step %lu: %lu mismatches, "
		    "%lu admitted past the end\n", label, (unsigned long)end,
		    (unsigned long)step, mismatches, beyond);
	}
}

static void
test_no_stamp(void)
{
	static const uint32_t limits[] = { 0, LOADER_LIMIT, 0xffffffffUL,
	    0x12345678UL };
	static const uint32_t magics[] = { 0, LOADER_STAMP_MAGIC ^ 1,
	    0xffffffffUL };
	uint32_t end;
	size_t i, j;

	for (i = 0; i < sizeof(magics) / sizeof(magics[0]); i++)
		for (j = 0; j < sizeof(limits) / sizeof(limits[0]); j++) {
			end = 0;
			CHECK(loader_stamp_decode(magics[i], limits[j], &end) ==
			    LOADER_STAMP_NONE);
			CHECK(end == FLASH_TOTAL_BYTES);
		}
	end = FLASH_TOTAL_BYTES;
	CHECK(flash_write_admitted(FLASH_FS_OFFSET,
	    FLASH_TOTAL_BYTES - FLASH_FS_OFFSET, end));
	CHECK(flash_write_admitted(FLASH_TOTAL_BYTES - FLASH_SECTOR_BYTES,
	    FLASH_SECTOR_BYTES, end));
	CHECK(flash_swap_extent(end) == FLASH_SWAP_BYTES);
	sweep(end, FLASH_SECTOR_BYTES, 16 * FLASH_SECTOR_BYTES, "no stamp");
}

static void
test_valid_stamp(void)
{
	uint32_t end = 0, limit;

	CHECK(loader_stamp_decode(LOADER_STAMP_MAGIC, LOADER_LIMIT, &end) ==
	    LOADER_STAMP_VALID);
	CHECK(end == LOADER_END);
	CHECK(flash_swap_extent(end) == LOADER_END - FLASH_SWAP_OFFSET);
	CHECK(flash_swap_extent(end) == 368UL * 1024);

	/* The exhaustive sweeps at the erase and program granularities. */
	sweep(end, FLASH_SECTOR_BYTES, 32 * FLASH_SECTOR_BYTES, "loader 4K");
	sweep(end, FLASH_PROG_BYTES, 4 * FLASH_SECTOR_BYTES, "loader 256");

	/* Each limit the decode accepts, at sector granularity. */
	for (limit = FLASH_XIP_BASE + FLASH_FS_OFFSET + FLASH_SECTOR_BYTES;
	    limit <= FLASH_XIP_BASE + FLASH_TOTAL_BYTES;
	    limit += FLASH_SECTOR_BYTES) {
		end = 0;
		CHECK(loader_stamp_decode(LOADER_STAMP_MAGIC, limit, &end) ==
		    LOADER_STAMP_VALID);
		CHECK(end == limit - FLASH_XIP_BASE);
		sweep(end, FLASH_SECTOR_BYTES, 2 * FLASH_SECTOR_BYTES,
		    "every limit");
	}
}

static void
test_edges(void)
{
	const uint32_t end = LOADER_END;

	CHECK(flash_write_admitted(end - FLASH_SECTOR_BYTES,
	    FLASH_SECTOR_BYTES, end));
	CHECK(!flash_write_admitted(end - FLASH_SECTOR_BYTES,
	    FLASH_SECTOR_BYTES + 1, end));
	CHECK(flash_write_admitted(end - FLASH_PROG_BYTES,
	    FLASH_PROG_BYTES, end));
	CHECK(!flash_write_admitted(end - FLASH_PROG_BYTES + 1,
	    FLASH_PROG_BYTES, end));
	CHECK(!flash_write_admitted(end, FLASH_PROG_BYTES, end));
	CHECK(!flash_write_admitted(end, 1, end));
	CHECK(!flash_write_admitted(end - 1, 2, end));
	CHECK(flash_write_admitted(end - 1, 1, end));
	CHECK(!flash_write_admitted(FLASH_FS_OFFSET, 0, end));
	CHECK(!flash_write_admitted(end - FLASH_SECTOR_BYTES, 0, end));
	/* Sums that would wrap a 32-bit offset + length. */
	CHECK(!flash_write_admitted(0xffffff00UL, 0x200, end));
	CHECK(!flash_write_admitted(FLASH_FS_OFFSET, 0xffffffffUL, end));
	CHECK(!flash_write_admitted(end - FLASH_SECTOR_BYTES,
	    0xffffffffUL - FLASH_SECTOR_BYTES + 2, end));
	/* The kernel below the filesystem region is never a target. */
	CHECK(!flash_write_admitted(0, FLASH_SECTOR_BYTES, end));
	CHECK(!flash_write_admitted(FLASH_FS_OFFSET - FLASH_SECTOR_BYTES,
	    FLASH_SECTOR_BYTES, end));
	CHECK(flash_write_admitted(FLASH_FS_OFFSET, FLASH_SECTOR_BYTES, end));
	/* A zero writable end, the state before the stamp is read. */
	CHECK(!flash_write_admitted(FLASH_FS_OFFSET, FLASH_SECTOR_BYTES, 0));
	CHECK(!flash_write_admitted(FLASH_SWAP_OFFSET, FLASH_SECTOR_BYTES, 0));
}

static void
test_invalid_stamps(void)
{
	static const uint32_t limits[] = {
		LOADER_LIMIT + 0x100,			/* Misaligned. */
		LOADER_LIMIT + 1,
		LOADER_LIMIT - FLASH_PROG_BYTES,
		FLASH_XIP_BASE + FLASH_FS_OFFSET,	/* Kernel region end. */
		FLASH_XIP_BASE + FLASH_SECTOR_BYTES,	/* Inside the kernel. */
		FLASH_XIP_BASE,
		0,
		FLASH_XIP_BASE - FLASH_SECTOR_BYTES,	/* Below XIP. */
		FLASH_XIP_BASE + FLASH_TOTAL_BYTES + FLASH_SECTOR_BYTES,
		FLASH_XIP_BASE + 2 * FLASH_TOTAL_BYTES,	/* Past the chip. */
		0xfffff000UL,
		0x101fc000UL - FLASH_XIP_BASE,		/* A size, not an address. */
	};
	uint32_t end;
	size_t i;

	for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
		end = 0xdeadbeefUL;
		CHECK(loader_stamp_decode(LOADER_STAMP_MAGIC, limits[i], &end) ==
		    LOADER_STAMP_INVALID);
		CHECK(end == FLASH_FS_OFFSET);
		CHECK(flash_swap_extent(end) == 0);
		if (end != FLASH_FS_OFFSET)
			printf("  invalid limit %#lx gave end %#lx\n",
			    (unsigned long)limits[i], (unsigned long)end);
		sweep(end, FLASH_SECTOR_BYTES, 8 * FLASH_SECTOR_BYTES,
		    "fail closed");
		CHECK(!flash_write_admitted(FLASH_FS_OFFSET, FLASH_SECTOR_BYTES,
		    end));
	}
	/* The top of the chip is a valid limit: the whole chip. */
	CHECK(loader_stamp_decode(LOADER_STAMP_MAGIC,
	    FLASH_XIP_BASE + FLASH_TOTAL_BYTES, &end) == LOADER_STAMP_VALID);
	CHECK(end == FLASH_TOTAL_BYTES);
}

static void
test_swap_extent(void)
{
	CHECK(flash_swap_extent(0) == 0);
	CHECK(flash_swap_extent(FLASH_FS_OFFSET) == 0);
	CHECK(flash_swap_extent(FLASH_SWAP_OFFSET) == 0);
	CHECK(flash_swap_extent(FLASH_SWAP_OFFSET + FLASH_SECTOR_BYTES) ==
	    FLASH_SECTOR_BYTES);
	CHECK(flash_swap_extent(LOADER_END) == LOADER_END - 0x1a0000UL);
	CHECK(flash_swap_extent(FLASH_TOTAL_BYTES) == FLASH_SWAP_BYTES);
}

/*
 * The swap writer through the bound. The model holds the whole chip, refuses
 * 0-to-1 program transitions, and records the highest byte any erase or
 * program reached; the ops refuse what flash_write_admitted() refuses, in
 * the order flash_erase() and flash_program() apply it.
 */
static unsigned char chip[FLASH_TOTAL_BYTES];
static uint32_t model_end;
static uint64_t highest_write;
static unsigned long refused;

static int
model_read(unsigned int offset, unsigned char *data, unsigned int len)
{
	if ((uint64_t)offset + len > FLASH_TOTAL_BYTES)
		return -1;
	memcpy(data, chip + offset, len);
	return 0;
}

static int
model_erase(unsigned int offset, unsigned int len)
{
	if (!flash_write_admitted(offset, len, model_end)) {
		refused++;
		return -1;
	}
	if ((offset | len) & (FLASH_SECTOR_BYTES - 1))
		return -1;
	memset(chip + offset, 0xff, len);
	if ((uint64_t)offset + len > highest_write)
		highest_write = (uint64_t)offset + len;
	return 0;
}

static int
model_program(unsigned int offset, const unsigned char *data,
    unsigned int len)
{
	unsigned int i;

	if (!flash_write_admitted(offset, len, model_end)) {
		refused++;
		return -1;
	}
	if ((offset | len) & (FLASH_PROG_BYTES - 1))
		return -1;
	for (i = 0; i < len; i++) {
		if ((data[i] & ~chip[offset + i]) != 0)
			return -1;
		chip[offset + i] &= data[i];
	}
	if ((uint64_t)offset + len > highest_write)
		highest_write = (uint64_t)offset + len;
	return 0;
}

static const struct flash_swap_ops model_ops = {
	model_read,
	model_erase,
	model_program,
};

static void
test_swap_writer(void)
{
	static unsigned char image[8 * FLASH_SECTOR_BYTES];
	unsigned char scratch[FLASH_PROG_BYTES];
	unsigned char loader[FLASH_TOTAL_BYTES - LOADER_END];
	unsigned int base;
	size_t i;

	for (i = 0; i < sizeof(image); i++)
		image[i] = (unsigned char)(i * 7 + 3);
	memset(chip, 0xa5, sizeof(chip));
	memcpy(loader, chip + LOADER_END, sizeof(loader));
	model_end = LOADER_END;

	/* An image wholly below the limit, ending at it. */
	highest_write = 0;
	base = LOADER_END - sizeof(image);
	CHECK(flash_swap_append(&model_ops, base, image, sizeof(image),
	    scratch) == 0);
	CHECK(memcmp(chip + base, image, sizeof(image)) == 0);
	CHECK(highest_write == LOADER_END);

	/* An image that crosses the limit stops at it. */
	refused = 0;
	highest_write = 0;
	base = LOADER_END - 2 * FLASH_SECTOR_BYTES;
	CHECK(flash_swap_append(&model_ops, base, image, sizeof(image),
	    scratch) == -1);
	CHECK(refused == 1);
	CHECK(highest_write <= LOADER_END);

	/* A rewrite of the top sectors through the spare sector. */
	refused = 0;
	highest_write = 0;
	CHECK(flash_swap_rewrite(&model_ops,
	    LOADER_END - FLASH_SECTOR_BYTES, image, 2 * FLASH_SECTOR_BYTES,
	    FLASH_SWAP_SCRATCH_OFFSET, scratch) == -1);
	CHECK(refused == 1);
	CHECK(highest_write <= LOADER_END);

	/* Every write at or above the limit, at program granularity. */
	refused = 0;
	for (base = LOADER_END; base < FLASH_TOTAL_BYTES;
	    base += FLASH_PROG_BYTES)
		CHECK(flash_swap_append(&model_ops, base, image,
		    FLASH_PROG_BYTES, scratch) == -1);
	CHECK(refused == (FLASH_TOTAL_BYTES - LOADER_END) / FLASH_PROG_BYTES);

	CHECK(memcmp(loader, chip + LOADER_END, sizeof(loader)) == 0);

	/* Failing closed, the swap writer reaches nothing at all. */
	model_end = FLASH_FS_OFFSET;
	refused = 0;
	highest_write = 0;
	CHECK(flash_swap_append(&model_ops, FLASH_SWAP_OFFSET +
	    FLASH_SECTOR_BYTES, image, sizeof(image), scratch) == -1);
	CHECK(refused == 1);
	CHECK(highest_write == 0);
}

int
main(void)
{
	test_no_stamp();
	test_valid_stamp();
	test_edges();
	test_invalid_stamps();
	test_swap_extent();
	test_swap_writer();
	if (failures != 0) {
		printf("loader_stamp: %d failures\n", failures);
		return 1;
	}
	printf("loader_stamp: PASS (stamp decode, write bound, swap extent, "
	    "swap writer)\n");
	return 0;
}
