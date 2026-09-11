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
 * Root filesystem on the RP2040's QSPI flash.
 *
 * DiscoBSD's other targets boot from an SD card, and this board has no
 * socket, so the onboard flash becomes the block device instead. The idea of
 * putting a Unix root on a Pico's flash behind a wear-leveling translation
 * layer is FUZIX's; none of its code is used here, because the FUZIX kernel
 * is GPL-2.0 and this tree is not. Its implementation was deliberately not
 * read. What both need is the same published library, Dhara, which is ISC
 * licensed and vendored under sys/arch/rp2040/dhara.
 *
 * The layering is:
 *
 *	bdevsw			block requests in DEV_BSIZE units
 *	  dhara_map_*		logical sectors, wear leveling, garbage
 *				collection			(ISC, vendored)
 *	    dhara_nand_*	this file: NOR geometry and erase or program
 *	      bootrom		flash_range_erase, flash_range_program
 *
 * Dhara is written for NAND, and this part is NOR, which costs nothing and
 * saves something. Its contract wants pages programmed sequentially within an
 * eraseblock and never reprogrammed, which NOR satisfies. NOR has no factory
 * bad blocks and no ECC, so the bad-block callbacks below are honest
 * constants rather than stubs awaiting work.
 *
 * The RP2040 constraint that shapes everything here: erasing or programming
 * flash takes the QSPI interface out of execute-in-place, so code driving one
 * cannot be fetched from flash while it runs. Those functions carry
 * __ramfunc, which conf/kern.ldscript places in RAM. Reads go through the XIP
 * window normally, and only while no erase or program is in flight.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/dk.h>
#include <sys/disk.h>

#include <machine/intr.h>

#define	__unused	__attribute__((__unused__))	/* XXX as in conf.c */

#include <rp2040/dev/flash.h>
#include <rp2040/dhara/map.h>

/*
 * Bootrom entry points. The RP2040 boot ROM publishes a function table whose
 * entries are found by a two-character code; see the RP2040 datasheet section
 * 2.8.3. Calling the ROM keeps this driver self-contained, since the flash
 * routines in the Pico SDK are themselves thin wrappers over these.
 */
#define	ROM_FUNC_TABLE_ADDR	0x14
#define	ROM_TABLE_LOOKUP_ADDR	0x18

#define	ROM_CONNECT_INTERNAL_FLASH	ROM_CODE('I', 'F')
#define	ROM_FLASH_EXIT_XIP		ROM_CODE('E', 'X')
#define	ROM_FLASH_RANGE_ERASE		ROM_CODE('R', 'E')
#define	ROM_FLASH_RANGE_PROGRAM		ROM_CODE('R', 'P')
#define	ROM_FLASH_FLUSH_CACHE		ROM_CODE('F', 'C')

typedef void *(*rom_lookup_fn)(u_short *table, u_int code);
typedef void (*rom_void_fn)(void);
typedef void (*rom_erase_fn)(u_int addr, u_int count, u_int blk, u_char cmd);
typedef void (*rom_program_fn)(u_int addr, const u_char *data, u_int count);

struct flash_rom {
	rom_void_fn	connect;
	rom_void_fn	exit_xip;
	rom_erase_fn	erase;
	rom_program_fn	program;
	rom_void_fn	flush;
};

static struct flash_rom flrom;
static int flrom_ready;

/* The second stage, copied out so XIP can be restored after a write. */
#define	BOOT2_WORDS	64
static u_int boot2_copy[BOOT2_WORDS];

static struct dhara_map flmap;
static u_char flpage[FLASH_UNIT_BYTES];
static u_char flcopy[FLASH_UNIT_BYTES];	/* dhara_nand_copy staging. */
static int flmap_ready;
static daddr_t flblocks;		/* Capacity in DEV_BSIZE blocks. */

/* The filesystem region as Dhara sees it; see flash.h for the choice. */
const struct dhara_nand flnand = {
	FLASH_LOG2_UNIT,
	FLASH_LOG2_UPB,
	FLASH_FS_BLOCKS,
};

#if DEV_BSIZE % FLASH_UNIT_BYTES != 0
#error "DEV_BSIZE must be a multiple of the Dhara page"
#endif

/*
 * The two reads below take 16-bit pointers from fixed boot ROM addresses.
 * GCC models a constant cast to a pointer as a zero-length array and reports
 * every such read as out of bounds, which volatile does not suppress because
 * the objection is to the address rather than to caching. The addresses are
 * architectural, so the diagnostic is disabled across exactly these two lines
 * rather than the file.
 */
#pragma GCC diagnostic push
#if defined(__GNUC__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

void *
rom_func_lookup(u_int code)
{
	rom_lookup_fn lookup;
	u_short *table;

	/*
	 * Both are fixed locations in the boot ROM holding 16-bit pointers,
	 * so they are read through volatile: the compiler has no object to
	 * reason about at an absolute address and warns about bounds if
	 * allowed to try.
	 */
	lookup = (rom_lookup_fn)(u_int)
	    *(volatile u_short *)ROM_TABLE_LOOKUP_ADDR;
	table = (u_short *)(u_int)
	    *(volatile u_short *)ROM_FUNC_TABLE_ADDR;
	return lookup(table, code);
}

#pragma GCC diagnostic pop

static void
flash_rom_init(void)
{
	const u_int *xip = (const u_int *)FLASH_XIP_BASE;
	int i;

	if (flrom_ready)
		return;

	flrom.connect = (rom_void_fn)rom_func_lookup(
	    ROM_CONNECT_INTERNAL_FLASH);
	flrom.exit_xip = (rom_void_fn)rom_func_lookup(ROM_FLASH_EXIT_XIP);
	flrom.erase = (rom_erase_fn)rom_func_lookup(ROM_FLASH_RANGE_ERASE);
	flrom.program = (rom_program_fn)rom_func_lookup(
	    ROM_FLASH_RANGE_PROGRAM);
	flrom.flush = (rom_void_fn)rom_func_lookup(ROM_FLASH_FLUSH_CACHE);

	/*
	 * Take a copy of the second stage before any write disables XIP,
	 * because restoring XIP afterwards means calling it, and by then it
	 * cannot be read from flash.
	 */
	for (i = 0; i < BOOT2_WORDS; i++)
		boot2_copy[i] = xip[i];

	flrom_ready = 1;
}

/*
 * Re-enter execute-in-place by calling the copied second stage. It returns to
 * its caller when entered with a non-zero link register, which is how the
 * Pico SDK re-enables XIP after a write, and is why the copy is kept.
 */
static __ramfunc void
flash_enter_xip(void)
{
	((void (*)(void))((u_int)boot2_copy + 1))();
}

/*
 * Erase one Dhara block, FLASH_ERASE_BYTES of the filesystem region. Runs
 * from RAM with interrupts masked, because XIP is down for the duration and
 * an interrupt vectoring into flash would fetch from a disabled interface.
 */
static __ramfunc int
flash_erase_block(u_int offset)
{
	int s;

	s = splhigh();
	flrom.connect();
	flrom.exit_xip();
	flrom.erase(offset, FLASH_ERASE_BYTES, FLASH_BLOCK_BYTES,
	    FLASH_BLOCK_ERASE_CMD);
	flrom.flush();
	flash_enter_xip();
	splx(s);
	return 0;
}

/*
 * Program one Dhara page, four chip pages, under the same constraints as
 * the erase above. The ROM routine takes any multiple of the chip page.
 */
static __ramfunc int
flash_program_unit(u_int offset, const u_char *data)
{
	int s;

	s = splhigh();
	flrom.connect();
	flrom.exit_xip();
	flrom.program(offset, data, FLASH_UNIT_BYTES);
	flrom.flush();
	flash_enter_xip();
	splx(s);
	return 0;
}

/*
 * Dhara's driver interface. Pages and blocks below are numbered within the
 * filesystem region, so every address gains FLASH_FS_OFFSET before it reaches
 * the chip, and the kernel below that offset is unreachable from here.
 */

int
dhara_nand_is_bad(const struct dhara_nand *n __unused, dhara_block_t b __unused)
{
	/* NOR ships no factory bad blocks and marks none at runtime. */
	return 0;
}

void
dhara_nand_mark_bad(const struct dhara_nand *n __unused,
    dhara_block_t b __unused)
{
	/* Nothing to record: see dhara_nand_is_bad. */
}

int
dhara_nand_erase(const struct dhara_nand *n __unused, dhara_block_t b,
    dhara_error_t *err)
{
	if (b >= FLASH_FS_BLOCKS) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	return flash_erase_block(FLASH_FS_OFFSET + b * FLASH_ERASE_BYTES);
}

int
dhara_nand_prog(const struct dhara_nand *n __unused, dhara_page_t p,
    const u_char *data, dhara_error_t *err)
{
	u_int offset = p * FLASH_UNIT_BYTES;

	if (offset >= FLASH_FS_BYTES) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	return flash_program_unit(FLASH_FS_OFFSET + offset, data);
}

int
dhara_nand_is_free(const struct dhara_nand *n __unused, dhara_page_t p)
{
	const u_char *q;
	u_int i;

	q = (const u_char *)(FLASH_XIP_BASE + FLASH_FS_OFFSET +
	    p * FLASH_UNIT_BYTES);
	for (i = 0; i < FLASH_UNIT_BYTES; i++)
		if (q[i] != 0xff)
			return 0;
	return 1;
}

int
dhara_nand_read(const struct dhara_nand *n __unused, dhara_page_t p,
    size_t offset, size_t length, u_char *data, dhara_error_t *err)
{
	const u_char *q;

	if (offset + length > FLASH_UNIT_BYTES ||
	    p * FLASH_UNIT_BYTES + offset + length > FLASH_FS_BYTES) {
		dhara_set_error(err, DHARA_E_ECC);
		return -1;
	}
	/* Reads come straight out of the XIP window; NOR carries no ECC. */
	q = (const u_char *)(FLASH_XIP_BASE + FLASH_FS_OFFSET +
	    p * FLASH_UNIT_BYTES + offset);
	bcopy(q, data, length);
	return 0;
}

int
dhara_nand_copy(const struct dhara_nand *n, dhara_page_t src,
    dhara_page_t dst, dhara_error_t *err)
{
	/*
	 * Staged through RAM rather than copied chip-side. The source cannot
	 * be read through the XIP window while the destination is being
	 * programmed, because the program takes XIP down. The buffer is
	 * static because a kilobyte does not belong on the kernel stack, and
	 * every caller holds splbio.
	 */
	if (dhara_nand_read(n, src, 0, FLASH_UNIT_BYTES, flcopy, err) < 0)
		return -1;
	return dhara_nand_prog(n, dst, flcopy, err);
}

/*
 * Block device.
 *
 * Minor numbers follow the SD driver: unit in bits 3 and up, partition in
 * the low three, with partition 0 the whole device and 1 to 4 the entries
 * of a PC partition table in the first 512 bytes of logical sector 0, as
 * tools/fsutil --repartition writes it. Offsets and sizes in that table are
 * in 512-byte sectors and are halved for DEV_BSIZE blocks, so fsutil's
 * even-numbered layout is a requirement rather than a convention.
 */

#define	NPARTITIONS	4
#define	RAWPART		0			/* Whole device. */

#define	flunit(dev)	(minor(dev) >> 3)
#define	flpart(dev)	(minor(dev) & 7)

struct fldisk {
	struct diskpart	part[NPARTITIONS + 1];	/* [0] is the whole device. */
	u_int		openpart;		/* Bit per open partition. */
};

static struct fldisk fldrives[NFL];

/*
 * Bring the map up and read the partition table. Returns 0 on success.
 */
static int
fl_setup(int unit)
{
	struct fldisk *du = &fldrives[unit];
	dhara_error_t err = DHARA_E_NONE;
	u_short buf[DEV_BSIZE / sizeof(u_short)];
	u_int i;

	if (! flmap_ready) {
		flash_rom_init();
		dhara_map_init(&flmap, &flnand, flpage, FLASH_GC_RATIO);
		/*
		 * A fresh chip has no stored state, which dhara_map_resume
		 * reports by returning -1 after initializing an empty map.
		 * That is the newly-formatted case rather than a failure.
		 */
		(void)dhara_map_resume(&flmap, &err);
		flblocks = (daddr_t)dhara_map_capacity(&flmap) /
		    (DEV_BSIZE / FLASH_UNIT_BYTES);
		flmap_ready = 1;
	}

	bzero(du->part, sizeof(du->part));
	du->part[RAWPART].dp_offset = 0;
	du->part[RAWPART].dp_nsectors = (u_int)flblocks * 2;
	printf("fl%d: %u kbytes on QSPI flash, %u kbytes raw\n", unit,
	    (u_int)flblocks, (u_int)(FLASH_FS_BYTES / 1024));

	/* The table lives in the first 512 bytes of logical block 0. */
	for (i = 0; i < DEV_BSIZE / FLASH_UNIT_BYTES; i++) {
		err = DHARA_E_NONE;
		if (dhara_map_read(&flmap, i,
		    (u_char *)buf + i * FLASH_UNIT_BYTES, &err) < 0) {
			printf("fl%d: cannot read partition table\n", unit);
			return ENXIO;
		}
	}
	if (buf[255] == MBR_MAGIC) {
		bcopy(&buf[223], &du->part[1], 64);
		for (i = 1; i <= NPARTITIONS; i++) {
			if (du->part[i].dp_type != 0)
				printf("fl%d%c: partition type %02x, "
				    "sector %u, size %u kbytes\n",
				    unit, i + 'a' - 1, du->part[i].dp_type,
				    du->part[i].dp_offset,
				    du->part[i].dp_nsectors / 2);
		}
	}
	return 0;
}

int
flopen(dev_t dev, int flags __unused, int mode __unused)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	struct fldisk *du;
	int error;

	if (unit >= NFL || part > NPARTITIONS)
		return ENXIO;
	du = &fldrives[unit];

	if (du->part[RAWPART].dp_nsectors == 0) {
		error = fl_setup(unit);
		if (error)
			return error;
	}
	du->openpart |= 1 << part;
	return 0;
}

int
flclose(dev_t dev, int mode __unused, int flag __unused)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	dhara_error_t err = DHARA_E_NONE;

	if (unit >= NFL || part > NPARTITIONS)
		return ENXIO;
	fldrives[unit].openpart &= ~(1 << part);
	if (flmap_ready)
		(void)dhara_map_sync(&flmap, &err);
	return 0;
}

/* Size in DEV_BSIZE blocks, which is what the block layer asks for. */
daddr_t
flsize(dev_t dev)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	struct fldisk *du;

	if (unit >= NFL || part > NPARTITIONS)
		return 0;
	du = &fldrives[unit];
	if (du->part[RAWPART].dp_nsectors == 0 && fl_setup(unit) != 0)
		return 0;
	return du->part[part].dp_nsectors >> 1;
}

void
flstrategy(struct buf *bp)
{
	int unit = flunit(bp->b_dev);
	struct fldisk *du = &fldrives[unit];
	struct diskpart *p = &du->part[flpart(bp->b_dev)];
	dhara_error_t err = DHARA_E_NONE;
	u_int per_blk = DEV_BSIZE / FLASH_UNIT_BYTES;
	u_int sector, nsect, i;
	u_char *addr;
	daddr_t part_size, offset;
	long nblk;
	int s, fail = 0;

	if (unit >= NFL || flpart(bp->b_dev) > NPARTITIONS) {
		bp->b_error = ENXIO;
		goto bad;
	}
	if (du->part[RAWPART].dp_nsectors == 0 && fl_setup(unit) != 0) {
		bp->b_error = ENXIO;
		goto bad;
	}

	/*
	 * Determine the size of the transfer, and make sure it is within
	 * the boundaries of the partition.
	 */
	part_size = p->dp_nsectors >> 1;
	offset = bp->b_blkno + (p->dp_offset >> 1);
	nblk = btod(bp->b_bcount);
	if (bp->b_blkno + nblk > part_size) {
		/* Exactly at the end reads as end of file. */
		if (bp->b_blkno == part_size) {
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			return;
		}
		/* Or truncate if part of it fits. */
		nblk = part_size - bp->b_blkno;
		if (nblk <= 0) {
			bp->b_error = EINVAL;
			goto bad;
		}
		bp->b_bcount = nblk << DEV_BSHIFT;
	}

	if (bp->b_dev == swapdev)
		led_control(LED_SWAP, 1);
	else
		led_control(LED_DISK, 1);

	sector = (u_int)offset * per_blk;
	nsect = (u_int)nblk * per_blk;
	addr = (u_char *)bp->b_addr;

	s = splbio();
	for (i = 0; i < nsect && ! fail; i++) {
		if (bp->b_flags & B_READ) {
			if (dhara_map_read(&flmap, sector + i,
			    addr + i * FLASH_UNIT_BYTES, &err) < 0)
				fail = 1;
		} else {
			if (dhara_map_write(&flmap, sector + i,
			    addr + i * FLASH_UNIT_BYTES, &err) < 0)
				fail = 1;
		}
	}
	splx(s);

	if (bp->b_dev == swapdev)
		led_control(LED_SWAP, 0);
	else
		led_control(LED_DISK, 0);

	if (fail) {
		bp->b_error = EIO;
		goto bad;
	}
	biodone(bp);
	return;

bad:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

int
flioctl(dev_t dev, u_int cmd, caddr_t addr, int flag __unused)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	struct diskpart *dp;

	if (unit >= NFL || part > NPARTITIONS)
		return ENXIO;
	dp = &fldrives[unit].part[part];

	switch (cmd) {
	case DIOCGETMEDIASIZE:
		/* Partition size in kbytes. */
		*(int *)addr = dp->dp_nsectors >> 1;
		return 0;

	case DIOCGETPART:
		*(struct diskpart *)addr = *dp;
		return 0;

	case DIOCREINIT:
		/* Forget the table so the next open rereads it. */
		fldrives[unit].part[RAWPART].dp_nsectors = 0;
		return 0;
	}
	return EINVAL;
}
