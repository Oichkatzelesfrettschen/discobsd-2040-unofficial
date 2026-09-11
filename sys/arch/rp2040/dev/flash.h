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
 * larger opcode only for a whole aligned block inside the range. This driver
 * erases 8192 bytes at a time, so the block path never fires, but the pair is
 * passed as the Pico SDK passes it rather than invented.
 */
#define	FLASH_BLOCK_BYTES	65536UL		/* W25Q 64K block. */
#define	FLASH_BLOCK_ERASE_CMD	0xd8		/* Block erase, per SDK. */

/*
 * The kernel occupies the low 512K, matching the STM32F407XE region, so the
 * filesystem starts above it. Keep in step with conf/RP2040.ld.
 */
#define	FLASH_FS_OFFSET		(512UL * 1024)
#define	FLASH_FS_BYTES		(FLASH_TOTAL_BYTES - FLASH_FS_OFFSET)
#define	FLASH_FS_BLOCKS		(FLASH_FS_BYTES / FLASH_ERASE_BYTES)

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

int	flopen(dev_t dev, int flags, int mode);
int	flclose(dev_t dev, int mode, int flag);
daddr_t	flsize(dev_t dev);
void	flstrategy(struct buf *bp);
int	flioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
#endif	/* KERNEL */

#endif	/* !_RP2040_DEV_FLASH_H_ */
