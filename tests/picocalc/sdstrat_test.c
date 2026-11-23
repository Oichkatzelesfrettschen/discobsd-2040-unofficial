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
 * Host test of the SD disk driver's request path (sdopen, sdstrategy,
 * sdsize in sys/arch/rp2040/dev/sd.c), compiled unchanged against the
 * shim headers in shim/. The card is a 64-sector array behind fake
 * sdspi_init/sdspi_read/sdspi_write that count every sector they touch, so
 * the test sees which sectors a request reached as well as what the buffer
 * received. The partition table in sector 0 holds two partitions: sector 8
 * for 33 sectors (an odd count, so its last block holds one sector) and
 * sector 41 for 20.
 *
 * Each request starts with b_resid set to a stale nonzero value, as a
 * recycled buffer leaves it. physio() subtracts b_resid from the count it
 * saved and advances the file offset by the difference even when B_ERROR is
 * set, so every outcome checks the residual as well as the data: a request
 * rejected before any I/O reports its whole count, a transfer that fails
 * reports the failed sector and every sector after it, and a request cut
 * short at the partition end adds its truncation on top.
 *
 * The card-detect pin and the microsecond timer are registers of the model,
 * so the test plays the keyboard callout: it sets the pin, advances the
 * timer and calls sd_detect_poll(). A fake card command can do the same
 * after it completes a sector, which is how an interrupt lands in the middle
 * of a request.
 */

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/config.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <machine/board.h>
#include <machine/dump.h>

#include "regmodel.h"

#include <rp2040/dev/psram.h>
#include <rp2040/dev/sd.h>
#include <rp2040/dev/sdspi.h>

#define	CARD_SECTORS	64
#define	STALE		0xdead
#define	GUARD		0xee

static unsigned char card[CARD_SECTORS][SD_SECTOR];
static unsigned char saved[CARD_SECTORS][SD_SECTOR];
static unsigned nread[CARD_SECTORS], nwrite[CARD_SECTORS];
static int fail_sector = -1;
static int swap_after = -1;
static unsigned bad_spl;
static struct buf ebuf;
static unsigned char ebuf_data[DEV_BSIZE];
static int failures;

static int init_fail;		/* sdspi_init() fails to find a card. */
static unsigned init_type = SD_HC;		/* The card type it reports. */
static unsigned init_sectors = CARD_SECTORS;	/* Its capacity; reads past CARD_SECTORS fail. */


int
sdspi_init(struct sdspi *sd)
{
	if (init_fail)
		return SDE_NOCARD;
	sd->type = (unsigned char)init_type;
	sd->nsectors = init_sectors;
	return SDE_OK;
}

static void card_swap(void);

/* A card command runs with interrupts enabled; see the file header of sd.c. */
static void
command_entered(void)
{
	if (shim_spl != 0)
		bad_spl++;
}

/* The callout fires after the command that served sector `swap_after`. */
static void
command_done(unsigned sector)
{
	if ((int)sector == swap_after) {
		swap_after = -1;
		card_swap();
	}
}

int
sdspi_read(struct sdspi *sd __attribute__((unused)), unsigned sector,
    unsigned char *buf)
{
	command_entered();
	if (sector >= CARD_SECTORS || (int)sector == fail_sector)
		return SDE_IO;
	nread[sector]++;
	memcpy(buf, card[sector], SD_SECTOR);
	command_done(sector);
	return SDE_OK;
}

int
sdspi_write(struct sdspi *sd __attribute__((unused)), unsigned sector,
    const unsigned char *buf)
{
	command_entered();
	if (sector >= CARD_SECTORS || (int)sector == fail_sector)
		return SDE_IO;
	nwrite[sector]++;
	memcpy(card[sector], buf, SD_SECTOR);
	command_done(sector);
	return SDE_OK;
}

/*
 * The crash dump's transfers. They count sectors as the whole-sector calls
 * do; the sum covers the bytes stored, the bytes the card received.
 */
int
sdspi_read_span(struct sdspi *sd __attribute__((unused)), unsigned sector,
    unsigned char *buf, unsigned off, unsigned len)
{
	command_entered();
	if (sector >= CARD_SECTORS || (int)sector == fail_sector ||
	    off > SD_SECTOR || len > SD_SECTOR - off)
		return SDE_IO;
	nread[sector]++;
	memcpy(buf, card[sector] + off, len);
	command_done(sector);
	return SDE_OK;
}

int
sdspi_write_span(struct sdspi *sd __attribute__((unused)), unsigned sector,
    const unsigned char *buf, unsigned len, unsigned *crc)
{
	if (sector >= CARD_SECTORS || (int)sector == fail_sector ||
	    len > SD_SECTOR)
		return SDE_IO;
	nwrite[sector]++;
	memcpy(card[sector], buf, len);
	memset(card[sector] + len, 0, SD_SECTOR - len);
	if (crc != NULL)
		*crc = crc32_update(*crc, card[sector], len);
	return SDE_OK;
}

dev_t dumpdev = NODEV;
dev_t swapdev;

/* psram_swap_all() of psram_xfer.c: the call that gives swap the whole part. */
static int psram_all_calls;

void
psram_swap_all(void)
{
	psram_all_calls++;
}

struct buf *
geteblk(void)
{
	memset(&ebuf, 0, sizeof(ebuf));
	ebuf.b_addr = (caddr_t)ebuf_data;
	return &ebuf;
}

void
brelse(struct buf *bp __attribute__((unused)))
{
}

void
biodone(struct buf *bp)
{
	bp->b_flags |= B_DONE;
}

/*
 * The raw-device path of physio() in sys/kern/vm_swp.c, reduced to what
 * reaches the driver: b_blkno is the file offset shifted right by
 * DEV_BSHIFT, one request per segment remainder, the offset advancing by
 * what moved. The low ten offset bits never reach the strategy routine.
 */
int
rawrw(dev_t dev, struct uio *uio, int flag __attribute__((unused)))
{
	struct buf b;
	struct iovec *iov;
	int error = 0, c;

	memset(&b, 0, sizeof(b));
	for (; uio->uio_iovcnt; uio->uio_iov++, uio->uio_iovcnt--) {
		iov = uio->uio_iov;
		while (iov->iov_len) {
			memset(&b, 0, sizeof(b));
			b.b_flags = B_BUSY | B_PHYS | B_INVAL |
			    (uio->uio_rw == UIO_READ ? B_READ : 0);
			b.b_dev = dev;
			b.b_addr = iov->iov_base;
			b.b_blkno = (unsigned)uio->uio_offset >> DEV_BSHIFT;
			b.b_bcount = (unsigned)iov->iov_len;
			c = b.b_bcount;
			sdstrategy(&b);
			c -= b.b_resid;
			iov->iov_base = (char *)iov->iov_base + c;
			iov->iov_len -= (size_t)c;
			uio->uio_resid -= (u_int)c;
			uio->uio_offset += c;
			if (b.b_resid || (b.b_flags & B_ERROR))
				break;
		}
		if (b.b_flags & B_ERROR)
			error = b.b_error;
		if (b.b_resid || error)
			break;
	}
	return error;
}

const char *panicstr;

void
timeout(void (*fn)(char *) __attribute__((unused)),
    char *arg __attribute__((unused)), int ticks __attribute__((unused)))
{
}

/*
 * The board the descriptor names. sdprobe() reads it through board_id()
 * and attaches on the PicoCalc alone.
 */
static int test_board = BOARD_ID_PICOCALC;

int
board_id(void)
{
	return test_board;
}

extern const struct driver sddriver;

/*
 * Root selection under the PicoCalc loader (sdrootattach()): whether the
 * loader's stamp is present, the root and pipe devices it sets, and panic()
 * as a return to the case that expects it, with the message kept.
 */
static int test_loader;
dev_t rootdev, pipedev;
static jmp_buf panic_return;
static const char *panic_message;

int
flash_loader_started(void)
{
	return test_loader;
}

void
panic(const char *msg)
{
	panic_message = msg;
	longjmp(panic_return, 1);
}

static void
check(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

/* The card-detect pin and the timer the keyboard callout reads. */
#define	SIO_GPIO_IN	0xd0000004UL
#define	TIMER_RAWL	0x40054028UL
#define	DET_PIN		22
#define	DEBOUNCE_US	50000U

static unsigned pin_level, now_us;

static void
set_pin(unsigned level)
{
	pin_level = level;
	model_reg(SIO_GPIO_IN) = (unsigned)level << DET_PIN;
}

static void
advance(unsigned us)
{
	now_us += us;
	model_reg(TIMER_RAWL) = now_us;
}

/* The pin flips and holds past the debounce interval. */
static void
card_swap(void)
{
	set_pin(!pin_level);
	sd_detect_poll();
	advance(DEBOUNCE_US);
	sd_detect_poll();
}

static void
fill_card(void)
{
	struct diskpart dp[2];
	unsigned s, i;

	for (s = 0; s < CARD_SECTORS; s++)
		for (i = 0; i < SD_SECTOR; i++)
			card[s][i] = (unsigned char)(s * 5 + i / 7 + 1);
	memset(dp, 0, sizeof(dp));
	dp[0].dp_type = PTYPE_BSDFFS;
	dp[0].dp_offset = 8;
	dp[0].dp_nsectors = 33;
	dp[1].dp_type = PTYPE_BSDFFS;
	dp[1].dp_offset = 41;
	dp[1].dp_nsectors = 20;
	memset(card[0] + 446, 0, 64);
	memcpy(card[0] + 446, dp, sizeof(dp));
	card[0][510] = 0x55;
	card[0][511] = 0xaa;
	memcpy(saved, card, sizeof(saved));
	memset(nread, 0, sizeof(nread));
	memset(nwrite, 0, sizeof(nwrite));
}

/* A request as physio() builds it for a raw device. */
struct req {
	int		part;
	int		rw;		/* B_READ or B_WRITE (0). */
	long		blkno;
	unsigned	bcount;
};

static unsigned char xbuf[4 * DEV_BSIZE + 64];

static void
issue(const struct req *r, struct buf *b, const unsigned char *payload)
{
	memset(xbuf, GUARD, sizeof(xbuf));
	if (payload != NULL)
		memcpy(xbuf, payload, sizeof(xbuf));
	memset(b, 0, sizeof(*b));
	b->b_flags = B_BUSY | B_PHYS | B_INVAL | r->rw;
	b->b_dev = (dev_t)r->part;
	b->b_addr = (caddr_t)xbuf;
	b->b_blkno = r->blkno;
	b->b_bcount = r->bcount;
	b->b_resid = STALE;
	sdstrategy(b);
}

/* Expect sectors [first, first + n) of the card moved and no others. */
static void
check_touched(const char *name, const unsigned *count, unsigned first,
    unsigned n)
{
	unsigned s;
	char what[96];

	for (s = 0; s < CARD_SECTORS; s++) {
		snprintf(what, sizeof(what), "%s: sector %u %s count", name, s,
		    count == nread ? "read" : "write");
		check(what, count[s], (s >= first && s < first + n) ? 1 : 0);
	}
}

/*
 * A read of bcount bytes where the first moved sectors reach the buffer.
 * moved is in sectors; the rest of the buffer keeps its guard bytes.
 */
static void
read_case(const char *name, struct req r, unsigned first_sector,
    unsigned moved, unsigned want_resid)
{
	struct buf b;
	unsigned i;
	char what[96];

	r.rw = B_READ;
	memset(nread, 0, sizeof(nread));
	issue(&r, &b, NULL);
	snprintf(what, sizeof(what), "%s: error flag", name);
	check(what, (b.b_flags & B_ERROR) != 0, 0);
	snprintf(what, sizeof(what), "%s: done", name);
	check(what, (b.b_flags & B_DONE) != 0, 1);
	snprintf(what, sizeof(what), "%s: b_resid", name);
	check(what, (long)b.b_resid, (long)want_resid);
	check_touched(name, nread, first_sector, moved);
	for (i = 0; i < moved * SD_SECTOR; i++)
		if (xbuf[i] != saved[first_sector + i / SD_SECTOR]
		    [i % SD_SECTOR]) {
			snprintf(what, sizeof(what), "%s: byte %u", name, i);
			check(what, xbuf[i],
			    saved[first_sector + i / SD_SECTOR][i % SD_SECTOR]);
			break;
		}
	for (i = moved * SD_SECTOR; i < sizeof(xbuf); i++)
		if (xbuf[i] != GUARD) {
			snprintf(what, sizeof(what),
			    "%s: buffer byte %u past the transfer", name, i);
			check(what, xbuf[i], GUARD);
			break;
		}
}

/* A write whose buffer holds a distinct pattern per byte. */
static void
write_case(const char *name, struct req r, unsigned first_sector,
    unsigned moved, unsigned want_resid)
{
	struct buf b;
	unsigned i;
	char what[96];
	unsigned char pat[sizeof(xbuf)];

	r.rw = 0;
	memcpy(card, saved, sizeof(card));
	memset(nwrite, 0, sizeof(nwrite));
	for (i = 0; i < sizeof(pat); i++)
		pat[i] = (unsigned char)(0x80 | (i * 3));
	issue(&r, &b, pat);
	snprintf(what, sizeof(what), "%s: error flag", name);
	check(what, (b.b_flags & B_ERROR) != 0, 0);
	snprintf(what, sizeof(what), "%s: b_resid", name);
	check(what, (long)b.b_resid, (long)want_resid);
	check_touched(name, nwrite, first_sector, moved);
	for (i = 0; i < CARD_SECTORS; i++) {
		int inside = i >= first_sector && i < first_sector + moved;
		const unsigned char *want = inside ?
		    pat + (i - first_sector) * SD_SECTOR : saved[i];

		if (memcmp(card[i], want, SD_SECTOR) != 0) {
			snprintf(what, sizeof(what), "%s: sector %u contents",
			    name, i);
			check(what, 1, 0);
		}
	}
	memcpy(card, saved, sizeof(card));
}

static unsigned touched(const unsigned *count);
static void error_case(const char *name, struct req r, int want_errno);

/*
 * Swap transfers. swap_with_buf_result() asks for an image segment's exact
 * byte count; on the swap device the last sector is padded on a write and
 * cut on a read, and every other device still refuses the count. Partition
 * 1 starts at card sector 8 and its block 1 at sector 10.
 */
static void
swapio_cases(void)
{
	struct req r;
	struct buf b;
	unsigned i;
	unsigned char pat[sizeof(xbuf)], want[SD_SECTOR];
	dev_t before = swapdev;

	swapdev = (dev_t)1;
	for (i = 0; i < sizeof(pat); i++)
		pat[i] = (unsigned char)(0x80 | (i * 3));

	memset(nread, 0, sizeof(nread));
	r = (struct req){ 1, B_READ, 1, 1300 };
	issue(&r, &b, NULL);
	check("swap read 1300: error flag", (b.b_flags & B_ERROR) != 0, 0);
	check("swap read 1300: done", (b.b_flags & B_DONE) != 0, 1);
	check("swap read 1300: b_resid", (long)b.b_resid, 0);
	check_touched("swap read 1300", nread, 10, 3);
	for (i = 0; i < 1300; i++)
		if (xbuf[i] != saved[10 + i / SD_SECTOR][i % SD_SECTOR])
			break;
	check("swap read 1300: bytes that match the card", (long)i, 1300);
	for (i = 1300; i < sizeof(xbuf) && xbuf[i] == GUARD; i++)
		continue;
	check("swap read 1300: memory past the count kept", (long)i,
	    (long)sizeof(xbuf));

	memset(nwrite, 0, sizeof(nwrite));
	r = (struct req){ 1, B_WRITE, 1, 1300 };
	issue(&r, &b, pat);
	check("swap write 1300: error flag", (b.b_flags & B_ERROR) != 0, 0);
	check("swap write 1300: b_resid", (long)b.b_resid, 0);
	check_touched("swap write 1300", nwrite, 10, 3);
	check("swap write 1300: whole sectors",
	    memcmp(card[10], pat, 2 * SD_SECTOR), 0);
	memset(want, 0, sizeof(want));
	memcpy(want, pat + 2 * SD_SECTOR, 1300 - 2 * SD_SECTOR);
	check("swap write 1300: last sector padded with zeros",
	    memcmp(card[12], want, SD_SECTOR), 0);
	check("swap write 1300: next sector kept",
	    memcmp(card[13], saved[13], SD_SECTOR), 0);
	memcpy(card, saved, sizeof(card));

	memset(nwrite, 0, sizeof(nwrite));
	r = (struct req){ 1, B_WRITE, 0, 100 };
	issue(&r, &b, pat);
	check("swap write 100: error flag", (b.b_flags & B_ERROR) != 0, 0);
	check_touched("swap write 100", nwrite, 8, 1);
	memset(want, 0, sizeof(want));
	memcpy(want, pat, 100);
	check("swap write 100: sector", memcmp(card[8], want, SD_SECTOR), 0);
	memcpy(card, saved, sizeof(card));

	/* A failed last sector leaves its bytes in the residual. */
	memset(nread, 0, sizeof(nread));
	fail_sector = 12;
	r = (struct req){ 1, B_READ, 1, 1300 };
	issue(&r, &b, NULL);
	fail_sector = -1;
	check("swap read, last sector fails: error flag",
	    (b.b_flags & B_ERROR) != 0, 1);
	check("swap read, last sector fails: errno", b.b_error, EIO);
	check("swap read, last sector fails: b_resid", (long)b.b_resid,
	    1300 - 2 * SD_SECTOR);
	check("swap read, last sector fails: memory past the sectors kept",
	    xbuf[2 * SD_SECTOR], GUARD);

	/* Block 16 holds the partition's one last sector: the rest is short. */
	memset(nread, 0, sizeof(nread));
	r = (struct req){ 1, B_READ, 16, 600 };
	issue(&r, &b, NULL);
	check("swap read past the last sector: error flag",
	    (b.b_flags & B_ERROR) != 0, 0);
	check("swap read past the last sector: b_resid", (long)b.b_resid, 88);
	check_touched("swap read past the last sector", nread, 8 + 32, 1);

	/* Another partition keeps the whole-sector rule. */
	r = (struct req){ 2, B_READ, 0, 1300 };
	error_case("1300 bytes off the swap device", r, EINVAL);
	swapdev = before;
}

/*
 * A request refused before any card command: the error flag and errno, the
 * whole count as the residual (physio() then leaves the offset where it
 * was), and no sector touched.
 */
static void
error_case(const char *name, struct req r, int want_errno)
{
	struct buf b;
	char what[96];

	memset(nread, 0, sizeof(nread));
	memset(nwrite, 0, sizeof(nwrite));
	issue(&r, &b, NULL);
	snprintf(what, sizeof(what), "%s: error flag", name);
	check(what, (b.b_flags & B_ERROR) != 0, 1);
	snprintf(what, sizeof(what), "%s: errno", name);
	check(what, b.b_error, want_errno);
	snprintf(what, sizeof(what), "%s: done", name);
	check(what, (b.b_flags & B_DONE) != 0, 1);
	snprintf(what, sizeof(what), "%s: b_resid", name);
	check(what, (long)b.b_resid, (long)r.bcount);
	snprintf(what, sizeof(what), "%s: sectors touched", name);
	check(what, touched(nread) + touched(nwrite), 0);
}

/*
 * A transfer cut short by `trigger`, a card error at fail_sector or the card
 * replaced after swap_after: sectors [first, first + moved) moved and no
 * other, EIO, and a residual of every byte that did not move.
 */
static void
cut_case(const char *name, struct req r, unsigned first_sector,
    unsigned moved, unsigned want_resid)
{
	struct buf b;
	char what[96];
	unsigned char pat[sizeof(xbuf)];
	unsigned i;

	memcpy(card, saved, sizeof(card));
	memset(nread, 0, sizeof(nread));
	memset(nwrite, 0, sizeof(nwrite));
	for (i = 0; i < sizeof(pat); i++)
		pat[i] = (unsigned char)(0x80 | (i * 3));
	issue(&r, &b, (r.rw & B_READ) ? NULL : pat);
	snprintf(what, sizeof(what), "%s: error flag", name);
	check(what, (b.b_flags & B_ERROR) != 0, 1);
	snprintf(what, sizeof(what), "%s: errno", name);
	check(what, b.b_error, EIO);
	snprintf(what, sizeof(what), "%s: done", name);
	check(what, (b.b_flags & B_DONE) != 0, 1);
	snprintf(what, sizeof(what), "%s: b_resid", name);
	check(what, (long)b.b_resid, (long)want_resid);
	check_touched(name, (r.rw & B_READ) ? nread : nwrite, first_sector,
	    moved);
	for (i = 0; i < moved * SD_SECTOR; i++)
		if ((r.rw & B_READ) && xbuf[i] != saved[first_sector +
		    i / SD_SECTOR][i % SD_SECTOR]) {
			snprintf(what, sizeof(what), "%s: byte %u", name, i);
			check(what, 1, 0);
			break;
		}
	memcpy(card, saved, sizeof(card));
}

static void
eof_case(const char *name, struct req r)
{
	struct buf b;
	unsigned s, touched = 0;
	char what[96];

	memset(nread, 0, sizeof(nread));
	issue(&r, &b, NULL);
	for (s = 0; s < CARD_SECTORS; s++)
		touched += nread[s];
	snprintf(what, sizeof(what), "%s: error flag", name);
	check(what, (b.b_flags & B_ERROR) != 0, 0);
	snprintf(what, sizeof(what), "%s: b_resid", name);
	check(what, (long)b.b_resid, (long)r.bcount);
	snprintf(what, sizeof(what), "%s: sectors read", name);
	check(what, touched, 0);
}

/*
 * A raw transfer through sdread or sdwrite. The segments are carved out of
 * rawbuf in order, segment i being lens[i] bytes long.
 */
static unsigned char rawbuf[4 * DEV_BSIZE];
static long raw_end;			/* The file offset after raw_io(). */

static int
raw_io(int write, int part, long offset, const unsigned *lens, int nseg,
    unsigned *resid)
{
	struct iovec iov[4];
	struct uio uio;
	unsigned total = 0, off = 0;
	int i, error;

	for (i = 0; i < nseg; i++) {
		iov[i].iov_base = (char *)rawbuf + off;
		iov[i].iov_len = lens[i];
		off += lens[i];
		total += lens[i];
	}
	uio.uio_iov = iov;
	uio.uio_iovcnt = nseg;
	uio.uio_offset = offset;
	uio.uio_resid = total;
	uio.uio_rw = write ? UIO_WRITE : UIO_READ;
	error = write ? sdwrite((dev_t)part, &uio, 0) :
	    sdread((dev_t)part, &uio, 0);
	*resid = uio.uio_resid;
	raw_end = (long)uio.uio_offset;
	return error;
}

static unsigned
touched(const unsigned *count)
{
	unsigned s, n = 0;

	for (s = 0; s < CARD_SECTORS; s++)
		n += count[s];
	return n;
}

/*
 * Raw transfers address 512-byte sectors through a 1 KiB block number, so
 * only 1 KiB-aligned starts are exact: a 512-byte read at offset 512 would
 * fetch sector 0 again. The others are refused before any card access.
 */
static void
raw_cases(void)
{
	static const unsigned one[1] = { 512 };
	static const unsigned two[2] = { 512, 512 };
	static const unsigned blk_then_sector[2] = { DEV_BSIZE, 512 };
	unsigned resid;
	long off;
	char what[96];

	/* Consecutive 512-byte reads, as dd's default block size makes. */
	for (off = 0; off < 4 * 512; off += 512) {
		int aligned = (off % DEV_BSIZE) == 0;
		unsigned sector = (unsigned)(off / 512);

		memset(nread, 0, sizeof(nread));
		memset(rawbuf, GUARD, sizeof(rawbuf));
		snprintf(what, sizeof(what), "raw 512-byte read at %ld", off);
		check(what, raw_io(0, 0, off, one, 1, &resid),
		    aligned ? 0 : EINVAL);
		check(what, (long)resid, aligned ? 0 : 512);
		check(what, touched(nread), aligned ? 1 : 0);
		if (aligned) {
			check(what, nread[sector], 1);
			check(what, memcmp(rawbuf, saved[sector], SD_SECTOR),
			    0);
		} else
			check(what, rawbuf[0], GUARD);
	}

	/* The fdisk write of the MBR at offset 0 lands on sector 0. */
	memcpy(card, saved, sizeof(card));
	memset(nwrite, 0, sizeof(nwrite));
	memset(rawbuf, 0x5a, sizeof(rawbuf));
	check("raw 512-byte write at 0", raw_io(1, 0, 0, one, 1, &resid), 0);
	check("raw 512-byte write at 0: sectors written", touched(nwrite), 1);
	check("raw 512-byte write at 0: sector 0", nwrite[0], 1);
	check("raw 512-byte write at 0: data", card[0][0], 0x5a);
	memcpy(card, saved, sizeof(card));

	/* Writes at offsets that would overwrite the even sector before. */
	memset(nwrite, 0, sizeof(nwrite));
	memset(rawbuf, 0x5a, sizeof(rawbuf));
	check("raw 512-byte write at 512", raw_io(1, 0, 512, one, 1, &resid),
	    EINVAL);
	check("raw 512-byte write at 512: resid", (long)resid, 512);
	check("raw 512-byte write at 512: sectors written", touched(nwrite),
	    0);
	check("raw 512-byte write at 512: card", memcmp(card, saved,
	    sizeof(card)), 0);
	memset(nwrite, 0, sizeof(nwrite));
	check("raw 512-byte write at 1536", raw_io(1, 0, 1536, one, 1, &resid),
	    EINVAL);
	check("raw 512-byte write at 1536: sectors written", touched(nwrite),
	    0);

	/* A misaligned multi-block request is refused the same way. */
	memset(nread, 0, sizeof(nread));
	check("raw 1 KiB read at 512", raw_io(0, 0, 512, (const unsigned[]){
	    DEV_BSIZE }, 1, &resid), EINVAL);
	check("raw 1 KiB read at 512: sectors read", touched(nread), 0);

	/*
	 * Segments after the first start at the advanced offset, so a
	 * non-final segment must be whole blocks too.
	 */
	memset(nread, 0, sizeof(nread));
	check("raw two 512-byte segments", raw_io(0, 0, 0, two, 2, &resid),
	    EINVAL);
	check("raw two 512-byte segments: sectors read", touched(nread), 0);
	memset(nread, 0, sizeof(nread));
	memset(rawbuf, GUARD, sizeof(rawbuf));
	check("raw block then sector", raw_io(0, 0, 0, blk_then_sector, 2,
	    &resid), 0);
	check("raw block then sector: resid", (long)resid, 0);
	check("raw block then sector: sectors read", touched(nread), 3);
	check("raw block then sector: sector 2", memcmp(rawbuf + DEV_BSIZE,
	    saved[2], SD_SECTOR), 0);
}

/*
 * A raw 2048-byte read of the whole card whose third sector fails, as the
 * review probe made it. physio() advances the offset by the count minus
 * b_resid even on an error, so the offset must stop at the failed sector for
 * a retry to read it again rather than skip it.
 */
static void
raw_fail_cases(void)
{
	static const unsigned four[1] = { 4 * SD_SECTOR };
	unsigned resid;

	memset(nread, 0, sizeof(nread));
	memset(rawbuf, GUARD, sizeof(rawbuf));
	fail_sector = 2;
	check("raw read failing at its third sector",
	    raw_io(0, 0, 0, four, 1, &resid), EIO);
	fail_sector = -1;
	check("raw failing read: bytes left", (long)resid, 1024);
	check("raw failing read: offset", raw_end, 1024);
	check("raw failing read: sectors read", touched(nread), 2);

	memset(nwrite, 0, sizeof(nwrite));
	memset(rawbuf, 0x5a, sizeof(rawbuf));
	fail_sector = 1;
	check("raw write failing at its second sector",
	    raw_io(1, 0, 0, four, 1, &resid), EIO);
	fail_sector = -1;
	check("raw failing write: bytes left", (long)resid, 1536);
	check("raw failing write: offset", raw_end, 512);
	memcpy(card, saved, sizeof(card));

	/* A request refused before I/O consumes zero bytes. */
	memset(nread, 0, sizeof(nread));
	check("raw read of an unopened partition",
	    raw_io(0, 3, 0, four, 1, &resid), ENXIO);
	check("raw refused read: bytes left", (long)resid, 2048);
	check("raw refused read: offset", raw_end, 0);
}

static void
open_all(void)
{
	int part;

	set_pin(0);
	for (part = 0; part < 3; part++)
		check("open for a case", sdopen((dev_t)part, 0, S_IFCHR), 0);
}

static void
close_all(void)
{
	int part;

	for (part = 0; part < 3; part++) {
		sdclose((dev_t)part, 0, S_IFCHR);
		sdclose((dev_t)part, 0, S_IFBLK);
	}
}

/*
 * The callout declares the card replaced while a request is between two
 * sector commands. The next command must not reach the new card: the request
 * stops with EIO and reports every sector it did not move. A change after
 * the final command leaves that request complete and fails the next one.
 */
static void
swap_cases(void)
{
	struct req r;
	struct buf b;

	r = (struct req){ 1, B_READ, 0, 2048 };
	swap_after = 9;
	cut_case("read, card replaced after its second sector", r, 8, 2, 1024);
	swap_after = -1;
	close_all();
	open_all();

	r = (struct req){ 1, B_WRITE, 0, 2048 };
	swap_after = 8;
	cut_case("write, card replaced after its first sector", r, 8, 1, 1536);
	swap_after = -1;
	close_all();
	open_all();

	r = (struct req){ 2, B_READ, 9, 2048 };
	swap_after = 59;
	cut_case("truncated read, card replaced after its first sector", r, 59, 1,
	    1536);
	swap_after = -1;
	close_all();
	open_all();

	r = (struct req){ 1, B_READ, 0, 2048 };
	swap_after = 11;
	memset(nread, 0, sizeof(nread));
	issue(&r, &b, NULL);
	check("card replaced after the last sector: error flag",
	    (b.b_flags & B_ERROR) != 0, 0);
	check("card replaced after the last sector: b_resid", (long)b.b_resid, 0);
	check("card replaced after the last sector: sectors read", touched(nread),
	    4);
	r = (struct req){ 1, B_READ, 0, 512 };
	error_case("the request after the replacement", r, EIO);
	swap_after = -1;
	close_all();
	open_all();
}

/* One-sector read of a partition; 0 or the errno. */
static int
try_read(int part)
{
	struct req r = { part, B_READ, 0, 512 };
	struct buf b;

	issue(&r, &b, NULL);
	return (b.b_flags & B_ERROR) ? b.b_error : 0;
}

/*
 * The card-detect debouncer, driven the way the keyboard callout drives it.
 * A level change latches the replacement only when it stays stable for the
 * whole interval and differs from the level the open card was accepted at.
 * Entry state: nothing open.
 */
static void
detect_cases(void)
{
	/*
	 * Active-low SD_DET: empty socket (1) refuses open with ENODEV; a swap
	 * while nothing is open followed by reinsertion (0) opens and accepts.
	 */
	set_pin(1);
	check("open with socket empty", sdopen(1, 0, S_IFCHR), ENODEV);
	set_pin(0);
	card_swap();
	set_pin(0);
	sd_detect_poll();
	advance(DEBOUNCE_US);
	sd_detect_poll();
	check("open after a swap with nothing held", sdopen(1, 0, S_IFCHR), 0);
	check("accepting the card samples the pin with interrupts masked",
	    shim_spl_pin, 1);
	check("accepting the card releases the interrupt mask", shim_spl, 0);
	check("read after the accepting open", try_read(1), 0);
	sdclose(1, 0, S_IFCHR);

	/*
	 * An insertion's debounce still running when the first open finishes
	 * must not mark the card just accepted as replaced.
	 */
	set_pin(1);
	sd_detect_poll();
	advance(DEBOUNCE_US);
	sd_detect_poll();
	set_pin(0);
	sd_detect_poll();
	advance(10000);
	check("open during an insertion's debounce", sdopen(1, 0, S_IFCHR), 0);
	advance(DEBOUNCE_US);
	sd_detect_poll();
	sd_detect_poll();
	check("read after the insertion's debounce expired", try_read(1), 0);

	/*
	 * A candidate seen before the open does not carry over: the pin is back
	 * at the accepted level when the card is accepted, so a new departure
	 * has to hold the whole interval from its own first sample.
	 */
	sdclose(1, 0, S_IFCHR);
	set_pin(!pin_level);
	sd_detect_poll();
	advance(10000);
	set_pin(!pin_level);
	check("open with a stale candidate", sdopen(1, 0, S_IFCHR), 0);
	set_pin(!pin_level);
	advance(DEBOUNCE_US);
	sd_detect_poll();
	check("read after a departure sampled once", try_read(1), 0);
	advance(DEBOUNCE_US);
	sd_detect_poll();
	check("read after that departure held the interval", try_read(1), EIO);
	sdclose(1, 0, S_IFCHR);
	check("open while removed card stays out", sdopen(1, 0, S_IFCHR), ENODEV);
	set_pin(0);
	check("open at the reinserted level", sdopen(1, 0, S_IFCHR), 0);

	/* An excursion that returns to the committed level before the interval. */
	set_pin(!pin_level);
	sd_detect_poll();
	advance(10000);
	set_pin(!pin_level);
	sd_detect_poll();
	advance(10 * DEBOUNCE_US);
	sd_detect_poll();
	sd_detect_poll();
	check("read after a brief excursion", try_read(1), 0);

	/*
	 * A level that flickers back and forth restarts the interval at its
	 * last change: 49999 us after that change it has not latched, 50000 us
	 * after it has.
	 */
	set_pin(!pin_level);
	sd_detect_poll();
	advance(30000);
	set_pin(!pin_level);
	sd_detect_poll();
	advance(5000);
	set_pin(!pin_level);
	sd_detect_poll();
	advance(DEBOUNCE_US - 1);
	sd_detect_poll();
	check("read 49999 us after the last change", try_read(1), 0);
	advance(1);
	sd_detect_poll();
	check("read 50000 us after the last change", try_read(1), EIO);
	check("reinit on a replaced card", sdioctl(0, DIOCREINIT, NULL, 0), EIO);
	sdclose(1, 0, S_IFCHR);
	set_pin(0);
	check("shipped state after the cases: interrupt mask", shim_spl, 0);
}

/*
 * sddump(): the crash dump of machine/dump.h on the card's first partition
 * of type DUMP_PART_TYPE. Entry 2 of the table (card sectors 41 to 60) is
 * made the dump partition; the three spans fill ten of its sectors.
 */
#define	DUMPDEV		((dev_t)(SD_BMAJOR << 8))
#define	DUMP_FIRST	41

static char dump_msg[] = "panic: test";
static unsigned char dump_data[3000], dump_u[700], dump_big[9000];
static unsigned dump_now;

static const struct dump_span dump_spans[3] = {
	{ DUMP_SEG_MESSAGE, dump_msg, sizeof(dump_msg) - 1 },
	{ DUMP_SEG_KERNEL_DATA, dump_data, sizeof(dump_data) },
	{ DUMP_SEG_UAREA, dump_u, sizeof(dump_u) },
};

static void
dump_table(unsigned type1, unsigned offset1, unsigned n1)
{
	fill_card();
	card[0][446 + 16 + 4] = (unsigned char)type1;
	memcpy(&card[0][446 + 16 + 8], &offset1, 4);
	memcpy(&card[0][446 + 16 + 12], &n1, 4);
	memset(nread, 0, sizeof(nread));
	memset(nwrite, 0, sizeof(nwrite));
}

static unsigned
writes_outside(unsigned first, unsigned n)
{
	unsigned s, k = 0;

	for (s = 0; s < CARD_SECTORS; s++)
		if ((s < first || s >= first + n) && nwrite[s] != 0)
			k++;
	return k;
}

/* Sector first holds a complete dump of dump_spans; report what is wrong. */
static void
check_dump(const char *name, unsigned first)
{
	struct dump_header h;
	struct dump_segment seg;
	unsigned char copy[SD_SECTOR];
	unsigned sector = first + 1, i, crc;
	char what[96];

	memcpy(&h, card[first], sizeof(h));
	snprintf(what, sizeof(what), "%s: magic", name);
	check(what, h.dh_magic, DUMP_MAGIC);
	snprintf(what, sizeof(what), "%s: version", name);
	check(what, h.dh_version, DUMP_VERSION);
	snprintf(what, sizeof(what), "%s: segments", name);
	check(what, h.dh_nsegments, 3);
	snprintf(what, sizeof(what), "%s: image crc", name);
	check(what, h.dh_image_crc, 0xc0ffee11U);
	snprintf(what, sizeof(what), "%s: time", name);
	check(what, h.dh_time, dump_now);
	snprintf(what, sizeof(what), "%s: bytes", name);
	check(what, h.dh_bytes, 10 * SD_SECTOR);
	/* The sum covers the header and table with dh_crc read as zero. */
	memcpy(copy, card[first], SD_SECTOR);
	memset(copy + offsetof(struct dump_header, dh_crc), 0, 4);
	crc = ~crc32_update(~0U, copy, 24 + 3 * 16);
	snprintf(what, sizeof(what), "%s: header crc", name);
	check(what, h.dh_crc, crc);
	for (i = 24 + 3 * 16; i < SD_SECTOR; i++)
		if (copy[i] != 0) {
			snprintf(what, sizeof(what), "%s: header sector padding",
			    name);
			check(what, i, -1);
			break;
		}
	for (i = 0; i < 3; i++) {
		memcpy(&seg, card[first] + 24 + i * 16, sizeof(seg));
		snprintf(what, sizeof(what), "%s: segment %u kind", name, i);
		check(what, seg.ds_kind, dump_spans[i].kind);
		snprintf(what, sizeof(what), "%s: segment %u bytes", name, i);
		check(what, seg.ds_bytes, dump_spans[i].bytes);
		snprintf(what, sizeof(what), "%s: segment %u address", name, i);
		check(what, seg.ds_address,
		    (unsigned)(unsigned long)dump_spans[i].addr);
		crc = ~crc32_update(~0U, dump_spans[i].addr,
		    dump_spans[i].bytes);
		snprintf(what, sizeof(what), "%s: segment %u crc", name, i);
		check(what, seg.ds_crc, crc);
		snprintf(what, sizeof(what), "%s: segment %u bytes on the card",
		    name, i);
		check(what, memcmp(card[sector], dump_spans[i].addr,
		    dump_spans[i].bytes) == 0 ? 1 : 0, 1);
		sector += (dump_spans[i].bytes + SD_SECTOR - 1) / SD_SECTOR;
	}
	snprintf(what, sizeof(what), "%s: end sector", name);
	check(what, sector, first + 10);
}

static void
dump_cases(void)
{
	struct dump_span big[3];
	unsigned i;

	for (i = 0; i < sizeof(dump_data); i++)
		dump_data[i] = (unsigned char)(i * 13 + 5);
	for (i = 0; i < sizeof(dump_u); i++)
		dump_u[i] = (unsigned char)(i * 3 + 1);
	dump_now = 1700000000U;

	/* The probe arms the socket; a bare Pico keeps NODEV. */
	dumpdev = NODEV;
	test_board = BOARD_ID_PICO;
	(void)sddriver.probe(NULL);
	check("dump device on a bare Pico", dumpdev == NODEV, 1);
	test_board = BOARD_ID_PICOCALC;
	(void)sddriver.probe(NULL);
	check("dump device on a PicoCalc", dumpdev, DUMPDEV);

	/* A complete dump, and nothing written outside the partition. */
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	check("dump", sddump(DUMPDEV, dump_spans, 3, 0xc0ffee11U, dump_now),
	    0);
	check_dump("dump", DUMP_FIRST);
	check("dump writes outside the partition",
	    writes_outside(DUMP_FIRST, 10), 0);
	check("dump header sector writes", nwrite[DUMP_FIRST], 2);
	check("dump reads the table alone", nread[0], 1);
	check("dump leaves the driver's card closed", (long)sdsize(2), 0);

	/* The first entry of the type wins: partition 1 at sector 8. */
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	card[0][446 + 4] = DUMP_PART_TYPE;
	check("dump on the first entry of the type",
	    sddump(DUMPDEV, dump_spans, 3, 0xc0ffee11U, dump_now), 0);
	check_dump("dump on the first entry", 8);

	/* Refusals that write zero sectors. */
	dump_table(PTYPE_BSDFFS, DUMP_FIRST, 20);
	check("dump without a dump partition",
	    sddump(DUMPDEV, dump_spans, 3, 0, 0), ENXIO);
	check("writes without a dump partition", writes_outside(0, 0), 0);
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, CARD_SECTORS);
	check("dump partition past the card",
	    sddump(DUMPDEV, dump_spans, 3, 0, 0), ENXIO);
	check("writes past the card", writes_outside(0, 0), 0);
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	card[0][511] = 0;
	check("dump without a table signature",
	    sddump(DUMPDEV, dump_spans, 3, 0, 0), ENXIO);
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	check("dump to another device", sddump(DUMPDEV + 1, dump_spans, 3, 0,
	    0), ENXIO);
	init_fail = 1;
	check("dump without a card", sddump(DUMPDEV, dump_spans, 3, 0, 0),
	    ENXIO);
	init_fail = 0;
	check("writes without a card", writes_outside(0, 0), 0);

	memcpy(big, dump_spans, sizeof(big));
	big[1].addr = dump_big;
	big[1].bytes = sizeof(dump_big);
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	memcpy(card[DUMP_FIRST], "OLDDUMP", 7);
	check("dump larger than the partition", sddump(DUMPDEV, big, 3, 0, 0),
	    ENOSPC);
	check("writes for an undersized partition", writes_outside(0, 0), 0);
	check("an undersized partition keeps its sector 0",
	    memcmp(card[DUMP_FIRST], "OLDDUMP", 7), 0);

	/* A failed sector leaves the header unwritten: sector 0 stays zero. */
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	fail_sector = DUMP_FIRST + 5;
	check("dump with a failing sector",
	    sddump(DUMPDEV, dump_spans, 3, 0, 0), EIO);
	fail_sector = -1;
	for (i = 0; i < SD_SECTOR && card[DUMP_FIRST][i] == 0; i++)
		continue;
	check("header sector after a failed dump", i, SD_SECTOR);
	check("sectors after the failed one", writes_outside(DUMP_FIRST, 5),
	    0);

	/*
	 * A block open that overlaps the partition is a mount: refused. The
	 * whole card overlaps everything; partition 1 (sectors 8 to 40) does
	 * not overlap sectors 41 to 60; a raw open is not a mount.
	 */
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	check("block open of the dump partition", sdopen(2, 0, S_IFBLK), 0);
	memset(nwrite, 0, sizeof(nwrite));
	check("dump on a mounted partition",
	    sddump(DUMPDEV, dump_spans, 3, 0, 0), EBUSY);
	check("writes on a mounted partition", writes_outside(0, 0), 0);
	close_all();
	check("block open of the whole card", sdopen(0, 0, S_IFBLK), 0);
	check("dump under a whole-card open",
	    sddump(DUMPDEV, dump_spans, 3, 0, 0), EBUSY);
	close_all();
	check("block open of partition 1", sdopen(1, 0, S_IFBLK), 0);
	check("raw open of the dump partition", sdopen(2, 0, S_IFCHR), 0);
	check("dump beside a mount",
	    sddump(DUMPDEV, dump_spans, 3, 0xc0ffee11U, dump_now), 0);
	check_dump("dump beside a mount", DUMP_FIRST);
	close_all();

	/* A replaced card is unmounted, so its dump partition is free. */
	dump_table(DUMP_PART_TYPE, DUMP_FIRST, 20);
	check("block open before a swap", sdopen(2, 0, S_IFBLK), 0);
	card_swap();
	check("dump on a replaced card",
	    sddump(DUMPDEV, dump_spans, 3, 0xc0ffee11U, dump_now), 0);
	check_dump("dump on a replaced card", DUMP_FIRST);
	close_all();
	set_pin(0);
}

/* Set MBR entry n (1 to 4) of the card in sector 0 to a type. */
static void
set_type(int n, unsigned type)
{
	card[0][446 + 16 * (n - 1) + 4] = (unsigned char)type;
}

/*
 * One call of sdrootattach() from a closed card. Returns 1 when it
 * panicked, with the message in panic_message. Every return leaves the card
 * closed: sdsize() of the whole card reads 0.
 */
static int
root_attach(const char *name)
{
	char what[96];
	volatile int panicked = 0;

	panic_message = NULL;
	rootdev = pipedev = makedev(0, 1);
	if (setjmp(panic_return) == 0)
		sdrootattach();
	else
		panicked = 1;
	snprintf(what, sizeof(what), "%s: card closed afterwards", name);
	check(what, (long)sdsize(makedev(SD_BMAJOR, 0)), 0);
	return panicked;
}

/*
 * The owner's card layout: a FAT32 entry, a Linux entry, then the b7 entry
 * in slot 3 at sector 50 for 10 sectors.
 */
static void
owner_layout(void)
{
	fill_card();
	set_type(1, 0x0c);
	set_type(2, 0x83);
	set_type(3, PTYPE_BSDFFS);
	memcpy(&card[0][446 + 32 + 8], &(unsigned){ 50 }, 4);
	memcpy(&card[0][446 + 32 + 12], &(unsigned){ 10 }, 4);
}

/*
 * The root open must find the partition and the card sdrootattach() chose.
 * Each case changes one thing between the selection and the open, as a card
 * swapped or rewritten in that interval would, and the open refuses with EIO
 * and leaves the card closed; the unchanged card then opens, because the
 * refusal keeps the record.
 */
static void
root_binding_case(const char *name, void (*change)(void), void (*undo)(void))
{
	char what[96];

	owner_layout();
	snprintf(what, sizeof(what), "%s: attach", name);
	check(what, root_attach(name), 0);
	change();
	snprintf(what, sizeof(what), "%s: root open refused", name);
	check(what, sdopen(rootdev, 0, S_IFBLK), EIO);
	snprintf(what, sizeof(what), "%s: card closed after refusal", name);
	check(what, (long)sdsize(makedev(SD_BMAJOR, 0)), 0);
	undo();
	snprintf(what, sizeof(what), "%s: unchanged card opens", name);
	check(what, sdopen(rootdev, 0, S_IFBLK), 0);
	sdclose(rootdev, 0, S_IFBLK);
}

static void
change_type(void)
{
	set_type(3, 0x83);
}

static void
undo_type(void)
{
	set_type(3, PTYPE_BSDFFS);
}

static void
change_offset(void)
{
	memcpy(&card[0][446 + 32 + 8], &(unsigned){ 51 }, 4);
}

static void
undo_offset(void)
{
	memcpy(&card[0][446 + 32 + 8], &(unsigned){ 50 }, 4);
}

static void
change_length(void)
{
	memcpy(&card[0][446 + 32 + 12], &(unsigned){ 11 }, 4);
}

static void
undo_length(void)
{
	memcpy(&card[0][446 + 32 + 12], &(unsigned){ 10 }, 4);
}

static void
change_capacity(void)
{
	init_sectors = 2 * CARD_SECTORS;
}

static void
undo_capacity(void)
{
	init_sectors = CARD_SECTORS;
}

static void
change_card_type(void)
{
	init_type = SD_V2;
}

static void
undo_card_type(void)
{
	init_type = SD_HC;
}

static void
root_binding_cases(void)
{
	root_binding_case("bind type", change_type, undo_type);
	root_binding_case("bind start", change_offset, undo_offset);
	root_binding_case("bind length", change_length, undo_length);
	root_binding_case("bind capacity", change_capacity, undo_capacity);
	root_binding_case("bind card type", change_card_type, undo_card_type);

	/* The open that matched consumed the record: the slot opens freely. */
	owner_layout();
	check("bind consumed: attach", root_attach("bind consumed"), 0);
	check("bind consumed: root opens", sdopen(rootdev, 0, S_IFBLK), 0);
	sdclose(rootdev, 0, S_IFBLK);
	change_type();
	check("bind consumed: later open", sdopen(rootdev, 0, S_IFBLK), 0);
	sdclose(rootdev, 0, S_IFBLK);

	/* The whole card and the other slots are not bound to the record. */
	owner_layout();
	check("bind others: attach", root_attach("bind others"), 0);
	change_type();
	check("bind others: whole card", sdopen(makedev(SD_BMAJOR, 0), 0, S_IFBLK), 0);
	sdclose(makedev(SD_BMAJOR, 0), 0, S_IFBLK);
	check("bind others: slot 1 opens", sdopen(makedev(SD_BMAJOR, 1), 0, S_IFBLK), 0);
	sdclose(makedev(SD_BMAJOR, 1), 0, S_IFBLK);
	undo_type();
	check("bind others: root opens", sdopen(rootdev, 0, S_IFBLK), 0);
	sdclose(rootdev, 0, S_IFBLK);
}

/*
 * Every byte of an MBR entry's status, type, start and length reaches the
 * table: the card reports a capacity large enough to hold an extent whose
 * four bytes all differ, so a decode that drops or moves one byte shows in
 * DIOCGETPART.
 */
static void
mbr_byte_cases(void)
{
	static const unsigned char entry[16] = {
		0x80, 0, 0, 0, 0x83, 0, 0, 0,
		0x01, 0x02, 0x03, 0x04, 0x04, 0x03, 0x02, 0x01
	};
	struct diskpart dp;

	fill_card();
	memcpy(&card[0][446 + 48], entry, sizeof(entry));
	init_sectors = 0x10000000;
	check("mbr bytes: slot 4 opens", sdopen(4, 0, S_IFBLK), 0);
	check("mbr bytes: getpart", sdioctl(4, DIOCGETPART, (caddr_t)&dp, 0), 0);
	check("mbr bytes: status", dp.dp_status, 0x80);
	check("mbr bytes: type", dp.dp_type, 0x83);
	check("mbr bytes: start", (long)dp.dp_offset, 0x04030201L);
	check("mbr bytes: length", (long)dp.dp_nsectors, 0x01020304L);
	sdclose(4, 0, S_IFBLK);
	init_sectors = CARD_SECTORS;

	/* Without the 0x55 0xaa signature the table is empty. */
	fill_card();
	card[0][511] = 0;
	check("mbr signature: slot 1 absent", sdopen(1, 0, S_IFBLK), ENXIO);
	check("mbr signature: whole card opens", sdopen(0, 0, S_IFBLK), 0);
	sdclose(0, 0, S_IFBLK);
}

static void
root_cases(void)
{
	/* No stamp: the configured root stays and the card is not read. */
	fill_card();
	test_loader = 0;
	check("no stamp: no panic", root_attach("no stamp"), 0);
	check("no stamp: rootdev kept", (long)rootdev, (long)makedev(0, 1));
	check("no stamp: card not read", (long)nread[0], 0);

	/*
	 * The stamp and the owner's card layout: a FAT32 entry, a Linux entry,
	 * then the b7 entry in slot 3.
	 */
	test_loader = 1;
	owner_layout();
	check("slot 3: no panic", root_attach("slot 3"), 0);
	check("slot 3: rootdev", (long)rootdev, (long)makedev(SD_BMAJOR, 3));
	check("slot 3: pipedev", (long)pipedev, (long)makedev(SD_BMAJOR, 3));
	check("slot 3: root opens", sdopen(rootdev, 0, S_IFBLK), 0);
	check("slot 3: root size", (long)sdsize(rootdev), 5);
	sdclose(rootdev, 0, S_IFBLK);

	root_binding_cases();

	/* The first b7 entry wins when two are present. */
	fill_card();
	check("two b7: no panic", root_attach("two b7"), 0);
	check("two b7: rootdev", (long)rootdev, (long)makedev(SD_BMAJOR, 1));

	/* No b7 entry: the panic, and the configured root is not taken. */
	fill_card();
	set_type(1, 0x83);
	set_type(2, 0x0c);
	check("no b7: panic", root_attach("no b7"), 1);
	check("no b7: message", panic_message != NULL &&
	    strcmp(panic_message, "root: no partition of type b7 on sd0") == 0, 1);
	/* The record of the earlier selection does not outlive the failure. */
	check("no b7: stale record cleared",
	    sdopen(makedev(SD_BMAJOR, 1), 0, S_IFBLK), 0);
	sdclose(makedev(SD_BMAJOR, 1), 0, S_IFBLK);

	/* No card in the socket. */
	fill_card();
	init_fail = 1;
	check("no card: panic", root_attach("no card"), 1);
	check("no card: message", panic_message != NULL &&
	    strcmp(panic_message, "root: sd0 unusable") == 0, 1);
	init_fail = 0;

	/* The stamp on a board without the socket. */
	fill_card();
	test_board = BOARD_ID_PICO;
	check("bare Pico: panic", root_attach("bare Pico"), 1);
	check("bare Pico: card not read", (long)nread[0], 0);
	test_board = BOARD_ID_PICOCALC;
	test_loader = 0;
}

/* MBR entry n: its type, first sector and sector count. */
static void
set_part(int n, unsigned type, unsigned offset, unsigned nsectors)
{
	unsigned char *e = &card[0][446 + 16 * (n - 1)];

	e[4] = (unsigned char)type;
	memcpy(e + 8, &offset, 4);
	memcpy(e + 12, &nsectors, 4);
}

#define	FL1		makedev(0, 8)
#define	SWAP_MIN	(2 * SWAP_IMAGE_GUARD + SWAP_IMAGE_ALIGN)

/*
 * One call of sdswapattach() from a closed card with swap on fl1 or on
 * `from`. A card that took swap holds that partition open with its table
 * read, so the open main() makes reads zero sectors, and the close that
 * swclose() forwards for /dev/swap leaves it open and able to transfer;
 * every other path leaves the card closed. swap_release() ends the hold.
 */
static void
swap_attach(const char *name, dev_t from)
{
	char what[96];

	swapdev = from;
	nread[0] = 0;
	sdswapattach();
	if (major(swapdev) == SD_BMAJOR) {
		unsigned reads = nread[0];

		snprintf(what, sizeof(what), "%s: partition held open", name);
		check(what, sdsize(swapdev) != 0, 1);
		snprintf(what, sizeof(what), "%s: open by main", name);
		check(what, sdopen(swapdev, 0, S_IFBLK), 0);
		snprintf(what, sizeof(what), "%s: table not read again", name);
		check(what, (long)nread[0], (long)reads);
		sdclose(swapdev, 0, S_IFBLK);
		snprintf(what, sizeof(what), "%s: held across a close", name);
		check(what, sdsize(swapdev) != 0, 1);
		return;
	}
	snprintf(what, sizeof(what), "%s: card closed afterwards", name);
	check(what, (long)sdsize(makedev(SD_BMAJOR, 0)), 0);
}

/* A call on a bare Pico, which declines to select the card, ends the previous hold. */
static void
swap_release(void)
{
	test_board = BOARD_ID_PICO;
	swap_attach("release", FL1);
	test_board = BOARD_ID_PICOCALC;
}

/*
 * Swap selection (sdswapattach()): which device holds swap after the
 * service ran, and the size sdsize() reports to init_main() for it.
 */
static void
swapdev_cases(void)
{
	init_sectors = 1U << 22;

	/* A bare Pico lacks a socket: fl1 stays and the card is not read. */
	fill_card();
	set_part(4, PTYPE_SWAP, 4096, 2 * 8192);
	test_board = BOARD_ID_PICO;
	swap_attach("bare Pico", FL1);
	check("bare Pico: swapdev kept", (long)swapdev, (long)FL1);
	check("bare Pico: card not read", (long)nread[0], 0);
	test_board = BOARD_ID_PICOCALC;

	/*
	 * The PSRAM took swap at probe: it keeps it over the b8 entry. The card
	 * is read for a b-a entry (tmp_cases()); with none it stays closed and
	 * the PSRAM keeps its split.
	 */
	psram_all_calls = 0;
	swap_attach("psram", makedev(PSRAM_MAJOR, 0));
	check("psram: swapdev kept", (long)swapdev,
	    (long)makedev(PSRAM_MAJOR, 0));
	check("psram: card read for the tmp entry", nread[0] != 0, 1);
	check("psram: no tmp entry, split kept", psram_all_calls, 0);

	/* No card in the socket. */
	init_fail = 1;
	swap_attach("no card", FL1);
	check("no card: swapdev kept", (long)swapdev, (long)FL1);
	init_fail = 0;

	/* The table cannot be read. */
	fail_sector = 0;
	swap_attach("unreadable table", FL1);
	check("unreadable table: swapdev kept", (long)swapdev, (long)FL1);
	fail_sector = -1;

	/* The b8 entry in slot 4 behind two entries of another type. */
	swap_attach("slot 4", FL1);
	check("slot 4: swapdev", (long)swapdev, (long)makedev(SD_BMAJOR, 4));
	check("slot 4: card read", nread[0] != 0, 1);
	check("slot 4: swap opens", sdopen(swapdev, 0, S_IFBLK), 0);
	check("slot 4: swap size", (long)sdsize(swapdev), 8192);
	sdclose(swapdev, 0, S_IFBLK);
	{
		/*
		 * The strategy routine still accepts the partition after the
		 * open and close of /dev/swap; the 64-sector model card holds
		 * no sector this far out, so the request ends at the card.
		 */
		struct buf b;
		struct req r = { minor(swapdev), B_READ, 0, 1024 };
		dev_t dev = swapdev;

		swapdev = (dev_t)minor(dev);
		issue(&r, &b, NULL);
		check("slot 4: request reaches the card after a close",
		    b.b_error, EIO);
		swapdev = dev;
	}

	/* No b8 entry: types b7 and b9 beside it are not swap. */
	set_part(4, PTYPE_BSDFFS, 4096, 2 * 8192);
	swap_attach("b7 only", FL1);
	check("b7 only: swapdev kept", (long)swapdev, (long)FL1);
	set_part(4, DUMP_PART_TYPE, 4096, 2 * 8192);
	swap_attach("b9 only", FL1);
	check("b9 only: swapdev kept", (long)swapdev, (long)FL1);

	/* One block short of two full images: passed over. */
	set_part(4, PTYPE_SWAP, 4096, 2 * (SWAP_MIN - 1));
	swap_attach("short", FL1);
	check("short: swapdev kept", (long)swapdev, (long)FL1);
	set_part(4, PTYPE_SWAP, 4096, 2 * SWAP_MIN);
	swap_attach("minimum", FL1);
	check("minimum: swapdev", (long)swapdev, (long)makedev(SD_BMAJOR, 4));

	/* A short b8 entry first, a usable one after it. */
	set_part(3, PTYPE_SWAP, 2048, 2 * 64);
	set_part(4, PTYPE_SWAP, 4096, 2 * 8192);
	swap_attach("short then usable", FL1);
	check("short then usable: swapdev", (long)swapdev,
	    (long)makedev(SD_BMAJOR, 4));

	/* Two usable entries: the first one. */
	set_part(3, PTYPE_SWAP, 4096 + 2 * 8192, 2 * 4096);
	swap_attach("two usable", FL1);
	check("two usable: swapdev", (long)swapdev, (long)makedev(SD_BMAJOR, 3));

	/*
	 * A partition past the swap map's reach reports the largest aligned
	 * 16-bit count, and one that is not a whole number of images loses
	 * its tail; a partition that is not swap reports its own size.
	 */
	set_part(3, PTYPE_BSDFFS, 4096, 2 * 100001);
	set_part(4, PTYPE_SWAP, 4096 + 2 * 100001, 2 * 100001);
	swap_attach("large", FL1);
	check("large: swapdev", (long)swapdev, (long)makedev(SD_BMAJOR, 4));
	check("large: swap opens", sdopen(swapdev, 0, S_IFBLK), 0);
	check("large: root opens", sdopen(makedev(SD_BMAJOR, 3), 0, S_IFBLK), 0);
	check("large: swap size", (long)sdsize(swapdev), 65532);
	check("large: other size", (long)sdsize(makedev(SD_BMAJOR, 3)), 100001);
	sdclose(makedev(SD_BMAJOR, 3), 0, S_IFBLK);
	sdclose(swapdev, 0, S_IFBLK);
	set_part(4, PTYPE_SWAP, 4096 + 2 * 100001, 2 * 8191);
	swap_attach("unaligned", FL1);
	check("unaligned: swap opens", sdopen(swapdev, 0, S_IFBLK), 0);
	check("unaligned: swap size", (long)sdsize(swapdev), 8188);
	sdclose(swapdev, 0, S_IFBLK);

	/*
	 * A b8 entry that shares a sector with another entry is passed over,
	 * since swap writes would land in the b7 root or another partition;
	 * one that ends where another starts shares zero sectors.
	 */
	{
		unsigned r = 65536, n = 2 * 8192;

		set_part(3, PTYPE_BSDFFS, r, n);
		set_part(4, PTYPE_SWAP, r + n - 1, n);
		swap_attach("over the root's end", FL1);
		check("over the root's end: swapdev kept", (long)swapdev,
		    (long)FL1);
		set_part(4, PTYPE_SWAP, r - n + 1, n);
		swap_attach("over the root's start", FL1);
		check("over the root's start: swapdev kept", (long)swapdev,
		    (long)FL1);
		set_part(4, PTYPE_SWAP, r + 2, 2 * SWAP_MIN);
		swap_attach("inside the root", FL1);
		check("inside the root: swapdev kept", (long)swapdev,
		    (long)FL1);
		set_part(4, PTYPE_SWAP, r - 2, n + 4);
		swap_attach("around the root", FL1);
		check("around the root: swapdev kept", (long)swapdev,
		    (long)FL1);
		set_part(4, PTYPE_SWAP, r + n, n);
		swap_attach("after the root", FL1);
		check("after the root: swapdev", (long)swapdev,
		    (long)makedev(SD_BMAJOR, 4));
		set_part(4, PTYPE_SWAP, r - n, n);
		swap_attach("before the root", FL1);
		check("before the root: swapdev", (long)swapdev,
		    (long)makedev(SD_BMAJOR, 4));

		/* A b8 entry over a later b7 one, a clean b8 entry after. */
		set_part(1, PTYPE_SWAP, r + n, n);
		set_part(2, PTYPE_BSDFFS, r + n + 1, 2);
		set_part(4, PTYPE_SWAP, r + 2 * n, n);
		swap_attach("overlapping then clean", FL1);
		check("overlapping then clean: swapdev", (long)swapdev,
		    (long)makedev(SD_BMAJOR, 4));

		/* Two b8 entries over one extent: neither is taken. */
		set_part(1, PTYPE_BSDFFS, 8, 33);
		set_part(2, PTYPE_BSDFFS, 41, 20);
		set_part(3, PTYPE_SWAP, r + n, n);
		set_part(4, PTYPE_SWAP, r + n, n);
		swap_attach("two b8 on one extent", FL1);
		check("two b8 on one extent: swapdev kept", (long)swapdev,
		    (long)FL1);
		set_part(3, PTYPE_BSDFFS, r, n);
	}

	/* An entry that runs past the card is not an entry. */
	set_part(4, PTYPE_SWAP, init_sectors - 16, 2 * 8192);
	swap_attach("past the card", FL1);
	check("past the card: swapdev kept", (long)swapdev, (long)FL1);

	swap_release();
	init_sectors = CARD_SECTORS;
	swapdev = FL1;
}

/*
 * The /tmp partition: the first usable PTYPE_TMP entry, reached as minor 5
 * whichever slot holds it, takes /tmp off the PSRAM and so gives swap the
 * whole part (psram_swap_all()) when the PSRAM holds swap, and is chosen
 * without a PSRAM too, so /etc/rc.rp2040 mounts it on any PicoCalc. It
 * needs the swap partition's size rule and its sector-sharing rule.
 */
#define	TMP_MIN		6144	/* kbytes: the PSRAM region it replaces */
#define	TMPDEV		makedev(SD_BMAJOR, 5)

static void
tmp_attach(const char *name, dev_t from, int calls)
{
	char what[96];

	psram_all_calls = 0;
	swap_attach(name, from);
	snprintf(what, sizeof(what), "%s: psram_swap_all calls", name);
	check(what, psram_all_calls, calls);
}

/* Open /dev/sd0t block, check what it names, close it. */
static void
tmp_names(const char *name, int slot, long blocks, int held)
{
	char what[96];
	struct diskpart dp;
	dev_t real = makedev(SD_BMAJOR, slot);

	snprintf(what, sizeof(what), "%s: open", name);
	check(what, sdopen(TMPDEV, 0, S_IFBLK), slot != 0 ? 0 : ENXIO);
	if (slot == 0) {
		close_all();
		return;
	}
	snprintf(what, sizeof(what), "%s: size", name);
	check(what, (long)sdsize(TMPDEV), blocks);
	snprintf(what, sizeof(what), "%s: size is the slot's", name);
	check(what, (long)sdsize(real), blocks);
	snprintf(what, sizeof(what), "%s: getpart", name);
	check(what, sdioctl(TMPDEV, DIOCGETPART, (caddr_t)&dp, 0), 0);
	snprintf(what, sizeof(what), "%s: type", name);
	check(what, dp.dp_type, PTYPE_TMP);
	snprintf(what, sizeof(what), "%s: extent is the slot's", name);
	{
		struct diskpart sp;

		check(what, sdioctl(real, DIOCGETPART, (caddr_t)&sp, 0), 0);
		check(what, dp.dp_offset == sp.dp_offset &&
		    dp.dp_nsectors == sp.dp_nsectors, 1);
	}
	/*
	 * The partition has one node: the slot's own minor refuses to open, so
	 * no second close can end the open /tmp holds, block or raw.
	 */
	snprintf(what, sizeof(what), "%s: slot block open refused", name);
	check(what, sdopen(real, 0, S_IFBLK), ENXIO);
	snprintf(what, sizeof(what), "%s: slot raw open refused", name);
	check(what, sdopen(real, 0, S_IFCHR), ENXIO);
	snprintf(what, sizeof(what), "%s: still open after the refusals", name);
	check(what, (long)sdsize(TMPDEV), blocks);
	snprintf(what, sizeof(what), "%s: raw node opens", name);
	check(what, sdopen(TMPDEV, 0, S_IFCHR), 0);
	sdclose(TMPDEV, 0, S_IFCHR);
	snprintf(what, sizeof(what), "%s: raw close keeps the block open", name);
	check(what, (long)sdsize(TMPDEV), blocks);
	sdclose(TMPDEV, 0, S_IFBLK);
	snprintf(what, sizeof(what), "%s: closed", name);
	check(what, (long)sdsize(TMPDEV), held ? blocks : 0);
}

static void
tmp_cases(void)
{
	const dev_t psram = makedev(PSRAM_MAJOR, 0);
	unsigned n = 2 * TMP_MIN, r = 65536;
	int err;

	init_sectors = 1U << 22;
	fill_card();

	/* Slot 4 holds the entry: the PSRAM gives up its /tmp region. */
	set_part(3, PTYPE_BSDFFS, r, n);
	set_part(4, PTYPE_TMP, r + n, n);
	tmp_attach("tmp slot 4, psram", psram, 1);
	check("tmp slot 4, psram: swapdev kept", (long)swapdev, (long)psram);
	check("tmp slot 4, psram: card closed afterwards",
	    (long)sdsize(makedev(SD_BMAJOR, 0)), 0);
	err = sdopen(0, 0, S_IFBLK);
	check("tmp slot 4: the card opens for the node", err, 0);
	sdclose(0, 0, S_IFBLK);
	tmp_names("tmp slot 4", 4, TMP_MIN, 0);

	/* The entry decides alone: swap on fl1 leaves the PSRAM split alone. */
	tmp_attach("tmp slot 4, fl1", FL1, 0);
	check("tmp slot 4, fl1: swapdev kept", (long)swapdev, (long)FL1);
	tmp_names("tmp slot 4, fl1", 4, TMP_MIN, 0);

	/* The card's own swap partition and the tmp partition together. */
	set_part(1, PTYPE_SWAP, 4096, 2 * 8192);
	tmp_attach("tmp and b8, fl1", FL1, 0);
	check("tmp and b8, fl1: swap on the card", (long)swapdev,
	    (long)makedev(SD_BMAJOR, 1));
	tmp_names("tmp and b8, fl1", 4, TMP_MIN, 1);
	swap_release();
	set_part(1, PTYPE_BSDFFS, 8, 33);

	/* A bare Pico lacks a socket: nothing is read or selected. */
	test_board = BOARD_ID_PICO;
	tmp_attach("tmp, bare Pico", psram, 0);
	check("tmp, bare Pico: card not read", (long)nread[0], 0);
	test_board = BOARD_ID_PICOCALC;

	/* No entry, or an unreadable table: the PSRAM keeps its split. */
	set_part(4, PTYPE_UNUSED, 0, 0);
	tmp_attach("no tmp entry", psram, 0);
	check("no tmp entry: node refused", sdopen(TMPDEV, 0, S_IFBLK), ENXIO);
	close_all();
	set_part(4, PTYPE_TMP, r + n, n);
	fail_sector = 0;
	tmp_attach("unreadable table, psram", psram, 0);
	fail_sector = -1;
	init_fail = 1;
	tmp_attach("no card, psram", psram, 0);
	init_fail = 0;

	/* One kilobyte short of the PSRAM region it replaces: passed over. */
	set_part(4, PTYPE_TMP, r + n, n - 2);
	tmp_attach("short tmp", psram, 0);
	check("short tmp: node refused", sdopen(TMPDEV, 0, S_IFBLK), ENXIO);
	close_all();
	set_part(4, PTYPE_TMP, r + n, n);
	tmp_attach("minimum tmp", psram, 1);

	/* Other types in the entry's place are not tmp. */
	set_part(4, PTYPE_SWAP, r + n, n);
	tmp_attach("b8 only, psram", psram, 0);
	set_part(4, DUMP_PART_TYPE, r + n, n);
	tmp_attach("b9 only, psram", psram, 0);
	set_part(4, PTYPE_BSDFFS, r + n, n);
	tmp_attach("b7 only, psram", psram, 0);
	set_part(4, 0x83, r + n, n);
	tmp_attach("83 only, psram", psram, 0);

	/*
	 * An entry that shares a sector with another one is passed over, since
	 * mkfs on it would destroy the neighbor: over the root's end, over its
	 * start, inside it; touching it is not an overlap.
	 */
	set_part(4, PTYPE_TMP, r + n - 1, n);
	tmp_attach("over the root's end", psram, 0);
	set_part(4, PTYPE_TMP, r - n + 1, n);
	tmp_attach("over the root's start", psram, 0);
	set_part(4, PTYPE_TMP, r + 2, n);
	tmp_attach("inside the root", psram, 0);
	set_part(4, PTYPE_TMP, r - n, n);
	tmp_attach("before the root", psram, 1);
	tmp_names("before the root", 4, TMP_MIN, 0);

	/* Two usable entries: the first one, whichever slot. */
	set_part(1, PTYPE_TMP, r + 2 * n, n + 2 * 1024);
	tmp_attach("two tmp", psram, 1);
	tmp_names("two tmp", 1, TMP_MIN + 1024, 0);
	/* An unusable first entry does not hide a usable one after it. */
	set_part(1, PTYPE_TMP, r + 2 * n, n - 2);
	tmp_attach("short then usable tmp", psram, 1);
	tmp_names("short then usable tmp", 4, TMP_MIN, 0);
	/* Two entries over one extent: neither is taken. */
	set_part(1, PTYPE_TMP, r - n, n);
	tmp_attach("two tmp on one extent", psram, 0);
	check("two tmp on one extent: node refused",
	    sdopen(TMPDEV, 0, S_IFBLK), ENXIO);
	close_all();

	/* An entry past the card's end is not an entry. */
	set_part(1, PTYPE_BSDFFS, 8, 33);
	set_part(4, PTYPE_TMP, init_sectors - 16, n);
	tmp_attach("past the card", psram, 0);

	/* Minors 6 and 7 of the unit and the second unit's minor 5 name zero partitions. */
	set_part(4, PTYPE_TMP, r + n, n);
	check("minor 6", sdopen(makedev(SD_BMAJOR, 6), 0, S_IFBLK), ENXIO);
	check("minor 7", sdopen(makedev(SD_BMAJOR, 7), 0, S_IFBLK), ENXIO);
	check("unit 1 minor 5", sdopen(makedev(SD_BMAJOR, 13), 0, S_IFBLK),
	    ENXIO);
	close_all();

	swap_release();
	init_sectors = CARD_SECTORS;
	swapdev = FL1;
}

#ifdef SDSWAP_ENABLED
#include <sys/conf.h>
#include <sys/systm.h>

/*
 * The fixed swap device (fl1, major 0) that sdswap_fallback() returns swap
 * to once the card that held it left.
 */
char swap_removable;
static daddr_t fl_blocks = 384;

static int
fl_open(dev_t dev __attribute__((unused)), int flag __attribute__((unused)),
    int mode __attribute__((unused)))
{
	return 0;
}

static daddr_t
fl_psize(dev_t dev __attribute__((unused)))
{
	return fl_blocks;
}

const struct bdevsw bdevsw[SD_BMAJOR + 1] = { [0] = { fl_open, fl_psize } };

/*
 * Swap returns to fl1 after the card that held it left (sdswap_fallback()).
 * main()'s open of the swap partition is the hold's own, and nothing closes
 * it, so the fallback releases the card: a replacement then opens and its
 * table is read, rather than EIO from the replaced flag of a card the
 * driver still counts as open. A block descriptor that is still open on
 * the old card, on the swap partition itself or on another one, keeps the
 * replacement refused until it closes, since block opens are not counted.
 */
static void
fallback_case(const char *name, int other)
{
	char what[96];
	dev_t held;

	fill_card();
	set_part(4, PTYPE_SWAP, 4096, 2 * 8192);
	swap_removable = 0;
	swapdev = FL1;
	sdswapattach();
	held = swapdev;
	snprintf(what, sizeof(what), "%s: swap on sd0d", name);
	check(what, (long)held, (long)makedev(SD_BMAJOR, 4));
	snprintf(what, sizeof(what), "%s: removable", name);
	check(what, swap_removable, 1);
	snprintf(what, sizeof(what), "%s: open by main", name);
	check(what, sdopen(held, FREAD | FWRITE, S_IFBLK), 0);
	if (other != 0) {
		snprintf(what, sizeof(what), "%s: descriptor opens", name);
		check(what, sdopen(makedev(SD_BMAJOR, other), 0, S_IFBLK), 0);
	}
	card_swap();
	snprintf(what, sizeof(what), "%s: size of fl1", name);
	check(what, (long)sdswap_fallback(), 384);
	snprintf(what, sizeof(what), "%s: swapdev is fl1", name);
	check(what, (long)swapdev, (long)FL1);
	nread[0] = 0;
	if (other != 0) {
		snprintf(what, sizeof(what), "%s: replacement refused", name);
		check(what, sdopen(makedev(SD_BMAJOR, 2), 0, S_IFBLK), EIO);
		snprintf(what, sizeof(what), "%s: old descriptor still open", name);
		check(what, (long)sdsize(makedev(SD_BMAJOR, other)) != 0, 1);
		sdclose(makedev(SD_BMAJOR, other), 0, S_IFBLK);
	}
	snprintf(what, sizeof(what), "%s: card released", name);
	check(what, (long)sdsize(makedev(SD_BMAJOR, 0)), 0);
	set_pin(0);
	snprintf(what, sizeof(what), "%s: replacement opens", name);
	check(what, sdopen(makedev(SD_BMAJOR, 2), 0, S_IFBLK), 0);
	snprintf(what, sizeof(what), "%s: replacement table read", name);
	check(what, nread[0] != 0, 1);
	sdclose(makedev(SD_BMAJOR, 2), 0, S_IFBLK);
}

static void
fallback_cases(void)
{
	init_sectors = 1U << 22;
	fallback_case("fallback", 0);
	fallback_case("fallback, swap partition open", 4);
	fallback_case("fallback, sd0a mounted", 1);
	swap_release();
	init_sectors = CARD_SECTORS;
	swapdev = FL1;
}
#endif

int
main(void)
{
	struct req r;

	/*
	 * Autoconfiguration. A bare Pico lacks a card slot, so the probe
	 * declines before it reaches a register; a PicoCalc attaches.
	 */
	test_board = BOARD_ID_PICO;
	check("probe on a bare Pico", sddriver.probe(NULL), 0);
	check("registers reached by the declined probe", model_n, 0);
	test_board = BOARD_ID_PICOCALC;
	check("probe on a PicoCalc", sddriver.probe(NULL), 1);

	mbr_byte_cases();
	root_cases();
	swapdev_cases();
	tmp_cases();
#ifdef SDSWAP_ENABLED
	fallback_cases();
#endif

	fill_card();

	/* The whole card, raw: the 512-byte transfers fdisk makes. */
	check("open whole card", sdopen(0, 0, S_IFCHR), 0);
	check("open partition 1", sdopen(1, 0, S_IFCHR), 0);
	check("open partition 2", sdopen(2, 0, S_IFCHR), 0);
	check("partition 3 is empty", sdopen(3, 0, S_IFCHR), ENXIO);
	check("unit 1", sdopen(8, 0, S_IFCHR), ENXIO);
	check("size of partition 1", (long)sdsize(1), 16);
	check("size of partition 2", (long)sdsize(2), 10);

	r = (struct req){ 0, B_READ, 0, 512 };
	read_case("mbr read", r, 0, 1, 0);
	r = (struct req){ 0, B_READ, 3, 1536 };
	read_case("three sectors", r, 6, 3, 0);
	r = (struct req){ 0, B_WRITE, 0, 512 };
	write_case("mbr write", r, 0, 1, 0);
	r = (struct req){ 0, B_WRITE, 5, 1024 };
	write_case("block write", r, 10, 2, 0);

	/* Partition offsets and multi-block transfers. */
	r = (struct req){ 1, B_READ, 2, 1024 };
	read_case("partition 1 block", r, 8 + 4, 2, 0);
	r = (struct req){ 1, B_READ, 1, 4096 };
	read_case("partition 1 four blocks", r, 8 + 2, 8, 0);
	r = (struct req){ 2, B_READ, 0, 512 };
	read_case("partition 2 first sector", r, 41, 1, 0);

	/* A request across the partition end moves what fits. */
	r = (struct req){ 2, B_READ, 9, 2048 };
	read_case("read across the end", r, 41 + 18, 2, 1024);
	r = (struct req){ 2, B_WRITE, 9, 2048 };
	write_case("write across the end", r, 41 + 18, 2, 1024);
	r = (struct req){ 2, B_READ, 8, 4096 };
	read_case("read three blocks short", r, 41 + 16, 4, 2048);

	/* The odd partition's last block holds one sector. */
	r = (struct req){ 1, B_READ, 16, 1024 };
	read_case("odd tail read", r, 8 + 32, 1, 512);
	r = (struct req){ 1, B_WRITE, 16, 1024 };
	write_case("odd tail write", r, 8 + 32, 1, 512);
	r = (struct req){ 1, B_READ, 16, 512 };
	read_case("odd tail one sector", r, 8 + 32, 1, 0);

	/* End of file, then past it. */
	r = (struct req){ 2, B_READ, 10, 1024 };
	eof_case("partition 2 end", r);
	r = (struct req){ 1, B_READ, 17, 1024 };
	eof_case("odd partition end", r);
	r = (struct req){ 2, B_READ, 11, 1024 };
	error_case("past the end", r, EINVAL);
	r = (struct req){ 1, B_READ, 18, 512 };
	error_case("past the odd end", r, EINVAL);

	/* Counts that are not whole sectors. */
	r = (struct req){ 0, B_READ, 0, 513 };
	error_case("513 bytes", r, EINVAL);
	r = (struct req){ 0, B_WRITE, 0, 100 };
	error_case("100 bytes", r, EINVAL);
	swapio_cases();

	/*
	 * A card error surfaces as EIO with the failed sector and every sector
	 * after it in b_resid; sectors before it moved, and none after it is
	 * attempted. Partition 1 block 1 starts at card sector 10.
	 */
	r = (struct req){ 1, B_READ, 1, 2048 };
	fail_sector = 10;
	cut_case("read failing at its first sector", r, 10, 0, 2048);
	fail_sector = 11;
	cut_case("read failing at its second sector", r, 10, 1, 1536);
	fail_sector = 13;
	cut_case("read failing at its last sector", r, 10, 3, 512);
	r = (struct req){ 1, B_WRITE, 1, 2048 };
	fail_sector = 11;
	cut_case("write failing at its second sector", r, 10, 1, 1536);
	/* The truncated tail stays in the residual on top of the failure. */
	r = (struct req){ 2, B_READ, 9, 2048 };
	fail_sector = 60;
	cut_case("truncated read failing at its second sector", r, 59, 1, 1536);
	fail_sector = 59;
	cut_case("truncated read failing at its first sector", r, 59, 0, 2048);
	r = (struct req){ 2, B_WRITE, 9, 2048 };
	fail_sector = 60;
	cut_case("truncated write failing at its second sector", r, 59, 1, 1536);
	fail_sector = -1;

	raw_cases();
	raw_fail_cases();
	swap_cases();

	/*
	 * DIOCREINIT replaces the table only when sector 0 reads: a failed
	 * refresh keeps every open partition's mapping, a good one adopts the
	 * rewritten table.
	 */
	fail_sector = 0;
	check("reinit with a failing sector 0", sdioctl(0, DIOCREINIT, NULL, 0),
	    EIO);
	fail_sector = -1;
	check("size of partition 1 after the failed reinit", (long)sdsize(1),
	    16);
	check("size of partition 2 after the failed reinit", (long)sdsize(2),
	    10);
	r = (struct req){ 1, B_READ, 0, 512 };
	read_case("partition 1 block 0 after the failed reinit", r, 8, 1, 0);
	r = (struct req){ 1, B_READ, 16, 1024 };
	read_case("partition 1 odd tail after the failed reinit", r, 8 + 32, 1,
	    512);
	card[0][446 + 12] = 8;		/* dp_nsectors of entry 1: 33 -> 8. */
	card[0][446 + 13] = 0;

	/*
	 * A block open (a mount, or a descriptor on sd0b) holds the mapping its
	 * buffers were read through, so the refresh waits for the last block
	 * close. A raw open, fdisk's own, does not hold it.
	 */
	check("block open of partition 2", sdopen(2, 0, S_IFBLK), 0);
	memset(nread, 0, sizeof(nread));
	check("reinit while a block partition is held",
	    sdioctl(0, DIOCREINIT, NULL, 0), EBUSY);
	check("reinit while held: sector 0 reads", nread[0], 0);
	check("reinit while held: size of partition 1", (long)sdsize(1), 16);
	sdclose(2, 0, S_IFBLK);
	check("reinit after the table was rewritten",
	    sdioctl(0, DIOCREINIT, NULL, 0), 0);
	check("size of partition 1 after the rewrite", (long)sdsize(1), 4);
	check("size of partition 2 after the rewrite", (long)sdsize(2), 10);
	memcpy(card, saved, sizeof(card));
	check("reinit with the original table", sdioctl(0, DIOCREINIT, NULL, 0),
	    0);
	check("size of partition 1 restored", (long)sdsize(1), 16);

	/* A partition nobody opened. */
	r = (struct req){ 3, B_READ, 0, 512 };
	error_case("unopened partition", r, ENXIO);

	/*
	 * DIOCGETPART reports the extents blkoverlap() in conf.c compares: the
	 * whole card for node 0, the table's entries for 1 and 2, nothing for an
	 * unused entry, and ENXIO for a node the driver does not have.
	 */
	{
		struct diskpart dp;

		check("getpart whole card", sdioctl(0, DIOCGETPART, (caddr_t)&dp,
		    0), 0);
		check("whole card offset", dp.dp_offset, 0);
		check("whole card sectors", dp.dp_nsectors, CARD_SECTORS);
		check("getpart partition 1", sdioctl(1, DIOCGETPART, (caddr_t)&dp,
		    0), 0);
		check("partition 1 offset", dp.dp_offset, 8);
		check("partition 1 sectors", dp.dp_nsectors, 33);
		check("getpart partition 2", sdioctl(2, DIOCGETPART, (caddr_t)&dp,
		    0), 0);
		check("partition 2 offset", dp.dp_offset, 41);
		check("partition 2 sectors", dp.dp_nsectors, 20);
		check("getpart unused entry", sdioctl(3, DIOCGETPART, (caddr_t)&dp,
		    0), 0);
		check("unused entry sectors", dp.dp_nsectors, 0);
		check("getpart beyond the table", sdioctl(5, DIOCGETPART,
		    (caddr_t)&dp, 0), ENXIO);
		check("getpart unit 1", sdioctl(8, DIOCGETPART, (caddr_t)&dp, 0),
		    ENXIO);
	}

	/*
	 * DIOCMEDIAGONE answers the removal flag: no while the card sits where
	 * it was accepted, yes once the pin change has held the interval.
	 */
	{
		int gone = -1;

		check("media gone before the swap", sdioctl(1, DIOCMEDIAGONE,
		    (caddr_t)&gone, 0), 0);
		check("media present", gone, 0);
		card_swap();
		gone = -1;
		check("media gone after the swap", sdioctl(1, DIOCMEDIAGONE,
		    (caddr_t)&gone, 0), 0);
		check("media removed", gone, 1);
		gone = -1;
		check("media gone, any partition", sdioctl(2, DIOCMEDIAGONE,
		    (caddr_t)&gone, 0), 0);
		check("media removed, partition 2", gone, 1);
		check("media gone, unit 1", sdioctl(8, DIOCMEDIAGONE,
		    (caddr_t)&gone, 0), ENXIO);
		/* The flag clears with the last close, as the card is accepted. */
		close_all();
		set_pin(0);
		check("open after the removal", sdopen(1, 0, S_IFCHR), 0);
		gone = -1;
		check("media gone after the reopen", sdioctl(1, DIOCMEDIAGONE,
		    (caddr_t)&gone, 0), 0);
		check("media present after the reopen", gone, 0);
		check("reopen partition 2", sdopen(2, 0, S_IFCHR), 0);
		check("reopen whole card", sdopen(0, 0, S_IFCHR), 0);
	}

	/* A card swap fails every transfer until the last close. */
	card_swap();
	r = (struct req){ 0, B_READ, 0, 512 };
	error_case("card replaced", r, EIO);
	/* The replacement outranks every other verdict, end of file included. */
	r = (struct req){ 0, B_READ, 0, 513 };
	error_case("card replaced, odd count", r, EIO);
	r = (struct req){ 2, B_READ, 10, 1024 };
	error_case("card replaced, end of file", r, EIO);
	check("reopen while the old card is held", sdopen(1, 0, S_IFCHR), EIO);

	close_all();
	check("size after the last close", (long)sdsize(1), 0);

	detect_cases();
	close_all();
	dump_cases();
	check("interrupt mask released", shim_spl, 0);
	check("card commands run with interrupts enabled", bad_spl, 0);

	if (failures != 0) {
		fprintf(stderr, "sdstrat_test: %d failures\n", failures);
		return 1;
	}
	printf("sdstrat_test: ok\n");
	return 0;
}
