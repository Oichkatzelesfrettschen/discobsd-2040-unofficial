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
 * The PicoCalc's SD card on SPI0 (GP16 RX, GP17 chip select, GP18 SCK,
 * GP19 TX; GP22 card detect), a block device with an MBR partition table.
 *
 * sdspi.c holds the protocol; this file supplies its byte transport on the
 * RP2040's PL022 (datasheet 4.4) and the disk driver around it. SPI0 runs
 * Motorola mode 0 with eight-bit frames. clk_peri (BUS_KHZ) divides by
 * CPSDVSR * (1 + SCR): 2 * 157 gives 398 kHz, inside the 400 kHz the card
 * identification phase allows, and 10 * 1 gives 12.5 MHz for data transfer,
 * under the 25 MHz default-speed limit of the SD Physical Layer
 * Simplified Specification. Chip select is a SIO output so that it stays
 * low across a command and its data phase.
 *
 * A transfer runs in the caller's context with interrupts enabled and
 * busy-waits on the card, bounded by the limits in sdspi.c. The kernel does
 * not switch processes inside a system call, and the callouts and
 * interrupts that run meanwhile never touch SPI0, which is why no lock
 * covers a transfer. The keyboard callout samples the card-detect pin
 * through sd_detect_poll(); a stable change marks the card replaced, which
 * fails every transfer with EIO until the last close, because blocks cached
 * for the old card must not reach the new one. The pin's polarity is not
 * interpreted: any stable change counts.
 *
 * The driver reads and writes one 512-byte sector per command (CMD17,
 * CMD24), and a transfer is any whole number of sectors. The request names
 * its start as a DEV_BSIZE (1 KiB) block number: physio() derives b_blkno
 * from the file offset shifted right by DEV_BSHIFT, so a raw transfer of one
 * sector, as fdisk makes for the master boot record, addresses the first
 * sector of its block. A request that crosses the end of its partition
 * transfers the sectors that fit and reports the rest in b_resid.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/config.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/sdspi.h>
#include <rp2040/dev/sd.h>

#define	SPI0_BASE		0x4003c000UL
#define	SSPCR0			0x00
#define	SSPCR1			0x04
#define	SSPDR			0x08
#define	SSPSR			0x0c
#define	SSPCPSR			0x10

#define	SSPCR0_DSS_8		0x7
#define	SSPCR0_SCR(n)		((u_int)(n) << 8)
#define	SSPCR1_SSE		(1UL << 1)
#define	SSPSR_TNF		(1UL << 1)
#define	SSPSR_RNE		(1UL << 2)

#define	SPI_REG(off)		PC_REG32(SPI0_BASE + (off))
#define	PC_RESET_SPI0		(1UL << 16)
#define	SIO_GPIO_IN		0xd0000004UL

/*
 * Divisors for the two speed classes. SCR rounds the slow clock down to at
 * most 400 kHz; the fast clock is 12.5 MHz when BUS_KHZ is 125000.
 */
#define	SD_PRESCALE_SLOW	2
#define	SD_SCR_SLOW	((BUS_KHZ * 1000UL + 2 * 400000UL - 1) / (2 * 400000UL) - 1)
#define	SD_PRESCALE_FAST	((BUS_KHZ * 1000UL + 12500000UL - 1) / 12500000UL)
_Static_assert(SD_SCR_SLOW <= 255, "SCR is eight bits");
_Static_assert(SD_PRESCALE_FAST >= 2 && SD_PRESCALE_FAST <= 254 &&
    SD_PRESCALE_FAST % 2 == 0, "CPSDVSR is even, 2 to 254");

#define	SD_DET_DEBOUNCE_US	50000

#define	sdunit(dev)		(minor(dev) >> 3)
#define	sdpart(dev)		(minor(dev) & 7)

static u_char
sd_xfer(u_char out)
{
	while ((SPI_REG(SSPSR) & SSPSR_TNF) == 0)
		continue;
	SPI_REG(SSPDR) = out;
	while ((SPI_REG(SSPSR) & SSPSR_RNE) == 0)
		continue;
	return (u_char)SPI_REG(SSPDR);
}

/* Two constant-address stores: the divider ownership verifier resolves them. */
static void
sd_select(int on)
{
	if (on)
		pc_gpio_put(PICOCALC_SD_CS, 0);
	else
		pc_gpio_put(PICOCALC_SD_CS, 1);
}

static void
sd_speed(int fast)
{
	SPI_REG(SSPCR1) = 0;
	SPI_REG(SSPCPSR) = fast ? SD_PRESCALE_FAST : SD_PRESCALE_SLOW;
	SPI_REG(SSPCR0) = SSPCR0_DSS_8 | SSPCR0_SCR(fast ? 0 : SD_SCR_SLOW);
	SPI_REG(SSPCR1) = SSPCR1_SSE;
}

static unsigned
sd_micros(void)
{
	return pc_micros();
}

static const struct sdspi_ops sd_ops = {
	sd_xfer, sd_select, sd_speed, sd_micros
};

static struct sdspi sdcard = { &sd_ops, SD_NONE, 0 };
static struct diskpart sdpt[MAXPARTITIONS + 1];	/* [0] is the whole card. */

/*
 * Open partitions as a bit mask, kept apart for block and raw opens: the
 * kernel reports the last close of each kind separately, so a raw close
 * must not drop a partition a mounted filesystem still holds.
 */
static u_int sd_open[2];
#define	sd_openpart	(sd_open[0] | sd_open[1])
#define	sd_kind(mode)	((mode) == S_IFBLK)
static u_char det_level, det_unsettled, card_changed;
static u_int det_t0;

void
sd_detect_poll(void)
{
	u_int level = (PC_REG32(SIO_GPIO_IN) >> PICOCALC_SD_DET) & 1;

	if (level != det_level) {
		det_level = level;
		det_unsettled = 1;
		det_t0 = pc_micros();
	} else if (det_unsettled &&
	    pc_micros() - det_t0 >= SD_DET_DEBOUNCE_US) {
		det_unsettled = 0;
		card_changed = 1;
	}
}

/* Read the partition table from sector 0, keeping entries inside the card. */
static int
sd_partitions(void)
{
	struct buf *bp = geteblk();
	u_short *w = (u_short *)bp->b_addr;
	int i, error = EIO;

	bzero(sdpt, sizeof(sdpt));
	sdpt[0].dp_nsectors = sdcard.nsectors;
	if (sdspi_read(&sdcard, 0, (u_char *)bp->b_addr) == SDE_OK) {
		error = 0;
		if (w[255] == MBR_MAGIC)
			bcopy(&w[223], &sdpt[1], 64);
		for (i = 1; i <= MAXPARTITIONS; i++) {
			if (sdpt[i].dp_offset > sdcard.nsectors ||
			    sdpt[i].dp_nsectors >
			    sdcard.nsectors - sdpt[i].dp_offset)
				bzero(&sdpt[i], sizeof(sdpt[i]));
			if (sdpt[i].dp_nsectors != 0)
				printf("sd0%c: partition type %02x, sector %u, "
				    "size %u kbytes\n", i + 'a' - 1,
				    sdpt[i].dp_type, sdpt[i].dp_offset,
				    sdpt[i].dp_nsectors / 2);
		}
	}
	brelse(bp);
	return error;
}

/* Bring the card up and read its partition table; 0 or an errno. */
static int
sd_setup(void)
{
	int err = sdspi_init(&sdcard);

	card_changed = 0;
	if (err != SDE_OK)
		return err == SDE_NOCARD ? ENODEV : EIO;
	printf("sd0: %s, %u kbytes\n", sdcard.type == SD_HC ? "SDHC/SDXC" :
	    sdcard.type == SD_V2 ? "SD 2.0" : "SD 1.x", sdcard.nsectors / 2);
	return sd_partitions();
}

int
sdopen(dev_t dev, int flag __unused, int mode)
{
	int part = sdpart(dev), error;

	if (sdunit(dev) != 0 || part > MAXPARTITIONS)
		return ENXIO;
	if (sd_openpart == 0) {
		if ((error = sd_setup()) != 0) {
			sdcard.type = SD_NONE;
			return error;
		}
	} else if (card_changed)
		return EIO;
	if (sdpt[part].dp_nsectors == 0)
		return ENXIO;
	sd_open[sd_kind(mode)] |= 1U << part;
	return 0;
}

int
sdclose(dev_t dev, int flag __unused, int mode)
{
	sd_open[sd_kind(mode)] &= ~(1U << sdpart(dev));
	if (sd_openpart == 0) {
		sdcard.type = SD_NONE;
		card_changed = 0;
	}
	return 0;
}

/* Size in DEV_BSIZE blocks, which is what the block layer asks for. */
daddr_t
sdsize(dev_t dev)
{
	if (sdunit(dev) != 0 || sdpart(dev) > MAXPARTITIONS || sd_openpart == 0)
		return 0;
	return sdpt[sdpart(dev)].dp_nsectors >> 1;
}

void
sdstrategy(struct buf *bp)
{
	int part = sdpart(bp->b_dev);
	struct diskpart *p;
	u_int first, avail, blocks, sector, i, n;
	u_char *addr = (u_char *)bp->b_addr;
	int err = 0;

	/*
	 * physio() subtracts b_resid from the count it saved, and a cached
	 * buffer arrives with whatever its last user left, so every request
	 * states its own residual.
	 */
	bp->b_resid = 0;
	if (sdunit(bp->b_dev) != 0 || part > MAXPARTITIONS ||
	    (sd_openpart & (1U << part)) == 0) {
		bp->b_error = ENXIO;
		goto bad;
	}
	if (card_changed || (bp->b_bcount & (SD_SECTOR - 1)) != 0) {
		bp->b_error = card_changed ? EIO : EINVAL;
		goto bad;
	}
	p = &sdpt[part];
	blocks = (p->dp_nsectors + 1) >> 1;	/* The last may hold one sector. */
	if ((u_int)bp->b_blkno >= blocks) {
		/* Exactly at the end reads as end of file. */
		if ((u_int)bp->b_blkno == blocks) {
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			return;
		}
		bp->b_error = EINVAL;
		goto bad;
	}
	first = (u_int)bp->b_blkno * (DEV_BSIZE / SD_SECTOR);
	avail = p->dp_nsectors - first;
	n = bp->b_bcount / SD_SECTOR;
	if (n > avail) {
		bp->b_resid = (n - avail) * SD_SECTOR;
		n = avail;
	}
	sector = p->dp_offset + first;
	for (i = 0; i < n && err == 0; i++, addr += SD_SECTOR)
		err = (bp->b_flags & B_READ) ?
		    sdspi_read(&sdcard, sector + i, addr) :
		    sdspi_write(&sdcard, sector + i, addr);
	if (err == 0) {
		biodone(bp);
		return;
	}
	bp->b_error = EIO;
bad:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

int
sdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag __unused)
{
	int part = sdpart(dev);

	if (sdunit(dev) != 0 || part > MAXPARTITIONS)
		return ENXIO;
	switch (cmd) {
	case DIOCGETMEDIASIZE:
		*(int *)addr = sdpt[part].dp_nsectors >> 1;
		return 0;
	case DIOCGETPART:
		*(struct diskpart *)addr = sdpt[part];
		return 0;
	case DIOCREINIT:
		/* Read the partition table again after fdisk rewrote it. */
		return sd_partitions();
	}
	return EINVAL;
}

static int
sdprobe(struct conf_device *config __unused)
{
	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_SPI0);
	pc_gpio_output(PICOCALC_SD_CS, 1);
	pc_pin(PICOCALC_SD_SCK, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_SD_TX, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_SD_RX, PC_FUNC_SPI, PC_PADS_PUE);
	pc_pin(PICOCALC_SD_DET, PC_FUNC_SIO, PC_PADS_PUE);
	sd_speed(0);
	det_level = (PC_REG32(SIO_GPIO_IN) >> PICOCALC_SD_DET) & 1;
	printf("sd0: SPI0 on GP%d-GP%d, card detect GP%d\n", PICOCALC_SD_RX,
	    PICOCALC_SD_TX, PICOCALC_SD_DET);
	return 1;
}

struct driver sddriver = {
	"sd", sdprobe,
};
