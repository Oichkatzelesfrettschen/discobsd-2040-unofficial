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
 * Memory access to the PicoCalc's ESP-PSRAM64H through the PIO program in
 * psram_pio.h. psram_xfer.c holds everything that touches the PIO block,
 * and tests/psram builds it unchanged against a register-level model.
 */

#ifndef _RP2040_DEV_PSRAM_XFER_H_
#define _RP2040_DEV_PSRAM_XFER_H_

#define	PSRAM_BYTES		(8UL * 1024 * 1024)

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
int psram_read(unsigned int addr, unsigned char *dst, unsigned int len);
int psram_write(unsigned int addr, const unsigned char *src,
    unsigned int len);

#endif /* !_RP2040_DEV_PSRAM_XFER_H_ */
