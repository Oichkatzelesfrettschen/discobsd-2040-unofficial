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

#ifndef _RP2040_DEV_SD_H_
#define _RP2040_DEV_SD_H_

/*
 * The PicoCalc's SD card socket on SPI0 as block device sd0 and raw device
 * rsd0. Minor number 0 is the whole card, 1 to 4 the MBR partitions
 * (sd0a to sd0d).
 */
#define	SD_BMAJOR		5	/* bdevsw index, rp2040/conf.c. */
#define	SD_CMAJOR		11	/* cdevsw index, rp2040/conf.c. */

struct uio;

int		sdopen(dev_t dev, int flag, int mode);
int		sdclose(dev_t dev, int flag, int mode);
void		sdstrategy(struct buf *bp);
daddr_t		sdsize(dev_t dev);
int		sdread(dev_t dev, struct uio *uio, int flag);
int		sdwrite(dev_t dev, struct uio *uio, int flag);
int		sdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

/*
 * Take the root from the card when the PicoCalc loader started the kernel;
 * main() calls it through conf_service_init (the Config line "service
 * sdroot") after binit() and before it mounts root.
 */
void		sdrootattach(void);

/*
 * Move swap to the card's first usable partition of type b8 unless the
 * PSRAM already holds it; main() calls it through conf_service_init (the
 * Config line "service sdswap") after sdrootattach() and before it opens
 * swapdev.
 */
void		sdswapattach(void);

/* Sample the card-detect pin; called from the keyboard poll callout. */
void		sd_detect_poll(void);

/*
 * Write a crash dump (machine/dump.h) of the spans to the card's dump
 * partition; dumpsys() in machdep.c calls it from boot(). Returns 0 or an
 * errno.
 */
struct dump_span;
int		sddump(dev_t dev, const struct dump_span *span, u_int nspan,
		    u_int image_crc, u_int time);

#endif /* !_RP2040_DEV_SD_H_ */
