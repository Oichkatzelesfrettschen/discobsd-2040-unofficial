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

#ifndef	_RP2040_DEV_FLASH_H_
#define	_RP2040_DEV_FLASH_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Root filesystem on the RP2040's QSPI flash.
 *
 * The board carries no SD socket, and DiscoBSD's other targets boot from one,
 * so this driver makes the onboard flash a block device instead. Wear
 * leveling and the logical-to-physical mapping come from Dhara, vendored
 * under sys/arch/rp2040/dhara.
 *
 * Chip geometry is the Winbond W25Q16JV's, measured on the part rather
 * than assumed: 256-byte program pages inside 4096-byte erase sectors.
 *
 * Dhara's geometry is chosen above the chip's. Its page is the unit it
 * maps and the block layer moves, 1024 bytes to match DEV_BSIZE, and its
 * block is two chip sectors erased together. Both sizes are set by Dhara's
 * metadata layout: a checkpoint page carries 132 bytes per data page and
 * a checkpoint group cannot outgrow an erase block, so 256-byte pages
 * spend half of the flash on checkpoints and 1024-byte pages in 4096-byte
 * blocks spend a quarter. Eight 1024-byte pages per block let seven of
 * them carry data, and the safety margin Dhara keeps grows with the block,
 * which is why the block stops at two sectors: tools/flashimg -c reports
 * the capacity each choice leaves.
 */

#define	FLASH_XIP_BASE		0x10000000UL	/* Execute-in-place window. */
#define	FLASH_TOTAL_BYTES	(2048UL * 1024)	/* W25Q16JV, JEDEC ef 40 15. */

#define	FLASH_PROG_BYTES	256UL		/* Chip program granularity. */
#define	FLASH_SECTOR_BYTES	4096UL		/* Chip erase granularity. */

#define	FLASH_UNIT_BYTES	1024UL		/* Dhara page, DEV_BSIZE. */
#define	FLASH_ERASE_BYTES	8192UL		/* Dhara block, two sectors. */

#define	FLASH_LOG2_UNIT		10		/* log2(FLASH_UNIT_BYTES) */
#define	FLASH_LOG2_UPB		3		/* log2(units per block) */

/*
 * Garbage collection ratio, the count of collection operations Dhara runs
 * per write. Smaller trades capacity for more predictable IO. Four is a
 * starting point and has not been measured on this part. It is part of the
 * on-flash format: tools/flashimg writes images with it, and every mount of
 * a chip must use the value the chip was written with.
 */
#define	FLASH_GC_RATIO		4

/*
 * The boot ROM's erase routine takes a block size and a block erase opcode
 * alongside the range. It erases in 4096-byte sectors by default and uses the
 * larger opcode only for a whole aligned block inside the range, which a
 * large swap write can contain. The pair is passed as the Pico SDK passes it.
 */
#define	FLASH_BLOCK_BYTES	65536UL		/* W25Q 64K block. */
#define	FLASH_BLOCK_ERASE_CMD	0xd8		/* Block erase, per SDK. */

/*
 * boot2 and the kernel occupy the low 128K, which conf/RP2040.ld
 * enforces as the FLASH region; doc/STORAGE.md records the measured
 * kernel size. The filesystem starts above it. Keep in step with
 * conf/RP2040.ld.
 */
#define	FLASH_FS_OFFSET		(128UL * 1024)
#define	FLASH_FS_BYTES		(FLASH_SWAP_OFFSET - FLASH_FS_OFFSET)
#define	FLASH_FS_BLOCKS		(FLASH_FS_BYTES / FLASH_ERASE_BYTES)

/*
 * Swap is the top of the chip, written in place by erase and program with
 * no translation layer: unit 1 of the driver, fl1. A swapped image is
 * rewritten whole and never read back after a reboot, so journaling it
 * only costs erases. Measured through Dhara, one 33K swap write erased 21
 * blocks. Here it erases the nine 4K sectors it covers. Swap is handed
 * out as erase-aligned contiguous images. The resource map starts at block
 * four, so its reserved first erase sector stages rewrites from /dev/tempN
 * through the same 256-byte physical-page buffer. Images of different sizes
 * still fragment the remaining space; 256K failed a 70K image with 115K free
 * in three runs, so 384K keeps room for the largest image beside two others
 * and the gaps.
 *
 * FLASH_SWAP_BYTES is the region's full extent. Under the PicoCalc loader
 * the region stops at the loader's stamped limit instead (loader_stamp.h),
 * so fl1's size, the swap bound in fl_raw() and nswap come from
 * flash_swap_extent() at boot; at the loader's limit 0x101fc000, flash
 * offset 0x1fc000, that is 368K.
 */
#define	FLASH_SWAP_BYTES	(384UL * 1024)
#define	FLASH_SWAP_OFFSET	(FLASH_TOTAL_BYTES - FLASH_SWAP_BYTES)
#define	FLASH_SWAP_SECTORS	(FLASH_SWAP_BYTES / FLASH_SECTOR_BYTES)
#define	FLASH_SWAP_SCRATCH_OFFSET FLASH_SWAP_OFFSET
#define	FLASH_PARTITION_COUNT	4

static inline int
flash_indices_valid(unsigned int unit, unsigned int partition,
    unsigned int drive_count)
{
	return unit < drive_count && partition <= FLASH_PARTITION_COUNT;
}

static inline int
flash_nand_range_valid(uint32_t page, size_t offset, size_t length)
{
	const size_t page_count = FLASH_FS_BYTES / FLASH_UNIT_BYTES;
	const size_t page_size = FLASH_UNIT_BYTES;

	if ((size_t)page >= page_count || offset > page_size)
		return 0;
	return length <= page_size - offset;
}

/*
 * Chip identity as flash_identity() returns it: the three JEDEC ID bytes
 * (manufacturer, memory type, capacity) and the 64-bit unique ID. The
 * capacity byte is the base-two logarithm of the size in bytes (15h for
 * the W25Q16JV's 2 MB).
 */
#define	FLASH_JEDEC_BYTES	3
#define	FLASH_UNIQUE_BYTES	8

/* True when n identity bytes are neither all zeros nor all ones. */
static inline int
flash_id_plausible(const unsigned char *p, unsigned int n)
{
	unsigned int i, zeros = 0, ones = 0;

	for (i = 0; i < n; i++) {
		zeros += p[i] == 0x00;
		ones += p[i] == 0xff;
	}
	return zeros != n && ones != n;
}

/* True when a JEDEC capacity byte names FLASH_TOTAL_BYTES. */
static inline int
flash_capacity_matches(unsigned int code)
{
	return code < 32 && (1UL << code) == FLASH_TOTAL_BYTES;
}

/*
 * Write the board's USB serial string into out, which holds
 * FLASH_SERIAL_CHARS bytes: the unique ID as 16 lowercase hex digits and
 * the suffix "-rp2040", or "rp2040" alone when unique is null. The suffix
 * keeps the /dev/serial/by-id name ending in "rp2040-if00", which the host
 * tools match, while two boards on one host get two names.
 */
#define	FLASH_SERIAL_CHARS	(2 * FLASH_UNIQUE_BYTES + sizeof("-rp2040"))

static inline void
flash_serial_string(char *out, const unsigned char *unique)
{
	static const char digits[] = "0123456789abcdef";
	static const char suffix[] = "-rp2040";
	unsigned int i, n = 0;

	if (unique != (const unsigned char *)0) {
		for (i = 0; i < FLASH_UNIQUE_BYTES; i++) {
			out[n++] = digits[unique[i] >> 4];
			out[n++] = digits[unique[i] & 0xf];
		}
		i = 0;
	} else {
		i = 1;		/* Drop the hyphen. */
	}
	while (suffix[i] != '\0')
		out[n++] = suffix[i++];
	out[n] = '\0';
}

/*
 * A function carrying this attribute is linked into .data and copied to RAM
 * by locore0.S. Erasing or programming flash takes the QSPI interface out of
 * execute-in-place, so any function driving one must already be in RAM when
 * it runs. See conf/kern.ldscript.
 */
#define	__ramfunc	__attribute__((noinline, section(".ramfunc")))

/*
 * Boot ROM entry points are found by a two-character code through a table
 * the ROM publishes; datasheet section 2.8.3. flash.c implements the lookup
 * and usb.c uses it for the USB-boot entry.
 */
#define	ROM_CODE(c1, c2)	((u_int)(c1) | ((u_int)(c2) << 8))

#ifdef KERNEL
struct buf;

void	*rom_func_lookup(u_int code);
int	flash_xip_cache(int request);
int	flash_identity(const unsigned char **jedec,
	    const unsigned char **unique);
void	flash_limit_init(void);
#ifdef STORAGE_STATS
u_int	flash_reset_hook_count(u_int erase);
void	flash_reset_hook_arm(u_int erase, u_int n);
/* Erases per swap sector since boot, saturating (dev/flash_wear.h). */
extern u_short flash_swap_wear[FLASH_SWAP_SECTORS];
#endif

int	flopen(dev_t dev, int flags, int mode);
int	flclose(dev_t dev, int flag, int mode);
daddr_t	flsize(dev_t dev);
void	flstrategy(struct buf *bp);
int	flioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
#endif	/* KERNEL */

#endif	/* !_RP2040_DEV_FLASH_H_ */
