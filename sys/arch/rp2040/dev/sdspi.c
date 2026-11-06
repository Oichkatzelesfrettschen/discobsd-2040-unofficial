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

/* SD card in SPI mode; see sdspi.h. */

#include "sdspi.h"

#define	CMD0			0	/* GO_IDLE_STATE */
#define	CMD8			8	/* SEND_IF_COND */
#define	CMD9			9	/* SEND_CSD */
#define	CMD16			16	/* SET_BLOCKLEN */
#define	CMD17			17	/* READ_SINGLE_BLOCK */
#define	CMD24			24	/* WRITE_BLOCK */
#define	CMD55			55	/* APP_CMD */
#define	CMD58			58	/* READ_OCR */
#define	ACMD41			41	/* SD_SEND_OP_COND */

#define	R1_IDLE			0x01
#define	R1_ILLEGAL		0x04
#define	R1_NONE			0xff	/* No response arrived. */

#define	TOKEN_START		0xfe	/* Single-block data start (7.3.3.1). */
#define	DATA_RESP_MASK		0x1f
#define	DATA_ACCEPTED		0x05	/* xxx00101 (7.3.3.3). */

#define	NCR_MAX			8	/* Response delay in bytes (7.3.2.1). */
#define	POWERUP_CLOCKS		10	/* 80 clocks; the card needs 74 (ch. 6). */
#define	CMD0_TRIES		5

/*
 * Time limits. The card may take one second to leave idle after ACMD41
 * (7.2.1), 100 ms to start a read (7.2.3), and 250 ms to program a block
 * (7.2.4); one busy limit covers the longer of the ready and busy waits.
 */
#define	INIT_US			1000000
#define	READ_US			100000
#define	BUSY_US			500000

/* Hold DO high for one byte after deselect so the card releases the line. */
static void
sd_done(const struct sdspi_ops *o)
{
	o->select(0);
	(void)o->xfer(0xff);
}

/* Clock until the card reports not busy (DO high). */
static int
sd_ready(const struct sdspi_ops *o)
{
	unsigned t0 = o->micros();

	while (o->xfer(0xff) != 0xff)
		if (o->micros() - t0 >= BUSY_US)
			return 0;
	return 1;
}

/*
 * Send one command and return its R1 byte, R1_NONE when none arrives.
 * Chip select stays low for the caller's data phase. Only CMD0 and CMD8
 * carry a checked CRC in SPI mode, where checking is off after reset
 * (7.2.2); every other command takes a dummy CRC with the end bit set.
 */
static unsigned char
sd_cmd(const struct sdspi_ops *o, unsigned cmd, unsigned arg)
{
	unsigned char r = R1_NONE;
	int i;

	o->select(1);
	if (!sd_ready(o))
		return R1_NONE;
	(void)o->xfer(0x40 | cmd);
	(void)o->xfer(arg >> 24);
	(void)o->xfer(arg >> 16);
	(void)o->xfer(arg >> 8);
	(void)o->xfer(arg);
	(void)o->xfer(cmd == CMD0 ? 0x95 : cmd == CMD8 ? 0x87 : 0x01);
	for (i = 0; i < NCR_MAX && (r & 0x80); i++)
		r = o->xfer(0xff);
	return (r & 0x80) ? R1_NONE : r;
}

/* Application command: CMD55, then acmd. Returns the last R1. */
static unsigned char
sd_acmd(const struct sdspi_ops *o, unsigned acmd, unsigned arg)
{
	unsigned char r = sd_cmd(o, CMD55, 0);

	if (r > R1_IDLE) {
		sd_done(o);
		return r;
	}
	sd_done(o);
	r = sd_cmd(o, acmd, arg);
	sd_done(o);
	return r;
}

static void
sd_tail(const struct sdspi_ops *o, unsigned char *p, int n)
{
	while (n-- > 0)
		*p++ = o->xfer(0xff);
}

/* Receive a start token and n data bytes plus the CRC, which is ignored. */
static int
sd_rx(const struct sdspi_ops *o, unsigned char *buf, unsigned n)
{
	unsigned t0 = o->micros();
	unsigned char b;

	while ((b = o->xfer(0xff)) == 0xff)
		if (o->micros() - t0 >= READ_US)
			return SDE_TIMEOUT;
	if (b != TOKEN_START)
		return SDE_IO;		/* A data error token (7.3.3.2). */
	while (n-- > 0)
		*buf++ = o->xfer(0xff);
	(void)o->xfer(0xff);
	(void)o->xfer(0xff);
	return SDE_OK;
}

unsigned
sdspi_csd_sectors(const unsigned char csd[16])
{
	unsigned c_size, mult, len;

	switch (csd[0] >> 6) {
	case 1:		/* CSD version 2.0, 5.3.3: (C_SIZE + 1) * 512 KiB. */
		c_size = ((unsigned)(csd[7] & 0x3f) << 16) | (csd[8] << 8) |
		    csd[9];
		return (c_size + 1) << 10;
	case 0:		/* CSD version 1.0, 5.3.2. */
		len = csd[5] & 0x0f;			/* READ_BL_LEN */
		c_size = ((unsigned)(csd[6] & 3) << 10) | (csd[7] << 2) |
		    (csd[8] >> 6);
		mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
		if (len < 9)
			return 0;
		return (c_size + 1) << (mult + 2 + len - 9);
	default:
		return 0;
	}
}

int
sdspi_init(struct sdspi *sd)
{
	const struct sdspi_ops *o = sd->ops;
	unsigned char r = R1_NONE, echo[4], ocr[4] = { 0, 0, 0, 0 }, csd[16];
	unsigned t0, hcs;
	int i, v2 = 0;

	sd->type = SD_NONE;
	sd->nsectors = 0;
	o->speed(0);
	o->select(0);
	for (i = 0; i < POWERUP_CLOCKS; i++)
		(void)o->xfer(0xff);

	/* 7.2.1: CMD0 with chip select low selects SPI mode. */
	for (i = 0; i < CMD0_TRIES && r != R1_IDLE; i++) {
		r = sd_cmd(o, CMD0, 0);
		sd_done(o);
	}
	if (r != R1_IDLE)
		return r == R1_NONE ? SDE_NOCARD : SDE_UNSUPPORTED;

	/* 4.3.13: a version 2.0 card echoes the voltage range and pattern. */
	r = sd_cmd(o, CMD8, 0x1aa);
	if (r == R1_IDLE) {
		sd_tail(o, echo, 4);
		if ((echo[2] & 0x0f) != 1 || echo[3] != 0xaa) {
			sd_done(o);
			return SDE_UNSUPPORTED;
		}
		v2 = 1;
	} else if (r == R1_NONE || !(r & R1_ILLEGAL)) {
		sd_done(o);
		return SDE_IO;
	}
	sd_done(o);

	/* 4.2.3: ACMD41 until the card leaves idle; HCS only for version 2.0. */
	hcs = v2 ? 0x40000000 : 0;
	t0 = o->micros();
	while ((r = sd_acmd(o, ACMD41, hcs)) != 0) {
		if (r != R1_NONE && r != R1_IDLE)
			return (r & R1_ILLEGAL) ? SDE_UNSUPPORTED : SDE_IO;
		if (o->micros() - t0 >= INIT_US)
			return SDE_TIMEOUT;
	}

	/* 7.2.1: CCS in the OCR says block addressing. */
	r = sd_cmd(o, CMD58, 0);
	if (r == 0)
		sd_tail(o, ocr, 4);
	sd_done(o);
	if (r != 0 || !(ocr[0] & 0x80))
		return SDE_IO;
	sd->type = v2 ? ((ocr[0] & 0x40) ? SD_HC : SD_V2) : SD_V1;

	if (sd->type != SD_HC) {
		r = sd_cmd(o, CMD16, SD_SECTOR);
		sd_done(o);
		if (r != 0)
			return SDE_IO;
	}
	o->speed(1);

	r = sd_cmd(o, CMD9, 0);
	i = r == 0 ? sd_rx(o, csd, 16) : SDE_IO;
	sd_done(o);
	if (i == SDE_OK && (sd->nsectors = sdspi_csd_sectors(csd)) == 0)
		i = SDE_IO;
	if (i != SDE_OK)
		sd->type = SD_NONE;
	return i;
}

/* The command argument for a sector: byte or block addressing. */
static unsigned
sd_addr(const struct sdspi *sd, unsigned sector)
{
	return sd->type == SD_HC ? sector : sector * SD_SECTOR;
}

int
sdspi_read(struct sdspi *sd, unsigned sector, unsigned char *buf)
{
	const struct sdspi_ops *o = sd->ops;
	int err = SDE_IO;

	if (sd->type == SD_NONE || sector >= sd->nsectors)
		return SDE_IO;
	if (sd_cmd(o, CMD17, sd_addr(sd, sector)) == 0)
		err = sd_rx(o, buf, SD_SECTOR);
	sd_done(o);
	return err;
}

int
sdspi_write(struct sdspi *sd, unsigned sector, const unsigned char *buf)
{
	const struct sdspi_ops *o = sd->ops;
	int err = SDE_IO;
	unsigned t0;
	unsigned i;

	if (sd->type == SD_NONE || sector >= sd->nsectors)
		return SDE_IO;
	if (sd_cmd(o, CMD24, sd_addr(sd, sector)) != 0) {
		sd_done(o);
		return SDE_IO;
	}
	(void)o->xfer(0xff);
	(void)o->xfer(TOKEN_START);
	for (i = 0; i < SD_SECTOR; i++)
		(void)o->xfer(buf[i]);
	(void)o->xfer(0xff);
	(void)o->xfer(0xff);
	if ((o->xfer(0xff) & DATA_RESP_MASK) == DATA_ACCEPTED) {
		/* 7.2.4: the card holds DO low while it programs. */
		err = SDE_OK;
		t0 = o->micros();
		while (o->xfer(0xff) != 0xff)
			if (o->micros() - t0 >= BUSY_US) {
				err = SDE_TIMEOUT;
				break;
			}
	}
	sd_done(o);
	return err;
}
