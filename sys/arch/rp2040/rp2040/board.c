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

#include <machine/board.h>

#ifndef BOARD_DESC_HOST
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#endif

#define CRC32_INIT	0xffffffffU
#define CRC32_FIN(value)	((value) ^ 0xffffffffU)

/*
 * The retained PICO_UART and PICOCALC configurations need their compiled
 * console while all four kernels remain build targets. PICO and the
 * PICOCALC_PSRAM union kernel carry the format's bare-Pico USB default.
 */
#if defined(PICO_UART)
#define BOARD_DEFAULT_ID	BOARD_ID_PICO
#define BOARD_DEFAULT_MAJOR	6
#define BOARD_DEFAULT_CRC	0xafef1f85U
#elif defined(PICOCALC)
#define BOARD_DEFAULT_ID	BOARD_ID_PICOCALC
#define BOARD_DEFAULT_MAJOR	10
#define BOARD_DEFAULT_CRC	0xb440490fU
#else
#define BOARD_DEFAULT_ID	BOARD_ID_PICO
#define BOARD_DEFAULT_MAJOR	7
#define BOARD_DEFAULT_CRC	0xae2d75b2U
#endif

/*
 * The kernel link places the descriptor in .board, where mkboarduf2.py
 * finds it by its magic. The host build of the reader links it as plain
 * data: a Mach-O host rejects a section name without a segment.
 */
#ifdef BOARD_DESC_HOST
#define BOARD_DESC_PLACEMENT
#else
#define BOARD_DESC_PLACEMENT	__attribute__((section(".board"), used))
#endif

const struct board_desc rp2040_board_desc BOARD_DESC_PLACEMENT = {
	BOARD_DESC_MAGIC,
	BOARD_DESC_VERSION,
	BOARD_DESC_LENGTH,
	BOARD_DEFAULT_ID,
	BOARD_DEFAULT_MAJOR,
	0,
	0,
	BOARD_DEFAULT_CRC,
};

#ifdef BOARD_DESC_HOST
const volatile struct board_desc *board_desc_test = &rp2040_board_desc;
#define BOARD_DESCRIPTOR board_desc_test
#else
#define BOARD_DESCRIPTOR (&rp2040_board_desc)
#endif

static const unsigned crc32_nibble[16] = {
	0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
	0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
	0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
	0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
};

static unsigned int
board_crc32(const volatile unsigned char *bytes, unsigned int length)
{
	unsigned int crc = CRC32_INIT;

	while (length-- != 0) {
		crc ^= *bytes++;
		crc = (crc >> 4) ^ crc32_nibble[crc & 15];
		crc = (crc >> 4) ^ crc32_nibble[crc & 15];
	}
	return CRC32_FIN(crc);
}

static int descriptor_checked;
static int descriptor_valid;
static int selected_board = BOARD_ID_PICO;
static int selected_console_major = 7;
static int selected_console_minor;

#ifdef BOARD_DESC_HOST
void
board_desc_test_reset(void)
{
	descriptor_checked = 0;
	descriptor_valid = 0;
	selected_board = BOARD_ID_PICO;
	selected_console_major = 7;
	selected_console_minor = 0;
}
#endif

static void
board_validate(void)
{
	const volatile struct board_desc *descriptor = BOARD_DESCRIPTOR;
	unsigned int checksum_length;

	if (descriptor_checked)
		return;
	descriptor_checked = 1;
	if (descriptor->magic != BOARD_DESC_MAGIC ||
	    descriptor->version != BOARD_DESC_VERSION ||
	    descriptor->length < BOARD_DESC_LENGTH ||
	    descriptor->length > 256 ||
	    (descriptor->board != BOARD_ID_PICO &&
	     descriptor->board != BOARD_ID_PICOCALC) ||
	    (descriptor->console_major != 6 &&
	     descriptor->console_major != 7 &&
	     descriptor->console_major != 10) ||
	    (descriptor->board == BOARD_ID_PICO &&
	     descriptor->console_major == 10) ||
	    descriptor->console_minor != 0)
		return;
	checksum_length = (unsigned int)((const volatile unsigned char *)&descriptor->crc32 -
	    (const volatile unsigned char *)descriptor);
	if (board_crc32((const volatile unsigned char *)descriptor, checksum_length) !=
	    descriptor->crc32)
		return;
	selected_board = descriptor->board;
	selected_console_major = descriptor->console_major;
	selected_console_minor = descriptor->console_minor;
	descriptor_valid = 1;
}

int
board_id(void)
{
	board_validate();
	return selected_board;
}

void
board_console(int *major_number, int *minor_number)
{
	board_validate();
	*major_number = selected_console_major;
	*minor_number = selected_console_minor;
}

void
board_report(void)
{
	board_validate();
#ifndef BOARD_DESC_HOST
	if (!descriptor_valid)
		printf("board: invalid descriptor, using bare Pico with USB console\n");
#endif
}
