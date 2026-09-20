/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include <sys/param.h>
#include <sys/dir.h>
#include <stdio.h>
#include <unistd.h>

/*
 * seek to an entry in a directory.
 * Only values returned by "telldir" should be passed to seekdir.
 */
void
seekdir(DIR *dirp, long loc)
{
	long base;
	long current_location;
	long offset;
	struct direct *entry;

	current_location = telldir(dirp);
	if (loc < 0 || (loc == current_location && dirp->dd_size == 0))
		return;
	/*
	 * rewinddir maps to cookie zero.  Refetching the first block makes
	 * directory changes visible instead of replaying the resident snapshot.
	 */
	if (loc != 0 && loc >= dirp->dd_seek &&
	    loc <= dirp->dd_seek + dirp->dd_size) {
		dirp->dd_loc = loc - dirp->dd_seek;
		return;
	}
	base = loc - loc % DIRBLKSIZ;
	offset = loc - base;
	if (lseek(dirp->dd_fd, base, SEEK_SET) == (off_t)-1)
		return;
	dirp->dd_seek = base;
	dirp->dd_loc = 0;
	dirp->dd_size = 0;
	while (dirp->dd_loc < offset) {
		entry = readdir(dirp);
		if (entry == NULL)
			return;
	}
}
