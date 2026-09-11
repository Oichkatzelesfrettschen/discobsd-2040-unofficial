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
 * Geometry is the Winbond W25Q16JV's, measured on the part rather than
 * assumed: 256-byte program pages inside 4096-byte erase sectors.
 */

#define	FLASH_XIP_BASE		0x10000000UL	/* Execute-in-place window. */
#define	FLASH_TOTAL_BYTES	(2048UL * 1024)	/* W25Q16JV, JEDEC ef 40 15. */

#define	FLASH_PAGE_BYTES	256UL		/* Program granularity. */
#define	FLASH_SECTOR_BYTES	4096UL		/* Erase granularity. */
#define	FLASH_PAGES_PER_SECTOR	(FLASH_SECTOR_BYTES / FLASH_PAGE_BYTES)

#define	FLASH_LOG2_PAGE		8		/* log2(FLASH_PAGE_BYTES) */
#define	FLASH_LOG2_PPB		4		/* log2(FLASH_PAGES_PER_SECTOR) */

/*
 * The kernel occupies the low 512K, matching the STM32F407XE region, so the
 * filesystem starts above it. Keep in step with conf/RP2040.ld.
 */
#define	FLASH_FS_OFFSET		(512UL * 1024)
#define	FLASH_FS_BYTES		(FLASH_TOTAL_BYTES - FLASH_FS_OFFSET)
#define	FLASH_FS_SECTORS	(FLASH_FS_BYTES / FLASH_SECTOR_BYTES)

/*
 * A function carrying this attribute is linked into .data and copied to RAM
 * by locore0.S. Erasing or programming flash takes the QSPI interface out of
 * execute-in-place, so any function driving one must already be in RAM when
 * it runs. See conf/kern.ldscript.
 */
#define	__ramfunc	__attribute__((noinline, section(".ramfunc")))

#endif	/* !_RP2040_DEV_FLASH_H_ */
