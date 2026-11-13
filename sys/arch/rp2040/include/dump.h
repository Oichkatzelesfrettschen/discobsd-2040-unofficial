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

#include <sys/stdint.h>

/*
 * The RP2040 crash-dump format.
 *
 * Every kernel configuration says "dumps on none", so dumpdev is NODEV and
 * boot() never calls the dump hook: the only swap device is the live flash
 * swap, which a dump would overwrite. A dump needs removable storage. The
 * missing mechanism is a polled panic-time writer to a dedicated, unmounted
 * raw partition on the SD card of an SD_ENABLED kernel, and savecore(8) to
 * copy it out; docs/research/kmem-options-synthesis.md records that scope.
 * This header fixes the on-media format both sides share, and nothing in
 * the kernel includes it until the writer exists, so it costs no byte.
 *
 * A dump is a struct dump_header, nsegments struct dump_segment entries,
 * then the segment bytes in table order. Every field is little-endian.
 * Each CRC is the CRC-32 the kernel computes for machdep.image_crc.
 */
#define	DUMP_MAGIC		0x504d5544u	/* "DUMP" */
#define	DUMP_VERSION		1

struct dump_header {
	uint32_t	dh_magic;	/* DUMP_MAGIC */
	uint16_t	dh_version;	/* DUMP_VERSION */
	uint16_t	dh_nsegments;	/* entries in the segment table */
	uint32_t	dh_image_crc;	/* machdep.image_crc of the kernel */
	uint32_t	dh_time;	/* seconds since the epoch at the dump */
	uint32_t	dh_bytes;	/* header, table and segments */
	uint32_t	dh_crc;		/* the header with dh_crc zero */
};

/* What a segment holds, and where it lived. */
#define	DUMP_SEG_MESSAGE	1	/* the panic string */
#define	DUMP_SEG_KERNEL_DATA	2	/* kernel data and bss */
#define	DUMP_SEG_UAREA		3	/* u or u0, USIZE bytes each */
#define	DUMP_SEG_SWAPRAM_TABLE	4	/* SwapRAM entries and segments */
#define	DUMP_SEG_SWAPRAM_POOL	5	/* the pool; decodes only with its table */

struct dump_segment {
	uint32_t	ds_kind;	/* DUMP_SEG_* */
	uint32_t	ds_address;	/* address of the first byte */
	uint32_t	ds_bytes;	/* length */
	uint32_t	ds_crc;		/* the segment bytes */
};

/*
 * savecore(8) copies a dump only when the target file system keeps this
 * much free after the dump and the kernel are written, the post-copy rule
 * of 4.4BSD savecore; a minfree file in the dump directory overrides it.
 */
#define	DUMP_RESERVE_BYTES	(1024UL * 1024)

#endif	/* _MACHINE_DUMP_H_ */
