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
 *   file-last      a regular file in / holds inode N; no inode is free.
 *   dir-last       an empty directory in / holds inode N; no inode is free.
 *   orphan-last    file-last, then the file at inode N loses its name but
 *                  keeps its link count: fsck must reconnect it.
 *   free-last      lost+found is renamed old-lf, the file at inode N is
 *                  deleted and one other file loses its name: the only free
 *                  inode is N, and fsck must create lost+found there to
 *                  reconnect the orphan.
 *   lf-last        lost+found is a new directory at inode N, the original
 *                  is renamed old-lf, and one file loses its name: fsck must
 *                  find lost+found at inode N and reconnect the orphan in it.
 *
 * "--check IMAGE" prints fsutil's own consistency report for an image,
 * opened read-only, so a gate can compare fsutil's file count with fsck's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdfs.h"

/* The library reads this for its tracing; fsutil(8) defines it. */
int verbose;

#define IMAGE_KBYTES	128
#define IMAGE_INODES	32	/* two inode blocks: inodes 1 through 32 */

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

/* A directory below the root, linked back to it, as fsutil's manifest does. */
static int
make_dir(fs_t *fs, const char *name)
{
	fs_inode_t dir, parent;
	char dotdot[64];

	if (fs_inode_create(fs, &dir, name, INODE_MODE_FDIR | 0755) != 2)
		return 0;
	if (!fs_inode_save(&dir, 1))
		return 0;
	snprintf(dotdot, sizeof dotdot, "%s/..", name);
	if (!fs_inode_link(fs, &dir, dotdot, BSDFS_ROOT_INODE))
		return 0;
	if (!fs_inode_get(fs, &parent, BSDFS_ROOT_INODE))
		return 0;
	parent.nlink++;
	return fs_inode_save(&parent, 1);
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

/* Inodes 1 through 3 belong to the format; the rest are free at the start. */
#define FREE_AT_START	(IMAGE_INODES - BSDFS_LOSTFOUND_INODE)

static int
build(const char *layout, const char *path)
{
	fs_t fs;

	memset(&fs, 0, sizeof fs);
	if (!fs_create(&fs, path, IMAGE_KBYTES, 0, IMAGE_INODES))
		return fail("cannot create", path);

	if (strcmp(layout, "dir-last") == 0) {
		if (!make_dir(&fs, "/d00") ||
		    number_of(&fs, "/d00") != IMAGE_INODES ||
		    !fill_files(&fs, 1, FREE_AT_START - 1))
			return fail("cannot build", layout);
	} else if (strcmp(layout, "lf-last") == 0) {
		if (!rename_to(&fs, "/lost+found", "/old-lf") ||
		    !make_dir(&fs, "/lost+found") ||
		    number_of(&fs, "/lost+found") != IMAGE_INODES ||
		    !fill_files(&fs, 0, FREE_AT_START - 2) ||
		    !drop_name(&fs, "/f05"))
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
check(const char *path)
{
	fs_t fs;

	if (!fs_open(&fs, path, 0, 0))
		return fail("cannot open", path);
	fs_check(&fs);
	fs_close(&fs);
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--check") == 0)
		return check(argv[2]);
	if (argc == 3)
		return build(argv[1], argv[2]);
	fprintf(stderr, "usage: image_gen LAYOUT IMAGE | --check IMAGE\n");
	return 2;
}
