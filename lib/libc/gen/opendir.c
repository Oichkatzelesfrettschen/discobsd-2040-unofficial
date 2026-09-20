/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/dir.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * open a directory.
 */
DIR *
opendir(const char *name)
{
	DIR *dirp;
	int fd;
	int saved_errno;

	fd = open(name, O_RDONLY);
	if (fd == -1)
		return NULL;
	dirp = malloc(sizeof(*dirp));
	if (dirp == NULL) {
		saved_errno = errno;
		(void)close(fd);
		errno = saved_errno;
		return NULL;
	}
	dirp->dd_fd = fd;
	dirp->dd_seek = 0;
	dirp->dd_loc = 0;
	dirp->dd_size = 0;
	return dirp;
}
