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
 * through sd_detect_poll() in the clock interrupt. It keeps the level the
 * open card was accepted at (the committed level) and the level last seen
 * (the candidate); a candidate that differs from the committed level and
 * holds for SD_DET_DEBOUNCE_US marks the card replaced, which fails every
 * transfer with EIO until the last close, because blocks cached for the old
 * card must not reach the new one. A level that returns to the committed
 * level before the interval ends leaves the committed level as it was. The pin's polarity is not
 * interpreted: any stable change counts. DIOCMEDIAGONE reports the flag to
 * unmount(2), which then releases a mount whose write-back the card can no
 * longer take; DIOCGETPART reports the extents blkoverlap() in conf.c compares.
 *
 * Every blocking spl on the RP2040 is PRIMASK (machine/intr.h), and
 * splsoftclock() blocks nothing, so the sections that must exclude the
 * callout use splbio(). They are the acceptance of a card, which sets both
 * levels and clears the replaced flag as one step, and the read of the
 * flag, which sdstrategy() makes before every sector command. The flag is
 * volatile so that the read in the sector loop is a load each time.
 *
 * The driver reads and writes one 512-byte sector per command (CMD17,
 * CMD24), and a transfer is any whole number of sectors. The request names
 * its start as a DEV_BSIZE (1 KiB) block number: physio() derives b_blkno
 * from the file offset shifted right by DEV_BSHIFT, so a raw transfer of one
 * sector, as fdisk makes for the master boot record, addresses the first
 * sector of its block. Because the low ten offset bits are lost on the way
 * to b_blkno, sdread and sdwrite refuse a raw request that does not start at
 * a DEV_BSIZE multiple, or whose non-final segments are not whole blocks,
 * with EINVAL instead of transferring the wrong sector. A request that
 * crosses the end of its partition transfers the sectors that fit and
 * reports the rest in b_resid.
 *
 * physio() advances the file offset by the count minus b_resid whether or not
 * B_ERROR is set, so b_resid names every byte that did not move on each
 * outcome: the whole count for a request refused before any card command,
 * and for a transfer that stops, the failed sector and every sector after it
 * on top of any partition-end truncation. A retry then starts at the first
 * byte that did not transfer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>

#include <machine/board.h>
#include <sys/conf.h>
#include <sys/config.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <machine/dump.h>

#include <rp2040/dev/flash.h>
#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/psram.h>
#include <rp2040/dev/sdspi.h>
#include <rp2040/dev/sd.h>
#ifdef DIAG_STATS
#include <sys/diag_stats.h>
#endif

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
#define	SSPSR_BSY		(1UL << 4)

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

/*
 * The swap partition's bounds in DEV_BSIZE blocks. The swap map's compact
 * descriptors (COMPACT_SWAPMAP, sys/map.h) hold 16-bit sizes and addresses,
 * and every image is a whole number of SWAP_IMAGE_ALIGN blocks, so the map
 * never names more than the largest aligned 16-bit count. A partition that
 * cannot hold two images of SWAP_IMAGE_GUARD blocks behind the reserved
 * first run matches fl1 at best, whose 380 blocks hold one, and is passed
 * over.
 */
#define	SD_SWAP_MAX_BLOCKS	((daddr_t)(0xffff & ~(SWAP_IMAGE_ALIGN - 1)))
#define	SD_SWAP_MIN_BLOCKS	(2 * SWAP_IMAGE_GUARD + SWAP_IMAGE_ALIGN)

/*
 * The scratch partition for /tmp. Minor 5 of each unit (/dev/sd0t) names the
 * first usable entry of type PTYPE_TMP (ba), whichever MBR slot holds it, so
 * /etc/rc.rp2040 mounts it without reading the table; with no usable entry
 * every operation on the minor fails with ENXIO, and the usable entry's own
 * slot minor refuses to open, so the partition has one node. An entry is
 * usable when it
 * holds at least SD_TMP_MIN_BLOCKS and shares zero sectors with another entry
 * of the table, so mkfs on it cannot reach the root, the swap partition or
 * the dump partition. The minimum is the size of the PSRAM region the card
 * replaces (PSRAM_BYTES less PSRAM_SWAP_BYTES, 6,144 kbytes): a smaller card
 * partition would trade the faster /tmp for less room.
 */
#define	SD_TMP_MINOR		5
#define	SD_TMP_MIN_BLOCKS	6144

#define	sdunit(dev)		(minor(dev) >> 3)
#define	sdpart(dev)		sd_part(minor(dev) & 7)

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
	while ((SPI_REG(SSPSR) & SSPSR_BSY) != 0)
		continue;
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

static struct sdspi sdcard = { &sd_ops, SD_NONE, 0, 0 };
static struct diskpart sdpt[MAXPARTITIONS + 1];	/* [0] is the whole card. */

/*
 * Partition slot i shares a sector with another nonempty entry of the
 * table. sd_partitions() kept only entries inside the card, so no end
 * below wraps.
 */
static int
sd_shares_sectors(int i)
{
	const struct diskpart *p = &sdpt[i], *q;

	for (q = &sdpt[1]; q <= &sdpt[MAXPARTITIONS]; q++) {
		if (q != p && q->dp_nsectors != 0 &&
		    q->dp_offset < p->dp_offset + p->dp_nsectors &&
		    p->dp_offset < q->dp_offset + q->dp_nsectors)
			return 1;
	}
	return 0;
}

/* The slot of the usable PTYPE_TMP entry, or MAXPARTITIONS + 1 for none. */
static int
sd_tmp_slot(void)
{
	int i;

	for (i = 1; i <= MAXPARTITIONS; i++) {
		if (sdpt[i].dp_type == PTYPE_TMP &&
		    (sdpt[i].dp_nsectors >> 1) >= SD_TMP_MIN_BLOCKS &&
		    !sd_shares_sectors(i))
			return i;
	}
	return MAXPARTITIONS + 1;
}

/*
 * The table slot a minor names; every caller bounds the result. Kept out of
 * line so the entry scan is one copy rather than one per caller.
 */
static int __attribute__((noinline))
sd_part(int minor)
{
	return minor == SD_TMP_MINOR ? sd_tmp_slot() : minor;
}

/*
 * Open partitions as a bit mask, kept apart for block and raw opens: the
 * kernel reports the last close of each kind separately, so a raw close
 * must not drop a partition a mounted filesystem still holds.
 */
static u_int sd_open[2];
/*
 * The swap partition sdswapattach() chose, held for the life of the boot.
 * It counts as a block open that no close ends. A block descriptor of the
 * partition (/dev/sd0d) shares the partition's bit of sd_open, and the
 * kernel closes a device once, at its last close, so that close would end
 * a hold kept in the bit. /dev/swap opens and closes nothing here
 * (swopen(), sys/dev/swap.c).
 */
static u_int sd_swaphold;
#ifdef SDSWAP_ENABLED
/*
 * The held partition until main() opens it as swapdev. That open is the
 * hold's own and leaves sd_open unchanged, so when swap returns to fl1
 * (sdswap_fallback()) the bits left are those of descriptors, each cleared
 * by its device's last close, which the hold's release must keep.
 */
static u_char sd_swapmain;
#endif
#define	sd_blkopen	(sd_open[1] | sd_swaphold)
#define	sd_openpart	(sd_open[0] | sd_blkopen)
#define	sd_kind(mode)	((mode) == S_IFBLK)

/*
 * Card-detect state. sd_detect_poll() writes all of it from the clock
 * interrupt; sd_accept() and sdclose() write it from process context.
 */
static u_char det_level, det_cand;
static volatile u_char card_changed;
static u_int det_t0;

static u_int
sd_det_pin(void)
{
	return (PC_REG32(SIO_GPIO_IN) >> PICOCALC_SD_DET) & 1;
}

void
sd_detect_poll(void)
{
	u_int level = sd_det_pin();

	if (level != det_cand) {
		det_cand = level;
		det_t0 = pc_micros();
	} else if (det_cand != det_level &&
	    pc_micros() - det_t0 >= SD_DET_DEBOUNCE_US) {
		det_level = det_cand;
		card_changed = 1;
	}
}

/*
 * Take the card just initialized as the baseline of change detection: the
 * pin's present level becomes both the committed and the candidate level,
 * and the replaced flag clears, in one step against the callout. A debounce
 * that was running for this card's insertion ends here instead of expiring
 * against it.
 */
static void
sd_accept(void)
{
	int s = splbio();

	det_level = sd_det_pin();
	det_cand = det_level;
	card_changed = 0;
	splx(s);
}

/* The replaced flag, read once against the callout. */
static int
sd_replaced(void)
{
	int s = splbio();
	int changed = card_changed;

	splx(s);
	return changed;
}

/*
 * The four 16-byte MBR entries at byte 446 of sector 0 are copied into
 * struct diskpart as they lie, so the layout of the structure and the byte
 * order of the target must match the on-disk entry: type at byte 4, start
 * at byte 8 and length at byte 12, both little-endian 32-bit sector
 * counts. A 16-byte structure of one status byte, three bytes, one type
 * byte, three bytes and two 32-bit counts is free of padding, so its size fixes
 * those offsets. A byte-by-byte decode costs 64 bytes more flash on the
 * PSRAM PicoCalc kernel (measured), so the assertions below hold the
 * assumption at build time instead.
 */
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "sd_partitions() copies little-endian MBR entries"
#endif
_Static_assert(sizeof(u_int) == 4 && sizeof(struct diskpart) == 16,
    "struct diskpart is the 16-byte MBR partition entry");

struct sd_mbr {
	struct diskpart slots[MAXPARTITIONS];
	u_short signature;
};

_Static_assert(__builtin_offsetof(struct sd_mbr, signature) == 64,
    "MBR signature follows four partition entries");

static int
sd_read_mbr(struct sdspi *card, struct sd_mbr *table)
{
	/* Exclude any trailing alignment padding after the on-disk signature. */
	return sdspi_read_span(card, 0, (u_char *)table, 446,
	    sizeof(table->slots) + sizeof(table->signature));
}

/*
 * The transfer verifies the sector CRC before the local table replaces
 * the accepted extents. A read error preserves the current mappings.
 */
static int
sd_partitions(void)
{
	struct sd_mbr table;
	struct diskpart *p;
	int i;

	if (sd_read_mbr(&sdcard, &table) != SDE_OK)
		return EIO;
	if (table.signature != MBR_MAGIC)
		bzero(table.slots, sizeof(table.slots));
	for (i = 0; i < MAXPARTITIONS; i++) {
		p = &table.slots[i];
		if (p->dp_offset > sdcard.nsectors ||
		    p->dp_nsectors > sdcard.nsectors - p->dp_offset)
			bzero(p, sizeof(*p));
		if (p->dp_nsectors != 0)
			printf("sd0%c: partition type %02x, sector %u, "
			    "size %u kbytes\n", i + 'a', p->dp_type,
			    p->dp_offset, p->dp_nsectors / 2);
	}
	bcopy(table.slots, &sdpt[1], sizeof(table.slots));
	sdpt[0].dp_nsectors = sdcard.nsectors;
	return 0;
}

/* Bring the card up and read its partition table; 0 or an errno. */
static int
sd_setup(void)
{
	int err = sdspi_init(&sdcard);

	if (err != SDE_OK)
		return err == SDE_NOCARD ? ENODEV : EIO;
	sd_accept();
	printf("sd0: %s, %u kbytes\n", sdcard.type == SD_HC ? "SDHC/SDXC" :
	    sdcard.type == SD_V2 ? "SD 2.0" : "SD 1.x", sdcard.nsectors / 2);
	return sd_partitions();
}

/*
 * The partition sdrootattach() selected and the card it sat on. sdrootattach()
 * closes the card, so the root open brings it up again; the card-detect
 * state restarts there and cannot say whether the medium changed in
 * between. sdopen() therefore requires the slot's type and extent and the
 * card's type and capacity to read as they did at selection. A pulled and
 * replaced card with the same layout and capacity is not distinguishable
 * by these fields. The first successful open of the slot consumes the
 * record (sdroot.slot returns to 0, the whole card, which is never a root).
 */
static struct {
	int	slot;		/* MBR slot chosen; 0 when none is pending. */
	u_int	type;		/* The slot's dp_type, dp_offset, dp_nsectors. */
	u_int	offset;
	u_int	nsectors;
	u_int	cardtype;	/* sdcard.type and sdcard.nsectors. */
	u_int	cardsectors;
} sdroot;

static int
sd_root_bound(int part)
{
	return sdpt[part].dp_type == sdroot.type &&
	    sdpt[part].dp_offset == sdroot.offset &&
	    sdpt[part].dp_nsectors == sdroot.nsectors &&
	    sdcard.type == sdroot.cardtype &&
	    sdcard.nsectors == sdroot.cardsectors;
}

int
sdopen(dev_t dev, int flag __unused, int mode)
{
	int part, error;

	if (sdunit(dev) != 0 || (minor(dev) & 7) > SD_TMP_MINOR)
		return ENXIO;
	if (sd_openpart == 0) {
		if ((error = sd_setup()) != 0) {
			sdcard.type = SD_NONE;
			return error;
		}
	} else if (sd_replaced())
		return EIO;
	part = sdpart(dev);
	if (part > MAXPARTITIONS || sdpt[part].dp_nsectors == 0)
		return ENXIO;
	/*
	 * The usable tmp entry has one identity, minor 5. A second node for the
	 * same sectors would share its open bit, and the close of one node
	 * would end the other's open under a mounted /tmp.
	 */
	if ((minor(dev) & 7) != SD_TMP_MINOR && part == sd_part(SD_TMP_MINOR)) {
		if (sd_openpart == 0)
			sdcard.type = SD_NONE;
		return ENXIO;
	}
	if (sdroot.slot != 0 && part == sdroot.slot) {
		if (!sd_root_bound(part)) {
			if (sd_openpart == 0)
				sdcard.type = SD_NONE;
			return EIO;
		}
		sdroot.slot = 0;
	}
#ifdef SDSWAP_ENABLED
	/* main()'s open of the held partition is the hold's own. */
	if (sd_swapmain == 1U << part && sd_kind(mode)) {
		sd_swapmain = 0;
		return 0;
	}
#endif
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

/*
 * The last sector of a swap transfer. swap_with_buf_result() (vm_swp.c)
 * moves an image segment's exact byte count, which the flash and PSRAM swap
 * devices accept, while the card moves whole sectors. An image extent is a
 * whole number of DEV_BSIZE blocks, so the sector that holds the last bytes
 * belongs to the extent: a write pads it with zeros and a read copies out
 * the bytes asked for, leaving the memory past them alone. The polled
 * strategy routine runs one request at a time, so one buffer serves.
 */
#ifdef UARTUSB_ENABLED
/* usbinit clears DPSRAM before disk attachment. Each transfer fills the
 * entire sector before consuming it. kern.ldscript reserves its range.
 */
_Static_assert(SD_SECTOR == 512, "USB workspace holds one SD sector");
extern u_char sd_tail[SD_SECTOR];
#else
static u_char sd_tail[SD_SECTOR];
#endif

#ifdef DIAG_STATS
/*
 * One sector transfer, counted by outcome with the slowest one kept. The
 * driver reports a CRC mismatch, a data error token and a bad response all
 * as SDE_IO, so IO errors stay one counter.
 */
static int
sd_sector(int reading, u_int sector, u_char *addr)
{
	u_int t0 = pc_micros();
	int err = reading ? sdspi_read(&sdcard, sector, addr) :
	    sdspi_write(&sdcard, sector, addr);

	diag_add(reading ? DIAG_SD_READS : DIAG_SD_WRITES, 1);
	diag_max(DIAG_SD_SLOWEST_US, pc_micros() - t0);
	if (err == SDE_TIMEOUT)
		diag_add(DIAG_SD_TIMEOUTS, 1);
	else if (err != SDE_OK)
		diag_add(DIAG_SD_IO_ERRORS, 1);
	return err;
}
#else
#define sd_sector(reading, sector, addr) ((reading) ? \
	sdspi_read(&sdcard, (sector), (addr)) : \
	sdspi_write(&sdcard, (sector), (addr)))
#endif

static int
sd_tail_io(struct buf *bp, u_int sector, u_char *addr, u_int len)
{
	if (sd_replaced())
		return EIO;
	if (bp->b_flags & B_READ) {
		if (sd_sector(1, sector, sd_tail) != SDE_OK)
			return EIO;
		bcopy(sd_tail, addr, len);
		return 0;
	}
	bcopy(addr, sd_tail, len);
	bzero(sd_tail + len, SD_SECTOR - len);
	return sd_sector(0, sector, sd_tail) != SDE_OK ? EIO : 0;
}

/* Size in DEV_BSIZE blocks, which is what the block layer asks for. */
daddr_t
sdsize(dev_t dev)
{
	daddr_t blocks;

	if (sdunit(dev) != 0 || sdpart(dev) > MAXPARTITIONS || sd_openpart == 0)
		return 0;
	blocks = sdpt[sdpart(dev)].dp_nsectors >> 1;
	/*
	 * init_main() frees this many blocks into the swap map, so the swap
	 * partition reports what the map can name and whole images only.
	 */
	if (dev == swapdev) {
		if (blocks > SD_SWAP_MAX_BLOCKS)
			blocks = SD_SWAP_MAX_BLOCKS;
		blocks &= ~(daddr_t)(SWAP_IMAGE_ALIGN - 1);
	}
	return blocks;
}

void
sdstrategy(struct buf *bp)
{
	int part = sdpart(bp->b_dev);
	struct diskpart *p;
	u_int first, avail, blocks, sector, i, n, tail;
	u_char *addr = (u_char *)bp->b_addr;
	int changed;

	/*
	 * physio() subtracts b_resid from the count it saved, and a cached
	 * buffer arrives with whatever its last user left, so every request
	 * states its own residual.
	 */
	bp->b_resid = 0;
	if (sdunit(bp->b_dev) != 0 || part > MAXPARTITIONS ||
	    (sd_openpart & (1U << part)) == 0) {
		bp->b_error = ENXIO;
		goto refused;
	}
	changed = sd_replaced();
	/* A count that ends inside a sector is the swapper's alone. */
	tail = bp->b_bcount & (SD_SECTOR - 1);
	if (changed || (tail != 0 && bp->b_dev != swapdev)) {
		bp->b_error = changed ? EIO : EINVAL;
		goto refused;
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
		goto refused;
	}
	first = (u_int)bp->b_blkno * (DEV_BSIZE / SD_SECTOR);
	avail = p->dp_nsectors - first;
	n = bp->b_bcount / SD_SECTOR;
	if (n >= avail) {
		bp->b_resid = (n - avail) * SD_SECTOR + tail;
		n = avail;
		tail = 0;
	}
	sector = p->dp_offset + first;
	/*
	 * The callout can declare the card replaced between two commands, so
	 * the flag is read before each one; i counts the sectors that moved.
	 */
	for (i = 0; i < n; i++, addr += SD_SECTOR) {
		if (sd_replaced() ||
		    sd_sector((bp->b_flags & B_READ) != 0, sector + i, addr) !=
		    SDE_OK)
			break;
	}
	if (i == n && tail != 0 && sd_tail_io(bp, sector + n, addr, tail) == 0)
		tail = 0;
	if (i == n && tail == 0) {
		biodone(bp);
		return;
	}
	bp->b_resid += (n - i) * SD_SECTOR + tail;
	bp->b_error = EIO;
	goto failed;
refused:
	bp->b_resid = bp->b_bcount;
failed:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

/*
 * physio() builds b_blkno from uio_offset >> DEV_BSHIFT and then advances
 * by what moved, so a transfer is exact only when it starts on a block and
 * every segment but the last is a whole number of blocks.
 */
static int
sd_misaligned(const struct uio *uio)
{
	int i;

	if ((uio->uio_offset & (DEV_BSIZE - 1)) != 0)
		return 1;
	for (i = 0; i < uio->uio_iovcnt - 1; i++)
		if ((uio->uio_iov[i].iov_len & (DEV_BSIZE - 1)) != 0)
			return 1;
	return 0;
}

int
sdread(dev_t dev, struct uio *uio, int flag)
{
	if (sd_misaligned(uio))
		return EINVAL;
	return rawrw(dev, uio, flag);
}

int
sdwrite(dev_t dev, struct uio *uio, int flag)
{
	if (sd_misaligned(uio))
		return EINVAL;
	return rawrw(dev, uio, flag);
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
	case DIOCMEDIAGONE:
		/*
		 * The removal flag the callout keeps: every transfer already
		 * fails with EIO, so unmount(2) may drop what cannot be written.
		 */
		*(int *)addr = sd_replaced();
		return 0;
	case DIOCREINIT:
		/*
		 * Read the partition table again after fdisk rewrote it. A block
		 * open (a mount, or a descriptor on sd0a to sd0d) holds buffers
		 * and inodes addressed through the current entries, so the table
		 * stays while one exists; fdisk's raw open does not count. A
		 * replaced card is not read: its sector 0 describes another
		 * card than the open partitions.
		 */
		if (sd_blkopen != 0)
			return EBUSY;
		if (sd_replaced())
			return EIO;
		return sd_partitions();
	}
	return EINVAL;
}

/*
 * The crash dump (machine/dump.h). boot() calls sddump() through dumpsys()
 * in machdep.c after a panic or a reboot with RB_DUMP, with every interrupt
 * masked, so it drives the card by polling on a card state of its own and
 * leaves sdcard, the partition table and the open masks as the system left
 * them. It initializes whatever card is in the socket, reads only the
 * partition entries and signature of its sector 0, and takes the first
 * entry of type DUMP_PART_TYPE that lies inside the card.
 *
 * A partition that a block open still holds is refused, because a mount
 * writes to it through the buffer cache: while the card that open accepted
 * is still in the socket, the dump partition must not overlap the extent of
 * any block-open minor, the whole card included. A replaced card cannot hold a
 * mount, since every transfer to it fails until the last close, so its
 * dump partition is free.
 */
struct sd_dumpdev {
	struct sdspi	card;
	u_int		offset;		/* The partition's first sector. */
};

static int
sd_dump_sector(void *arg, u_int sector, const u_char *data, u_int len,
    u_int *crc)
{
	struct sd_dumpdev *dd = arg;

	return sdspi_write_span(&dd->card, dd->offset + sector, data, len,
	    crc) == SDE_OK ? 0 : EIO;
}

/* A block open of the accepted card covers part of the given extent. */
static int
sd_dump_busy(u_int offset, u_int nsectors)
{
	u_int part, end;

	if (card_changed)
		return 0;
	for (part = 0; part <= MAXPARTITIONS; part++) {
		if ((sd_blkopen & (1U << part)) == 0)
			continue;
		end = sdpt[part].dp_offset + sdpt[part].dp_nsectors;
		if (sdpt[part].dp_offset < offset + nsectors && offset < end)
			return 1;
	}
	return 0;
}

int
sddump(dev_t dev, const struct dump_span *span, u_int nspan,
    u_int image, u_int when)
{
	struct sd_dumpdev dd = { { &sd_ops, SD_NONE, 0, 0 }, 0 };
	struct sd_mbr table;
	struct diskpart dp;
	int i, error;

	if (dev != makedev(SD_BMAJOR, 0))
		return ENXIO;
	sd_select(0);
	if (sdspi_init(&dd.card) != SDE_OK) {
		printf("dump: no SD card\n");
		return ENXIO;
	}
	if (sd_read_mbr(&dd.card, &table) != SDE_OK ||
	    table.signature != MBR_MAGIC) {
		printf("dump: no partition table on sd0\n");
		return ENXIO;
	}
	for (i = 0; i < MAXPARTITIONS; i++) {
		dp = table.slots[i];
		if (dp.dp_type == DUMP_PART_TYPE && dp.dp_nsectors != 0 &&
		    dp.dp_offset <= dd.card.nsectors &&
		    dp.dp_nsectors <= dd.card.nsectors - dp.dp_offset)
			break;
	}
	if (i == MAXPARTITIONS) {
		printf("dump: no partition of type %02x on sd0\n",
		    DUMP_PART_TYPE);
		return ENXIO;
	}
	if (sd_dump_busy(dp.dp_offset, dp.dp_nsectors)) {
		printf("dump: sd0%c is in use\n", 'a' + i);
		return EBUSY;
	}
	dd.offset = dp.dp_offset;
	printf("dump: sd0%c, %u kbytes: ", 'a' + i, dp.dp_nsectors / 2);
	error = dump_write(sd_dump_sector, &dd, dp.dp_nsectors, span, nspan,
	    image, when);
	if (error == 0)
		printf("succeeded\n");
	else
		printf("failed, error %d\n", error);
	return error;
}

static int
sdprobe(const struct conf_device *config __unused)
{
	if (board_id() != BOARD_ID_PICOCALC)
		return 0;
	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_SPI0);
	pc_gpio_output(PICOCALC_SD_CS, 1);
	pc_pin(PICOCALC_SD_SCK, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_SD_TX, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_SD_RX, PC_FUNC_SPI, PC_PADS_PUE);
	pc_pin(PICOCALC_SD_DET, PC_FUNC_SIO, PC_PADS_PUE);
	sd_speed(0);
	det_cand = det_level = sd_det_pin();
	printf("sd0: SPI0 on GP%d-GP%d, card detect GP%d\n", PICOCALC_SD_RX,
	    PICOCALC_SD_TX, PICOCALC_SD_DET);
	/* The socket exists: a panic dumps to the card in it (sddump()). */
	dumpdev = makedev(SD_BMAJOR, 0);
	return 1;
}

const struct driver sddriver = {
	"sd", sdprobe,
};

/*
 * Root under the PicoCalc UF2 loader. The loader programs only the kernel's
 * UF2 and lends the flash above it to every other application, so the fl0
 * root region holds foreign bytes and a Dhara resume over them would treat
 * them as a journal. A kernel the loader started therefore takes its root
 * from the first MBR entry of type PTYPE_BSDFFS (b7) on the card, and stops
 * when the card lacks one rather than open fl0. main() copied rootdev into
 * pipedev before services attach, so pipedev moves with it. The card state
 * returns to closed afterwards; the root mount opens it again through
 * sdopen(), which refuses a slot or card that no longer matches the record
 * kept in sdroot. PICOCALC_SD selects the card on every boot. The standard
 * configuration keeps the flash root on an unstamped boot.
 */
void
sdrootattach(void)
{
	int i, error = ENXIO;

#ifdef PICOCALC_SD
	printf("root: configured for SD, root on sd0\n");
#else
	if (!flash_loader_started())
		return;
	printf("root: started by the PicoCalc loader, root on sd0\n");
#endif
	bzero(&sdroot, sizeof(sdroot));
	if (board_id() == BOARD_ID_PICOCALC && (error = sd_setup()) == 0) {
		for (i = 1; i <= MAXPARTITIONS; i++) {
			if (sdpt[i].dp_nsectors != 0 &&
			    sdpt[i].dp_type == PTYPE_BSDFFS)
				break;
		}
		if (i <= MAXPARTITIONS) {
			sdroot.type = sdpt[i].dp_type;
			sdroot.offset = sdpt[i].dp_offset;
			sdroot.nsectors = sdpt[i].dp_nsectors;
			sdroot.cardtype = sdcard.type;
			sdroot.cardsectors = sdcard.nsectors;
			sdroot.slot = i;
			rootdev = pipedev = makedev(SD_BMAJOR, i);
			printf("root: sd0%c\n", 'a' + i - 1);
		} else
			error = ENOENT;
	}
	sdcard.type = SD_NONE;
	if (error == ENOENT)
		panic("root: no partition of type b7 on sd0");
	if (error != 0) {
		printf("root: sd0 error %d\n", error);
		panic("root: sd0 unusable");
	}
}

#ifdef SDSWAP_ENABLED
/* The configured swap device sdswapattach() replaced with the card. */
static dev_t sd_swapfixed;
#endif

/*
 * A usable PTYPE_TMP entry takes /tmp off the PSRAM, whose swap region then
 * spans the whole part (psram_swap_all(), psram_xfer.c) before init_main()
 * sizes the swap map from it. The choice is made once per boot from the
 * table read here; the entry's file system is made by /etc/rc.rp2040.
 */
static void
sd_tmpattach(void)
{
#ifdef PSRAM_ENABLED
	int i = sd_tmp_slot();

	if (i > MAXPARTITIONS)
		return;
	psram_swap_all();
	printf("tmp: sd0%c, swap takes all %u kbytes of the psram\n",
	    'a' + i - 1, (unsigned int)(PSRAM_BYTES / 1024));
#endif
}

/*
 * Swap on the card. A PicoCalc whose PSRAM did not take the swap device
 * (psramprobe() in psram.c) swaps to the first MBR entry of type PTYPE_SWAP
 * (b8) that holds SD_SWAP_MIN_BLOCKS and shares zero sectors with another
 * entry of the table, so swap writes reach neither the b7 root that
 * sdrootattach() chose nor a partition mounted later. Raw QSPI flash,
 * which lacks wear leveling under fl1, then takes zero swap traffic while
 * such a card is in the socket. With no card, no such entry or a card that does not answer, the
 * configured fl1 stays. main() runs this after sdrootattach() and before it
 * opens swapdev. The chosen partition is held in sd_swaphold from here, so
 * that open finds the card up and the table read and no close releases it;
 * every other path leaves the card closed. sdsize() bounds what the swap map
 * is given.
 *
 * With the PSRAM holding swap, the card is read for a usable PTYPE_TMP entry
 * (sd_tmp_slot()): when one exists, sd_tmpattach() gives the PSRAM's whole
 * 8 MB to swap and the entry becomes /dev/sd0t, and with none the PSRAM
 * keeps its 2 MB swap and 6 MB /tmp regions.
 */
void
sdswapattach(void)
{
	int i, error;

	sd_swaphold = 0;
#ifdef SDSWAP_ENABLED
	sd_swapmain = 0;
#endif
	if (board_id() != BOARD_ID_PICOCALC)
		return;
	if ((error = sd_setup()) != 0) {
		sdcard.type = SD_NONE;
		if (error != ENODEV && major(swapdev) != PSRAM_MAJOR)
			printf("swap: sd0 error %d, swap stays on fl1\n", error);
		return;
	}
	if (major(swapdev) == PSRAM_MAJOR) {
		sd_tmpattach();
		sdcard.type = SD_NONE;
		return;
	}
	for (i = 1; i <= MAXPARTITIONS; i++) {
		if (sdpt[i].dp_type == PTYPE_SWAP &&
		    (sdpt[i].dp_nsectors >> 1) >= SD_SWAP_MIN_BLOCKS &&
		    !sd_shares_sectors(i))
			break;
	}
	if (i > MAXPARTITIONS) {
		sdcard.type = SD_NONE;
		return;
	}
	sd_swaphold = 1U << i;
#ifdef SDSWAP_ENABLED
	/* The card can leave: a failed swap transfer is survivable (systm.h). */
	swap_removable = 1;
	sd_swapfixed = swapdev;
	sd_swapmain = sd_swaphold;
#endif
	swapdev = makedev(SD_BMAJOR, i);
	printf("swap: sd0%c\n", 'a' + i - 1);
}

#ifdef SDSWAP_ENABLED
/*
 * Give swap back to the device the configuration named, after the card that
 * held it left (swap_lost_fallback(), sys/kern/vm_swap.c): open it as main()
 * opens swapdev, release the card's partition and make it swapdev again. The
 * result is its size in blocks, or 0 when it does not open, which leaves the
 * card as swapdev and swap on the SwapRAM pool alone.
 */
size_t
sdswap_fallback(void)
{
	dev_t dev = sd_swapfixed;
	int blocks;

	if ((*bdevsw[major(dev)].d_open)(dev, FREAD | FWRITE, S_IFBLK) != 0)
		return 0;
	blocks = (*bdevsw[major(dev)].d_psize)(dev);
	if (blocks <= 0)
		return 0;
	/*
	 * Release the hold. main()'s open of the partition left clear every bit of
	 * sd_open (sd_swapmain), so with no descriptor open the next open
	 * brings a replacement card up; a descriptor's bit stays and keeps a
	 * replacement refused until that descriptor closes.
	 */
	sd_swaphold = 0;
	swapdev = dev;
	printf("swap: sd0 gone, swap on (%d,%d)\n", major(dev), minor(dev));
	return blocks;
}
#endif
