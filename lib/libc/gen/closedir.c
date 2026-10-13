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

/*
 * close a directory.
 */
int
closedir(DIR *dirp)
{
	int result;
	int saved_errno;

	result = close(dirp->dd_fd);
	saved_errno = errno;
	free(dirp);
	errno = saved_errno;
	return result;
}
