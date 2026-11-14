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
 * Formatter driver for the tail locator gate: a FAT32 volume with a chosen
 * cluster size, made by sbin/newfs_msdos/mkfs_msdos.c itself.
 *
 *	mkfat IMAGE SECTORS SECTORS_PER_CLUSTER
 *
 * usr.sbin/msdosfs newfs sizes a volume in 32-bit bytes and picks the
 * cluster size from the volume size; a 32 KiB cluster FAT32 volume holds at
 * least 65525 clusters, which is 2 GiB, so the gate formats on the host with
 * an explicit cluster size. The image is sparse: mkfs_msdos writes only the
 * reserved sectors, the FATs and the root directory cluster.
 */

#include <sys/types.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mkfs_msdos.h"

int
main(int argc, char **argv)
{
	struct msdos_options options;

	if (argc != 4)
		return 2;
	memset(&options, 0, sizeof(options));
	options.create_size = (off_t)strtoull(argv[2], NULL, 10) * 512;
	options.fat_type = 32;
	options.bytes_per_sector = 512;
	options.sectors_per_cluster = (uint8_t)strtoul(argv[3], NULL, 10);
	options.timestamp_set = 1;
	options.timestamp = 1700000000;
	return mkfs_msdos(argv[1], NULL, &options) == 0 ? 0 : 1;
}
