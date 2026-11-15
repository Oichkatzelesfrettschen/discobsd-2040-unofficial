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
 * The flash side of trim_order_test.c: the vendored Dhara map over a NOR
 * model with the geometry of rp2040/dev/flash.h. This file includes host
 * headers and the kernel-side test includes the kernel's, so the two meet
 * only through the plain int, unsigned and byte-pointer functions below
 * (tests/kernel/hostkern.h states why).
 *
 * A reset (td_reset) discards the map's RAM state and resumes it from the
 * NOR bytes, so the device returns to its last checkpoint. td_save and
 * td_restore copy the NOR bytes, which lets the test rebuild any prefix of
 * a recorded request sequence. td_load writes a filesystem image the way
 * tools/flashimg does: every 1 KiB unit except the free blocks that do not
 * chain the free list.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"
#include <rp2040/dev/flash.h>

#include "trim_dhara.h"

static uint8_t *region, *saved;
static uint8_t page[FLASH_UNIT_BYTES];
static struct dhara_map map;
static const struct dhara_nand nand = {
	FLASH_LOG2_UNIT, FLASH_LOG2_UPB, FLASH_FS_BLOCKS
};
static unsigned faults, pages;

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
	return 0;
}

int
dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
    const uint8_t *data, dhara_error_t *err)
{
	uint8_t *q = region + (size_t)p * FLASH_UNIT_BYTES;
	size_t i;

	(void)n;
	(void)err;
	for (i = 0; i < FLASH_UNIT_BYTES; i++)
		if (q[i] != 0xff)
			faults++;
	for (i = 0; i < FLASH_UNIT_BYTES; i++)
		q[i] &= data[i];
	pages++;
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

void
td_reset(void)
{
	dhara_error_t err = DHARA_E_NONE;

	memset(&map, 0, sizeof(map));
	dhara_map_init(&map, &nand, page, FLASH_GC_RATIO);
	(void)dhara_map_resume(&map, &err);
}

void
td_erase(void)
{
	if (region == NULL) {
		region = malloc(FLASH_FS_BYTES);
		saved = malloc(FLASH_FS_BYTES);
		if (region == NULL || saved == NULL)
			abort();
	}
	memset(region, 0xff, FLASH_FS_BYTES);
	faults = pages = 0;
	td_reset();
}

int
td_read(unsigned sector, unsigned char *data)
{
	dhara_error_t err = DHARA_E_NONE;

	return dhara_map_read(&map, sector, data, &err);
}

int
td_write(unsigned sector, const unsigned char *data)
{
	dhara_error_t err = DHARA_E_NONE;

	return dhara_map_write(&map, sector, data, &err);
}

int
td_trim(unsigned sector)
{
	dhara_error_t err = DHARA_E_NONE;

	return dhara_map_trim(&map, sector, &err);
}

int
td_sync(void)
{
	dhara_error_t err = DHARA_E_NONE;

	return dhara_map_sync(&map, &err);
}

unsigned
td_live(void)
{
	return (unsigned)dhara_map_size(&map);
}

/* Dhara pages programmed since the last td_erase(). */
unsigned
td_pages(void)
{
	return pages;
}

unsigned
td_faults(void)
{
	return faults;
}

void
td_save(void)
{
	memcpy(saved, region, FLASH_FS_BYTES);
}

void
td_restore(void)
{
	memcpy(region, saved, FLASH_FS_BYTES);
	td_reset();
}

static uint32_t
word(const uint8_t *p, size_t offset)
{
	return (uint32_t)p[offset] | (uint32_t)p[offset + 1] << 8 |
	    (uint32_t)p[offset + 2] << 16 | (uint32_t)p[offset + 3] << 24;
}

/*
 * The free list of the unpartitioned 2.11BSD filesystem at unit 0: the
 * superblock's nfree and free[200] at bytes 16 and 20, then each chain
 * block's own nfree and 200 entries.
 */
int
td_load(const char *path, unsigned *units, unsigned *skipped)
{
	uint8_t *image, *skip;
	const uint8_t *list;
	uint32_t nfree, b, i;
	size_t n, first, u;
	FILE *f;

	td_erase();
	image = calloc(FLASH_FS_BYTES, 1);
	f = fopen(path, "rb");
	if (image == NULL || f == NULL)
		return -1;
	n = fread(image, 1, FLASH_FS_BYTES, f);
	fclose(f);
	*units = (unsigned)((n + FLASH_UNIT_BYTES - 1) / FLASH_UNIT_BYTES);
	skip = calloc(*units, 1);
	if (skip == NULL)
		return -1;
	list = image;
	first = 20;
	nfree = word(image, 16);
	while (nfree != 0 && nfree <= 200) {
		for (i = 1; i < nfree; i++) {
			b = word(list, first + 4 * i);
			if (b < *units)
				skip[b] = 1;
		}
		b = word(list, first);
		if (b == 0 || b >= *units)
			break;
		list = image + (size_t)b * FLASH_UNIT_BYTES;
		nfree = word(list, 0);
		first = 4;
	}
	*skipped = 0;
	for (u = 0; u < *units; u++) {
		if (skip[u]) {
			(*skipped)++;
			continue;
		}
		if (td_write((unsigned)u, image + u * FLASH_UNIT_BYTES) < 0)
			return -1;
	}
	free(skip);
	free(image);
	return td_sync();
}
