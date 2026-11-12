/*
 * The part of NetBSD's usr.sbin/makefs/makefs.h the msdosfs forks under
 * usr.sbin/makefs/msdos read: the image descriptor behind every buffer
 * (fsinfo_t), the source node msdosfs_mkfile and msdosfs_mkdire take
 * (fsnode), and the timestamp override (stampst). msdosfs.c fills them.
 */
#ifndef MSDOSFS_COMPAT_MAKEFS_H
#define MSDOSFS_COMPAT_MAKEFS_H

#include <sys/types.h>
#include <sys/stat.h>

typedef struct {
	struct stat st;		/* the source file's status */
} fsinode;

typedef struct {
	fsinode *inode;
	char *name;		/* the last path component */
} fsnode;

typedef struct makefs_fsinfo {
	int fd;			/* the image or device */
	int sectorsize;		/* bytes per bread/bwrite block number */
	off_t offset;		/* byte offset of the file system */
	void *fs_specific;	/* struct msdos_options for msdosfs_mount */
} fsinfo_t;

/*
 * When st_ino is nonzero, every timestamp the forks write and the cluster
 * allocator's starting point come from stampst instead of the clock.
 */
extern struct stat stampst;

#endif /* !MSDOSFS_COMPAT_MAKEFS_H */
