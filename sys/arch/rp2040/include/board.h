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

#ifndef _RP2040_BOARD_H_
#define _RP2040_BOARD_H_

#define BOARD_DESC_MAGIC	0x44524f42U
#define BOARD_DESC_VERSION	1

#define BOARD_ID_PICO		1
#define BOARD_ID_PICOCALC	2

/*
 * Every field has an explicit width and the reserved byte is part of the
 * checksum, so image tools and the kernel agree without host ABI padding.
 */
struct board_desc {
	unsigned int	magic;
	unsigned short	version;
	unsigned short	length;
	unsigned char	board;
	unsigned char	console_major;
	unsigned char	console_minor;
	unsigned char	reserved;
	unsigned int	crc32;
};

#define BOARD_DESC_LENGTH	((unsigned short)sizeof(struct board_desc))

int board_id(void);
void board_console(int *, int *);
void board_report(void);

/* The running flash image's identity; board.c image_identify(). */
extern unsigned int image_length;
extern unsigned int image_crc;
extern unsigned int image_crc_stored;

#endif /* !_RP2040_BOARD_H_ */
