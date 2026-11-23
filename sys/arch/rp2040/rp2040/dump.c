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
 * fit, which writes zero sectors, or EIO when a sector write fails, which leaves
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
