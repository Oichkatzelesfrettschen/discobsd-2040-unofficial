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
 * The production Dhara collector trigger (auto_gc() in
 * sys/arch/rp2040/dhara/map.c), compiled unmodified, over a NOR model with
 * the geometry of rp2040/dev/flash.h. check.sh also builds it against
 * mutated copies of map.c.
 *
 *   trigger_model random LIVE WRITES CEILING
 *	LIVE sectors written once, then WRITES uniformly random rewrites
 *	with a checkpoint every 20. Fails ("FAIL ceiling") when pages
 *	programmed per write exceed CEILING / 100.
 *   trigger_model adversary WRITES RESETS
 *	every logical sector written in order, then WRITES rewrites of the
 *	last-written sector, which keeps the journal tail live as long as
 *	possible. Fails on any write or checkpoint error ("FAIL write",
 *	"FAIL checkpoint") and when the journal grows into the safety margin
 *	("FAIL margin"). With RESETS set, a checkpoint and then a reset with
 *	resume follow every write, and every sector is checked against its
 *	last checkpointed contents ("FAIL sector", "FAIL resume").
 *   trigger_model high ROUNDS
 *	24 sectors, 0 to 7 and eight to 0xfffffffe from 0xfe000000 up, written
 *	in a rotated order, then ROUNDS rewrites of rotating sectors (the
 *	checkpoint's root is a high id for some and a low id for others),
 *	each followed by a checkpoint and a reset with resume. Every sector and the live count must survive
 *	("FAIL resume", "FAIL sector"): the checkpoint-tear test in
 *	journal.c accepts every id when two or more sectors are live.
 *
 * The program exits 1 on any FAIL.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"
#include <rp2040/dev/flash.h>

static uint8_t *region;
static unsigned long pages, erases;
static struct dhara_map map;
static uint8_t page[FLASH_UNIT_BYTES];
static const struct dhara_nand nand = {
	FLASH_LOG2_UNIT, FLASH_LOG2_UPB, FLASH_FS_BLOCKS
};

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
	(void)err;
	memset(region + (size_t)b * FLASH_ERASE_BYTES, 0xff, FLASH_ERASE_BYTES);
	erases++;
	return 0;
}

/*
 * A program over an unerased page breaks the model's own invariant. It ends
 * the run at once: an error return would let journal.c treat the block as
 * bad, skip it and finish the write. Each program also samples the journal
 * with the page being programmed counted in, since collection inside one
 * map write grows the journal before a checkpoint advances tail_sync.
 */
static void observe(unsigned long pending);
static int observing;

int
dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
    const uint8_t *data, dhara_error_t *err)
{
	uint8_t *q = region + (size_t)p * FLASH_UNIT_BYTES;
	size_t i;

	(void)n;
	(void)err;
	for (i = 0; i < FLASH_UNIT_BYTES; i++) {
		if (q[i] != 0xff) {
			printf("FAIL program over unerased page %lu\n",
			    (unsigned long)p);
			exit(1);
		}
	}
	memcpy(q, data, FLASH_UNIT_BYTES);
	pages++;
	if (observing)
		observe(1);
	return 0;
}

int
dhara_nand_is_free(const struct dhara_nand *n, dhara_page_t p)
{
	const uint8_t *q = region + (size_t)p * FLASH_UNIT_BYTES;
	size_t i;

	(void)n;
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
	(void)err;
	memcpy(data, region + (size_t)p * FLASH_UNIT_BYTES + offset, length);
	return 0;
}

int
dhara_nand_copy(const struct dhara_nand *n, dhara_page_t src,
    dhara_page_t dst, dhara_error_t *err)
{
	uint8_t buf[FLASH_UNIT_BYTES];

	dhara_nand_read(n, src, 0, sizeof(buf), buf, err);
	return dhara_nand_prog(n, dst, buf, err);
}

static void
resume(void)
{
	dhara_error_t err = DHARA_E_NONE;

	memset(&map, 0, sizeof(map));
	dhara_map_init(&map, &nand, page, FLASH_GC_RATIO);
	observing = 0;
	(void)dhara_map_resume(&map, &err);
	observing = 1;
}

static unsigned long peak_size, least_free;

static void
observe(unsigned long pending)
{
	unsigned long size = dhara_journal_size(&map.journal) + pending;
	unsigned long cap = dhara_journal_capacity(&map.journal);

	if (size > peak_size)
		peak_size = size;
	if (cap - size < least_free)
		least_free = cap - size;
}

static int failures;

static void
fail(const char *what, unsigned long n, int err)
{
	failures++;
	printf("FAIL %s at %lu: %s\n", what, n, dhara_strerror((dhara_error_t)err));
}

static void
fill(uint8_t *data, unsigned long sector, unsigned long version)
{
	memset(data, (int)((sector * 31 + version * 7) & 0x7f), FLASH_UNIT_BYTES);
	memcpy(data, &sector, sizeof(sector));
	memcpy(data + 16, &version, sizeof(version));
}

static int
put(unsigned long sector, unsigned long version, unsigned long n)
{
	uint8_t data[FLASH_UNIT_BYTES];
	dhara_error_t err = DHARA_E_NONE;

	fill(data, sector, version);
	if (dhara_map_write(&map, (dhara_sector_t)sector, data, &err) < 0) {
		fail("write", n, err);
		return -1;
	}
	observe(0);
	return 0;
}

static int
checkpoint(unsigned long n)
{
	dhara_error_t err = DHARA_E_NONE;

	if (dhara_map_sync(&map, &err) < 0) {
		fail("checkpoint", n, err);
		return -1;
	}
	observe(0);
	return 0;
}

static unsigned long
xorshift(void)
{
	static unsigned long s = 0x9e3779b9UL;

	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s & 0xffffffffUL;
}

static void
start(void)
{
	region = malloc(FLASH_FS_BYTES);
	if (region == NULL)
		exit(2);
	memset(region, 0xff, FLASH_FS_BYTES);
	resume();
	least_free = (unsigned long)-1;
}

static int
run_random(unsigned long live, unsigned long writes, unsigned long ceiling)
{
	unsigned long n, p0, e0;

	for (n = 0; n < live; n++)
		if (put(n, 0, n) < 0)
			return 1;
	if (checkpoint(0) < 0)
		return 1;
	p0 = pages;
	e0 = erases;
	peak_size = 0;
	least_free = (unsigned long)-1;
	for (n = 0; n < writes; n++) {
		if (put(xorshift() % live, n + 1, n) < 0)
			return 1;
		if (n % 20 == 19 && checkpoint(n) < 0)
			return 1;
	}
	printf("live=%lu writes=%lu pages_per_write=%.2f erases_per_write=%.3f "
	    "peak_journal=%lu least_free=%lu journal_capacity=%lu "
	    "map_capacity=%lu\n", live, writes,
	    (double)(pages - p0) / writes, (double)(erases - e0) / writes,
	    peak_size, least_free,
	    (unsigned long)dhara_journal_capacity(&map.journal),
	    (unsigned long)dhara_map_capacity(&map));
	if ((pages - p0) * 100 > ceiling * writes) {
		printf("FAIL ceiling: %.2f pages per write above %.2f\n",
		    (double)(pages - p0) / writes, ceiling / 100.0);
		failures++;
	}
	return failures != 0;
}

static unsigned long *version;

static int
verify(unsigned long cap, unsigned long n)
{
	uint8_t want[FLASH_UNIT_BYTES], got[FLASH_UNIT_BYTES];
	dhara_error_t err;
	unsigned long s;

	for (s = 0; s < cap; s++) {
		err = DHARA_E_NONE;
		if (dhara_map_read(&map, (dhara_sector_t)s, got, &err) < 0) {
			fail("read after resume", n, err);
			return -1;
		}
		fill(want, s, version[s]);
		if (memcmp(want, got, sizeof(want)) != 0) {
			printf("FAIL sector: %lu lost after reset at %lu\n", s, n);
			failures++;
			return -1;
		}
	}
	return 0;
}

static int
run_adversary(unsigned long writes, int resets)
{
	unsigned long cap = dhara_map_capacity(&map), n;

	version = calloc(cap, sizeof(*version));
	if (version == NULL)
		return 2;
	for (n = 0; n < cap; n++)
		if (put(n, 0, n) < 0)
			return 1;
	if (checkpoint(0) < 0)
		return 1;
	peak_size = 0;
	least_free = (unsigned long)-1;
	for (n = 0; n < writes; n++) {
		if (put(cap - 1, n + 1, n) < 0)
			break;
		if (resets) {
			if (checkpoint(n) < 0)
				break;
			version[cap - 1] = n + 1;
			resume();
			if (dhara_map_size(&map) != cap) {
				printf("FAIL resume: %lu sectors held at %lu\n",
				    (unsigned long)dhara_map_size(&map), n);
				failures++;
				break;
			}
			if (verify(cap, n) < 0)
				break;
		}
	}
	if (least_free < (unsigned long)DHARA_MAX_RETRIES <<
	    map.journal.nand->log2_ppb) {
		printf("FAIL margin: %lu free journal pages, below the safety "
		    "margin\n", least_free);
		failures++;
	}
	printf("adversary writes=%lu resets=%d peak_journal=%lu least_free=%lu "
	    "journal_capacity=%lu map_capacity=%lu failures=%d\n", n, resets,
	    peak_size, least_free,
	    (unsigned long)dhara_journal_capacity(&map.journal),
	    (unsigned long)dhara_map_capacity(&map), failures);
	return failures != 0;
}

static const unsigned long high_ids[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	0xff000000UL, 0xff000001UL, 0xff000002UL, 0xff0000ffUL,
	0xff00ff00UL, 0xffff0000UL, 0xffff0001UL, 0xffffff00UL,
	0xfffffffeUL, 0xfffffffdUL, 0xfffffffcUL, 0xfffffe00UL,
	0xfffe0000UL, 0xfe000000UL, 0xff800000UL, 0xffffff80UL,
};
#define NHIGH (sizeof(high_ids) / sizeof(high_ids[0]))

static int
verify_high(const unsigned long *ver, unsigned long n)
{
	uint8_t want[FLASH_UNIT_BYTES], got[FLASH_UNIT_BYTES];
	dhara_error_t err;
	unsigned i;

	if (dhara_map_size(&map) != NHIGH) {
		printf("FAIL resume: %lu sectors held at %lu, want %lu\n",
		    (unsigned long)dhara_map_size(&map), n,
		    (unsigned long)NHIGH);
		failures++;
		return -1;
	}
	for (i = 0; i < NHIGH; i++) {
		err = DHARA_E_NONE;
		if (dhara_map_read(&map, (dhara_sector_t)high_ids[i], got,
		    &err) < 0) {
			fail("read after resume", n, err);
			return -1;
		}
		fill(want, high_ids[i], ver[i]);
		if (memcmp(want, got, sizeof(want)) != 0) {
			printf("FAIL sector: %lx lost after reset at %lu\n",
			    high_ids[i], n);
			failures++;
			return -1;
		}
	}
	return 0;
}

static int
run_high(unsigned long rounds)
{
	unsigned long ver[NHIGH], n;
	unsigned i;

	memset(ver, 0, sizeof(ver));
	/* The last write of the first order is a high id, of the second a low one. */
	for (i = 0; i < NHIGH; i++)
		if (put(high_ids[(i + NHIGH / 2) % NHIGH], 0, i) < 0)
			return 1;
	if (checkpoint(0) < 0)
		return 1;
	resume();
	if (verify_high(ver, 0) < 0)
		return 1;
	for (n = 1; n <= rounds; n++) {
		i = (unsigned)(n * 5 % NHIGH);
		if (put(high_ids[i], n, n) < 0 || checkpoint(n) < 0)
			return 1;
		ver[i] = n;
		resume();
		if (verify_high(ver, n) < 0)
			return 1;
	}
	printf("high rounds=%lu sectors=%lu failures=%d\n", rounds,
	    (unsigned long)NHIGH, failures);
	return failures != 0;
}

int
main(int argc, char **argv)
{
	if (argc == 5 && strcmp(argv[1], "random") == 0) {
		start();
		return run_random(strtoul(argv[2], NULL, 10),
		    strtoul(argv[3], NULL, 10), strtoul(argv[4], NULL, 10));
	}
	if (argc == 4 && strcmp(argv[1], "adversary") == 0) {
		start();
		return run_adversary(strtoul(argv[2], NULL, 10), atoi(argv[3]));
	}
	if (argc == 3 && strcmp(argv[1], "high") == 0) {
		start();
		return run_high(strtoul(argv[2], NULL, 10));
	}
	fprintf(stderr, "usage: trigger_model random LIVE WRITES CEILING | "
	    "adversary WRITES RESETS | high ROUNDS\n");
	return 2;
}
