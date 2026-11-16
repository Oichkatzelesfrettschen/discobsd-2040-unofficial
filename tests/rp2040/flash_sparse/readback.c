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
 * Read a flash region image back through the vendored Dhara map, as the
 * kernel's fl_setup() resumes it: dhara_map_resume() over the region, then
 * dhara_map_read() of sectors 0 to UNITS - 1 into OUTPUT. An unmapped
 * sector reads as 0xff bytes. The map's live sector count
 * (dhara_map_size()) is printed as "live N".
 *
 * Usage: readback region.bin UNITS output.img
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rp2040/dev/flash.h"
#include "map.h"

static uint8_t *region;

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

/* The readback never writes: a program or an erase is a failure. */
int
dhara_nand_erase(const struct dhara_nand *n, dhara_block_t b,
    dhara_error_t *err)
{
	(void)n;
	(void)b;
	dhara_set_error(err, DHARA_E_BAD_BLOCK);
	return -1;
}

int
dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
    const uint8_t *data, dhara_error_t *err)
{
	(void)n;
	(void)p;
	(void)data;
	dhara_set_error(err, DHARA_E_BAD_BLOCK);
	return -1;
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
	if (offset + length > FLASH_UNIT_BYTES ||
	    (size_t)p * FLASH_UNIT_BYTES + offset + length > FLASH_FS_BYTES) {
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
	(void)n;
	(void)src;
	(void)dst;
	dhara_set_error(err, DHARA_E_BAD_BLOCK);
	return -1;
}

int
main(int argc, char **argv)
{
	static const struct dhara_nand nand = {
		FLASH_LOG2_UNIT, FLASH_LOG2_UPB, FLASH_FS_BLOCKS
	};
	struct dhara_map map;
	uint8_t page[FLASH_UNIT_BYTES], data[FLASH_UNIT_BYTES];
	dhara_error_t err = DHARA_E_NONE;
	unsigned long units, s;
	FILE *f;

	if (argc != 4) {
		fprintf(stderr, "usage: readback region.bin UNITS output.img\n");
		return 2;
	}
	units = strtoul(argv[2], NULL, 10);
	region = malloc(FLASH_FS_BYTES);
	if (region == NULL)
		return 2;
	f = fopen(argv[1], "rb");
	if (f == NULL || fread(region, 1, FLASH_FS_BYTES, f) != FLASH_FS_BYTES) {
		fprintf(stderr, "readback: cannot read %s\n", argv[1]);
		return 2;
	}
	fclose(f);
	dhara_map_init(&map, &nand, page, FLASH_GC_RATIO);
	if (dhara_map_resume(&map, &err) < 0) {
		fprintf(stderr, "readback: no map in %s\n", argv[1]);
		return 1;
	}
	f = fopen(argv[3], "wb");
	if (f == NULL)
		return 2;
	for (s = 0; s < units; s++) {
		err = DHARA_E_NONE;
		if (dhara_map_read(&map, (dhara_sector_t)s, data, &err) < 0) {
			fprintf(stderr, "readback: sector %lu, dhara error %d\n",
			    s, (int)err);
			return 1;
		}
		if (fwrite(data, 1, sizeof(data), f) != sizeof(data))
			return 2;
	}
	if (fclose(f) != 0)
		return 2;
	printf("live %lu\n", (unsigned long)dhara_map_size(&map));
	return 0;
}
