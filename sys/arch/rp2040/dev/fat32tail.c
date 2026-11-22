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
 * Locator for the root file system in the tail of DISCOBSD.UF2; see
 * fat32tail.h for the file layout. Section names refer to the Microsoft
 * FAT32 File System Specification, version 1.03 (fatgen103).
 *
 * Every loop is bounded. The directory search stops with FT32_E_CORRUPT
 * after FT32_SEARCH_READS sector reads in all, whatever the chains hold
 * (a looping chain, or sibling entries that alias one long chain), and the
 * file walk raises its cluster number on every step and stays inside the
 * volume. The walk reads sectors through one caller buffer and allocates
 * nothing.
 */

#include "fat32tail.h"

#define	UF2_START0		0x0a324655u	/* "UF2\n" */
#define	UF2_START1		0x9e5d5157u
#define	UF2_END			0x0ab16f30u

/*
 * sys/sys/fs.h: FSMAGIC1 is the first word of struct fs, the first word of
 * block SUPERB (0) of the root image. FSMAGIC2 is the last word of struct
 * fs, which is exactly one DEV_BSIZE (1024) bytes long, so it is the last
 * word of the second sector of the image (tools/fsutil/superblock.c writes
 * the 256 words in this order).
 */
#define	FS_MAGIC1		0x3c3c5346u	/* 'F' 'S' '<' '<' */
#define	FS_MAGIC2		0x53463e3eu	/* '>' '>' 'F' 'S' */
#define	FS_MAGIC2_OFF		(FT32_SECTOR - 4)
#define	FS_TAIL_SECTORS		2

#define	DIR_ENTRY		32
#define	DIR_PER_SECTOR		(FT32_SECTOR / DIR_ENTRY)
#define	ATTR_VOLUME		0x08	/* Volume label; with 0x0f, a long-name part. */
#define	ATTR_DIR		0x10
#define	DIR_END			0x00	/* "FAT Directory Structure": no entry follows. */
#define	DIR_FREE		0xe5	/* A deleted entry. */

#define	FAT_EOC			0x0ffffff8u	/* "FAT Data Structure": end of chain. */
#define	FAT32_MIN_CLUSTERS	65525u
#define	FAT_PER_SECTOR		(FT32_SECTOR / 4)

#define	NOENT			(-1)	/* dir_scan: no match; never an FT32 result. */

/*
 * Sector reads the directory search may make. The bound is the root
 * directory scanned twice (files, then subdirectories) and 16 subdirectories,
 * each of up to 512 sectors (8192 entries), with one FAT read per cluster on
 * volumes whose clusters are one sector: 18 * 512 * 2 = 18432, rounded up.
 * A directory chain is not limited otherwise, so 16 entries that share one
 * long chain cost 16 full scans without it.
 */
#define	FT32_SEARCH_READS	20000u
#define	NO_BUDGET		0xffffffffu

struct vol {
	fat32tail_read_fn	readfn;
	void			*ctx;
	unsigned char		*buf;
	int			valid;		/* buf holds sector bufno. */
	unsigned		bufno;
	unsigned		fat;		/* First sector of the active FAT. */
	unsigned		data;		/* First sector of cluster 2. */
	unsigned		nclusters;	/* Data clusters, numbered from 2. */
	unsigned		spc;		/* Sectors per cluster. */
	unsigned		shift;		/* log2 of the bytes per cluster. */
	unsigned		root;		/* First cluster of the root directory. */
	unsigned		budget;		/* Sector reads left. */
};

struct found {
	unsigned	clus;
	unsigned	size;
};

static unsigned
le16(const unsigned char *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static unsigned
le32(const unsigned char *p)
{
	return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 |
	    (unsigned)p[3] << 24;
}

/* Bring sector n into the buffer; a failed read leaves it unknown. */
static int
rd(struct vol *v, unsigned n)
{
	if (v->valid && v->bufno == n)
		return FT32_OK;
	v->valid = 0;
	if (v->budget == 0)
		return FT32_E_CORRUPT;
	v->budget--;
	if (v->readfn(v->ctx, n, v->buf) != 0)
		return FT32_E_IO;
	v->bufno = n;
	v->valid = 1;
	return FT32_OK;
}

static unsigned
clus_sector(const struct vol *v, unsigned clus)
{
	return v->data + (clus - 2) * v->spc;
}

static int
clus_ok(const struct vol *v, unsigned clus)
{
	return clus >= 2 && clus - 2 < v->nclusters;
}

/* The FAT entry of cluster clus, which the caller has range-checked. */
static int
fat_next(struct vol *v, unsigned clus, unsigned *next)
{
	int r;

	if ((r = rd(v, v->fat + clus / FAT_PER_SECTOR)) != FT32_OK)
		return r;
	*next = le32(v->buf + clus % FAT_PER_SECTOR * 4) & 0x0fffffffu;
	return FT32_OK;
}

/*
 * "Boot Sector and BPB": the sector at lba must describe a FAT32 volume.
 * "FAT Type Determination": the count of data clusters alone separates
 * FAT32 from FAT12 and FAT16, so a BPB with the FAT32 fields and fewer than
 * 65525 clusters is refused. An exFAT boot sector (exFAT specification,
 * "Main Boot Sector") holds zeros at offsets 11 through 63, so its
 * bytes-per-sector field reads 0 and fails the sector-size test.
 */
static int
bpb(struct vol *v, unsigned lba)
{
	const unsigned char *b = v->buf;
	unsigned tot, rsvd, nfats, fatsz, fatn, ext, first, spc;
	int r;

	if ((r = rd(v, lba)) != FT32_OK)
		return r;
	spc = b[13];
	rsvd = le16(b + 14);
	nfats = b[16];
	fatsz = le32(b + 36);
	tot = le16(b + 19) != 0 ? le16(b + 19) : le32(b + 32);
	ext = le16(b + 40);
	if (b[510] != 0x55 || b[511] != 0xaa || le16(b + 11) != FT32_SECTOR ||
	    spc == 0 || (spc & (spc - 1)) != 0 || spc > 128 ||
	    rsvd == 0 || nfats == 0 || le16(b + 17) != 0 ||
	    le16(b + 22) != 0 || fatsz == 0 || tot == 0 || tot <= rsvd ||
	    lba > 0xffffffffu - tot)
		return FT32_E_NOTFAT32;
	if (fatsz > (tot - rsvd) / nfats)
		return FT32_E_NOTFAT32;
	first = rsvd + nfats * fatsz;
	if (first >= tot)
		return FT32_E_NOTFAT32;
	v->nclusters = (tot - first) / spc;
	if (v->nclusters < FAT32_MIN_CLUSTERS || v->nclusters > 0x0ffffff4u ||
	    fatsz < (v->nclusters + 2 + FAT_PER_SECTOR - 1) / FAT_PER_SECTOR)
		return FT32_E_NOTFAT32;
	/* BPB_ExtFlags: with mirroring off, the low bits name the live FAT. */
	fatn = (ext & 0x80) != 0 ? (ext & 0x0f) : 0;
	if (fatn >= nfats)
		return FT32_E_NOTFAT32;
	v->spc = spc;
	for (v->shift = 9; (1u << (v->shift - 9)) != spc; v->shift++)
		;
	v->fat = lba + rsvd + fatn * fatsz;
	v->data = lba + first;
	v->root = le32(b + 44);
	if (!clus_ok(v, v->root))
		return FT32_E_NOTFAT32;
	return FT32_OK;
}

/* "FAT Directory Structure": the 11-byte name "DISCOBSDUF2", any case. */
static int
name_match(const unsigned char *e)
{
	static const unsigned char want[11] = {
		'D', 'I', 'S', 'C', 'O', 'B', 'S', 'D', 'U', 'F', '2'
	};
	unsigned i, c;

	for (i = 0; i < 11; i++) {
		c = e[i];
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		if (c != want[i])
			return 0;
	}
	return 1;
}

static unsigned
entry_cluster(const unsigned char *e)
{
	return le16(e + 20) << 16 | le16(e + 26);
}

/*
 * Scan the directory whose chain starts at clus. With subdirs clear, report
 * the first file entry named DISCOBSD.UF2. With subdirs set, scan each
 * subdirectory of it that way, in directory order. Deleted entries and the
 * volume label and long-name entries (attribute bit 3) never match; the
 * entry that starts with 0x00 ends the directory.
 */
static int
dir_scan(struct vol *v, unsigned clus, int subdirs, struct found *f)
{
	const unsigned char *e;
	unsigned s, i, sector;
	int r;

	for (;;) {
		if (!clus_ok(v, clus))
			return FT32_E_CORRUPT;
		for (s = 0; s < v->spc; s++) {
			sector = clus_sector(v, clus) + s;
			for (i = 0; i < DIR_PER_SECTOR; i++) {
				if ((r = rd(v, sector)) != FT32_OK)
					return r;
				e = v->buf + i * DIR_ENTRY;
				if (e[0] == DIR_END)
					return NOENT;
				if (e[0] == DIR_FREE || (e[11] & ATTR_VOLUME) != 0)
					continue;
				if (!subdirs) {
					if ((e[11] & ATTR_DIR) == 0 && name_match(e)) {
						f->clus = entry_cluster(e);
						f->size = le32(e + 28);
						return FT32_OK;
					}
				} else if ((e[11] & ATTR_DIR) != 0 && e[0] != '.') {
					r = dir_scan(v, entry_cluster(e), 0, f);
					if (r != NOENT)
						return r;
				}
			}
		}
		if ((r = fat_next(v, clus, &clus)) != FT32_OK)
			return r;
		if (clus >= FAT_EOC)
			return NOENT;
	}
}

/*
 * The file's size bytes must lie in one run of consecutive clusters:
 * every link but the last is the next cluster number, and the last
 * names a cluster or ends the chain. Because the cluster number rises on
 * each step and stays inside the volume, the walk ends.
 */
static int
run_check(struct vol *v, const struct found *f)
{
	unsigned c, last, next;
	int r;

	if (!clus_ok(v, f->clus))
		return FT32_E_CORRUPT;
	last = f->clus + ((f->size - 1) >> v->shift);
	for (c = f->clus; ; c++) {
		if ((r = fat_next(v, c, &next)) != FT32_OK)
			return r;
		if (c == last)
			return next >= FAT_EOC || clus_ok(v, next) ?
			    FT32_OK : FT32_E_CORRUPT;
		if (next != c + 1)
			return next >= FAT_EOC || !clus_ok(v, next) ?
			    FT32_E_CORRUPT : FT32_E_FRAGMENTED;
		if (!clus_ok(v, next))
			return FT32_E_CORRUPT;
	}
}

/* The UF2 block at sector n of the file: magic words, number n, total. */
static int
uf2_block(struct vol *v, unsigned n, unsigned total, unsigned first_sector)
{
	const unsigned char *b = v->buf;
	int r;

	if ((r = rd(v, first_sector + n)) != FT32_OK)
		return r;
	if (le32(b) != UF2_START0 || le32(b + 4) != UF2_START1 ||
	    le32(b + 508) != UF2_END || le32(b + 20) != n ||
	    (total != 0 && le32(b + 24) != total))
		return FT32_E_UF2HEAD;
	return FT32_OK;
}

int
fat32tail_find(fat32tail_read_fn readfn, void *ctx, unsigned volume_lba,
    unsigned char buf[FT32_SECTOR], struct fat32tail *out)
{
	struct vol v;
	struct found f;
	unsigned first, nsect, nblocks, headsec;
	int r;

	v.readfn = readfn;
	v.ctx = ctx;
	v.buf = buf;
	v.valid = 0;
	v.budget = FT32_SEARCH_READS;
	if ((r = bpb(&v, volume_lba)) != FT32_OK)
		return r;
	r = dir_scan(&v, v.root, 0, &f);
	if (r == NOENT)
		r = dir_scan(&v, v.root, 1, &f);
	v.budget = NO_BUDGET;
	if (r == NOENT)
		return FT32_E_ABSENT;
	if (r != FT32_OK)
		return r;
	if (f.size < FT32_SECTOR)
		return FT32_E_UF2HEAD;
	if ((r = run_check(&v, &f)) != FT32_OK)
		return r;

	/*
	 * The file is one run, so file sector k is sector first + k. Counts
	 * are in sectors: the file holds fewer than 2^23 of them, so no sum
	 * below reaches 2^32.
	 */
	first = clus_sector(&v, f.clus);
	nsect = f.size / FT32_SECTOR;
	if ((r = uf2_block(&v, 0, 0, first)) != FT32_OK)
		return r;
	nblocks = le32(v.buf + 24);
	if (nblocks == 0 || nblocks > nsect)
		return FT32_E_UF2HEAD;
	if ((r = uf2_block(&v, nblocks - 1, nblocks, first)) != FT32_OK)
		return r;

	headsec = (nblocks + FT32_HEAD_SECTORS - 1) &
	    ~(unsigned)(FT32_HEAD_SECTORS - 1);
	if (f.size % FT32_SECTOR != 0 || nsect < headsec + FS_TAIL_SECTORS)
		return FT32_E_TAIL;

	first += headsec;
	if ((r = rd(&v, first)) != FT32_OK)
		return r;
	if (le32(v.buf) != FS_MAGIC1)
		return FT32_E_NOMAGIC;
	if ((r = rd(&v, first + 1)) != FT32_OK)
		return r;
	if (le32(v.buf + FS_MAGIC2_OFF) != FS_MAGIC2)
		return FT32_E_NOMAGIC;
	out->lba = first;
	out->nsectors = nsect - headsec;
	return FT32_OK;
}
