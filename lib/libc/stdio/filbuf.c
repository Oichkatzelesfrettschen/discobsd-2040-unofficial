/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

extern char *_smallbuf;	/* findiop.c: one byte per descriptor for unbuffered reads */

/*
 * Refill a stream's read buffer for getc(), which calls here when _cnt
 * runs out. Returns the next byte, or EOF at end of input or on error.
 * On an r+ stream this is also where output mode ends and input mode
 * begins; the switch the other way lives in _flsbuf.
 *
 * The first buffer is sized from st_blksize so a read matches the file
 * system's block, with BUFSIZ when fstat cannot say; an unbuffered stream
 * reads one byte into its _smallbuf slot, or into a stack byte when no
 * slot exists, so that _base stays NULL and no later call frees it.
 */
int
_filbuf(FILE *iop)
{
	struct stat stbuf;
	long size;
	char c;

	/*
	 * An r+ stream entering read mode: C17 7.21.5.3p7 requires an
	 * fflush or a positioning call between output and input, and
	 * fflush leaves _cnt zero on such a stream so the read arrives
	 * here. Pending output is written before the buffer is reused
	 * for input, and the write flag drops so the two modes never hold
	 * the buffer at once.
	 */
	if (iop->_flag & _IORW) {
		if (iop->_flag & _IOWRT) {
			if (fflush(iop) == EOF)
				return (EOF);
			iop->_flag &= ~_IOWRT;
		}
		iop->_flag |= _IOREAD;
	}
	if ((iop->_flag & _IOREAD) == 0)
		return (EOF);

	/* A byte ungetc parked in the slot comes back before anything else. */
	if (iop->_flag & _IOUNGET) {
		iop->_flag &= ~_IOUNGET;
		iop->_cnt = iop->_bufsiz;
		return (iop->_ub[0]);
	}

	if (iop->_flag & (_IOSTRG|_IOEOF))
		return (EOF);

	for (;;) {
		if (iop->_base != NULL)
			break;
		if (iop->_flag & _IONBF) {
			iop->_base = _smallbuf ? &_smallbuf[fileno(iop)] : &c;
			break;
		}
		if (fstat(fileno(iop), &stbuf) < 0 || stbuf.st_blksize <= 0)
			size = BUFSIZ;
		else
			size = stbuf.st_blksize;
		if ((iop->_base = malloc((size_t) size)) == NULL) {
			iop->_flag |= _IONBF;
			continue;
		}
		iop->_flag |= _IOMYBUF;
		iop->_bufsiz = (int) size;
		break;
	}

	/*
	 * Reading the terminal flushes line-buffered output first, so a
	 * prompt written without a newline is visible before the wait.
	 */
	if (iop == stdin) {
		if (stdout->_flag & _IOLBF)
			fflush(stdout);
		if (stderr->_flag & _IOLBF)
			fflush(stderr);
	}

	iop->_cnt = read(fileno(iop), iop->_base,
	    (iop->_flag & _IONBF) ? 1 : (size_t) iop->_bufsiz);
	iop->_ptr = iop->_base;
	if ((iop->_flag & _IONBF) && iop->_base == &c)
		iop->_base = NULL;
	if (--iop->_cnt < 0) {
		if (iop->_cnt == -1) {
			iop->_flag |= _IOEOF;
			/*
			 * End of file on an r+ stream ends read mode, and
			 * the cursor returns to the buffer's base so that
			 * no consumed read-ahead is left for _flsbuf to
			 * mistake for queued output.
			 */
			if (iop->_flag & _IORW) {
				iop->_flag &= ~_IOREAD;
				iop->_ptr = iop->_base;
			}
		} else
			iop->_flag |= _IOERR;
		iop->_cnt = 0;
		return (EOF);
	}
	return (*iop->_ptr++ & 0377);
}
