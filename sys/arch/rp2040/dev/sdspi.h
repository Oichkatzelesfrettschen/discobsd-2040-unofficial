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

/*
 * SD memory card in SPI mode: command framing, initialization, capacity
 * and single-block transfers, over a byte-wide transport.
 *
 * Section numbers name the SD Physical Layer Simplified Specification
 * (Part 1), Chapter 7 for SPI mode and Chapter 5 for the CSD register.
 * The transport supplies one full-duplex byte, chip select, the SPI clock
 * class and a microsecond clock, so sdspi.c compiles without kernel headers and
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
	unsigned char		crc;		/* The card checks CRCs (CMD59). */
};

int	sdspi_init(struct sdspi *sd);
int	sdspi_read(struct sdspi *sd, unsigned sector, unsigned char *buf);
int	sdspi_write(struct sdspi *sd, unsigned sector, const unsigned char *buf);

/*
 * The crash-dump writer's transfers (sddump() in sd.c), which run with no
 * sector buffer to spare. sdspi_read_span() reads one sector and keeps
 * bytes off to off + len - 1 in buf. sdspi_write_span() writes len bytes
 * of buf and then zeros as one sector and, when crc is not null, folds
 * each byte into *crc through crc32_update() (kern/subr_crc32.c) as it
 * leaves, so the sum matches the card even when buf changes meanwhile.
 */
int	sdspi_read_span(struct sdspi *sd, unsigned sector, unsigned char *buf,
	    unsigned off, unsigned len);
int	sdspi_write_span(struct sdspi *sd, unsigned sector,
	    const unsigned char *buf, unsigned len, unsigned *crc);
unsigned crc32_update(unsigned crc, const void *buf, unsigned len);

/* Sectors a 16-byte CSD register describes; 0 when the version is unknown. */
unsigned sdspi_csd_sectors(const unsigned char csd[16]);

#endif /* !_SDSPI_H */
