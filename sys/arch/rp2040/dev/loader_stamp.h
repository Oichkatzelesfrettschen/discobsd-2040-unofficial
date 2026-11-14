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

#ifndef	_RP2040_DEV_LOADER_STAMP_H_
#define	_RP2040_DEV_LOADER_STAMP_H_

#include <stdint.h>

#include <rp2040/dev/flash.h>

/*
 * The writable end of the QSPI flash, decided once at boot.
 *
 * The PicoCalc UF2 loader (pelrun/uf2loader) keeps its own stage in the top
 * of the flash and, after it programs an application, stamps two words into
 * the application's vector table: the magic below at flash offset 0x110 and,
 * at 0x114, the first flash address the application may not write, as an
 * execute-in-place address. Both words are vector entries 4 and 5, which
 * ARMv6-M reserves and every DiscoBSD image leaves zero, so an image flashed
 * any other way carries no stamp and keeps the whole chip.
 *
 * With the stamp, everything at and above the stamped limit belongs to the
 * loader. A limit that is not sector aligned, lies inside the kernel's own
 * flash region, or lies past the chip, names no layout this kernel can
 * honor, so the decode fails closed: the writable end becomes the start of
 * the filesystem region, the lowest offset any flash write targets, and no
 * erase or program passes flash_write_admitted().
 *
 * Everything here is pure arithmetic on the two words, so the host gate
 * under tests/loader_stamp compiles this header unchanged.
 */

#define	LOADER_STAMP_MAGIC_OFFSET	0x110UL	/* Vector entry 4. */
#define	LOADER_STAMP_LIMIT_OFFSET	0x114UL	/* Vector entry 5. */
#define	LOADER_STAMP_MAGIC		0xe98cc638UL

#define	LOADER_STAMP_NONE	0	/* No magic: the whole chip is writable. */
#define	LOADER_STAMP_VALID	1	/* Writable below the stamped limit. */
#define	LOADER_STAMP_INVALID	2	/* Magic with a bad limit: fail closed. */

/*
 * Decode the two stamp words into the writable end, a byte offset from the
 * start of the flash. The return value is one of LOADER_STAMP_* above.
 */
static inline int
loader_stamp_decode(uint32_t magic, uint32_t limit, uint32_t *write_end)
{
	uint32_t offset;

	if (magic != LOADER_STAMP_MAGIC) {
		*write_end = FLASH_TOTAL_BYTES;
		return LOADER_STAMP_NONE;
	}
	offset = limit - FLASH_XIP_BASE;
	if (limit >= FLASH_XIP_BASE && offset > FLASH_FS_OFFSET &&
	    offset <= FLASH_TOTAL_BYTES &&
	    (offset & (FLASH_SECTOR_BYTES - 1)) == 0) {
		*write_end = offset;
		return LOADER_STAMP_VALID;
	}
	*write_end = FLASH_FS_OFFSET;
	return LOADER_STAMP_INVALID;
}

/*
 * Whether an erase or program of len bytes at offset stays inside the
 * writable flash, [FLASH_FS_OFFSET, write_end). The kernel below
 * FLASH_FS_OFFSET is never a target, so it is refused as well. The test is
 * written on differences so that no sum can wrap; a zero length is refused,
 * since no caller issues one and the boot ROM routines expect whole pages.
 * It is forced inline because its kernel callers run from RAM.
 */
static inline __attribute__((always_inline)) int
flash_write_admitted(uint32_t offset, uint32_t len, uint32_t write_end)
{
	if (offset < FLASH_FS_OFFSET || offset >= write_end || len == 0)
		return 0;
	return len - 1 < write_end - offset;
}

/*
 * Bytes of raw swap, fl1, below the writable end: the region starts at
 * FLASH_SWAP_OFFSET and stops at the end or at FLASH_SWAP_BYTES, whichever
 * comes first. An end at or below the region's start leaves no swap.
 */
static inline uint32_t
flash_swap_extent(uint32_t write_end)
{
	if (write_end <= FLASH_SWAP_OFFSET)
		return 0;
	if (write_end - FLASH_SWAP_OFFSET > FLASH_SWAP_BYTES)
		return FLASH_SWAP_BYTES;
	return write_end - FLASH_SWAP_OFFSET;
}

#endif	/* !_RP2040_DEV_LOADER_STAMP_H_ */
