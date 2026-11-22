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
