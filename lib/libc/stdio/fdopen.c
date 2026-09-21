/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Wrap a stream around an open descriptor. The mode string is repeated by
 * the caller because the descriptor does not carry its own, and _sflags
 * reads it the way fopen does, so "rb+" reaches an update stream here too.
 * The descriptor's own flags are the ones it was opened with, which is why
 * an append mode seeks to the end rather than setting O_APPEND: the seek is
 * the part fdopen can perform on a descriptor it did not open.
 */
#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

FILE *
fdopen(int fd, const char *mode)
{
	static int nofile = -1;
	FILE *iop;
	int oflags, sflag;

	if (nofile < 0)
		nofile = getdtablesize();

	if (fd < 0 || fd >= nofile)
		return (NULL);

	sflag = _sflags(mode, &oflags);
	if (sflag == 0)
		return (NULL);

	iop = _findiop();
	if (iop == NULL)
		return (NULL);

	if (oflags & O_APPEND)
		(void) lseek(fd, (off_t)0, SEEK_END);

	iop->_cnt = 0;
	iop->_file = (short) fd;
	iop->_bufsiz = 0;
	iop->_flag = (short) sflag;
	iop->_base = iop->_ptr = NULL;
	return (iop);
}
