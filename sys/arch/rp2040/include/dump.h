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

#ifndef _MACHINE_DUMP_H_
#define _MACHINE_DUMP_H_

/*
 * The RP2040 crash-dump format, shared by the kernel's writer
 * (rp2040/dump.c) and savecore(8).
 *
 * A dump lives on a dedicated partition of the PicoCalc's SD card, the
 * first MBR entry of type DUMP_PART_TYPE, and never on the flash swap,
 * which a dump would overwrite while it is live. sdprobe() arms sd0 as
 * dumpdev on a PicoCalc; every other kernel keeps "dumps on none" and
 * links no writer.
 *
 * The partition holds DUMP_SECTOR-byte sectors. Sector 0 carries a struct
 * dump_header, then dh_nsegments struct dump_segment entries, then zeros.
 * The segments follow from sector 1 in table order; each starts on a sector
 * boundary and is padded with zeros to the next one. dh_bytes counts every
 * sector written, sector 0 included. Every field is little-endian, the
 * RP2040's byte order, and every CRC is zlib's crc32(), the sum
 * machdep.image_crc reports.
 *
 * The writer fills sector 0 with zeros first, then writes the segments,
 * then the header and table last, so sector 0 holds a valid header only
 * once every segment is on the card and an unfinished dump reads as none.
 * savecore(8) zeroes sector 0 once its copy is on stable storage.
 *
 * Kernel data runs from the start of .data to the end of the SwapRAM pool,
 * so the pool's tables and its compressed bytes travel in
 * DUMP_SEG_KERNEL_DATA as one unit, located by the matching kernel's
 * symbols.
 */
#define	DUMP_MAGIC		0x504d5544u	/* "DUMP" as stored bytes */
#define	DUMP_VERSION		1
#define	DUMP_SECTOR		512
#define	DUMP_PART_TYPE		0xb9		/* MBR partition type */

struct dump_header {
	unsigned int	dh_magic;	/* DUMP_MAGIC */
	unsigned short	dh_version;	/* DUMP_VERSION */
	unsigned short	dh_nsegments;	/* entries in the segment table */
	unsigned int	dh_image_crc;	/* machdep.image_crc of the kernel */
	unsigned int	dh_time;	/* seconds since the epoch at the dump */
	unsigned int	dh_bytes;	/* every sector, sector 0 included */
	unsigned int	dh_crc;		/* header and table, dh_crc as zero */
};

/* What a segment holds, and where it lived. */
#define	DUMP_SEG_MESSAGE	1	/* the panic string, unterminated */
#define	DUMP_SEG_KERNEL_DATA	2	/* .data, .bss and the SwapRAM pool */
#define	DUMP_SEG_UAREA		3	/* u0 or u, USIZE bytes each */

struct dump_segment {
	unsigned int	ds_kind;	/* DUMP_SEG_* */
	unsigned int	ds_address;	/* address of the first byte */
	unsigned int	ds_bytes;	/* length, padding excluded */
	unsigned int	ds_crc;		/* the segment bytes */
};

/* The table shares sector 0 with the header. */
#define	DUMP_MAXSEGMENTS ((DUMP_SECTOR - sizeof(struct dump_header)) / \
	sizeof(struct dump_segment))

/* Sectors a segment of the given length occupies. */
#define	DUMP_SECTORS(bytes)	(((bytes) + DUMP_SECTOR - 1) / DUMP_SECTOR)

_Static_assert(sizeof(struct dump_header) == 24, "dump header layout");
_Static_assert(sizeof(struct dump_segment) == 16, "dump segment layout");

/*
 * savecore(8) copies a dump only when the target file system keeps this
 * much free after the dump and its info file are written, the post-copy
 * rule of 4.4BSD savecore; a minfree file in the dump directory, holding
 * kilobytes, overrides it.
 */
#define	DUMP_RESERVE_BYTES	(1024UL * 1024)

#ifdef KERNEL
/*
 * One region the writer copies. dump_write() computes each CRC over the
 * bytes as they leave for the card, so a region the writer's own stack
 * occupies, the current u area, still reads back with a matching sum.
 */
struct dump_span {
	unsigned int	kind;		/* DUMP_SEG_* */
	const void	*addr;
	unsigned int	bytes;
};

/*
 * The medium: write len bytes of buf and then zeros to one sector of the
 * dump partition, folding each byte sent into *crc when crc is not NULL.
 * Returns 0 on success.
 */
typedef int (*dump_sector_fn)(void *arg, unsigned int sector,
    const unsigned char *buf, unsigned int len, unsigned int *crc);

int	dump_write(dump_sector_fn put, void *arg, unsigned int nsectors,
	    const struct dump_span *span, unsigned int nspan,
	    unsigned int image_crc, unsigned int time);
#endif

#endif	/* _MACHINE_DUMP_H_ */
