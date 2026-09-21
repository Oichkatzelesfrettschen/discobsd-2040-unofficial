/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * Point an existing stream at another file. C17 7.21.5.4p2 closes the file
 * the stream held first and ignores any error from that close; the mode is
 * read before the close so a mode _sflags refuses leaves the stream alone.
 */
FILE *
freopen(const char *file, const char *mode, FILE *iop)
{
	int f, oflags, sflag;

	sflag = _sflags(mode, &oflags);
	if (sflag == 0)
		return (NULL);

	(void) fclose(iop);

	f = open(file, oflags, 0666);
	if (f < 0)
		return (NULL);

	if (oflags & O_APPEND)
		(void) lseek(f, (off_t)0, SEEK_END);

	iop->_cnt = 0;
	iop->_file = (short) f;
	iop->_bufsiz = 0;
	iop->_flag = (short) sflag;
	iop->_base = iop->_ptr = NULL;
	return (iop);
}
