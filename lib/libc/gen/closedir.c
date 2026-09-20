/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/dir.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * close a directory.
 */
void
closedir(DIR *dirp)
{
	(void)close(dirp->dd_fd);
	free(dirp);
}
