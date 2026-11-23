/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
