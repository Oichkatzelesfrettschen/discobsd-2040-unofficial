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
 * The PicoCalc's ESP-PSRAM64H as a block device, major PSRAM_MAJOR, unit 0.
 * psram.c is the bdevsw side; psram_xfer.c moves the bytes.
 */

#ifndef _RP2040_DEV_PSRAM_H_
#define _RP2040_DEV_PSRAM_H_

#include <rp2040/dev/psram_xfer.h>

#define	PSRAM_MAJOR	3

#ifdef KERNEL
struct buf;

int	psramopen(dev_t dev, int flag, int mode);
int	psramclose(dev_t dev, int flag, int mode);
void	psramstrategy(struct buf *bp);
daddr_t	psramsize(dev_t dev);
#endif

#endif /* !_RP2040_DEV_PSRAM_H_ */
