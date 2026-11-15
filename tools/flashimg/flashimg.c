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
 * Write a DiscoBSD disk image into an RP2040 flash image in the on-flash
 * format sys/arch/rp2040/dev/flash.c reads.
 *
 * The kernel keeps its root behind Dhara, so the bytes in flash are Dhara's
 * journal rather than the filesystem itself, and a disk image from fsutil
 * cannot be programmed as it is. This tool runs the same vendored Dhara
 * sources against a memory model of the filesystem region, with the
 * geometry and garbage-collection ratio from dev/flash.h, writes the
 * FLASH_UNIT_BYTES units of the disk image through dhara_map_write, and
 * emits the resulting region. Programmed at FLASH_FS_OFFSET, the kernel
 * resumes it as a map it wrote itself.
 *
 * Every unit Dhara maps is live: garbage collection copies it forward for
 * the life of the map. A free block of a 2.11BSD filesystem holds nothing
 * the system reads before it is allocated and overwritten, except the
 * blocks that chain the free list, so those free blocks are left unwritten
 * and read back as 0xff (dhara_map_read). Every other unit, the partition
 * table, superblocks, inode areas, allocated blocks, the chain blocks and
 * anything outside a UFS partition, is written.
 *
 * Usage: flashimg [-c] disk.img flash.img
 *
 * With -c the capacity is printed and nothing is written, which is how the
 * partition sizes in distrib/rp2040/Makefile.inc are chosen.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rp2040/dev/flash.h"
#include "map.h"

static uint8_t *region;			/* FLASH_FS_BYTES of model flash. */

/*
 * The 2.11BSD superblock of tools/fsutil/bsdfs.h as it lies on disk, in
 * 32-bit little-endian words: magic, isize, fsize, swapsz, nfree, free[200],
 * ... tfree at byte 972 ... and the second magic at byte 1020. A free-list
 * chain block holds nfree and its own 200 entries in the same form.
 */
#define	UFS_PART_TYPE		0xb7
#define	UFS_MAGIC1		0x3c3c5346	/* "FS<<" */
#define	UFS_MAGIC2		0x53463e3e	/* ">>FS" */
#define	UFS_NICFREE		200
#define	SB_ISIZE		4
#define	SB_FSIZE		8
#define	SB_NFREE		16
#define	SB_FREE			20
#define	SB_TFREE		972
#define	SB_MAGIC2		1020

enum unit_state { UNIT_WRITE, UNIT_FREE, UNIT_CHAIN };

static uint32_t
word(const uint8_t *p, size_t offset)
{
	return (uint32_t)p[offset] | (uint32_t)p[offset + 1] << 8 |
	    (uint32_t)p[offset + 2] << 16 | (uint32_t)p[offset + 3] << 24;
}

/*
 * Walk the free list of the filesystem whose block 0 is image unit base
 * and mark its blocks in state[]: UNIT_CHAIN for a block whose contents
 * continue the list (alloc() in sys/kern/ufs_alloc.c reads it when the
 * superblock's list runs out), UNIT_FREE for the others. The walk must
 * account for exactly the superblock's tfree blocks, each once and inside
 * the data area, or the image is refused rather than written partly.
 */
static int
mark_free_list(const uint8_t *image, unsigned long units, unsigned long base,
    uint8_t *state, unsigned long *chains, unsigned long *frees)
{
	const uint8_t *sb = image + base * FLASH_UNIT_BYTES;
	const uint8_t *list;
	uint32_t isize, fsize, nfree, tfree, b, i;
	unsigned long walked = 0;
	size_t first;

	isize = word(sb, SB_ISIZE);
	fsize = word(sb, SB_FSIZE);
	tfree = word(sb, SB_TFREE);
	if (fsize > units - base || isize >= fsize) {
		fprintf(stderr, "flashimg: filesystem at unit %lu does not fit "
		    "the image\n", base);
		return -1;
	}
	nfree = word(sb, SB_NFREE);
	list = sb;
	first = SB_FREE;
	while (nfree != 0) {
		if (nfree > UFS_NICFREE) {
			fprintf(stderr, "flashimg: free count %lu at unit %lu\n",
			    (unsigned long)nfree, base);
			return -1;
		}
		for (i = nfree; i-- > 0; ) {
			b = word(list, first + 4 * i);
			if (i == 0 && b == 0)
				break;		/* The end of the list. */
			if (b < isize || b >= fsize ||
			    state[base + b] != UNIT_WRITE) {
				fprintf(stderr, "flashimg: free block %lu of the "
				    "filesystem at unit %lu is invalid or listed "
				    "twice\n", (unsigned long)b, base);
				return -1;
			}
			walked++;
			state[base + b] = i == 0 ? UNIT_CHAIN : UNIT_FREE;
			if (i == 0)
				(*chains)++;
			else
				(*frees)++;
		}
		if (i == 0 && word(list, first) == 0)
			break;
		list = image + (base + word(list, first)) * FLASH_UNIT_BYTES;
		nfree = word(list, 0);
		first = 4;
	}
	if (walked != tfree) {
		fprintf(stderr, "flashimg: free list of the filesystem at unit %lu "
		    "holds %lu blocks, its superblock counts %lu\n", base,
		    walked, (unsigned long)tfree);
		return -1;
	}
	return 0;
}

/*
 * Mark the free blocks of every UFS partition the image's partition table
 * names. An image without a table, or a partition without both magic
 * words, is written whole.
 */
static int
mark_free_blocks(const uint8_t *image, unsigned long units, uint8_t *state,
    unsigned long *chains, unsigned long *frees)
{
	const uint8_t *entry;
	unsigned long base;
	uint32_t offset;
	int i;

	if (units == 0 || image[510] != 0x55 || image[511] != 0xaa)
		return 0;
	for (i = 0; i < 4; i++) {
		entry = image + 446 + 16 * i;
		offset = word(entry, 8);
		if (entry[4] != UFS_PART_TYPE || offset % 2 != 0)
			continue;
		base = offset / 2;
		if (base >= units ||
		    word(image + base * FLASH_UNIT_BYTES, 0) != UFS_MAGIC1 ||
		    word(image + base * FLASH_UNIT_BYTES, SB_MAGIC2) !=
		    UFS_MAGIC2)
			continue;
		if (mark_free_list(image, units, base, state, chains, frees) < 0)
			return -1;
	}
	return 0;
}

static const struct dhara_nand flnand = {
	FLASH_LOG2_UNIT,
	FLASH_LOG2_UPB,
	FLASH_FS_BLOCKS,
};

/*
 * The NOR model. Erase sets a block to 0xff; program clears bits, and a
 * program over unerased bytes is an error here, where the chip would
 * silently AND, because it means Dhara's sequential-programming contract
 * was broken.
 */

int
dhara_nand_is_bad(const struct dhara_nand *n, dhara_block_t b)
{
	(void)n; (void)b;
	return 0;
}

void
dhara_nand_mark_bad(const struct dhara_nand *n, dhara_block_t b)
{
	(void)n; (void)b;
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
	memset(region + b * FLASH_ERASE_BYTES, 0xff, FLASH_ERASE_BYTES);
	return 0;
}

int
dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
    const uint8_t *data, dhara_error_t *err)
{
	uint8_t *q = region + p * FLASH_UNIT_BYTES;
	size_t i;

	(void)n;
	if (p * FLASH_UNIT_BYTES >= FLASH_FS_BYTES) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	for (i = 0; i < FLASH_UNIT_BYTES; i++) {
		if (q[i] != 0xff) {
			fprintf(stderr, "flashimg: page %lu programmed twice\n",
			    (unsigned long)p);
			dhara_set_error(err, DHARA_E_BAD_BLOCK);
			return -1;
		}
	}
	memcpy(q, data, FLASH_UNIT_BYTES);
	return 0;
}

int
dhara_nand_is_free(const struct dhara_nand *n, dhara_page_t p)
{
	const uint8_t *q = region + p * FLASH_UNIT_BYTES;
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
	if (offset + length > FLASH_UNIT_BYTES ||
	    p * FLASH_UNIT_BYTES + offset + length > FLASH_FS_BYTES) {
		dhara_set_error(err, DHARA_E_ECC);
		return -1;
	}
	memcpy(data, region + p * FLASH_UNIT_BYTES + offset, length);
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

int
main(int argc, char **argv)
{
	struct dhara_map map;
	uint8_t page[FLASH_UNIT_BYTES];		/* Dhara's own page buffer. */
	dhara_error_t err = DHARA_E_NONE;
	unsigned long capacity, units, unit, written = 0;
	unsigned long chains = 0, frees = 0;
	uint8_t *image, *state;
	int capacity_only = 0;
	FILE *in, *out;
	size_t n;

	if (argc > 1 && strcmp(argv[1], "-c") == 0) {
		capacity_only = 1;
		argc--;
		argv++;
	}
	if (argc != (capacity_only ? 1 : 3)) {
		fprintf(stderr, "usage: flashimg [-c] disk.img flash.img\n");
		return 2;
	}

	region = malloc(FLASH_FS_BYTES);
	if (region == NULL) {
		perror("malloc");
		return 1;
	}
	memset(region, 0xff, FLASH_FS_BYTES);

	dhara_map_init(&map, &flnand, page, FLASH_GC_RATIO);
	(void)dhara_map_resume(&map, &err);
	capacity = dhara_map_capacity(&map);
	printf("flashimg: region %lu kbytes, capacity %lu sectors of %lu "
	    "bytes, %lu kbytes\n", (unsigned long)(FLASH_FS_BYTES / 1024),
	    capacity, (unsigned long)FLASH_UNIT_BYTES,
	    capacity * FLASH_UNIT_BYTES / 1024);
	if (capacity_only)
		return 0;

	/*
	 * The image is read whole: the free list is walked before any unit is
	 * written. One unit past the capacity is read so that an image too
	 * large is reported; a short last unit reads as zeros, as before.
	 */
	image = calloc(capacity + 1, FLASH_UNIT_BYTES);
	state = calloc(capacity + 1, 1);
	if (image == NULL || state == NULL) {
		perror("calloc");
		return 1;
	}
	in = fopen(argv[1], "rb");
	if (in == NULL) {
		perror(argv[1]);
		return 1;
	}
	n = fread(image, 1, (capacity + 1) * FLASH_UNIT_BYTES, in);
	if (ferror(in)) {
		perror(argv[1]);
		return 1;
	}
	fclose(in);
	units = (n + FLASH_UNIT_BYTES - 1) / FLASH_UNIT_BYTES;
	if (units > capacity) {
		fprintf(stderr, "%s: exceeds the %lu-sector capacity\n",
		    argv[1], capacity);
		return 1;
	}
	if (mark_free_blocks(image, units, state, &chains, &frees) < 0)
		return 1;

	for (unit = 0; unit < units; unit++) {
		if (state[unit] == UNIT_FREE)
			continue;
		err = DHARA_E_NONE;
		if (dhara_map_write(&map, (dhara_sector_t)unit,
		    image + unit * FLASH_UNIT_BYTES, &err) < 0) {
			fprintf(stderr, "flashimg: write of sector %lu failed, "
			    "dhara error %d\n", unit, (int)err);
			return 1;
		}
		written++;
	}

	err = DHARA_E_NONE;
	if (dhara_map_sync(&map, &err) < 0) {
		fprintf(stderr, "flashimg: sync failed, dhara error %d\n",
		    (int)err);
		return 1;
	}

	out = fopen(argv[2], "wb");
	if (out == NULL) {
		perror(argv[2]);
		return 1;
	}
	if (fwrite(region, 1, FLASH_FS_BYTES, out) != FLASH_FS_BYTES ||
	    fclose(out) != 0) {
		perror(argv[2]);
		return 1;
	}
	printf("flashimg: %lu of %lu image sectors written (%lu free-list "
	    "chain blocks kept, %lu free blocks left unwritten), %lu of %lu "
	    "capacity sectors live (%lu.%lu percent), %s, program at 0x%08lx\n",
	    written, units, chains, frees, written, capacity,
	    written * 100 / capacity, written * 1000 / capacity % 10, argv[2],
	    (unsigned long)(FLASH_XIP_BASE + FLASH_FS_OFFSET));
	return 0;
}
