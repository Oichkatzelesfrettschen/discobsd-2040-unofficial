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
 * Host test of the SD card protocol layer (sys/arch/rp2040/dev/sdspi.c).
 *
 * The card model is a byte-level SPI slave that replays a recorded
 * transcript: an ordered list of the commands a host must send, each with
 * the argument and SPI speed class it expects, and the card's answer (response
 * delay in bytes, R1, trailing R3/R7 bytes, a data block or a data error
 * token, a write's data response and busy time). A command that departs from
 * the transcript, a command sent with chip select high, a CMD0 or CMD8 whose
 * CRC byte is not the fixed value the specification gives (Bus Transfer
 * Protection), a start-up with fewer than 74 clocks, or a transcript left
 * unconsumed fails the scenario, so an extra, missing or mistimed command
 * is caught as well
 * as a wrong result. The model clock advances 20 microseconds per byte at
 * the initialization speed and 1 microsecond at the fast speed, so a
 * timeout is measured in the model's time and a missing timeout stops at a
 * byte budget instead of hanging.
 *
 * Section numbers name the SD Physical Layer Simplified Specification.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdspi.h"

static int failures;

static void
fail(const char *scenario, const char *what, long got, long want)
{
	fprintf(stderr, "FAIL %s: %s: %ld, want %ld\n", scenario, what, got,
	    want);
	failures++;
}

/* Transcript steps. */
enum { D_NONE, D_CSD, D_BLOCK, D_ERRTOK, D_NOTOKEN, D_WRITE };

struct step {
	int		cmd;		/* Expected command, -1 ends the list. */
	long		arg;		/* Expected argument, -1 for any. */
	int		fast;		/* Expected speed class. */
	int		ncr;		/* 0xff bytes before R1. */
	int		r1;		/* R1, -1 for no response. */
	int		ntail;		/* R3 or R7 bytes after R1. */
	unsigned char	tail[4];
	int		data;		/* D_ kind of the data phase. */
	int		dly;		/* 0xff bytes before the token. */
	int		resp;		/* D_WRITE: data response byte. */
	int		busy;		/* D_WRITE: bytes held low, -1 forever. */
	int		sticky;		/* Answer any number of repeats. */
};

#define	END	{ -1, 0, 0, 0, 0, 0, { 0 }, 0, 0, 0, 0, 0 }

static const struct step *script;
static int pos, nsteps;
static const char *scenario;
static const unsigned char *csd_payload;
static unsigned char block_payload[SD_SECTOR];
static unsigned char captured[SD_SECTOR];

/* Model state. */
static unsigned now;
static int cs, fast_mode, clocks_before_cmd, seen_cmd, cmd_with_cs_high;
static long bytes_total;
static unsigned char q[1024];
static int qh, qt;
static int busy_left;
static enum { S_IDLE, S_CMD, S_TOKEN, S_DATA } st;
static unsigned char frame[6];
static int fn, wr_have;
static const struct step *wr_step;
static int cmds_seen;
static unsigned char *churn;	/* A write source the model changes. */
static int churn_len;

static void
push(int b)
{
	q[qt++ & 1023] = (unsigned char)b;
}

static void
respond(const struct step *s)
{
	int i;

	for (i = 0; i < s->ncr; i++)
		push(0xff);
	if (s->r1 < 0)
		return;
	push(s->r1);
	for (i = 0; i < s->ntail; i++)
		push(s->tail[i]);
	if (s->r1 != 0 || s->data == D_NONE)
		return;
	if (s->data == D_WRITE) {
		st = S_TOKEN;
		wr_step = s;
		wr_have = 0;
		return;
	}
	for (i = 0; i < s->dly; i++)
		push(0xff);
	switch (s->data) {
	case D_CSD:
		push(0xfe);
		for (i = 0; i < 16; i++)
			push(csd_payload[i]);
		push(0xaa);
		push(0xaa);
		break;
	case D_BLOCK:
		push(0xfe);
		for (i = 0; i < SD_SECTOR; i++)
			push(block_payload[i]);
		push(0xaa);
		push(0xaa);
		break;
	case D_ERRTOK:
		push(0x01);		/* Data Error Token. */
		break;
	default:
		break;			/* D_NOTOKEN: the card stays silent. */
	}
}

static void
command_complete(void)
{
	int cmd = frame[0] & 0x3f;
	long arg = ((long)frame[1] << 24) | (frame[2] << 16) |
	    (frame[3] << 8) | frame[4];
	const struct step *s = pos < nsteps ? &script[pos] : NULL;
	int j;
	struct step bad = { 0, 0, 0, 0, 0x05, 0, { 0 }, 0, 0, 0, 0, 0 };

	cmds_seen++;
	if (!seen_cmd) {
		seen_cmd = 1;
		if (clocks_before_cmd < 74)
			fail(scenario, "clocks before the first command",
			    clocks_before_cmd, 74);
	}
	if ((frame[5] & 1) == 0)
		fail(scenario, "end bit of the command frame", frame[5] & 1, 1);
	if ((cmd == 0 && frame[5] != 0x95) || (cmd == 8 && frame[5] != 0x87)) {
		/* A card that checks the CRC flags the error (Bus Transfer Protection). */
		bad.r1 = 0x09;
		respond(&bad);
		return;
	}
	/* A run of sticky steps answers its commands in any order, any number of times. */
	for (j = pos; s != NULL && s->sticky && s->cmd != cmd &&
	    j + 1 < nsteps && script[j + 1].sticky; )
		s = &script[++j];
	if (s == NULL || s->cmd != cmd) {
		fprintf(stderr, "FAIL %s: command %d (arg %ld), transcript "
		    "wants %d\n", scenario, cmd, arg, s ? s->cmd : -1);
		failures++;
		respond(&bad);
		return;
	}
	if (s->arg >= 0 && s->arg != arg)
		fail(scenario, "command argument", arg, s->arg);
	if (s->fast != fast_mode)
		fail(scenario, "SPI speed class at a command", fast_mode,
		    s->fast);
	if (!s->sticky)
		pos++;
	respond(s);
}

static unsigned char
model_xfer(unsigned char in)
{
	unsigned char out = 0xff;

	if (++bytes_total > 5000000) {
		fprintf(stderr, "FAIL %s: byte budget exhausted, a wait has "
		    "no timeout\n", scenario);
		exit(2);
	}
	now += fast_mode ? 1 : 20;
	if (qh != qt)
		out = q[qh++ & 1023];
	else if (busy_left != 0) {
		out = 0x00;
		if (busy_left > 0)
			busy_left--;
	}
	if (!cs) {
		if (!seen_cmd && in == 0xff)
			clocks_before_cmd += 8;
		return out;
	}
	switch (st) {
	case S_IDLE:
		if ((in & 0xc0) == 0x40) {
			frame[0] = in;
			fn = 1;
			st = S_CMD;
		}
		break;
	case S_CMD:
		frame[fn++] = in;
		if (fn == 6) {
			st = S_IDLE;
			command_complete();
		}
		break;
	case S_TOKEN:
		if (in == 0xfe) {
			st = S_DATA;
			wr_have = 0;
		}
		break;
	default:
		if (wr_have < SD_SECTOR)
			captured[wr_have] = in;
		if (churn != NULL && wr_have < churn_len) {
			/*
			 * The source changes around the byte just sent, as
			 * the dump writer's own stack does under it: a sum
			 * taken before or after the transfer disagrees.
			 */
			churn[wr_have] ^= 0x5a;
			if (wr_have + 1 < churn_len)
				churn[wr_have + 1] ^= 0x33;
		}
		if (++wr_have == SD_SECTOR + 2) {
			/* command_complete() consumed the step already. */
			push(wr_step->resp);
			busy_left = wr_step->busy;
			st = S_IDLE;
		}
		break;
	}
	return out;
}

static void
model_select(int on)
{
	if (on && !cs && st != S_IDLE && st != S_CMD)
		cmd_with_cs_high++;
	cs = on;
	if (!on) {
		st = S_IDLE;
		qh = qt = 0;		/* A programming card stays busy. */
	}
}

static void
model_speed(int fast)
{
	fast_mode = fast;
}

static unsigned
model_micros(void)
{
	return now;
}

static const struct sdspi_ops ops = {
	model_xfer, model_select, model_speed, model_micros
};

static struct sdspi card = { &ops, SD_NONE, 0 };

static void
load(const char *name, const struct step *s)
{
	int n = 0;

	while (s[n].cmd >= 0)
		n++;
	script = s;
	nsteps = n;
	pos = 0;
	scenario = name;
	now = 1000;
	cs = 0;
	fast_mode = 0;
	clocks_before_cmd = 0;
	seen_cmd = 0;
	bytes_total = 0;
	qh = qt = 0;
	busy_left = 0;
	st = S_IDLE;
	cmds_seen = 0;
	card.type = SD_NONE;
	card.nsectors = 0;
}

/* Every non-sticky step must have been consumed; chip select must be high. */
static void
finish(void)
{
	int i, left = 0;

	for (i = pos; i < nsteps; i++)
		if (!script[i].sticky)
			left++;
	if (left != 0)
		fail(scenario, "transcript steps never sent", left, 0);
	if (cs != 0)
		fail(scenario, "chip select left low", cs, 0);
	if (cmd_with_cs_high != 0)
		fail(scenario, "data phases cut by deselect", cmd_with_cs_high,
		    0);
}

/*
 * CSD registers, decoded by hand from the CSD Version 1.0 and 2.0 field tables.
 *
 * csd_v1_2gb: byte 0 = 0x00 is version 1.0; READ_BL_LEN is the low nibble
 * of byte 5, 0xa = 10 (1024-byte read blocks); C_SIZE is the two low bits of
 * byte 6 (3), all of byte 7 (0xae) and the top two bits of byte 8 (3):
 * 3 << 10 | 0xae << 2 | 3 = 3771; C_SIZE_MULT is the two low bits of byte 9
 * (3) then the top bit of byte 10 (1): 7. Capacity is (3771 + 1) * 2^(7 + 2)
 * blocks of 1024 bytes = 1,931,264 KiB, so 3,862,528 sectors.
 */
static const unsigned char csd_v1_2gb[16] = {
	0x00, 0x7f, 0x00, 0x32, 0x5b, 0x5a, 0x83, 0xae,
	0xfe, 0xfb, 0xcf, 0xff, 0x92, 0x80, 0x40, 0xdf
};
#define	SECTORS_V1_2GB	3862528u

/*
 * csd_v1_512k: READ_BL_LEN 9 (byte 5 = 0x09), C_SIZE 255 (byte 7 = 0x3f,
 * top bits of byte 8 = 3), C_SIZE_MULT 0: (255 + 1) * 4 blocks of 512
 * bytes = 1024 sectors.
 */
static const unsigned char csd_v1_512k[16] = {
	0x00, 0x2f, 0x00, 0x32, 0x5b, 0x09, 0x00, 0x3f,
	0xc0, 0x00, 0x00, 0xff, 0x92, 0x80, 0x40, 0xdf
};

/*
 * csd_v2_128g: byte 0 = 0x40 is version 2.0; C_SIZE is the low six bits
 * of byte 7 (0x03), byte 8 (0xb7) and byte 9 (0x2f) = 0x3b72f = 243,503.
 * Capacity is (243,503 + 1) * 512 KiB = 124,674,048 KiB, so
 * 243,504 * 1,024 = 249,348,096 sectors of 512 bytes.
 */
static const unsigned char csd_v2_128g[16] = {
	0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0x03,
	0xb7, 0x2f, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01
};
#define	SECTORS_V2_128G	249348096u

#define	ACMD(n, r1v, argv)						\
	{ 55, 0, 0, 1, 0x01, 0, { 0 }, 0, 0, 0, 0, 0 },			\
	{ 41, argv, 0, n, r1v, 0, { 0 }, 0, 0, 0, 0, 0 }
#define	CMD0_OK		{ 0, 0, 0, 1, 0x01, 0, { 0 }, 0, 0, 0, 0, 0 }
#define	CMD8_V2		{ 8, 0x1aa, 0, 2, 0x01, 4, { 0, 0, 0x01, 0xaa }, \
			  0, 0, 0, 0, 0 }
#define	CMD8_V1		{ 8, 0x1aa, 0, 1, 0x05, 0, { 0 }, 0, 0, 0, 0, 0 }
#define	CMD58(b0)	{ 58, 0, 0, 1, 0x00, 4, { b0, 0xff, 0x80, 0 },	\
			  0, 0, 0, 0, 0 }
#define	CMD16_OK	{ 16, 512, 0, 1, 0x00, 0, { 0 }, 0, 0, 0, 0, 0 }
#define	CMD9_CSD	{ 9, 0, 1, 1, 0x00, 0, { 0 }, D_CSD, 3, 0, 0, 0 }

static void
test_init_sdsc_v1(void)
{
	/* A 2 GB card: CMD8 illegal, three idle ACMD41 answers, byte addressing. */
	static const struct step t[] = {
		CMD0_OK, CMD8_V1,
		ACMD(1, 0x01, 0), ACMD(1, 0x01, 0), ACMD(1, 0x01, 0),
		ACMD(1, 0x00, 0),
		CMD58(0x80), CMD16_OK, CMD9_CSD,
		{ 17, 3 * 512, 1, 1, 0x00, 0, { 0 }, D_BLOCK, 5, 0, 0, 0 },
		{ 24, 4 * 512, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0xe5, 4, 0 },
		END
	};
	unsigned char buf[SD_SECTOR];
	int i;

	load("sdsc v1", t);
	csd_payload = csd_v1_2gb;
	for (i = 0; i < SD_SECTOR; i++)
		block_payload[i] = (unsigned char)(i * 7 + 3);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init result", -1, SDE_OK);
	if (card.type != SD_V1)
		fail(scenario, "card type", card.type, SD_V1);
	if (card.nsectors != SECTORS_V1_2GB)
		fail(scenario, "capacity", card.nsectors, SECTORS_V1_2GB);
	if (sdspi_read(&card, 3, buf) != SDE_OK ||
	    memcmp(buf, block_payload, SD_SECTOR) != 0)
		fail(scenario, "read data", 0, 1);
	for (i = 0; i < SD_SECTOR; i++)
		buf[i] = (unsigned char)(i ^ 0x5a);
	if (sdspi_write(&card, 4, buf) != SDE_OK)
		fail(scenario, "write result", -1, SDE_OK);
	if (memcmp(buf, captured, SD_SECTOR) != 0)
		fail(scenario, "written data", 0, 1);
	finish();
}

static void
test_init_sdhc(void)
{
	static const struct step t[] = {
		CMD0_OK, CMD8_V2,
		ACMD(1, 0x01, 0x40000000), ACMD(1, 0x00, 0x40000000),
		CMD58(0xc0), CMD9_CSD,
		{ 17, 5, 1, 1, 0x00, 0, { 0 }, D_BLOCK, 0, 0, 0, 0 },
		{ 24, 6, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0xe5, 0, 0 },
		END
	};
	unsigned char buf[SD_SECTOR];
	int i;

	load("sdhc", t);
	csd_payload = csd_v2_128g;
	for (i = 0; i < SD_SECTOR; i++)
		block_payload[i] = (unsigned char)(255 - i);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init result", -1, SDE_OK);
	if (card.type != SD_HC)
		fail(scenario, "card type", card.type, SD_HC);
	if (card.nsectors != SECTORS_V2_128G)
		fail(scenario, "capacity", card.nsectors, SECTORS_V2_128G);
	if (sdspi_read(&card, 5, buf) != SDE_OK ||
	    memcmp(buf, block_payload, SD_SECTOR) != 0)
		fail(scenario, "read data", 0, 1);
	memset(buf, 0xa5, sizeof buf);
	if (sdspi_write(&card, 6, buf) != SDE_OK)
		fail(scenario, "write result", -1, SDE_OK);
	finish();
}

static void
test_init_sdsc_v2(void)
{
	/* A version 2.0 card without CCS is byte addressed and takes CMD16. */
	static const struct step t[] = {
		CMD0_OK, CMD8_V2,
		ACMD(1, 0x00, 0x40000000),
		CMD58(0x80), CMD16_OK, CMD9_CSD,
		{ 17, 2 * 512, 1, 1, 0x00, 0, { 0 }, D_BLOCK, 0, 0, 0, 0 },
		END
	};
	unsigned char buf[SD_SECTOR];

	load("sdsc v2", t);
	csd_payload = csd_v1_512k;
	memset(block_payload, 0x11, sizeof block_payload);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init result", -1, SDE_OK);
	if (card.type != SD_V2)
		fail(scenario, "card type", card.type, SD_V2);
	if (card.nsectors != 1024)
		fail(scenario, "capacity", card.nsectors, 1024);
	if (sdspi_read(&card, 2, buf) != SDE_OK)
		fail(scenario, "read result", -1, SDE_OK);
	finish();
}

static void
test_failures(void)
{
	/* No card: every command goes unanswered. */
	static const struct step nocard[] = {
		{ 0, 0, 0, 0, -1, 0, { 0 }, 0, 0, 0, 0, 1 }, END
	};
	/* The card never leaves idle. */
	static const struct step stuck[] = {
		CMD0_OK, CMD8_V2,
		{ 55, 0, 0, 1, 0x01, 0, { 0 }, 0, 0, 0, 0, 1 },
		{ 41, 0x40000000, 0, 1, 0x01, 0, { 0 }, 0, 0, 0, 0, 1 },
		END
	};
	/* An MMC card answers CMD55 as an illegal command. */
	static const struct step mmc[] = {
		CMD0_OK, CMD8_V1,
		{ 55, 0, 0, 1, 0x05, 0, { 0 }, 0, 0, 0, 0, 0 }, END
	};
	/* The echoed check pattern is wrong. */
	static const struct step badpattern[] = {
		CMD0_OK,
		{ 8, 0x1aa, 0, 1, 0x01, 4, { 0, 0, 0x01, 0x55 }, 0, 0, 0, 0, 0 },
		END
	};
	/* The card accepts no voltage in the host's range. */
	static const struct step badvolt[] = {
		CMD0_OK,
		{ 8, 0x1aa, 0, 1, 0x01, 4, { 0, 0, 0x00, 0xaa }, 0, 0, 0, 0, 0 },
		END
	};
	/* The OCR never reports power-up complete. */
	static const struct step noocr[] = {
		CMD0_OK, CMD8_V2, ACMD(1, 0x00, 0x40000000),
		{ 58, 0, 0, 1, 0x00, 4, { 0x00, 0, 0, 0 }, 0, 0, 0, 0, 0 },
		END
	};
	unsigned t0;

	load("no card", nocard);
	if (sdspi_init(&card) != SDE_NOCARD)
		fail(scenario, "no-card result", 0, SDE_NOCARD);
	if (card.type != SD_NONE)
		fail(scenario, "no-card type", card.type, SD_NONE);
	if (cmds_seen != 5)
		fail(scenario, "CMD0 attempts", cmds_seen, 5);
	finish();

	load("stuck in idle", stuck);
	t0 = now;
	if (sdspi_init(&card) != SDE_TIMEOUT)
		fail(scenario, "stuck result", 0, SDE_TIMEOUT);
	if (now - t0 < 1000000 || now - t0 > 1100000)
		fail(scenario, "initialization wait in microseconds",
		    now - t0, 1000000);
	finish();

	load("mmc", mmc);
	if (sdspi_init(&card) != SDE_UNSUPPORTED)
		fail(scenario, "illegal-command result", 0, SDE_UNSUPPORTED);
	finish();

	load("bad pattern", badpattern);
	if (sdspi_init(&card) != SDE_UNSUPPORTED)
		fail(scenario, "bad-pattern result", 0, SDE_UNSUPPORTED);
	finish();

	load("bad voltage", badvolt);
	if (sdspi_init(&card) != SDE_UNSUPPORTED)
		fail(scenario, "bad-voltage result", 0, SDE_UNSUPPORTED);
	finish();

	load("ocr not ready", noocr);
	if (sdspi_init(&card) != SDE_IO)
		fail(scenario, "ocr result", 0, SDE_IO);
	finish();
}

/* Transfer errors, each on an initialized SDHC card. */
static void
init_hc(const struct step *transfer)
{
	static struct step t[16];
	static const struct step head[] = {
		CMD0_OK, CMD8_V2, ACMD(1, 0x00, 0x40000000), CMD58(0xc0),
		CMD9_CSD
	};
	int n = (int)(sizeof head / sizeof head[0]);

	memcpy(t, head, sizeof head);
	t[n] = transfer[0];
	t[n + 1] = (struct step)END;
	load(scenario, t);
	csd_payload = csd_v2_128g;
}

static void
test_transfer_errors(void)
{
	unsigned char buf[SD_SECTOR];
	unsigned t0;
	struct step s;

	scenario = "read error token";
	s = (struct step){ 17, 7, 1, 1, 0x00, 0, { 0 }, D_ERRTOK, 2, 0, 0, 0 };
	init_hc(&s);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init", -1, SDE_OK);
	if (sdspi_read(&card, 7, buf) != SDE_IO)
		fail(scenario, "result", 0, SDE_IO);
	finish();

	scenario = "read error response";
	s = (struct step){ 17, 7, 1, 1, 0x20, 0, { 0 }, D_BLOCK, 0, 0, 0, 0 };
	init_hc(&s);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init", -1, SDE_OK);
	if (sdspi_read(&card, 7, buf) != SDE_IO)
		fail(scenario, "result", 0, SDE_IO);
	finish();

	scenario = "read token never arrives";
	s = (struct step){ 17, 7, 1, 1, 0x00, 0, { 0 }, D_NOTOKEN, 0, 0, 0, 0 };
	init_hc(&s);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init", -1, SDE_OK);
	t0 = now;
	if (sdspi_read(&card, 7, buf) != SDE_TIMEOUT)
		fail(scenario, "result", 0, SDE_TIMEOUT);
	if (now - t0 < 100000 || now - t0 > 110000)
		fail(scenario, "read wait in microseconds", now - t0, 100000);
	finish();

	scenario = "write data rejected";
	s = (struct step){ 24, 7, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0x0d, 0, 0 };
	init_hc(&s);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init", -1, SDE_OK);
	if (sdspi_write(&card, 7, buf) != SDE_IO)
		fail(scenario, "result", 0, SDE_IO);
	finish();

	scenario = "write CRC error response";
	s = (struct step){ 24, 7, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0x0b, 0, 0 };
	init_hc(&s);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init", -1, SDE_OK);
	if (sdspi_write(&card, 7, buf) != SDE_IO)
		fail(scenario, "result", 0, SDE_IO);
	finish();

	scenario = "write busy forever";
	s = (struct step){ 24, 7, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0xe5, -1, 0 };
	init_hc(&s);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init", -1, SDE_OK);
	t0 = now;
	if (sdspi_write(&card, 7, buf) != SDE_TIMEOUT)
		fail(scenario, "result", 0, SDE_TIMEOUT);
	if (now - t0 < 500000 || now - t0 > 520000)
		fail(scenario, "busy wait in microseconds", now - t0, 500000);
	/* The card is still busy: the next command waits and gives up. */
	card.type = SD_HC;
	t0 = now;
	if (sdspi_read(&card, 7, buf) != SDE_IO)
		fail(scenario, "command while busy", 0, SDE_IO);
	if (now - t0 < 500000 || now - t0 > 520000)
		fail(scenario, "ready wait in microseconds", now - t0, 500000);

	/* Out of range and uninitialized: no bus traffic at all. */
	scenario = "range";
	{
		static const struct step none[] = { END };

		load(scenario, none);
		if (sdspi_read(&card, 0, buf) != SDE_IO)
			fail(scenario, "uninitialized read", 0, SDE_IO);
		card.type = SD_HC;
		card.nsectors = 100;
		if (sdspi_read(&card, 100, buf) != SDE_IO)
			fail(scenario, "read past the end", 0, SDE_IO);
		if (sdspi_write(&card, 100, buf) != SDE_IO)
			fail(scenario, "write past the end", 0, SDE_IO);
		if (bytes_total != 0)
			fail(scenario, "bytes on the bus", bytes_total, 0);
	}
}

static void
test_csd(void)
{
	unsigned char c[16];

	scenario = "csd";
	if (sdspi_csd_sectors(csd_v1_2gb) != SECTORS_V1_2GB)
		fail(scenario, "version 1.0 2 GB", sdspi_csd_sectors(csd_v1_2gb),
		    SECTORS_V1_2GB);
	if (sdspi_csd_sectors(csd_v1_512k) != 1024)
		fail(scenario, "version 1.0 512 KiB",
		    sdspi_csd_sectors(csd_v1_512k), 1024);
	if (sdspi_csd_sectors(csd_v2_128g) != SECTORS_V2_128G)
		fail(scenario, "version 2.0", sdspi_csd_sectors(csd_v2_128g),
		    SECTORS_V2_128G);
	memcpy(c, csd_v1_2gb, sizeof c);
	c[0] = 0x80;
	if (sdspi_csd_sectors(c) != 0)
		fail(scenario, "reserved version", sdspi_csd_sectors(c), 0);
	memcpy(c, csd_v1_2gb, sizeof c);
	c[5] = 0x08;
	if (sdspi_csd_sectors(c) != 0)
		fail(scenario, "read block shorter than a sector",
		    sdspi_csd_sectors(c), 0);
	memcpy(c, csd_v2_128g, sizeof c);
	c[7] |= 0xc0;		/* Bits 71:70 are reserved in version 2.0. */
	if (sdspi_csd_sectors(c) != SECTORS_V2_128G)
		fail(scenario, "reserved bits ignored", sdspi_csd_sectors(c),
		    SECTORS_V2_128G);
}

/*
 * The crash dump's transfers: a read that keeps a window of the sector, a
 * write of a short span padded with zeros, and the CRC folded over exactly
 * the bytes that left even while the source changes under the transfer.
 */
static void
test_spans(void)
{
	static const struct step t[] = {
		CMD0_OK, CMD8_V2,
		ACMD(1, 0x01, 0x40000000), ACMD(1, 0x00, 0x40000000),
		CMD58(0xc0), CMD9_CSD,
		{ 17, 5, 1, 1, 0x00, 0, { 0 }, D_BLOCK, 0, 0, 0, 0 },
		{ 17, 4, 1, 1, 0x00, 0, { 0 }, D_BLOCK, 0, 0, 0, 0 },
		{ 24, 6, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0xe5, 0, 0 },
		{ 24, 7, 1, 1, 0x00, 0, { 0 }, D_WRITE, 0, 0xe5, 0, 0 },
		END
	};
	unsigned char buf[SD_SECTOR], src[100];
	unsigned crc, want, start;
	int i;

	load("spans", t);
	csd_payload = csd_v2_128g;
	for (i = 0; i < SD_SECTOR; i++)
		block_payload[i] = (unsigned char)(i * 7 + 3);
	if (sdspi_init(&card) != SDE_OK)
		fail(scenario, "init result", -1, SDE_OK);
	if (sdspi_read_span(&card, 0, buf, 500, 13) != SDE_IO)
		fail(scenario, "window past the sector", 0, SDE_IO);
	memset(buf, 0xee, sizeof buf);
	if (sdspi_read_span(&card, 5, buf, 446, 66) != SDE_OK ||
	    memcmp(buf, block_payload + 446, 66) != 0)
		fail(scenario, "window bytes", 0, 1);
	for (i = 66; i < SD_SECTOR; i++)
		if (buf[i] != 0xee) {
			fail(scenario, "byte stored past the window", i, -1);
			break;
		}
	/* A window inside the sector, which ends before the sector does. */
	memset(buf, 0xee, sizeof buf);
	if (sdspi_read_span(&card, 4, buf, 10, 20) != SDE_OK ||
	    memcmp(buf, block_payload + 10, 20) != 0)
		fail(scenario, "inner window bytes", 0, 1);
	for (i = 20; i < SD_SECTOR; i++)
		if (buf[i] != 0xee) {
			fail(scenario, "byte stored past the inner window", i,
			    -1);
			break;
		}

	for (i = 0; i < (int)sizeof src; i++)
		src[i] = (unsigned char)(i + 1);
	churn = src;
	churn_len = sizeof src;
	start = 0x12345678;
	crc = start;
	if (sdspi_write_span(&card, 6, src, sizeof src, &crc) != SDE_OK)
		fail(scenario, "span write result", -1, SDE_OK);
	churn = NULL;
	want = crc32_update(start, captured, sizeof src);
	if (crc != want)
		fail(scenario, "sum of the bytes sent", (long)crc, (long)want);
	for (i = sizeof src; i < SD_SECTOR; i++)
		if (captured[i] != 0) {
			fail(scenario, "padding byte", i, 0);
			break;
		}

	memset(captured, 0xff, sizeof captured);
	crc = start;
	if (sdspi_write_span(&card, 7, src, 0, &crc) != SDE_OK)
		fail(scenario, "empty span write result", -1, SDE_OK);
	if (crc != start)
		fail(scenario, "sum of an empty span", (long)crc, (long)start);
	for (i = 0; i < SD_SECTOR; i++)
		if (captured[i] != 0) {
			fail(scenario, "zero sector byte", i, 0);
			break;
		}
	finish();
}

int
main(void)
{
	test_csd();
	test_init_sdsc_v1();
	test_init_sdhc();
	test_init_sdsc_v2();
	test_failures();
	test_transfer_errors();
	test_spans();
	if (failures != 0) {
		fprintf(stderr, "sdspi_test: %d failures\n", failures);
		return 1;
	}
	printf("sdspi_test: ok\n");
	return 0;
}
