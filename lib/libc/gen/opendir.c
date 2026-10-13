/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

/* Validate the opened descriptor before allocating its directory stream. */
DIR *
opendir(const char *path)
{
	DIR *directory;
	struct stat status;
	int descriptor;
	int saved_errno;

	descriptor = open(path, O_RDONLY);
	if (descriptor == -1)
		return NULL;
	if (fstat(descriptor, &status) == -1)
		goto fail;
	if (!S_ISDIR(status.st_mode)) {
		errno = ENOTDIR;
		goto fail;
	}
	if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) == -1)
		goto fail;
	directory = malloc(sizeof(*directory));
	if (directory == NULL)
		goto fail;

	directory->dd_fd = descriptor;
	directory->dd_seek = 0;
	directory->dd_loc = 0;
	directory->dd_size = 0;
	return directory;

fail:
	saved_errno = errno;
	(void)close(descriptor);
	errno = saved_errno;
	return NULL;
}
