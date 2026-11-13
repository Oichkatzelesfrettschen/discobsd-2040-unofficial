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
 * The PicoCalc's 8 MB ESP-PSRAM64H as the swap device.
 *
 * config() runs psramprobe() before init_main() opens swapdev. A device
 * that answers with the manufacturer and known-good-die bytes and passes
 * the quad-mode pattern test replaces fl1 as swapdev; any other outcome
 * prints one line and leaves swapdev on the fl1 that the configuration
 * names. Transfers complete inside psramstrategy(), as the flash driver's
 * do, because swap_with_buf() sleeps only until B_DONE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/config.h>
#include <sys/errno.h>
#include <sys/storage_stats.h>

#include <machine/board.h>

#include <rp2040/dev/psram.h>

static int psram_up;

int
psramopen(dev_t dev, int flag __unused, int mode __unused)
{
	return (minor(dev) == 0 && psram_up) ? 0 : ENXIO;
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
	return (minor(dev) == 0 && psram_up) ? PSRAM_BYTES / DEV_BSIZE : 0;
}

void
psramstrategy(struct buf *bp)
{
	unsigned int off, len = bp->b_bcount;
	int err;

	bp->b_resid = 0;
	if (minor(bp->b_dev) != 0 || !psram_up) {
		bp->b_error = ENXIO;
		goto bad;
	}
	err = psram_block_offset(bp->b_blkno, DEV_BSIZE, len, &off);
	if (err != 0) {
		bp->b_error = EINVAL;
		goto bad;
	}
	storage_note((bp->b_flags & B_READ) ? STORAGE_SWAP_READS :
	    STORAGE_SWAP_WRITES, 1);
	storage_note((bp->b_flags & B_READ) ? STORAGE_SWAP_READ_BYTES :
	    STORAGE_SWAP_WRITE_BYTES, len);
	if ((bp->b_flags & B_READ) != 0)
		err = psram_read(off, (unsigned char *)bp->b_addr, len);
	else
		err = psram_write(off, (const unsigned char *)bp->b_addr,
		    len);
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
psramprobe(struct conf_device *config __unused)
{
	int err;

	if (board_id() != BOARD_ID_PICOCALC)
		return 0;
	psram_hw_init();
	err = psram_device_init();
	if (psram_id_read)
		printf("psram0: read id %02x/%02x eid %02x%02x%02x%02x%02x%02x\n",
		    psram_mfid, psram_kgd, psram_eid[0], psram_eid[1],
		    psram_eid[2], psram_eid[3], psram_eid[4], psram_eid[5]);
	if (err == PSRAM_E_ID)
		printf("psram0: id %x/%x is not an ESP-PSRAM64H, swap stays on fl1\n",
		    psram_mfid, psram_kgd);
	else if (err != 0)
		printf("psram0: not usable (error %d), swap stays on fl1\n",
		    err);
	if (err != 0)
		return 0;
	psram_up = 1;
	swapdev = makedev(PSRAM_MAJOR, 0);
	printf("psram0: ESP-PSRAM64H %u kbytes on PIO0, swap\n",
	    (unsigned int)(PSRAM_BYTES / 1024));
	return 1;
}

struct driver psramdriver = {
	"psram", psramprobe,
};
