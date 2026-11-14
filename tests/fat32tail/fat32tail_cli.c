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
 * Host driver for the tail locator (sys/arch/rp2040/dev/fat32tail.c).
 *
 *	fat32tail_cli IMAGE VOLUME_LBA            one locate
 *	fat32tail_cli IMAGE VOLUME_LBA sweep      a read failure at every read
 *	fat32tail_cli IMAGE VOLUME_LBA fuzz SEED COUNT
 *
 * The locate prints "ok LBA NSECTORS READS", "error NAME READS" or, when the
 * walk passes the read budget, "error budget READS"; the
 * sweep fails the Nth read for each N the locate makes and requires the
 * I/O error every time, then requires the unfailed locate to repeat its
 * answer. The fuzz run damages bytes of four of the sectors a clean walk
 * reads, chosen by a fixed seed, and requires that the walk returns a
 * defined result inside a read budget with an extent that lies inside the
 * image. The sanitizers the gate compiles this with report any out-of-bounds
 * access.
 *
 * The image's bytes are the device: sector n is bytes 512*n of the file,
 * and a read past its end fails.
 */

#define _FILE_OFFSET_BITS 64

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "fat32tail.h"

/*
 * No walk of a sound volume makes more than a few hundred reads; a walk that
 * passes this budget has lost its bound, and the driver stops it and says so.
 */
#define READ_BUDGET	(1ul << 20)

struct dev {
	int		fd;
	unsigned long long nsectors;
	unsigned long	reads;
	unsigned long	fail_at;	/* The read that fails; 0 for none. */
	unsigned long	budget;		/* Reads allowed; 0 for no limit. */
	unsigned long long rng;		/* 0 when no damage is injected. */
	int		damage;		/* Sectors to damage per locate. */
	unsigned long	damage_at[4];
	unsigned long	exceeded;
};

static unsigned long long
next(unsigned long long *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 7;
	*state ^= *state << 17;
	return *state;
}

static int
dev_read(void *ctx, unsigned lba, unsigned char buf[FT32_SECTOR])
{
	struct dev *d = ctx;
	unsigned long n = ++d->reads;
	int i;

	if (d->budget != 0 && n > d->budget) {
		d->exceeded = n;
		return -1;
	}
	if (d->fail_at == n || lba >= d->nsectors)
		return -1;
	if (pread(d->fd, buf, FT32_SECTOR, (off_t)lba * FT32_SECTOR) !=
	    FT32_SECTOR)
		return -1;
	for (i = 0; i < d->damage; i++) {
		if (d->damage_at[i] == n) {
			unsigned long long r = next(&d->rng);

			buf[r % FT32_SECTOR] = (unsigned char)(r >> 32);
			if ((r >> 40) & 1)
				buf[(r >> 8) % 128] ^= (unsigned char)(r >> 48);
		}
	}
	return 0;
}

static const char *
name(int code)
{
	static const char *const names[] = {
		"ok", "io", "notfat32", "absent", "fragmented", "uf2head",
		"tail", "nomagic", "corrupt",
	};

	return code >= 0 && code <= FT32_E_CORRUPT ? names[code] : "unknown";
}

static int
locate(struct dev *d, unsigned volume, struct fat32tail *out)
{
	unsigned char buf[FT32_SECTOR];

	d->reads = 0;
	d->exceeded = 0;
	return fat32tail_find(dev_read, d, volume, buf, out);
}

static int
sweep(struct dev *d, unsigned volume)
{
	struct fat32tail want, got;
	unsigned long total, n;
	int code = locate(d, volume, &want);

	total = d->reads;
	for (n = 1; n <= total; n++) {
		d->fail_at = n;
		code = locate(d, volume, &got);
		if (d->exceeded != 0) {
			printf("FAIL read %lu of %lu failed and the walk passed the read budget\n",
			    n, total);
			return 1;
		}
		if (code != FT32_E_IO) {
			printf("FAIL read %lu of %lu failed and the walk said %s\n",
			    n, total, name(code));
			return 1;
		}
	}
	d->fail_at = 0;
	code = locate(d, volume, &got);
	if (code != FT32_OK || got.lba != want.lba ||
	    got.nsectors != want.nsectors) {
		printf("FAIL the repeated walk said %s\n", name(code));
		return 1;
	}
	printf("sweep ok %lu\n", total);
	return 0;
}

static int
fuzz(struct dev *d, unsigned volume, unsigned long long seed,
    unsigned long count)
{
	struct fat32tail ext;
	unsigned long run, nok = 0, total;
	int code, i;

	if (seed == 0)
		seed = 1;
	(void)locate(d, volume, &ext);
	total = d->reads != 0 ? d->reads : 1;
	d->rng = seed * 0x9e3779b97f4a7c15ull | 1;
	d->damage = 4;
	d->budget = 1ul << 22;
	for (run = 0; run < count; run++) {
		for (i = 0; i < d->damage; i++)
			d->damage_at[i] = 1 + next(&d->rng) % total;
		d->exceeded = 0;
		code = locate(d, volume, &ext);
		if (d->exceeded != 0) {
			printf("FAIL run %lu: more than %lu reads\n", run,
			    d->budget);
			return 1;
		}
		if (code < FT32_OK || code > FT32_E_CORRUPT) {
			printf("FAIL run %lu: result %d\n", run, code);
			return 1;
		}
		if (code == FT32_OK) {
			nok++;
			if (ext.nsectors == 0 || ext.lba < volume ||
			    ext.lba >= d->nsectors ||
			    ext.nsectors > d->nsectors - ext.lba) {
				printf("FAIL run %lu: extent %u+%u outside the image\n",
				    run, ext.lba, ext.nsectors);
				return 1;
			}
		}
	}
	printf("fuzz ok %lu runs, %lu located\n", count, nok);
	return 0;
}

int
main(int argc, char **argv)
{
	struct dev d;
	struct fat32tail ext;
	off_t size;
	unsigned volume;
	int code;

	if (argc < 3)
		return 2;
	memset(&d, 0, sizeof(d));
	volume = (unsigned)strtoul(argv[2], NULL, 10);
	if ((d.fd = open(argv[1], 0)) < 0)
		return 2;
	size = lseek(d.fd, 0, SEEK_END);
	d.nsectors = (unsigned long long)size / FT32_SECTOR;
	d.budget = READ_BUDGET;
	if (argc == 4 && strcmp(argv[3], "sweep") == 0)
		return sweep(&d, volume);
	if (argc == 6 && strcmp(argv[3], "fuzz") == 0)
		return fuzz(&d, volume, strtoull(argv[4], NULL, 10),
		    strtoul(argv[5], NULL, 10));
	if (argc != 3)
		return 2;
	code = locate(&d, volume, &ext);
	if (d.exceeded != 0)
		printf("error budget %lu\n", d.reads);
	else if (code == FT32_OK)
		printf("ok %u %u %lu\n", ext.lba, ext.nsectors, d.reads);
	else
		printf("error %s %lu\n", name(code), d.reads);
	return 0;
}
