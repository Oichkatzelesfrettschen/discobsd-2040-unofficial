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
#include <limits.h>

/*
 * FILENAME_MAX is the name length C17 7.21.1 lets a program assume it can
 * open, and namei refuses a longer one, so the two are one number and the
 * header's copy of it is pinned where both are in scope.
 */
_Static_assert(FILENAME_MAX == PATH_MAX,
    "FILENAME_MAX in <stdio.h> is PATH_MAX from <sys/syslimits.h>");

/*
 * Open a stream on a named file. _sflags reads the mode string once and
 * decides the descriptor flags and the stream flag together. An append
 * stream also seeks to the end here, so ftell answers the file's size
 * before the first write; O_APPEND is what holds later writes there.
 */
FILE *
fopen(const char *file, const char *mode)
{
	FILE *iop;
	int f, oflags, sflag;

	sflag = _sflags(mode, &oflags);
	if (sflag == 0)
		return (NULL);

	iop = _findiop();
	if (iop == NULL)
		return (NULL);

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
