/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/dir.h>

/*
 * return a pointer into a directory
 */
long
telldir(DIR *dirp)
{
	return dirp->dd_seek + dirp->dd_loc;
}
