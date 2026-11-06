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
 * saved, so every outcome checks the residual as well as the data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "regmodel.h"

#include <rp2040/dev/sd.h>
#include <rp2040/dev/sdspi.h>

#define	CARD_SECTORS	64
#define	STALE		0xdead
#define	GUARD		0xee

static unsigned char card[CARD_SECTORS][SD_SECTOR];
static unsigned char saved[CARD_SECTORS][SD_SECTOR];
static unsigned nread[CARD_SECTORS], nwrite[CARD_SECTORS];
static int fail_sector = -1;
static struct buf ebuf;
static unsigned char ebuf_data[DEV_BSIZE];
static int failures;

int
sdspi_init(struct sdspi *sd)
{
	sd->type = SD_HC;
	sd->nsectors = CARD_SECTORS;
	return SDE_OK;
}

int
sdspi_read(struct sdspi *sd __attribute__((unused)), unsigned sector,
    unsigned char *buf)
{
	if (sector >= CARD_SECTORS || (int)sector == fail_sector)
		return SDE_IO;
	nread[sector]++;
	memcpy(buf, card[sector], SD_SECTOR);
	return SDE_OK;
}

int
sdspi_write(struct sdspi *sd __attribute__((unused)), unsigned sector,
    const unsigned char *buf)
{
	if (sector >= CARD_SECTORS || (int)sector == fail_sector)
		return SDE_IO;
	nwrite[sector]++;
	memcpy(card[sector], buf, SD_SECTOR);
	return SDE_OK;
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
			iov->iov_base += c;
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

static void
check(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
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

int
main(void)
{
	struct req r;
	struct buf b;
	int part;

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

	/* A card error surfaces as EIO. */
	fail_sector = 11;
	r = (struct req){ 1, B_READ, 1, 2048 };
	error_case("card error", r, EIO);
	fail_sector = -1;

	raw_cases();

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

	/* A card swap fails every transfer until the last close. */
	model_reg(0xd0000004UL) = 1UL << 22;
	sd_detect_poll();
	model_reg(0x40054028UL) = 60000;
	sd_detect_poll();
	r = (struct req){ 0, B_READ, 0, 512 };
	error_case("card replaced", r, EIO);
	check("reopen while the old card is held", sdopen(1, 0, S_IFCHR), EIO);

	for (part = 0; part < 3; part++)
		sdclose((dev_t)part, 0, S_IFCHR);
	check("size after the last close", (long)sdsize(1), 0);
	memset(&b, 0, sizeof(b));

	if (failures != 0) {
		fprintf(stderr, "sdstrat_test: %d failures\n", failures);
		return 1;
	}
	printf("sdstrat_test: ok\n");
	return 0;
}
