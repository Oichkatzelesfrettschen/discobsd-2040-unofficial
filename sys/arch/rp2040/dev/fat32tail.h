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
 * Locator for the root file system that rides in the tail of the PicoCalc
 * loader composite file.
 *
 * The PicoCalc UF2 loader (pelrun/uf2loader) lists the .uf2 files of the
 * /pico1-apps directory on the SD card and programs the blocks of the
 * selected file that pass its block checks into flash (a block at XIP_BASE
 * is dropped, because the loader keeps its own boot2). It ignores each
 * 512-byte chunk that fails the checks. The composite file exploits that:
 * it is the kernel's UF2 blocks, zero padding, and the root file system
 * image after them. tools/picocalc/mkloaderuf2.py writes it;
 * fat32tail_find() locates the root image from inside the running kernel,
 * with a read-only walk of the card's FAT32 file system.
 *
 * Layout (two definitions the gate compares: this header and the builder):
 *
 *	file offset 0		N UF2 blocks of 512 bytes; N is the numBlocks
 *				field of the first block
 *	N * 512 ... head	zero padding
 *	head			root image, to the end of the file
 *
 *	head = roundup(N * 512, FT32_HEAD_ALIGN)
 *
 * The walk reads the boot sector, finds the 8.3 short entry DISCOBSD.UF2
 * (any letter case; deleted entries, labels and long-name parts never match)
 * in the root directory, else in the first subdirectory of the root that
 * holds it, requires the file's cluster chain to be one contiguous run,
 * checks the UF2 head and the whole-sector length of the tail, and checks
 * the DiscoBSD file-system magic words in the tail. The head is a multiple
 * of 32 KiB, so the tail starts on a sector whatever the cluster size.
 * Section names refer to the Microsoft FAT32 File System
 * Specification (version 1.03; "fatgen103"). The file compiles without kernel
 * headers, so tests/fat32tail links it against a sector callback over an
 * image, as tests/picocalc does for sdspi.c.
 */

#ifndef _FAT32TAIL_H
#define _FAT32TAIL_H

#define	FT32_SECTOR		512

/* The composite's head is padded to a multiple of this many bytes. */
#define	FT32_HEAD_ALIGN		32768
#define	FT32_HEAD_SECTORS	(FT32_HEAD_ALIGN / FT32_SECTOR)

/* Results. */
#define	FT32_OK			0
#define	FT32_E_IO		1	/* The read callback failed. */
#define	FT32_E_NOTFAT32		2	/* The boot sector is not a FAT32 BPB. */
#define	FT32_E_ABSENT		3	/* No DISCOBSD.UF2 in the searched directories. */
#define	FT32_E_FRAGMENTED	4	/* The file's clusters are not one run. */
#define	FT32_E_UF2HEAD		5	/* The file does not start with a UF2 head. */
#define	FT32_E_TAIL		6	/* The tail is empty, short or not whole sectors. */
#define	FT32_E_NOMAGIC		7	/* The tail does not start a DiscoBSD file system. */
#define	FT32_E_CORRUPT		8	/* A directory or file chain is damaged or loops. */

/*
 * Read the 512-byte sector at absolute sector number lba into buf; return
 * 0, or nonzero when the transfer fails.
 */
typedef int (*fat32tail_read_fn)(void *ctx, unsigned lba,
    unsigned char buf[FT32_SECTOR]);

/* The tail's extent on the device, in sectors. */
struct fat32tail {
	unsigned	lba;		/* First sector of the tail. */
	unsigned	nsectors;	/* Sectors in the tail. */
};

/*
 * Locate the tail of DISCOBSD.UF2 on the FAT32 volume whose boot sector is
 * at volume_lba (0 for a volume with no partition table). buf is the one
 * sector buffer the walk uses. On FT32_OK, *out holds the extent; any other
 * result leaves *out unchanged.
 */
int	fat32tail_find(fat32tail_read_fn readfn, void *ctx, unsigned volume_lba,
	    unsigned char buf[FT32_SECTOR], struct fat32tail *out);

#endif /* !_FAT32TAIL_H */
