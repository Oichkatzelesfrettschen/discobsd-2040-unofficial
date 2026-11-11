/*
 * The <dirent.h> msdosfs_conv.c and msdosfs.c include outside the kernel.
 * DiscoBSD's libc ships <sys/dir.h> (struct direct, 63-byte names) and no
 * <dirent.h>, so the Makefile puts this directory on the include path for
 * the DiscoBSD build only; host builds use the host's header. The only
 * reader is msdosfs_win2unixfn, which assembles a long name into d_name
 * and clamps it to sizeof(d_name) - 1: 255 bytes, the longest FAT name in
 * the single-byte encoding the tool mounts with.
 */
#ifndef MSDOSFS_DISCOBSD_DIRENT_H
#define MSDOSFS_DISCOBSD_DIRENT_H

struct dirent {
	unsigned short d_namlen;
	char d_name[256];
};

#endif /* !MSDOSFS_DISCOBSD_DIRENT_H */
