/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/dir.h>
#include <errno.h>
#include <unistd.h>

/*
 * dd_seek identifies dd_buf[0], and dd_loc identifies the next entry.
 * Advancing dd_seek only when the buffer is exhausted lets telldir return an
 * allocation-free cookie and lets seekdir reuse every position still resident.
 */

/*
 * get next entry in a directory.
 */
struct direct *
readdir(DIR *dirp)
{
	struct direct *entry;
	long remaining_bytes;
	ssize_t read_count;

	for (;;) {
		if (dirp->dd_loc >= dirp->dd_size) {
			dirp->dd_seek += dirp->dd_size;
			dirp->dd_loc = 0;
			read_count = read(dirp->dd_fd, dirp->dd_buf, DIRBLKSIZ);
			if (read_count <= 0) {
				dirp->dd_size = 0;
				return NULL;
			}
			dirp->dd_size = read_count;
		}
		remaining_bytes = dirp->dd_size - dirp->dd_loc;
		if (remaining_bytes <
		    (long)(sizeof(*entry) - sizeof(entry->d_name))) {
			errno = EIO;
			return NULL;
		}
		entry = (struct direct *)(dirp->dd_buf + dirp->dd_loc);
		if (entry->d_namlen > MAXNAMLEN ||
		    entry->d_reclen < DIRSIZ(entry) ||
		    (entry->d_reclen & 3) != 0 ||
		    entry->d_reclen > remaining_bytes ||
		    entry->d_name[entry->d_namlen] != '\0') {
			errno = EIO;
			return NULL;
		}
		dirp->dd_loc += entry->d_reclen;
		if (entry->d_ino == 0)
			continue;
		return entry;
	}
}
