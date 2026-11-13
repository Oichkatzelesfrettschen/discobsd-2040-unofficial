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
 * The crash-dump stream of machine/dump.h, written through a sector
 * function so that tests/picocalc/dump_test.c runs it against a memory
 * card and the kernel against the SD card (sddump() in dev/sd.c).
 *
 * It runs after panic with every interrupt masked: it allocates nothing,
 * sleeps nowhere and keeps its state on the stack, a header sector image
 * of at most DUMP_SPANS table entries. The sector function owns the
 * transfer and its time bounds.
 */

#include <sys/errno.h>
#include <sys/exec_hsaout.h>
#include <machine/dump.h>

/* Table entries the stack image holds; machdep.c emits four. */
#define	DUMP_SPANS	8

_Static_assert(DUMP_SPANS <= DUMP_MAXSEGMENTS, "the table fits sector 0");

/*
 * Write the spans as a dump on sectors 0 to nsectors - 1. Returns 0, EINVAL
 * for a span list the table cannot hold, ENOSPC when the dump would not
 * fit, which writes nothing, or EIO when a sector write fails, which leaves
 * sector 0 zeroed or untouched and never a valid header.
 */
int
dump_write(dump_sector_fn put, void *arg, unsigned int nsectors,
    const struct dump_span *span, unsigned int nspan,
    unsigned int image_crc, unsigned int time)
{
	struct {
		struct dump_header	h;
		struct dump_segment	s[DUMP_SPANS];
	} sect0;
	const unsigned char *p;
	unsigned int need = 1, sector, left, len, crc, i;

	if (nspan == 0 || nspan > DUMP_SPANS)
		return EINVAL;
	/* At most DUMP_SPANS lengths of 2^23 sectors: the sum cannot wrap. */
	for (i = 0; i < nspan; i++)
		need += DUMP_SECTORS(span[i].bytes);
	if (need > nsectors)
		return ENOSPC;

	/* A length of zero sends a sector of zeros. */
	if (put(arg, 0, (const unsigned char *)"", 0, (unsigned int *)0) != 0)
		return EIO;
	sector = 1;
	for (i = 0; i < nspan; i++) {
		crc = CRC32_INIT;
		p = span[i].addr;
		for (left = span[i].bytes; left != 0; left -= len) {
			len = left < DUMP_SECTOR ? left : DUMP_SECTOR;
			if (put(arg, sector++, p, len, &crc) != 0)
				return EIO;
			p += len;
		}
		sect0.s[i].ds_kind = span[i].kind;
		sect0.s[i].ds_address = (unsigned int)(unsigned long)span[i].addr;
		sect0.s[i].ds_bytes = span[i].bytes;
		sect0.s[i].ds_crc = CRC32_FIN(crc);
	}

	sect0.h.dh_magic = DUMP_MAGIC;
	sect0.h.dh_version = DUMP_VERSION;
	sect0.h.dh_nsegments = nspan;
	sect0.h.dh_image_crc = image_crc;
	sect0.h.dh_time = time;
	sect0.h.dh_bytes = sector * DUMP_SECTOR;
	sect0.h.dh_crc = 0;
	len = sizeof(sect0.h) + nspan * sizeof(sect0.s[0]);
	sect0.h.dh_crc = CRC32_FIN(crc32_update(CRC32_INIT, &sect0, len));
	if (put(arg, 0, (const unsigned char *)&sect0, len,
	    (unsigned int *)0) != 0)
		return EIO;
	return 0;
}
