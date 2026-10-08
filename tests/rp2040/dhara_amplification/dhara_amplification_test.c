/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Host oracle for the program cost of Dhara checkpoints under the RP2040
 * geometry.
 *
 * The vendored map and journal run against a NOR model with the geometry
 * and garbage-collection ratio of rp2040/dev/flash.h. The model counts
 * Dhara page programs: dhara_nand_prog() and dhara_nand_copy() each
 * program one FLASH_UNIT_BYTES page, which the kernel issues as
 * FLASH_UNIT_BYTES / FLASH_PROG_BYTES program calls, and it counts block
 * erases. As in tools/flashimg, programming a page that is not erased is an
 * error, because it breaks Dhara's sequential-programming contract.
 *
 * Expected counts are written for the geometry flash.h selects, where one
 * checkpoint group fills an erase block: seven user pages and one metadata
 * page. dhara_map_sync() pads an open group to its metadata page by copying
 * live tail pages. From an empty journal below the auto_gc() threshold,
 * with every write naming a distinct sector:
 *
 *	isolated: each write followed by a sync programs 8 pages;
 *	batched:  N writes followed by one sync program 8 * ceil(N / 7).
 *
 * The constants are the prediction, not values read back from the journal,
 * so a journal that chooses a different group size fails these traces as
 * well as the geometry table that check.sh compares.
 *
 * "geometry" prints the derived geometry as "key value" lines. With no
 * argument the traces run; an unmet expectation prints a FAIL line naming
 * the trace and exits 1.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"
#include <rp2040/dev/flash.h>

#define GROUP_PAGES		8UL	/* Predicted pages per group. */
#define GROUP_USER_PAGES	7UL	/* Predicted user pages. */
#define TRACE_LENGTH		20UL	/* The plan's twenty-request example. */
#define BATCH_LIMIT		21UL	/* Three full groups: every boundary. */

static uint8_t *region;			/* FLASH_FS_BYTES of model flash. */
static unsigned long page_programs;
static unsigned long block_erases;
static int failures;

static const struct dhara_nand nand = {
	.log2_page_size = FLASH_LOG2_UNIT,
	.log2_ppb = FLASH_LOG2_UPB,
	.num_blocks = FLASH_FS_BLOCKS,
};

static void
fail(const char *trace, const char *what, unsigned long n,
    unsigned long got, unsigned long want)
{
	failures++;
	printf("FAIL %s: %s at N=%lu: %lu, expected %lu\n", trace, what, n,
	    got, want);
}

int
dhara_nand_is_bad(const struct dhara_nand *n, dhara_block_t b)
{
	(void)n;
	(void)b;
	return 0;
}

void
dhara_nand_mark_bad(const struct dhara_nand *n, dhara_block_t b)
{
	(void)n;
	(void)b;
}

int
dhara_nand_erase(const struct dhara_nand *n, dhara_block_t b,
    dhara_error_t *err)
{
	(void)n;
	if (b >= FLASH_FS_BLOCKS) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	memset(region + (size_t)b * FLASH_ERASE_BYTES, 0xff,
	    FLASH_ERASE_BYTES);
	block_erases++;
	return 0;
}

int
dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
    const uint8_t *data, dhara_error_t *err)
{
	uint8_t *q;
	size_t i;

	(void)n;
	if ((size_t)p >= FLASH_FS_BYTES / FLASH_UNIT_BYTES) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	q = region + (size_t)p * FLASH_UNIT_BYTES;
	for (i = 0; i < FLASH_UNIT_BYTES; i++) {
		if (q[i] != 0xff) {
			printf("FAIL model: page %lu programmed twice\n",
			    (unsigned long)p);
			failures++;
			dhara_set_error(err, DHARA_E_BAD_BLOCK);
			return -1;
		}
	}
	memcpy(q, data, FLASH_UNIT_BYTES);
	page_programs++;
	return 0;
}

int
dhara_nand_is_free(const struct dhara_nand *n, dhara_page_t p)
{
	const uint8_t *q;
	size_t i;

	(void)n;
	if ((size_t)p >= FLASH_FS_BYTES / FLASH_UNIT_BYTES)
		return 0;
	q = region + (size_t)p * FLASH_UNIT_BYTES;
	for (i = 0; i < FLASH_UNIT_BYTES; i++)
		if (q[i] != 0xff)
			return 0;
	return 1;
}

int
dhara_nand_read(const struct dhara_nand *n, dhara_page_t p, size_t offset,
    size_t length, uint8_t *data, dhara_error_t *err)
{
	(void)n;
	if (offset > FLASH_UNIT_BYTES || length > FLASH_UNIT_BYTES - offset ||
	    (size_t)p >= FLASH_FS_BYTES / FLASH_UNIT_BYTES) {
		dhara_set_error(err, DHARA_E_ECC);
		return -1;
	}
	memcpy(data, region + (size_t)p * FLASH_UNIT_BYTES + offset, length);
	return 0;
}

int
dhara_nand_copy(const struct dhara_nand *n, dhara_page_t src,
    dhara_page_t dst, dhara_error_t *err)
{
	uint8_t buf[FLASH_UNIT_BYTES];

	if (dhara_nand_read(n, src, 0, FLASH_UNIT_BYTES, buf, err) < 0)
		return -1;
	return dhara_nand_prog(n, dst, buf, err);
}

/* The content written to sector s in generation g. */
static void
pattern(uint8_t *data, unsigned long s, unsigned long g)
{
	size_t i;

	for (i = 0; i < FLASH_UNIT_BYTES; i++)
		data[i] = (uint8_t)(s * 31UL + g * 7UL + i);
}

/* An erased region and an empty map: the state flashimg starts from. */
static void
fresh(struct dhara_map *m, uint8_t *page)
{
	dhara_error_t err = DHARA_E_NONE;

	memset(region, 0xff, FLASH_FS_BYTES);
	dhara_map_init(m, &nand, page, FLASH_GC_RATIO);
	(void)dhara_map_resume(m, &err);
	page_programs = 0;
	block_erases = 0;
}

static int
write_sector(struct dhara_map *m, unsigned long s, unsigned long g)
{
	uint8_t data[FLASH_UNIT_BYTES];
	dhara_error_t err = DHARA_E_NONE;

	pattern(data, s, g);
	if (dhara_map_write(m, (dhara_sector_t)s, data, &err) < 0) {
		printf("FAIL io: write of sector %lu failed, dhara error %d\n",
		    s, (int)err);
		failures++;
		return -1;
	}
	return 0;
}

static int
sync_map(struct dhara_map *m)
{
	dhara_error_t err = DHARA_E_NONE;

	if (dhara_map_sync(m, &err) < 0) {
		printf("FAIL io: sync failed, dhara error %d\n", (int)err);
		failures++;
		return -1;
	}
	return 0;
}

/* Sectors [0, count) must read back as generation g. */
static void
verify(struct dhara_map *m, const char *trace, unsigned long count,
    unsigned long g)
{
	uint8_t want[FLASH_UNIT_BYTES], got[FLASH_UNIT_BYTES];
	dhara_error_t err = DHARA_E_NONE;
	unsigned long s;

	for (s = 0; s < count; s++) {
		pattern(want, s, g);
		if (dhara_map_read(m, (dhara_sector_t)s, got, &err) < 0 ||
		    memcmp(want, got, sizeof(want)) != 0) {
			printf("FAIL %s: sector %lu does not read back\n",
			    trace, s);
			failures++;
			return;
		}
	}
}

static unsigned long
batched_prediction(unsigned long n)
{
	return GROUP_PAGES * ((n + GROUP_USER_PAGES - 1) / GROUP_USER_PAGES);
}

static void
geometry(struct dhara_map *m, uint8_t *page)
{
	fresh(m, page);
	printf("unit_bytes %lu\n", (unsigned long)FLASH_UNIT_BYTES);
	printf("prog_bytes %lu\n", (unsigned long)FLASH_PROG_BYTES);
	printf("prog_calls_per_unit %lu\n",
	    (unsigned long)(FLASH_UNIT_BYTES / FLASH_PROG_BYTES));
	printf("erase_bytes %lu\n", (unsigned long)FLASH_ERASE_BYTES);
	printf("fs_blocks %lu\n", (unsigned long)FLASH_FS_BLOCKS);
	printf("log2_ppb %u\n", (unsigned int)nand.log2_ppb);
	printf("log2_ppc %u\n", (unsigned int)m->journal.log2_ppc);
	printf("group_pages %lu\n", 1UL << m->journal.log2_ppc);
	printf("group_user_pages %lu\n", (1UL << m->journal.log2_ppc) - 1);
	printf("gc_ratio %lu\n", (unsigned long)FLASH_GC_RATIO);
	printf("capacity_sectors %lu\n",
	    (unsigned long)dhara_map_capacity(m));
}

/* (a) TRACE_LENGTH isolated write+sync pairs from an empty journal. */
static void
trace_isolated(struct dhara_map *m, uint8_t *page)
{
	unsigned long i, before;

	fresh(m, page);
	for (i = 0; i < TRACE_LENGTH; i++) {
		before = page_programs;
		if (write_sector(m, i, 0) < 0 || sync_map(m) < 0)
			return;
		if (page_programs - before != GROUP_PAGES)
			fail("trace-a", "programs per write+sync", i + 1,
			    page_programs - before, GROUP_PAGES);
	}
	verify(m, "trace-a", TRACE_LENGTH, 0);
	printf("trace-a isolated N=%lu programs=%lu expected=%lu "
	    "prog_calls=%lu erases=%lu\n", TRACE_LENGTH, page_programs,
	    TRACE_LENGTH * GROUP_PAGES,
	    page_programs * (FLASH_UNIT_BYTES / FLASH_PROG_BYTES),
	    block_erases);
}

/* (b) N writes then one sync, for every N through three full groups. */
static void
trace_batched(struct dhara_map *m, uint8_t *page)
{
	unsigned long n, i;

	for (n = 1; n <= BATCH_LIMIT; n++) {
		fresh(m, page);
		for (i = 0; i < n; i++)
			if (write_sector(m, i, 0) < 0)
				return;
		if (sync_map(m) < 0)
			return;
		if (page_programs != batched_prediction(n))
			fail("trace-b", "programs for N writes and one sync",
			    n, page_programs, batched_prediction(n));
		verify(m, "trace-b", n, 0);
		if (n == TRACE_LENGTH)
			printf("trace-b batched N=%lu programs=%lu "
			    "expected=%lu prog_calls=%lu erases=%lu\n", n,
			    page_programs, batched_prediction(n),
			    page_programs *
			    (FLASH_UNIT_BYTES / FLASH_PROG_BYTES),
			    block_erases);
	}
	printf("trace-b batched N=1..%lu checked against 8*ceil(N/7)\n",
	    BATCH_LIMIT);
}

/*
 * Write every sector once and sync, which leaves the journal at or above
 * the map capacity, where each write first runs auto_gc().
 */
static int
fill(struct dhara_map *m, uint8_t *page, unsigned long *capacity)
{
	unsigned long s;

	fresh(m, page);
	*capacity = (unsigned long)dhara_map_capacity(m);
	for (s = 0; s < *capacity; s++)
		if (write_sector(m, s, 0) < 0)
			return -1;
	if (sync_map(m) < 0)
		return -1;
	if (dhara_journal_size(&m->journal) < dhara_map_capacity(m)) {
		printf("FAIL trace-c: journal size %lu below capacity %lu "
		    "after the fill\n",
		    (unsigned long)dhara_journal_size(&m->journal), *capacity);
		failures++;
		return -1;
	}
	page_programs = 0;
	block_erases = 0;
	return 0;
}

/* (c) Both traces again at the auto_gc() threshold; reported, not asserted. */
static void
trace_threshold(struct dhara_map *m, uint8_t *page)
{
	unsigned long capacity, i, before, low = (unsigned long)-1, high = 0;

	if (fill(m, page, &capacity) < 0)
		return;
	for (i = 0; i < TRACE_LENGTH; i++) {
		before = page_programs;
		if (write_sector(m, i, 1) < 0 || sync_map(m) < 0)
			return;
		if (page_programs - before < low)
			low = page_programs - before;
		if (page_programs - before > high)
			high = page_programs - before;
	}
	verify(m, "trace-c", TRACE_LENGTH, 1);
	printf("trace-c isolated-at-threshold N=%lu capacity=%lu "
	    "programs=%lu per_op=%lu..%lu erases=%lu\n", TRACE_LENGTH,
	    capacity, page_programs, low, high, block_erases);

	if (fill(m, page, &capacity) < 0)
		return;
	for (i = 0; i < TRACE_LENGTH; i++)
		if (write_sector(m, i, 1) < 0)
			return;
	if (sync_map(m) < 0)
		return;
	verify(m, "trace-c", TRACE_LENGTH, 1);
	printf("trace-c batched-at-threshold N=%lu capacity=%lu "
	    "programs=%lu erases=%lu\n", TRACE_LENGTH, capacity,
	    page_programs, block_erases);
}

/*
 * (d) Steady-state cost by map occupancy; reported, not asserted. A share
 * of the capacity is written once and synced; then the most recently
 * written TRACE_LENGTH sectors are rewritten in rounds, each write followed
 * by a sync as flstrategy() does, until about WARMUP_TURNS times the
 * capacity has been written, so cold sectors reach the tail as they do once
 * a root image ages. MEASURE_ROUNDS isolated rounds and then
 * MEASURE_ROUNDS batched rounds, each of TRACE_LENGTH writes and one sync,
 * are measured; seven rounds span whole checkpoint groups, so the totals do
 * not depend on where in a group the measurement starts. 100 percent is the
 * shipped image, which flashimg writes at the full capacity.
 */
#define WARMUP_TURNS	3UL
#define MEASURE_ROUNDS	7UL

static void
trace_occupancy(struct dhara_map *m, uint8_t *page)
{
	static const unsigned long percent[] = { 100, 90, 85, 80, 75, 50 };
	unsigned long k, capacity, live, base, round, rounds, i, r;
	unsigned long isolated, batched, writes;

	for (k = 0; k < sizeof(percent) / sizeof(percent[0]); k++) {
		fresh(m, page);
		capacity = (unsigned long)dhara_map_capacity(m);
		live = capacity * percent[k] / 100;
		base = live - TRACE_LENGTH;
		for (i = 0; i < live; i++)
			if (write_sector(m, i, 0) < 0)
				return;
		if (sync_map(m) < 0)
			return;
		rounds = WARMUP_TURNS * capacity / TRACE_LENGTH;
		for (round = 1; round <= rounds; round++)
			for (i = 0; i < TRACE_LENGTH; i++)
				if (write_sector(m, base + i, round) < 0 ||
				    sync_map(m) < 0)
					return;
		writes = MEASURE_ROUNDS * TRACE_LENGTH;
		page_programs = 0;
		for (r = 0; r < MEASURE_ROUNDS; r++, round++)
			for (i = 0; i < TRACE_LENGTH; i++)
				if (write_sector(m, base + i, round) < 0 ||
				    sync_map(m) < 0)
					return;
		isolated = page_programs;
		page_programs = 0;
		for (r = 0; r < MEASURE_ROUNDS; r++, round++) {
			for (i = 0; i < TRACE_LENGTH; i++)
				if (write_sector(m, base + i, round) < 0)
					return;
			if (sync_map(m) < 0)
				return;
		}
		batched = page_programs;
		round--;
		verify(m, "trace-d", base, 0);
		for (i = 0; i < TRACE_LENGTH; i++) {
			uint8_t want[FLASH_UNIT_BYTES], got[FLASH_UNIT_BYTES];
			dhara_error_t err = DHARA_E_NONE;

			pattern(want, base + i, round);
			if (dhara_map_read(m, (dhara_sector_t)(base + i), got,
			    &err) < 0 || memcmp(want, got, sizeof(want)) != 0) {
				printf("FAIL trace-d: hot sector %lu does not "
				    "read back\n", base + i);
				failures++;
				return;
			}
		}
		printf("trace-d occupancy=%lu%% live=%lu/%lu writes=%lu "
		    "isolated=%lu batched=%lu pages_per_write_x100=%lu/%lu\n",
		    percent[k], live, capacity, writes, isolated, batched,
		    isolated * 100 / writes, batched * 100 / writes);
	}
}

int
main(int argc, char **argv)
{
	static struct dhara_map map;
	static uint8_t page[FLASH_UNIT_BYTES];

	region = malloc(FLASH_FS_BYTES);
	if (region == NULL) {
		perror("malloc");
		return 2;
	}
	if (argc == 2 && strcmp(argv[1], "geometry") == 0) {
		geometry(&map, page);
		free(region);
		return 0;
	}
	if (argc != 1) {
		fprintf(stderr, "usage: dhara_amplification_test [geometry]\n");
		free(region);
		return 2;
	}
	trace_isolated(&map, page);
	trace_batched(&map, page);
	trace_threshold(&map, page);
	trace_occupancy(&map, page);
	free(region);
	return failures == 0 ? 0 : 1;
}
