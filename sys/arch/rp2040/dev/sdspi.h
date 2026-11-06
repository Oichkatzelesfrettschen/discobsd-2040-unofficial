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
 * SD memory card in SPI mode: command framing, initialization, capacity
 * and single-block transfers, over a byte-wide transport.
 *
 * Section numbers name the SD Physical Layer Simplified Specification
 * (Part 1), Chapter 7 for SPI mode and Chapter 5 for the CSD register.
 * The transport supplies one full-duplex byte, chip select, the SPI clock
 * class and a microsecond clock, so sdspi.c includes no kernel header and
 * tests/picocalc links it against a card model that replays recorded
 * command transcripts.
 */

#ifndef _SDSPI_H
#define _SDSPI_H

#define	SD_SECTOR		512

/* Card types sdspi_init() identifies. */
#define	SD_NONE			0
#define	SD_V1			1	/* Standard capacity, version 1.x. */
#define	SD_V2			2	/* Standard capacity, version 2.0 or later. */
#define	SD_HC			3	/* SDHC or SDXC, block addressed. */

/* Results. */
#define	SDE_OK			0
#define	SDE_NOCARD		1	/* No answer to CMD0. */
#define	SDE_UNSUPPORTED		2	/* Not an SD memory card this driver drives. */
#define	SDE_TIMEOUT		3	/* A busy or ready wait ran out. */
#define	SDE_IO			4	/* Error response, token or data response. */

struct sdspi_ops {
	unsigned char	(*xfer)(unsigned char out);
	void		(*select)(int on);	/* Chip select, active low. */
	void		(*speed)(int fast);	/* 0: at most 400 kHz. */
	unsigned	(*micros)(void);	/* Free-running, wraps. */
};

struct sdspi {
	const struct sdspi_ops	*ops;
	unsigned char		type;		/* SD_NONE until initialized. */
	unsigned		nsectors;	/* Capacity in SD_SECTOR units. */
};

int	sdspi_init(struct sdspi *sd);
int	sdspi_read(struct sdspi *sd, unsigned sector, unsigned char *buf);
int	sdspi_write(struct sdspi *sd, unsigned sector, const unsigned char *buf);

/* Sectors a 16-byte CSD register describes; 0 when the version is unknown. */
unsigned sdspi_csd_sectors(const unsigned char csd[16]);

#endif /* !_SDSPI_H */
