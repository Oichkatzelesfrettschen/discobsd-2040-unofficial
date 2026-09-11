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
 * geometry and garbage-collection ratio from dev/flash.h, writes every
 * 256-byte sector of the disk image through dhara_map_write, and emits the
 * resulting region. Programmed at FLASH_FS_OFFSET, the kernel resumes it
 * as a map it wrote itself.
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
	uint8_t data[FLASH_UNIT_BYTES];		/* One sector of the image. */
	dhara_error_t err = DHARA_E_NONE;
	unsigned long capacity, written = 0;
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

	in = fopen(argv[1], "rb");
	if (in == NULL) {
		perror(argv[1]);
		return 1;
	}
	for (;;) {
		memset(data, 0, sizeof(data));
		n = fread(data, 1, sizeof(data), in);
		if (n == 0)
			break;
		if (written >= capacity) {
			fprintf(stderr, "%s: exceeds the %lu-sector capacity\n",
			    argv[1], capacity);
			return 1;
		}
		err = DHARA_E_NONE;
		if (dhara_map_write(&map, (dhara_sector_t)written, data,
		    &err) < 0) {
			fprintf(stderr, "flashimg: write of sector %lu failed, "
			    "dhara error %d\n", written, (int)err);
			return 1;
		}
		written++;
	}
	if (ferror(in)) {
		perror(argv[1]);
		return 1;
	}
	fclose(in);

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
	printf("flashimg: %lu sectors written, %lu kbytes of %s, program at "
	    "0x%08lx\n", written, written * FLASH_UNIT_BYTES / 1024, argv[2],
	    (unsigned long)(FLASH_XIP_BASE + FLASH_FS_OFFSET));
	return 0;
}
