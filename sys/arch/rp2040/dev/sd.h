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

#ifndef _RP2040_DEV_SD_H_
#define _RP2040_DEV_SD_H_

/*
 * The PicoCalc's SD card socket on SPI0 as block device sd0 and raw device
 * rsd0. Minor number 0 is the whole card, 1 to 4 the MBR partitions
 * (sd0a to sd0d).
 */
#define	SD_BMAJOR		5	/* bdevsw index, rp2040/conf.c. */
#define	SD_CMAJOR		11	/* cdevsw index, rp2040/conf.c. */

int		sdopen(dev_t dev, int flag, int mode);
int		sdclose(dev_t dev, int flag, int mode);
void		sdstrategy(struct buf *bp);
daddr_t		sdsize(dev_t dev);
int		sdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

/* Sample the card-detect pin; called from the keyboard poll callout. */
void		sd_detect_poll(void);

#endif /* !_RP2040_DEV_SD_H_ */
