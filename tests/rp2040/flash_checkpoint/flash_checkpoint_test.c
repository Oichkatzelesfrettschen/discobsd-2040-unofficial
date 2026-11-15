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
 * Host oracle for the flash root's checkpoint policy: a write request is
 * durable once Dhara writes the checkpoint that covers it, either when the
 * write fills a checkpoint group or when DIOCCHECKPOINT forces one, and a
 * reset returns the device to the last checkpoint.
 *
 * The driver text comes from sys/arch/rp2040/dev/flash.c through extract.py
 * (everything above the boot ROM layer: flstrategy, flioctl, flclose,
 * fl_checkpoint, fl_setup and the dhara_nand_* callbacks) and runs over the
 * vendored Dhara map and journal. This file supplies the ROM layer as a NOR
 * model of the whole 2 MiB part: erase sets 4 KiB sectors to 0xff, a program
 * takes whole 256-byte pages and may only clear bits, and programming a byte
 * that is not erased is counted as a fault, because it breaks Dhara's
 * program-once contract. A power cycle keeps the model's bytes, discards the
 * driver's RAM state and opens the device again, which runs fl_setup() and
 * dhara_map_resume().
 *
 * Every scenario prints "FAIL <scenario>: <what>" for an unmet expectation;
 * the program exits 1 when any failed. The model's own program-page and
 * erase-sector counts are the wear measure: they count requested 256-byte
 * pages and 4 KiB sectors, the units of machdep.storage_stats.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef unsigned char	u_char;
typedef unsigned short	u_short;
typedef unsigned int	u_int;
typedef long		daddr_t;
typedef int		dev_t;
typedef char		*caddr_t;

#include "disk.h"
#include "storage_stats.h"
#include <rp2040/dhara/map.h>
#include <rp2040/dev/flash.h>

/* The kernel names the extracted driver uses. */
#define	__unused	__attribute__((unused))
#define	DEV_BSIZE	1024
#define	DEV_BSHIFT	10
#define	btod(x)		(((x) + DEV_BSIZE - 1) >> DEV_BSHIFT)
#define	major(x)	((int)(((int)(x) >> 8) & 0377))
#define	minor(x)	((int)((x) & 0377))
#define	makedev(x, y)	((dev_t)(((x) << 8) | (y)))
#define	NFL		2
#define	SWAP_IMAGE_ALIGN 4
#define	LED_SWAP	0x04
#define	LED_DISK	0x02
#define	B_READ		0x00001
#define	B_DONE		0x00002
#define	B_ERROR		0x00004
#define	FREAD		0x0001		/* sys/fcntl.h */
#define	FWRITE		0x0002

struct buf {
	int	b_flags;
	dev_t	b_dev;
	daddr_t	b_blkno;
	u_int	b_bcount;
	u_int	b_resid;
	caddr_t	b_addr;
	int	b_error;
};

#define	ROOT_DEV	makedev(0, 0)	/* fl0, the whole Dhara device */
#define	SWAP_DEV	makedev(0, 8)	/* fl1, raw swap */

static dev_t swapdev = SWAP_DEV;
static int spl_level;
static unsigned raw_calls;
static uint32_t stats[STORAGE_METRIC_COUNT];

static void
panic(const char *message)
{
	fprintf(stderr, "panic: %s\n", message);
	exit(2);
}

static int
splbio(void)
{
	int s = spl_level;

	spl_level = 1;
	return s;
}

static void
splx(int s)
{
	spl_level = s;
}

static void
clock_catchup(void)
{
}

static void
led_control(int mask, int on)
{
	(void)mask;
	(void)on;
}

static void
biodone(struct buf *bp)
{
	bp->b_flags |= B_DONE;
}

static void
bzero(void *p, size_t n)
{
	memset(p, 0, n);
}

static void
bcopy(const void *s, void *d, size_t n)
{
	memmove(d, s, n);
}

static void
storage_note(enum storage_metric metric, uint32_t amount)
{
	stats[metric] += amount;
}

/* The fl_setup() banner lines are not part of the oracle. */
static int
quiet_printf(const char *format, ...)
{
	(void)format;
	return 0;
}

/* Swap bypasses Dhara; this records that a swap request never reaches it. */
static int
fl_raw(struct buf *bp, u_int off, u_int len)
{
	(void)bp;
	(void)off;
	(void)len;
	raw_calls++;
	return 0;
}

/* The NOR model standing in for the boot ROM routines. */
static u_char nor[FLASH_TOTAL_BYTES];
static unsigned long program_pages, erase_sectors, model_faults;

#undef	FLASH_XIP_BASE
#define	FLASH_XIP_BASE	((uintptr_t)nor)

static void
model_fault(const char *what)
{
	model_faults++;
	printf("model: %s\n", what);
}

static void
flash_rom_init(void)
{
}

/* flash_limit_init() sets the raw swap extent; a plain Pico has all of it. */
static uint32_t flswap_bytes = FLASH_SWAP_BYTES;

/*
 * Both entry points accept only an aligned range inside the root region,
 * [FLASH_FS_OFFSET, FLASH_SWAP_OFFSET). The offset is compared before the
 * subtraction: an offset past the end of the region would wrap the unsigned
 * difference and pass the length test.
 */
static int
flash_erase(u_int offset, u_int len)
{
	if (offset % FLASH_SECTOR_BYTES != 0 || len % FLASH_SECTOR_BYTES != 0 ||
	    offset < FLASH_FS_OFFSET || offset >= FLASH_SWAP_OFFSET ||
	    len > FLASH_SWAP_OFFSET - offset) {
		model_fault("erase outside the root region");
		return 0;
	}
	if (spl_level == 0)
		model_fault("erase without splbio");
	memset(nor + offset, 0xff, len);
	erase_sectors += len / FLASH_SECTOR_BYTES;
	return 0;
}

static int
flash_program(u_int offset, const u_char *data, u_int len)
{
	u_int i;

	if (offset % FLASH_PROG_BYTES != 0 || len % FLASH_PROG_BYTES != 0 ||
	    offset < FLASH_FS_OFFSET || offset >= FLASH_SWAP_OFFSET ||
	    len > FLASH_SWAP_OFFSET - offset) {
		model_fault("program outside the root region");
		return 0;
	}
	if (spl_level == 0)
		model_fault("program without splbio");
	for (i = 0; i < len; i++) {
		if (nor[offset + i] != 0xff) {
			model_fault("program over unerased flash");
			break;
		}
	}
	for (i = 0; i < len; i++)
		nor[offset + i] &= data[i];
	program_pages += len / FLASH_PROG_BYTES;
	return 0;
}

#define	printf	quiet_printf
#include FLASH_CHECKPOINT_DRIVER
#undef	printf

u_char flpage[FLASH_UNIT_BYTES];
u_char flscratch[FLASH_PROG_BYTES];

static int failures;

static void
expect(int condition, const char *scenario, const char *what)
{
	if (!condition) {
		failures++;
		printf("FAIL %s: %s\n", scenario, what);
	}
}

/* Discard every RAM structure of the driver and Dhara, as a reset does. */
static void
power_cycle(const char *scenario)
{
	memset(&flmap, 0, sizeof(flmap));
	flmap_ready = 0;
	flblocks = 0;
	memset(fldrives, 0, sizeof(fldrives));
	memset(flpage, 0, sizeof(flpage));
	memset(flscratch, 0, sizeof(flscratch));
	flscratch_owner = FLS_FREE;
	expect(flopen(ROOT_DEV, 0, 0) == 0, scenario, "open after reset");
}

static void
fresh_chip(const char *scenario)
{
	memset(nor, 0xff, sizeof(nor));
	program_pages = erase_sectors = model_faults = 0;
	raw_calls = 0;
	memset(stats, 0, sizeof(stats));
	power_cycle(scenario);
}

static void
fill(u_char *data, daddr_t blkno, unsigned seed)
{
	u_int i;

	for (i = 0; i < DEV_BSIZE; i++)
		data[i] = (u_char)(seed * 131U + (unsigned)blkno * 7U + i);
}

static int
transfer(dev_t dev, int flags, daddr_t blkno, u_char *data)
{
	struct buf b;

	memset(&b, 0, sizeof(b));
	b.b_flags = flags;
	b.b_dev = dev;
	b.b_blkno = blkno;
	b.b_bcount = DEV_BSIZE;
	b.b_addr = (caddr_t)data;
	flstrategy(&b);
	return (b.b_flags & B_ERROR) != 0 ? b.b_error : 0;
}

static void
write_block(const char *scenario, daddr_t blkno, unsigned seed)
{
	u_char data[DEV_BSIZE];

	fill(data, blkno, seed);
	expect(transfer(ROOT_DEV, 0, blkno, data) == 0, scenario, "write");
}

/* The block holds the pattern of seed, or reads blank for seed 0. */
static int
block_is(daddr_t blkno, unsigned seed)
{
	u_char want[DEV_BSIZE], got[DEV_BSIZE];

	if (transfer(ROOT_DEV, B_READ, blkno, got) != 0)
		return 0;
	if (seed == 0)
		memset(want, 0xff, sizeof(want));
	else
		fill(want, blkno, seed);
	return memcmp(want, got, sizeof(want)) == 0;
}

/* The kernel's checkpoint request, as ufs_sync() issues it. */
static int
checkpoint(void)
{
	int status = -1;

	if (flioctl(ROOT_DEV, DIOCCHECKPOINT, (caddr_t)&status, 0) != 0)
		return -1;
	return status;
}

static unsigned long
groups(unsigned long writes)
{
	return (writes + 6) / 7;
}

/* a: a write the last checkpoint does not cover is gone after a reset. */
static void
test_reset_before_checkpoint(void)
{
	const char *t = "reset-before-checkpoint";

	fresh_chip(t);
	write_block(t, 5, 1);
	expect(checkpoint() == 0, t, "first checkpoint");
	write_block(t, 5, 2);
	expect(block_is(5, 2), t, "the new contents read back before reset");
	power_cycle(t);
	expect(block_is(5, 1), t, "the checkpointed contents after reset");
	/* The resumed map writes, checkpoints and resumes again. */
	write_block(t, 5, 3);
	write_block(t, 6, 3);
	expect(checkpoint() == 0, t, "checkpoint after resume");
	power_cycle(t);
	expect(block_is(5, 3) && block_is(6, 3), t, "writes after resume");
	expect(model_faults == 0, t, "NOR contract kept");
}

/* b: the forced checkpoint makes a single write durable. */
static void
test_checkpoint_then_reset(void)
{
	const char *t = "checkpoint-then-reset";

	fresh_chip(t);
	write_block(t, 9, 1);
	expect(checkpoint() == 0, t, "checkpoint");
	power_cycle(t);
	expect(block_is(9, 1), t, "the write survives the reset");
	/* The driver's last close forces one too. */
	write_block(t, 10, 2);
	expect(flclose(ROOT_DEV, 0, 0) == 0, t, "close");
	power_cycle(t);
	expect(block_is(10, 2), t, "the write before close survives");
	expect(model_faults == 0, t, "NOR contract kept");
}

/*
 * c: from a checkpointed state ten writes fill one seven-page group, whose
 * metadata page makes it durable without a forced checkpoint; the three
 * writes of the open group are lost.
 */
static void
test_group_fill(void)
{
	const char *t = "group-fill";
	uint32_t attempts;
	daddr_t b;

	fresh_chip(t);
	for (b = 100; b < 110; b++)
		write_block(t, b, 1);
	expect(checkpoint() == 0, t, "checkpoint of the old contents");
	attempts = stats[STORAGE_CHECKPOINT_ATTEMPTS];
	for (b = 100; b < 110; b++)
		write_block(t, b, 2);
	expect(stats[STORAGE_CHECKPOINT_ATTEMPTS] == attempts, t,
	    "a group fill is not a counted checkpoint attempt");
	power_cycle(t);
	for (b = 100; b < 107; b++)
		expect(block_is(b, 2), t, "the filled group is durable");
	for (b = 107; b < 110; b++)
		expect(block_is(b, 1), t, "the open group is not");
	expect(model_faults == 0, t, "NOR contract kept");
}

/*
 * d: the A04 case. The write has completed, so nothing above the driver is
 * dirty; reads change nothing; the checkpoint alone, with no further data
 * write, makes the pending map change durable.
 */
static void
test_flush_without_write(void)
{
	const char *t = "flush-without-write";
	unsigned long pages, erases;
	u_char data[DEV_BSIZE];

	fresh_chip(t);
	write_block(t, 20, 1);
	expect(checkpoint() == 0, t, "first checkpoint");
	write_block(t, 20, 2);
	pages = program_pages;
	erases = erase_sectors;
	expect(block_is(20, 2), t, "read");
	expect(transfer(ROOT_DEV, B_READ, 21, data) == 0, t, "read unmapped");
	expect(program_pages == pages && erase_sectors == erases, t,
	    "reads never program or erase");
	expect(!dhara_journal_is_clean(&flmap.journal), t,
	    "the map change is pending");
	expect(checkpoint() == 0, t, "checkpoint");
	expect(program_pages > pages, t, "the checkpoint programs the group");
	expect(dhara_journal_is_clean(&flmap.journal), t, "nothing pending");
	power_cycle(t);
	expect(block_is(20, 2), t, "the pending change is durable");
	expect(model_faults == 0, t, "NOR contract kept");
}

/*
 * e: wear. From an empty journal, below the auto_gc() threshold, a forced
 * checkpoint pads the open group: each group is eight Dhara pages of four
 * program pages, in one 8 KiB block of two sectors. N writes with one
 * checkpoint cost 32 * ceil(N / 7) program pages and 2 * ceil(N / 7)
 * sectors; N writes each followed by a checkpoint cost 32 * N and 2 * N.
 */
static void
test_wear(void)
{
	const char *t = "wear";
	unsigned long n, i;
	char what[96];

	for (n = 1; n <= 21; n++) {
		fresh_chip(t);
		for (i = 0; i < n; i++)
			write_block(t, (daddr_t)(200 + i), 1);
		expect(checkpoint() == 0, t, "checkpoint");
		snprintf(what, sizeof(what),
		    "N=%lu batched: %lu pages %lu sectors, expected %lu %lu", n,
		    program_pages, erase_sectors, 32 * groups(n), 2 * groups(n));
		expect(program_pages == 32 * groups(n) &&
		    erase_sectors == 2 * groups(n), t, what);
	}
	fresh_chip(t);
	for (i = 0; i < 20; i++) {
		write_block(t, (daddr_t)(200 + i), 1);
		expect(checkpoint() == 0, t, "checkpoint");
	}
	snprintf(what, sizeof(what),
	    "20 isolated: %lu pages %lu sectors, expected 640 40",
	    program_pages, erase_sectors);
	expect(program_pages == 640 && erase_sectors == 40, t, what);
	printf("wear: 20 writes, a checkpoint after each: %lu program pages, "
	    "%lu erase sectors\n", program_pages, erase_sectors);
	fresh_chip(t);
	for (i = 0; i < 20; i++)
		write_block(t, (daddr_t)(200 + i), 1);
	expect(checkpoint() == 0, t, "checkpoint");
	printf("wear: 20 writes, one checkpoint: %lu program pages, "
	    "%lu erase sectors\n", program_pages, erase_sectors);
	expect(model_faults == 0, t, "NOR contract kept");
}

/*
 * A map with every sector live holds the journal at the auto_gc()
 * threshold: each write first copies live tail pages. The shipped image
 * leaves its free blocks unmapped and starts below that state. The trace
 * of check-dhara-amplification (trace d) is repeated through the driver
 * and printed, not asserted: fill the capacity, rewrite the 20 most recent
 * sectors until three capacities are written, then measure 140 writes each
 * way.
 */
static void
measure_full(unsigned period, unsigned long *pages, unsigned long *sectors)
{
	const char *t = "full-occupancy";
	const daddr_t cap = flblocks;
	unsigned long written = 0, i;
	unsigned long p0, s0;

	for (i = 0; i < (unsigned long)cap; i++) {
		write_block(t, (daddr_t)i, 1);
		if (++written % 20 == 0)
			expect(checkpoint() == 0, t, "fill checkpoint");
	}
	expect(checkpoint() == 0, t, "fill checkpoint");
	for (i = 0; written < 3 * (unsigned long)cap; i++, written++) {
		write_block(t, cap - 20 + (daddr_t)(i % 20), 2);
		if (written % period == 0)
			expect(checkpoint() == 0, t, "warm checkpoint");
	}
	expect(checkpoint() == 0, t, "warm checkpoint");
	p0 = program_pages;
	s0 = erase_sectors;
	for (i = 0; i < 140; i++) {
		write_block(t, cap - 20 + (daddr_t)(i % 20), 3);
		if ((i + 1) % period == 0)
			expect(checkpoint() == 0, t, "measured checkpoint");
	}
	*pages = program_pages - p0;
	*sectors = erase_sectors - s0;
	expect(block_is(cap - 1, 3) && block_is(0, 1), t, "contents");
	expect(model_faults == 0, t, "NOR contract kept");
}

static void
test_full_occupancy(void)
{
	unsigned long pages, sectors;

	fresh_chip("full-occupancy");
	measure_full(1, &pages, &sectors);
	printf("full occupancy: 140 writes, a checkpoint after each: %lu program "
	    "pages (%.2f Dhara pages per write), %lu erase sectors\n", pages,
	    pages / 4.0 / 140, sectors);
	fresh_chip("full-occupancy");
	measure_full(20, &pages, &sectors);
	printf("full occupancy: 140 writes, a checkpoint per 20: %lu program "
	    "pages (%.2f Dhara pages per write), %lu erase sectors\n", pages,
	    pages / 4.0 / 140, sectors);
}

/* f: a checkpoint with nothing pending programs nothing; swap stays raw. */
static void
test_idle_checkpoint(void)
{
	const char *t = "idle-checkpoint";
	unsigned long pages, erases;
	uint32_t attempts, successes;
	u_char data[DEV_BSIZE];
	int status = -1;

	fresh_chip(t);
	write_block(t, 30, 1);
	expect(checkpoint() == 0, t, "checkpoint");
	pages = program_pages;
	erases = erase_sectors;
	attempts = stats[STORAGE_CHECKPOINT_ATTEMPTS];
	successes = stats[STORAGE_CHECKPOINT_SUCCESSES];
	expect(checkpoint() == 0, t, "idle checkpoint");
	expect(program_pages == pages && erase_sectors == erases, t,
	    "an idle checkpoint programs and erases nothing");
	expect(stats[STORAGE_CHECKPOINT_ATTEMPTS] == attempts + 1 &&
	    stats[STORAGE_CHECKPOINT_SUCCESSES] == successes + 1, t,
	    "the forced checkpoint is counted");
	fill(data, 0, 4);
	expect(transfer(SWAP_DEV, 0, 8, data) == 0 && raw_calls == 1, t,
	    "a swap write takes the raw path");
	expect(program_pages == pages && erase_sectors == erases &&
	    stats[STORAGE_CHECKPOINT_ATTEMPTS] == attempts + 1, t,
	    "a swap write touches neither Dhara nor a checkpoint");
	expect(flioctl(SWAP_DEV, DIOCCHECKPOINT, (caddr_t)&status, 0) == EINVAL,
	    t, "the swap unit has no checkpoint");
	/* ioctl(2) passes the descriptor's flags; reading alone is refused. */
	expect(flioctl(ROOT_DEV, DIOCCHECKPOINT, (caddr_t)&status, FREAD) ==
	    EBADF, t, "a read-only descriptor cannot force a checkpoint");
	status = -1;
	expect(flioctl(ROOT_DEV, DIOCCHECKPOINT, (caddr_t)&status,
	    FREAD | FWRITE) == 0 && status == 0, t,
	    "a writable descriptor can force a checkpoint");
	/* The last close follows the same terms as the ioctl. */
	write_block(t, 31, 2);
	attempts = stats[STORAGE_CHECKPOINT_ATTEMPTS];
	pages = program_pages;
	expect(flclose(ROOT_DEV, FREAD, 0) == 0 &&
	    flclose(SWAP_DEV, FREAD | FWRITE, 0) == 0, t,
	    "read-only and swap closes");
	expect(stats[STORAGE_CHECKPOINT_ATTEMPTS] == attempts &&
	    program_pages == pages, t,
	    "a read-only close and a swap close force no checkpoint");
	expect(flclose(ROOT_DEV, FREAD | FWRITE, 0) == 0 &&
	    stats[STORAGE_CHECKPOINT_ATTEMPTS] == attempts + 1, t,
	    "a writable close forces one");
	expect(model_faults == 0, t, "NOR contract kept");
}

/*
 * The model must report an erase or a program aimed into the raw swap region
 * as a fault and leave the chip untouched. The swap bytes start as 0xa5, which
 * an erase would clear and a program over unerased cells would also fault.
 */
static void
test_model_range(void)
{
	static const u_int offsets[] = {
		FLASH_SWAP_OFFSET, FLASH_SWAP_OFFSET + FLASH_SECTOR_BYTES,
		FLASH_TOTAL_BYTES - FLASH_SECTOR_BYTES
	};
	static const u_char page[FLASH_PROG_BYTES];
	static u_char copy[FLASH_TOTAL_BYTES - FLASH_SWAP_OFFSET];
	const char *t = "model-range";
	unsigned i;

	fresh_chip(t);
	memset(nor + FLASH_SWAP_OFFSET, 0xa5, sizeof(copy));
	memcpy(copy, nor + FLASH_SWAP_OFFSET, sizeof(copy));
	for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
		spl_level = 1;
		flash_erase(offsets[i], FLASH_SECTOR_BYTES);
		flash_program(offsets[i], page, FLASH_PROG_BYTES);
		spl_level = 0;
	}
	expect(model_faults == 2 * (sizeof(offsets) / sizeof(offsets[0])), t,
	    "every erase and program into the swap region is a model fault");
	expect(erase_sectors == 0 && program_pages == 0, t,
	    "a refused range is neither erased nor programmed");
	expect(memcmp(copy, nor + FLASH_SWAP_OFFSET, sizeof(copy)) == 0, t,
	    "the swap region is untouched");
	/* The last root sector is still accepted. */
	model_faults = 0;
	spl_level = 1;
	flash_erase(FLASH_SWAP_OFFSET - FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES);
	spl_level = 0;
	expect(model_faults == 0 && erase_sectors == 1, t,
	    "the last sector of the root region is accepted");
}

int
main(void)
{
	test_model_range();
	test_reset_before_checkpoint();
	test_checkpoint_then_reset();
	test_group_fill();
	test_flush_without_write();
	test_wear();
	test_idle_checkpoint();
	test_full_occupancy();
	if (failures != 0) {
		printf("flash_checkpoint: %d expectations failed\n", failures);
		return 1;
	}
	printf("flash_checkpoint: all expectations met\n");
	return 0;
}
