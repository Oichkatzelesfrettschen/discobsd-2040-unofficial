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
 * SD card in SPI mode; see sdspi.h. Section names refer to the SD
 * Physical Layer Simplified Specification, Part 1, its SPI Mode chapter
 * unless named otherwise.
 */

#include "sdspi.h"

#define	CMD0			0	/* GO_IDLE_STATE */
#define	CMD8			8	/* SEND_IF_COND */
#define	CMD9			9	/* SEND_CSD */
#define	CMD13			13	/* SEND_STATUS */
#define	CMD16			16	/* SET_BLOCKLEN */
#define	CMD17			17	/* READ_SINGLE_BLOCK */
#define	CMD24			24	/* WRITE_BLOCK */
#define	CMD55			55	/* APP_CMD */
#define	CMD58			58	/* READ_OCR */
#define	CMD59			59	/* CRC_ON_OFF */
#define	ACMD41			41	/* SD_SEND_OP_COND */

#define	R1_IDLE			0x01
#define	R1_ILLEGAL		0x04
#define	R1_NONE			0xff	/* No response arrived. */

#define	TOKEN_START		0xfe	/* Start Block Token, single block. */
#define	DATA_RESP_MASK		0x1f
#define	DATA_ACCEPTED		0x05	/* xxx00101, Data Response Token. */

#define	NCR_MAX			8	/* NCR, the response delay in bytes. */
#define	POWERUP_CLOCKS		10	/* 80 clocks; Power Up asks for 74. */
#define	CMD0_TRIES		5

/*
 * Time limits ("Read, Write and Erase Timeout Conditions" and "Card
 * Initialization and Identification Process"): one second to leave idle
 * after ACMD41, 100 ms to start a read and 250 ms to program a block; one
 * busy limit covers the longer of the ready and busy waits.
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
 * CRC7 of a command frame's first five bytes, generator x^7 + x^3 + 1
 * ("CRC7" under Bus Transfer Protection), returned in the frame's last
 * byte's layout: the sum in bits 7-1 and the end bit set. CMD0 with a zero
 * argument gives 95h and CMD8 with 1AAh gives 87h, the two values a card
 * checks before CMD59 has turned checking on.
 */
static unsigned char
sd_crc7(const unsigned char *p, int n)
{
	unsigned crc = 0;
	int i;

	while (n-- > 0) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc & 0x80) ? (crc << 1) ^ 0x12 : crc << 1;
		crc &= 0xff;
	}
	return (unsigned char)(crc | 1);
}

/*
 * Fold one byte into the CRC16 of a data block, generator
 * x^16 + x^12 + x^5 + 1 with a zero start value ("CRC16"). The three
 * shifts are the generator's taps applied to a whole byte at once.
 */
static unsigned
sd_crc16(unsigned crc, unsigned char b)
{
	unsigned x = ((crc >> 8) ^ b) & 0xff;

	x ^= x >> 4;
	return ((crc << 8) ^ (x << 12) ^ (x << 5) ^ x) & 0xffff;
}

/*
 * Send one command and return its R1 byte, R1_NONE when none arrives.
 * Chip select stays low for the caller's data phase. Every frame carries
 * its CRC7: a card checks it for CMD0 and CMD8 always and for every
 * command once CMD59 has turned checking on.
 */
static unsigned char
sd_cmd(const struct sdspi_ops *o, unsigned cmd, unsigned arg)
{
	unsigned char r = R1_NONE, frame[6];
	int i;

	o->select(1);
	if (!sd_ready(o))
		return R1_NONE;
	frame[0] = 0x40 | cmd;
	frame[1] = arg >> 24;
	frame[2] = arg >> 16;
	frame[3] = arg >> 8;
	frame[4] = arg;
	frame[5] = sd_crc7(frame, 5);
	for (i = 0; i < 6; i++)
		(void)o->xfer(frame[i]);
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

/*
 * Receive a start token and n data bytes plus their CRC16, keeping bytes
 * off to off + len - 1 of the n in buf. With checking on, a block whose
 * sum disagrees with the two bytes the card sent after it is an error: the
 * caller's buffer already holds the bytes, and the result says not to use
 * them.
 */
static int
sd_rx(const struct sdspi *sd, unsigned char *buf, unsigned n,
    unsigned off, unsigned len)
{
	const struct sdspi_ops *o = sd->ops;
	unsigned t0 = o->micros();
	unsigned char b;
	unsigned i, crc = 0, sent;

	while ((b = o->xfer(0xff)) == 0xff)
		if (o->micros() - t0 >= READ_US)
			return SDE_TIMEOUT;
	if (b != TOKEN_START)
		return SDE_IO;		/* A Data Error Token. */
	for (i = 0; i < n; i++) {
		b = o->xfer(0xff);
		crc = sd_crc16(crc, b);
		if (i - off < len)
			*buf++ = b;
	}
	sent = (unsigned)o->xfer(0xff) << 8;
	sent |= o->xfer(0xff);
	if (sd->crc && sent != crc)
		return SDE_IO;
	return SDE_OK;
}

unsigned
sdspi_csd_sectors(const unsigned char csd[16])
{
	unsigned c_size, mult, len;

	switch (csd[0] >> 6) {
	case 1:		/* CSD Register (CSD Version 2.0): (C_SIZE + 1) * 512 KiB. */
		c_size = ((unsigned)(csd[7] & 0x3f) << 16) | (csd[8] << 8) |
		    csd[9];
		return (c_size + 1) << 10;
	case 0:		/* CSD Register (CSD Version 1.0). */
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
	sd->crc = 0;
	o->speed(0);
	o->select(0);
	for (i = 0; i < POWERUP_CLOCKS; i++)
		(void)o->xfer(0xff);

	/* Mode Selection: CMD0 with chip select low selects SPI mode. */
	for (i = 0; i < CMD0_TRIES && r != R1_IDLE; i++) {
		r = sd_cmd(o, CMD0, 0);
		sd_done(o);
	}
	if (r != R1_IDLE)
		return r == R1_NONE ? SDE_NOCARD : SDE_UNSUPPORTED;

	/* CMD8 (SEND_IF_COND): a version 2.0 card echoes range and pattern. */
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

	/*
	 * CRC_ON_OFF with option bit 1, before ACMD41 as Bus Transfer
	 * Protection asks. A card that refuses the command runs unchecked,
	 * as SPI mode does after reset.
	 */
	r = sd_cmd(o, CMD59, 1);
	sd_done(o);
	sd->crc = r == R1_IDLE;

	/* ACMD41 until the card leaves idle; HCS only for version 2.0. */
	hcs = v2 ? 0x40000000 : 0;
	t0 = o->micros();
	while ((r = sd_acmd(o, ACMD41, hcs)) != 0) {
		if (r != R1_NONE && r != R1_IDLE)
			return (r & R1_ILLEGAL) ? SDE_UNSUPPORTED : SDE_IO;
		if (o->micros() - t0 >= INIT_US)
			return SDE_TIMEOUT;
	}

	/* CCS in the OCR says block addressing. */
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
	i = r == 0 ? sd_rx(sd, csd, 16, 0, 16) : SDE_IO;
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
	return sdspi_read_span(sd, sector, buf, 0, SD_SECTOR);
}

int
sdspi_read_span(struct sdspi *sd, unsigned sector, unsigned char *buf,
    unsigned off, unsigned len)
{
	const struct sdspi_ops *o = sd->ops;
	int err = SDE_IO;

	if (sd->type == SD_NONE || sector >= sd->nsectors ||
	    off > SD_SECTOR || len > SD_SECTOR - off)
		return SDE_IO;
	if (sd_cmd(o, CMD17, sd_addr(sd, sector)) == 0)
		err = sd_rx(sd, buf, SD_SECTOR, off, len);
	sd_done(o);
	return err;
}

int
sdspi_write(struct sdspi *sd, unsigned sector, const unsigned char *buf)
{
	return sdspi_write_span(sd, sector, buf, SD_SECTOR, (unsigned *)0);
}

int
sdspi_write_span(struct sdspi *sd, unsigned sector, const unsigned char *buf,
    unsigned len, unsigned *crc)
{
	const struct sdspi_ops *o = sd->ops;
	int err = SDE_IO;
	unsigned t0;
	unsigned i, crc16 = 0;
	unsigned char b, r;

	if (sd->type == SD_NONE || sector >= sd->nsectors || len > SD_SECTOR)
		return SDE_IO;
	if (sd_cmd(o, CMD24, sd_addr(sd, sector)) != 0) {
		sd_done(o);
		return SDE_IO;
	}
	(void)o->xfer(0xff);
	(void)o->xfer(TOKEN_START);
	for (i = 0; i < SD_SECTOR; i++) {
		/* One load per byte: the sum covers exactly the byte sent. */
		b = i < len ? buf[i] : 0;
		(void)o->xfer(b);
		crc16 = sd_crc16(crc16, b);
		if (crc != (unsigned *)0 && i < len)
			*crc = crc32_update(*crc, &b, 1);
	}
	(void)o->xfer(crc16 >> 8);
	(void)o->xfer(crc16);
	if ((o->xfer(0xff) & DATA_RESP_MASK) == DATA_ACCEPTED) {
		/* Data Write: the card holds DO low while it programs. */
		err = SDE_OK;
		t0 = o->micros();
		while (o->xfer(0xff) != 0xff)
			if (o->micros() - t0 >= BUSY_US) {
				err = SDE_TIMEOUT;
				break;
			}
	}
	sd_done(o);
	if (err == SDE_OK) {
		/*
		 * SEND_STATUS: the data response accepted the block before
		 * the card programmed it, so a failed program shows only in
		 * the R2 the card returns afterwards ("Data Write").
		 */
		r = sd_cmd(o, CMD13, 0);
		b = o->xfer(0xff);
		sd_done(o);
		if (r != 0 || b != 0)
			err = SDE_IO;
	}
	return err;
}
