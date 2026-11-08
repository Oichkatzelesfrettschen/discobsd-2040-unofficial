/*
 * Build filesystem images whose highest-numbered inode is in use.
 *
 * tools/fsutil is the host library every root image is built with. It
 * numbers inodes 1 through (isize - 1) * INOPB inclusive, the range the
 * kernel's ialloc() scans, and hands out the highest free inode of its
 * in-core list first, so a volume whose whole inode table is free at the
 * start gives its first new file the last inode. Each layout below builds on
 * that to put one known object at the last inode and then fills the table, as
 * a root image made with FS_INODES equal to its file count does.
 *
 * Layouts (N is the inode count, IMAGE_INODES):
 *   empty          a freshly formatted volume: inodes 1 through 3 (the
 *                  legacy inode, / and lost+found) are in use, the rest are
 *                  free.
 *   file-last      a regular file in / holds inode N; no inode is free.
 *   dir-last       an empty directory in / holds inode N; no inode is free.
 *   orphan-last    file-last, then the file at inode N loses its name but
 *                  keeps its link count: fsck must reconnect it.
 *   free-last      lost+found is renamed old-lf, the file at inode N is
 *                  deleted and one other file loses its name: the only free
 *                  inode is N, and fsck must create lost+found there to
 *                  reconnect the orphan.
 *   full-table     lost+found is renamed old-lf and one file loses its name,
 *                  with every inode in use: fsck has no inode to create
 *                  lost+found in and must decline.
 *   lf-last        lost+found is a new directory at inode N, the original
 *                  is renamed old-lf, and one file loses its name: fsck must
 *                  find lost+found at inode N and reconnect the orphan in it.
 *   lf-hole-last   lf-last, with the slack of ".." in lost+found cut into
 *                  an empty entry (inode 0, no name) behind it. tools/fsutil's
 *                  checker reuses an empty entry before it cuts the slack of
 *                  a live one, so this block is the one that reaches it.
 *   tail-evict     a 96-inode volume (six inode blocks, so fsck holds the
 *                  inodes in two windows). A directory in lost+found whose
 *                  inode is in the second window has a length that is not a
 *                  multiple of 1024, which makes fsck dirty that window
 *                  while it repairs lost+found, and lost+found holds an
 *                  entry for an unallocated inode. A file in the first
 *                  window has a link count of two for one name, so pass 4
 *                  reads an inode of the first window while the second is
 *                  still dirty: the repaired lost+found block must survive
 *                  the write-back of the second window.
 *   orphan-dir     ten files, then a directory with a file inside it at
 *                  a middle inode; the directory loses its name in / but
 *                  keeps its link count and its ".." entry: fsck must
 *                  reconnect it into lost+found and repoint its "..".
 *
 * "--check IMAGE" prints fsutil's own consistency report for an image,
 * opened read-only, so a gate can compare fsutil's file count with fsck's.
 * "--inodes IMAGE" prints the inode counts of the superblock the way the
 * kernel's statfs() derives them (sys/kern/ufs_syscalls2.c): the i-list holds
 * (isize - 1) * INOPB inodes, tinode of them are free and the rest are used,
 * so a gate can compare the figures bin/df reports from the raw device.
 * "--fix IMAGE" opens the image for writing, as fsutil --check --fix does,
 * and lets fsutil repair what it reports.
 * "--lookup IMAGE PATH" prints the inode number PATH names, resolved through
 * fsutil's own directory lookup, and exits 1 when no entry has that name, so
 * a gate can require a repaired entry to carry the name it was given.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdfs.h"

/* The library reads this for its tracing; fsutil(8) defines it. */
int verbose;

#define IMAGE_KBYTES	128
#define IMAGE_INODES	32	/* two inode blocks: inodes 1 through 32 */
#define TAIL_INODES	96	/* six inode blocks: inodes 1 through 96 */

static int
fail(const char *what, const char *name)
{
	fprintf(stderr, "image_gen: %s: %s\n", what, name);
	return 1;
}

/* A one-block regular file named name, directly below the root. */
static int
make_file(fs_t *fs, const char *name)
{
	fs_file_t file;
	unsigned char byte = 'x';

	if (!fs_file_create(fs, &file, name, 0644))
		return 0;
	if (!fs_file_write(&file, &byte, 1)) {
		fs_file_close(&file);
		return 0;
	}
	return fs_file_close(&file);
}

/* A directory named by path, linked back to its parent inode. */
static int
make_dir_in(fs_t *fs, const char *name, unsigned parent_number)
{
	fs_inode_t dir, parent;
	char dotdot[64];

	if (fs_inode_create(fs, &dir, name, INODE_MODE_FDIR | 0755) != 2)
		return 0;
	if (!fs_inode_save(&dir, 1))
		return 0;
	snprintf(dotdot, sizeof dotdot, "%s/..", name);
	if (!fs_inode_link(fs, &dir, dotdot, parent_number))
		return 0;
	if (!fs_inode_get(fs, &parent, parent_number))
		return 0;
	parent.nlink++;
	return fs_inode_save(&parent, 1);
}

/* A directory below the root, linked back to it, as fsutil's manifest does. */
static int
make_dir(fs_t *fs, const char *name)
{
	return make_dir_in(fs, name, BSDFS_ROOT_INODE);
}

/*
 * Turn the slack of the last entry of the first directory block into an
 * empty entry: the record of ".." keeps its 12 bytes and the rest of the
 * block becomes an entry with inode 0, no name and the remaining length.
 */
static int
add_empty_entry(fs_t *fs, const char *name)
{
	fs_inode_t dir;
	unsigned char block[BSDFS_BSIZE];
	unsigned dotdot = 12;	/* "." takes 12 bytes, ".." starts behind it */
	unsigned hole = dotdot + 12;

	if (!fs_inode_lookup(fs, &dir, name))
		return 0;
	if (!fs_inode_read(&dir, 0, block, sizeof block))
		return 0;
	/* ".." must be the last entry, its record running to the block end. */
	if (block[dotdot + 4] + (block[dotdot + 5] << 8) !=
	    (int)(sizeof block - dotdot))
		return 0;
	block[dotdot + 4] = 12;
	block[dotdot + 5] = 0;
	memset(block + hole, 0, sizeof block - hole);
	block[hole + 4] = (sizeof block - hole) & 0xff;
	block[hole + 5] = (sizeof block - hole) >> 8;
	return fs_inode_write(&dir, 0, block, sizeof block) &&
	    fs_inode_save(&dir, 1);
}

/* The inode number a path names, or 0. */
static unsigned
number_of(fs_t *fs, const char *name)
{
	fs_inode_t inode;

	return fs_inode_lookup(fs, &inode, name) ? inode.number : 0;
}

/*
 * Remove the name and keep the inode allocated with its original link
 * count: the count is raised first, and the library's delete lowers it back.
 * The library leaves the lowered inode to its caller to save.
 */
static int
drop_name(fs_t *fs, const char *name)
{
	fs_inode_t inode;

	if (!fs_inode_lookup(fs, &inode, name))
		return 0;
	inode.nlink++;
	if (!fs_inode_save(&inode, 1))
		return 0;
	return fs_inode_delete(fs, &inode, name) &&
	    fs_inode_save(&inode, 1);
}

/* Give the inode a second name, then remove the first. */
static int
rename_to(fs_t *fs, const char *from, const char *to)
{
	fs_inode_t parent;
	unsigned number = number_of(fs, from);

	return number != 0 && fs_inode_link(fs, &parent, to, number) &&
	    drop_name(fs, from);
}

/* Remove the file and free its inode, counting the inode free. */
static int
delete_file(fs_t *fs, const char *name)
{
	fs_inode_t inode;

	if (!fs_inode_lookup(fs, &inode, name))
		return 0;
	if (!fs_inode_delete(fs, &inode, name) ||
	    !fs_inode_save(&inode, 1))
		return 0;
	fs->tinode++;
	fs->dirty = 1;
	return 1;
}

/* An empty regular file: no data block to leak when its inode is dropped. */
static int
make_empty_file(fs_t *fs, const char *name)
{
	fs_file_t file;

	return fs_file_create(fs, &file, name, 0644) && fs_file_close(&file);
}

/* Create /f<first> through /f<last>, one file each. */
static int
fill_files(fs_t *fs, unsigned first, unsigned last)
{
	char name[16];
	unsigned i;

	for (i = first; i <= last; i++) {
		snprintf(name, sizeof name, "/f%02u", i);
		if (!make_file(fs, name))
			return 0;
	}
	return 1;
}

/*
 * The tail-evict layout. tools/fsutil hands out inodes in batches of the 32
 * lowest free numbers, highest first of each batch, so the first allocation
 * of a 96-inode volume is inode 35 and the 33rd is inode 67. /w00 is the
 * first (a low inode, link count raised by one), 31 fillers use up the rest
 * of the batch, and the two that follow, /lost+found/sub and
 * /lost+found/ghost, land in the inode block fsck loads as its second
 * window: 66 and 67 in either order.
 */
static int
build_tail_evict(fs_t *fs)
{
	fs_inode_t inode;
	unsigned sub_number, ghost_number;

	if (!make_file(fs, "/w00") || !fill_files(fs, 1, 31))
		return 0;
	if (!make_dir_in(fs, "/lost+found/sub", BSDFS_LOSTFOUND_INODE) ||
	    !make_empty_file(fs, "/lost+found/ghost"))
		return 0;
	sub_number = number_of(fs, "/lost+found/sub");
	ghost_number = number_of(fs, "/lost+found/ghost");
	if (sub_number < 65 || ghost_number < 65)
		return 0;
	/* The directory's length stops being a multiple of DIRBLKSIZ. */
	if (!fs_inode_get(fs, &inode, sub_number))
		return 0;
	inode.size = 1000;
	if (!fs_inode_save(&inode, 1))
		return 0;
	/* The entry stays; the inode it names is freed without a trace. */
	if (!fs_inode_get(fs, &inode, ghost_number))
		return 0;
	inode.mode = 0;
	inode.nlink = 0;
	if (!fs_inode_save(&inode, 1))
		return 0;
	/* A second link count for the first file, which has one name. */
	if (!fs_inode_lookup(fs, &inode, "/w00") || inode.number >= 65)
		return 0;
	inode.nlink++;
	return fs_inode_save(&inode, 1);
}

/* Inodes 1 through 3 belong to the format; the rest are free at the start. */
#define FREE_AT_START	(IMAGE_INODES - BSDFS_LOSTFOUND_INODE)

static int
build(const char *layout, const char *path)
{
	fs_t fs;

	memset(&fs, 0, sizeof fs);
	if (!fs_create(&fs, path, IMAGE_KBYTES, 0,
	    strcmp(layout, "tail-evict") == 0 ? TAIL_INODES : IMAGE_INODES))
		return fail("cannot create", path);

	if (strcmp(layout, "tail-evict") == 0) {
		if (!build_tail_evict(&fs))
			return fail("cannot build", layout);
	} else if (strcmp(layout, "empty") == 0) {
		/* Nothing more. */
	} else if (strcmp(layout, "dir-last") == 0) {
		if (!make_dir(&fs, "/d00") ||
		    number_of(&fs, "/d00") != IMAGE_INODES ||
		    !fill_files(&fs, 1, FREE_AT_START - 1))
			return fail("cannot build", layout);
	} else if (strcmp(layout, "lf-last") == 0 ||
	    strcmp(layout, "lf-hole-last") == 0) {
		if (!rename_to(&fs, "/lost+found", "/old-lf") ||
		    !make_dir(&fs, "/lost+found") ||
		    number_of(&fs, "/lost+found") != IMAGE_INODES ||
		    (strcmp(layout, "lf-hole-last") == 0 &&
		    !add_empty_entry(&fs, "/lost+found")) ||
		    !fill_files(&fs, 0, FREE_AT_START - 2) ||
		    !drop_name(&fs, "/f05"))
			return fail("cannot build", layout);
	} else if (strcmp(layout, "orphan-dir") == 0) {
		if (!fill_files(&fs, 0, 9) || !make_dir(&fs, "/d00") ||
		    number_of(&fs, "/d00") != IMAGE_INODES - 10 ||
		    !make_file(&fs, "/d00/inner") || !drop_name(&fs, "/d00"))
			return fail("cannot build", layout);
	} else {
		if (!fill_files(&fs, 0, FREE_AT_START - 1) ||
		    number_of(&fs, "/f00") != IMAGE_INODES)
			return fail("cannot build", layout);
		if (strcmp(layout, "file-last") == 0) {
			/* Nothing more. */
		} else if (strcmp(layout, "orphan-last") == 0) {
			if (!drop_name(&fs, "/f00"))
				return fail("cannot build", layout);
		} else if (strcmp(layout, "full-table") == 0) {
			if (!rename_to(&fs, "/lost+found", "/old-lf") ||
			    !drop_name(&fs, "/f05"))
				return fail("cannot build", layout);
		} else if (strcmp(layout, "free-last") == 0) {
			if (!rename_to(&fs, "/lost+found", "/old-lf") ||
			    !delete_file(&fs, "/f00") ||
			    !drop_name(&fs, "/f05"))
				return fail("cannot build", layout);
		} else {
			return fail("unknown layout", layout);
		}
	}
	if (!fs_sync(&fs, 1))
		return fail("cannot write", path);
	fs_close(&fs);
	return 0;
}

static int
check(const char *path, int writable)
{
	fs_t fs;

	if (!fs_open(&fs, path, writable, 0))
		return fail("cannot open", path);
	fs_check(&fs);
	fs_close(&fs);
	return 0;
}

static int
inodes(const char *path)
{
	fs_t fs;
	unsigned total;

	if (!fs_open(&fs, path, 0, 0))
		return fail("cannot open", path);
	total = (fs.isize - 1) * BSDFS_INODES_PER_BLOCK;
	printf("total %u used %u free %u\n", total, total - fs.tinode,
	    fs.tinode);
	fs_close(&fs);
	return 0;
}

static int
lookup(const char *path, const char *name)
{
	fs_t fs;
	fs_inode_t inode;

	if (!fs_open(&fs, path, 0, 0))
		return fail("cannot open", path);
	if (!fs_inode_lookup(&fs, &inode, name)) {
		fs_close(&fs);
		return fail("no entry", name);
	}
	printf("%u\n", inode.number);
	fs_close(&fs);
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--check") == 0)
		return check(argv[2], 0);
	if (argc == 3 && strcmp(argv[1], "--fix") == 0)
		return check(argv[2], 1);
	if (argc == 3 && strcmp(argv[1], "--inodes") == 0)
		return inodes(argv[2]);
	if (argc == 4 && strcmp(argv[1], "--lookup") == 0)
		return lookup(argv[2], argv[3]);
	if (argc == 3)
		return build(argv[1], argv[2]);
	fprintf(stderr,
	    "usage: image_gen LAYOUT IMAGE | --check IMAGE | --fix IMAGE |\n"
	    "       --inodes IMAGE | --lookup IMAGE PATH\n");
	return 2;
}
