/*
 * The host half of the host fsck: the only translation unit that reaches the
 * host's <stdio.h> and <sys/stat.h>. See fsck_glue.h.
 */
#include <stdio.h>
#include <sys/stat.h>

#include "fsck_glue.h"

int
fsck_host_stat(const char *path, unsigned *mode, unsigned *dev,
    unsigned *rdev)
{
	struct stat sb;

	if (stat(path, &sb) < 0)
		return -1;
	*mode = (unsigned)sb.st_mode;
	*dev = (unsigned)sb.st_dev;
	*rdev = (unsigned)sb.st_rdev;
	return 0;
}

void
fsck_host_flush(void)
{
	fflush(stdout);
}

int
fsck_host_getc(void)
{
	return fgetc(stdin);
}
