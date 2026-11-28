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
 * The PicoCalc's 8 MB ESP-PSRAM64H as the swap device.
 *
 * config() runs psramprobe() before init_main() opens swapdev. A device
 * that answers with the manufacturer and known-good-die bytes, passes
 * the quad-mode pattern test and addresses all 8 MB replaces fl1 as
 * swapdev; any other outcome prints one line and leaves swapdev on the
 * fl1 that the configuration names. The part holds a 2 MB swap region and a
 * 6 MB /tmp region unless sdswapattach() (sd.c) finds a PTYPE_TMP partition
 * on the card, which takes /tmp and leaves the whole part to swap
 * (psram_swap_all(), psram_xfer.c). Transfers complete inside
 * psramstrategy(), as the flash driver's do, because swap_with_buf()
 * sleeps only until B_DONE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/config.h>
#include <sys/errno.h>
#include <sys/storage_stats.h>
#ifdef DIAG_STATS
#include <sys/diag_stats.h>
#endif

#include <machine/board.h>

#include <rp2040/dev/psram.h>

static int psram_up;

/*
 * Swap keeps minor 0 (psramprobe() names makedev(PSRAM_MAJOR, 0)), a whole
 * number of image alignments that holds at least one guarded image, and a
 * block count a COMPACT_SWAPMAP entry can carry.
 */
_Static_assert(PSRAM_SWAP_BYTES % DEV_BSIZE == 0 &&
    (PSRAM_SWAP_BYTES / DEV_BSIZE) % SWAP_IMAGE_ALIGN == 0,
    "PSRAM swap is a whole number of SWAP_IMAGE_ALIGN blocks");
_Static_assert(PSRAM_SWAP_BYTES / DEV_BSIZE >=
    2 * SWAP_IMAGE_GUARD + SWAP_IMAGE_ALIGN,
    "PSRAM swap holds a guarded image");
_Static_assert(PSRAM_SWAP_BYTES / DEV_BSIZE <= 0xffff,
    "PSRAM swap fits a 16-bit swap map entry");
_Static_assert(PSRAM_SWAP_BYTES < PSRAM_BYTES, "the /tmp region is not empty");
/* psram_swap_all() makes the whole part the swap device. */
_Static_assert((PSRAM_BYTES / DEV_BSIZE) % SWAP_IMAGE_ALIGN == 0 &&
    PSRAM_BYTES / DEV_BSIZE <= 0xffff,
    "whole-part PSRAM swap is aligned and fits a 16-bit swap map entry");

int
psramopen(dev_t dev, int flag __unused, int mode __unused)
{
	return (psram_up && psram_region_bytes(minor(dev)) != 0) ? 0 : ENXIO;
}

int
psramclose(dev_t dev __unused, int flag __unused, int mode __unused)
{
	return 0;
}

/* Size in DEV_BSIZE blocks, which is what the block layer asks for. */
daddr_t
psramsize(dev_t dev)
{
	return psram_up ? psram_region_bytes(minor(dev)) / DEV_BSIZE : 0;
}

void
psramstrategy(struct buf *bp)
{
	unsigned int off, len = bp->b_bcount;
	int err;

	bp->b_resid = 0;
	if (!psram_up || psram_region_bytes(minor(bp->b_dev)) == 0) {
		bp->b_error = ENXIO;
		goto bad;
	}
	err = psram_region_offset(minor(bp->b_dev), bp->b_blkno, DEV_BSIZE,
	    len, &off);
	if (err != 0) {
#ifdef DIAG_STATS
		diag_add(DIAG_PSRAM_RANGES, 1);
#endif
		bp->b_error = EINVAL;
		goto bad;
	}
	if (minor(bp->b_dev) == 0) {	/* the swap counters count swap alone */
		storage_note((bp->b_flags & B_READ) ? STORAGE_SWAP_READS :
		    STORAGE_SWAP_WRITES, 1);
		storage_note((bp->b_flags & B_READ) ?
		    STORAGE_SWAP_READ_BYTES : STORAGE_SWAP_WRITE_BYTES, len);
	}
	if ((bp->b_flags & B_READ) != 0)
		err = psram_read(off, (unsigned char *)bp->b_addr, len);
	else
		err = psram_write(off, (const unsigned char *)bp->b_addr,
		    len);
#ifdef DIAG_STATS
	diag_add(DIAG_PSRAM_XFERS, 1);
	diag_add(DIAG_PSRAM_BYTES, len);
	if (err == PSRAM_E_TIMEOUT)
		diag_add(DIAG_PSRAM_TIMEOUTS, 1);
	else if (err == PSRAM_E_STALL)
		diag_add(DIAG_PSRAM_STALLS, 1);
	else if (err == PSRAM_E_RANGE)
		diag_add(DIAG_PSRAM_RANGES, 1);
#endif
	if (err != 0) {
		bp->b_error = err == PSRAM_E_RANGE ? EINVAL : EIO;
		goto bad;
	}
	biodone(bp);
	return;

bad:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

static int
psramprobe(const struct conf_device *config __unused)
{
	unsigned char lines[PSRAM_LINES_MAX];
	unsigned int i, n;
	int err;

	if (board_id() != BOARD_ID_PICOCALC)
		return 0;
	psram_hw_init();
	err = psram_device_init();
#ifdef DIAG_STATS
	diag_set(DIAG_PSRAM_PROBE_RAN, 1);
	diag_set(DIAG_PSRAM_PROBE_ERR, err < 0 ? (uint32_t)-err : (uint32_t)err);
#endif
	n = psram_probe_lines(psram_id_read, err, lines);
	for (i = 0; i < n; i++) {
		switch (lines[i]) {
		case PSRAM_LINE_READ_ID:
			printf("psram0: read id %02x/%02x eid %02x%02x%02x%02x%02x%02x\n",
			    psram_mfid, psram_kgd, psram_eid[0], psram_eid[1],
			    psram_eid[2], psram_eid[3], psram_eid[4],
			    psram_eid[5]);
			break;
		case PSRAM_LINE_NOT_PART:
			printf("psram0: id %x/%x is not an ESP-PSRAM64H, swap stays on fl1\n",
			    psram_mfid, psram_kgd);
			break;
		case PSRAM_LINE_WRAP:
			printf("psram0: addresses wrap below %u kbytes, swap stays on fl1\n",
			    (unsigned int)(PSRAM_BYTES / 1024));
			break;
		case PSRAM_LINE_UNUSABLE:
			printf("psram0: not usable (error %d), swap stays on fl1\n",
			    err);
			break;
		case PSRAM_LINE_UP:
			psram_up = 1;
			swapdev = makedev(PSRAM_MAJOR, 0);
			printf("psram0: ESP-PSRAM64H %u kbytes on PIO0, swap %u kbytes, "
			    "tmp %u kbytes\n", (unsigned int)(PSRAM_BYTES / 1024),
			    (unsigned int)(PSRAM_SWAP_BYTES / 1024),
			    (unsigned int)((PSRAM_BYTES - PSRAM_SWAP_BYTES) / 1024));
			break;
		}
	}
	return err == 0;
}

const struct driver psramdriver = {
	"psram", psramprobe,
};
