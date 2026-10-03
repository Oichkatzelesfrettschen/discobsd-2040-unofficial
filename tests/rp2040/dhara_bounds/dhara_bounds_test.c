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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "journal.h"
#include <rp2040/dev/flash.h>

#define PAGE_SIZE_BYTES 1024U
#define BLOCK_COUNT 16U

static unsigned int nand_read_count;
static dhara_page_t nand_read_page;
static size_t nand_read_offset;
static size_t nand_read_length;
static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		failures++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
		    #condition); \
	} \
} while (0)

int
dhara_nand_read(const struct dhara_nand *nand, dhara_page_t page,
	size_t offset, size_t length, uint8_t *data, dhara_error_t *err)
{
	(void)nand;
	(void)err;
	nand_read_count++;
	nand_read_page = page;
	nand_read_offset = offset;
	nand_read_length = length;
	if (offset > PAGE_SIZE_BYTES || length > PAGE_SIZE_BYTES - offset)
		return -1;
	memset(data, 0x5a, length);
	return 0;
}

static void
test_buffered_user_page(void)
{
	const struct dhara_nand nand = { 10, 3, BLOCK_COUNT };
	struct dhara_journal journal = { 0 };
	uint8_t page_buffer[PAGE_SIZE_BYTES];
	uint8_t metadata[DHARA_META_SIZE];
	dhara_error_t err = DHARA_E_NONE;
	size_t offset = DHARA_HEADER_SIZE + DHARA_COOKIE_SIZE +
	    6U * DHARA_META_SIZE;

	dhara_journal_init(&journal, &nand, page_buffer);
	memset(page_buffer, 0xa5, sizeof(page_buffer));
	journal.head = 6;
	nand_read_count = 0;

	CHECK(dhara_journal_read_meta(&journal, 6, metadata, &err) == 0);
	CHECK(err == DHARA_E_NONE);
	CHECK(nand_read_count == 0);
	CHECK(metadata[0] == 0xa5);
	CHECK(metadata[DHARA_META_SIZE - 1] == 0xa5);
	CHECK(offset + DHARA_META_SIZE <= sizeof(page_buffer));
}

static void
test_checkpoint_page_rejected(void)
{
	const struct dhara_nand nand = { 10, 3, BLOCK_COUNT };
	struct dhara_journal journal = { 0 };
	uint8_t page_buffer[PAGE_SIZE_BYTES];
	uint8_t metadata[DHARA_META_SIZE];
	dhara_error_t err = DHARA_E_NONE;
	size_t index;

	memset(page_buffer, 0xa5, sizeof(page_buffer));
	memset(metadata, 0x3c, sizeof(metadata));
	dhara_journal_init(&journal, &nand, page_buffer);
	journal.head = 7;
	nand_read_count = 0;

	CHECK(dhara_journal_read_meta(&journal, 7, metadata, &err) == -1);
	CHECK(err == DHARA_E_CORRUPT_MAP);
	CHECK(nand_read_count == 0);
	for (index = 0; index < sizeof(metadata); index++)
		CHECK(metadata[index] == 0x3c);
}

static void
test_invalid_page_rejected(void)
{
	const struct dhara_nand nand = { 10, 3, BLOCK_COUNT };
	struct dhara_journal journal = { 0 };
	uint8_t page_buffer[PAGE_SIZE_BYTES];
	uint8_t metadata[DHARA_META_SIZE];
	dhara_error_t err = DHARA_E_NONE;
	dhara_page_t page_limit = (dhara_page_t)BLOCK_COUNT << nand.log2_ppb;

	dhara_journal_init(&journal, &nand, page_buffer);
	journal.head = 8;
	nand_read_count = 0;

	CHECK(dhara_journal_read_meta(&journal, page_limit, metadata, &err) == -1);
	CHECK(err == DHARA_E_CORRUPT_MAP);
	CHECK(nand_read_count == 0);
	journal.log2_ppc = (uint8_t)(sizeof(dhara_page_t) * 8U);
	err = DHARA_E_NONE;
	CHECK(dhara_journal_read_meta(&journal, 6, metadata, &err) == -1);
	CHECK(err == DHARA_E_CORRUPT_MAP);
	CHECK(nand_read_count == 0);
}

static void
test_metadata_slice_bounds(void)
{
	const struct dhara_nand nand = { 7, 3, BLOCK_COUNT };
	struct dhara_journal journal = { 0 };
	uint8_t page_buffer[PAGE_SIZE_BYTES];
	uint8_t metadata[DHARA_META_SIZE];
	dhara_error_t err = DHARA_E_NONE;

	dhara_journal_init(&journal, &nand, page_buffer);
	journal.head = 8;
	nand_read_count = 0;

	CHECK(dhara_journal_read_meta(&journal, 6, metadata, &err) == -1);
	CHECK(err == DHARA_E_CORRUPT_MAP);
	CHECK(nand_read_count == 0);
}

static void
test_nand_user_page_read(void)
{
	const struct dhara_nand nand = { 10, 3, BLOCK_COUNT };
	struct dhara_journal journal = { 0 };
	uint8_t page_buffer[PAGE_SIZE_BYTES];
	uint8_t metadata[DHARA_META_SIZE];
	dhara_error_t err = DHARA_E_NONE;
	size_t expected_offset = DHARA_HEADER_SIZE + DHARA_COOKIE_SIZE +
	    6U * DHARA_META_SIZE;

	dhara_journal_init(&journal, &nand, page_buffer);
	journal.head = 8;
	nand_read_count = 0;

	CHECK(dhara_journal_read_meta(&journal, 6, metadata, &err) == 0);
	CHECK(err == DHARA_E_NONE);
	CHECK(nand_read_count == 1);
	CHECK(nand_read_page == 7);
	CHECK(nand_read_offset == expected_offset);
	CHECK(nand_read_length == DHARA_META_SIZE);
	CHECK(metadata[0] == 0x5a);
	CHECK(metadata[DHARA_META_SIZE - 1] == 0x5a);
}

static void
test_flash_nand_range_bounds(void)
{
	const uint32_t page_count = FLASH_FS_BYTES / FLASH_UNIT_BYTES;
	const uint32_t last_page = page_count - 1U;
	const size_t page_size = FLASH_UNIT_BYTES;

	CHECK(flash_nand_range_valid(0, 0, page_size));
	CHECK(flash_nand_range_valid(last_page, 0, page_size));
	CHECK(!flash_nand_range_valid(page_count, 0, page_size));
	CHECK(!flash_nand_range_valid(UINT32_MAX, 0, page_size));
	CHECK(flash_nand_range_valid(last_page, page_size - 1U, 1));
	CHECK(flash_nand_range_valid(last_page, page_size, 0));
	CHECK(!flash_nand_range_valid(last_page, page_size, 1));
	CHECK(!flash_nand_range_valid(last_page, SIZE_MAX, 1));
	CHECK(!flash_nand_range_valid(last_page, 1, SIZE_MAX));
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "corrupt") == 0) {
		test_checkpoint_page_rejected();
		return failures == 0 ? 0 : 1;
	}
	if (argc != 1) {
		fprintf(stderr, "usage: dhara_bounds_test [corrupt]\n");
		return 2;
	}
	test_buffered_user_page();
	test_checkpoint_page_rejected();
	test_invalid_page_rejected();
	test_metadata_slice_bounds();
	test_nand_user_page_read();
	test_flash_nand_range_bounds();
	if (failures != 0)
		return 1;
	puts("dhara_bounds_test: PASS");
	return 0;
}
