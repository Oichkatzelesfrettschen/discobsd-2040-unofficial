/* Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Erase counts of the raw swap sectors, compiled into flash.c only for
 * STORAGE_STATS kernels.
 *
 * The raw swap region has no wear leveling: an image lands at the sector its
 * reservation names, so the erases of a workload concentrate on a few
 * sectors and an aggregate counter (STORAGE_SWAP_ERASE_SECTORS) hides the
 * spread. flash_wear_note() adds one erase to the counter of every swap
 * sector an erase request covers. A counter holds 16 bits and saturates at
 * FLASH_WEAR_MAX instead of wrapping, so a full counter reads as "at least
 * this many" and never as a small one. A block erase covers sixteen sectors
 * and counts each of them, since the chip erases every sector of the block.
 *
 * flash_erase() calls it with interrupts masked and execute-in-place still
 * up, before the request reaches the boot ROM. The host test
 * (tests/rp2040/flash_wear) includes this header and holds each rule to an
 * oracle.
 */
#ifndef _RP2040_DEV_FLASH_WEAR_H_
#define	_RP2040_DEV_FLASH_WEAR_H_

#include <rp2040/dev/flash.h>

/* The counters saturate here (FLASH_SWAP_SECTORS is in flash.h). */
#define	FLASH_WEAR_MAX		0xffffU

/*
 * Count one erase on each sector of [offset, offset + len), both multiples
 * of FLASH_SECTOR_BYTES, that lies in the swap region of nsectors sectors
 * starting at swap_offset. Sectors below swap_offset (the Dhara root) and
 * sectors past the last counter are not counted, so a request that crosses
 * either edge counts the part inside.
 */
static inline void
flash_wear_note(u_short *wear, u_int nsectors, u_int swap_offset, u_int offset,
    u_int len)
{
	u_int first, i, n;

	if (offset < swap_offset) {
		if (len <= swap_offset - offset)
			return;
		len -= swap_offset - offset;
		offset = swap_offset;
	}
	first = (offset - swap_offset) / FLASH_SECTOR_BYTES;
	n = len / FLASH_SECTOR_BYTES;
	for (i = 0; i < n && first + i < nsectors; i++)
		if (wear[first + i] != FLASH_WEAR_MAX)
			wear[first + i]++;
}

#endif	/* !_RP2040_DEV_FLASH_WEAR_H_ */
