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
 * Memory access to the PicoCalc's ESP-PSRAM64H through the PIO program in
 * psram_pio.h. psram_xfer.c holds everything that touches the PIO block,
 * and tests/psram builds it unchanged against a register-level model.
 */

#ifndef _RP2040_DEV_PSRAM_XFER_H_
#define _RP2040_DEV_PSRAM_XFER_H_

#define	PSRAM_BYTES		(8UL * 1024 * 1024)
/*
 * The part holds two regions, one per block-device minor: minor 0, the
 * swap device, takes the first PSRAM_SWAP_BYTES, and minor 1, the /tmp
 * RAM disk, the rest. Neither survives a reset. psram_swap_all() widens
 * minor 0 to the whole part and empties minor 1, for a kernel whose /tmp
 * lives on the SD card (sdswapattach(), sd.c); a reset restores the split.
 */
#define	PSRAM_SWAP_BYTES	(2UL * 1024 * 1024)
#define	PSRAM_REGIONS		2

/*
 * Bytes per chip-select frame. A frame of this size at the PIO clock in
 * psram_pio.h keeps CS low for under 5 us, inside the 8 us tCEM limit, and
 * 64 divides the 1 KB page, so no frame crosses a page boundary.
 */
#define	PSRAM_CHUNK		64

/* IO_BANK0 FUNCSEL value that routes a pin to PIO0 (datasheet 2.19.2). */
#define	PSRAM_FUNC_PIO0		6

#define	PSRAM_E_TIMEOUT		(-1)	/* the state machine stopped */
#define	PSRAM_E_STALL		(-2)	/* the RX FIFO overran during a read */
#define	PSRAM_E_RANGE		(-3)	/* the range leaves the device */
#define	PSRAM_E_ID		(-4)	/* identification bytes are wrong */
#define	PSRAM_E_QUAD		(-5)	/* quad-mode pattern test failed */
#define	PSRAM_E_SIZE		(-6)	/* addresses wrap below PSRAM_BYTES */

#define	PSRAM_MFID		0x0d	/* manufacturer, datasheet Figure 6-7 */
#define	PSRAM_KGD_PASS		0x5d	/* known good die, Table 6-1 */

/*
 * Read ID answers a 48-bit EID after the manufacturer and known-good-die
 * bytes, most significant bit first (ESP-PSRAM64H Figure 6-7, LY68L6400
 * Figure 12, APS6404L-3SQR Figure 13). Only the APS6404L sheet assigns a
 * field, EID[47:45] the density (010 for 64 Mbit); the ESP and Lyontek
 * sheets leave all 48 bits undefined, so the bytes identify a part only
 * against reads of reference parts. psram_eid[0] holds EID[47:40].
 */
#define	PSRAM_EID_BYTES		6

extern unsigned char psram_mfid, psram_kgd, psram_eid[PSRAM_EID_BYTES];
extern int psram_id_read;

void psram_hw_init(void);
int psram_device_init(void);
unsigned int psram_chunk_len(unsigned int addr, unsigned int len);
int psram_block_offset(long blkno, unsigned int bsize, unsigned int len,
    unsigned int *off);
void psram_swap_all(void);
unsigned long psram_region_bytes(int minor);
int psram_region_offset(int minor, long blkno, unsigned int bsize,
    unsigned int len, unsigned int *off);
int psram_read(unsigned int addr, unsigned char *dst, unsigned int len);
int psram_write(unsigned int addr, const unsigned char *src,
    unsigned int len);

#endif /* !_RP2040_DEV_PSRAM_XFER_H_ */
