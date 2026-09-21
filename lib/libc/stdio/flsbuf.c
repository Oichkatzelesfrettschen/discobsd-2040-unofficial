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

/*
 * Flush a stream's write buffer for putc(), which calls here with the
 * character that did not fit. Returns that character, or EOF when the
 * stream cannot be written or a write came up short. On an r+ stream
 * this is also where input mode ends and output mode begins; the switch
 * the other way lives in _filbuf.
 *
 * A line-buffered stream writes when the buffer fills or on a newline; an
 * unbuffered stream writes the one character; a fully buffered stream
 * writes the buffer and stores the character at its base. The first
 * buffer is sized from st_blksize, BUFSIZ when fstat cannot say, and
 * stdout on a terminal becomes line buffered at that moment. A string
 * stream has no write function and its capacity is its buffer, so it
 * returns EOF here and the count stays where the caller left it.
 */
int
_flsbuf(unsigned char c, FILE *iop)
{
	struct stat stbuf;
	char *base;
	long size;
	int n, rn;

	/*
	 * An r+ stream entering write mode from read mode. C17 7.21.5.3p7
	 * allows the switch without a positioning call only at end of
	 * file, where _filbuf has already emptied the buffer; a switch
	 * mid-buffer is undefined, and discarding the read-ahead is the
	 * harmless answer to it. The test is on _IOREAD alone: a
	 * line-buffered stream queues bytes through the putc macro before
	 * write mode is on, and those are output, not read-ahead.
	 */
	if (iop->_flag & _IORW) {
		if (iop->_flag & _IOREAD) {
			iop->_ptr = iop->_base;
			iop->_cnt = 0;
		}
		iop->_flag |= _IOWRT;
		iop->_flag &= ~(_IOEOF|_IOREAD);
	}
	if ((iop->_flag & _IOWRT) == 0)
		return (EOF);
	if (iop->_flag & _IOSTRG)
		return (EOF);

	for (;;) {
		if (iop->_flag & _IOLBF) {
			base = iop->_base;
			*iop->_ptr++ = (char) c;
			if (iop->_ptr >= base + iop->_bufsiz || c == '\n') {
				rn = (int)(iop->_ptr - base);
				n = write(fileno(iop), base, (size_t) rn);
				iop->_ptr = base;
				iop->_cnt = 0;
			} else
				rn = n = 0;
			break;
		}
		if (iop->_flag & _IONBF) {
			rn = 1;
			n = write(fileno(iop), &c, 1);
			iop->_cnt = 0;
			break;
		}
		if ((base = iop->_base) == NULL) {
			if (fstat(fileno(iop), &stbuf) < 0 ||
			    stbuf.st_blksize <= 0)
				size = BUFSIZ;
			else
				size = stbuf.st_blksize;
			if ((iop->_base = base = malloc((size_t) size)) == NULL) {
				iop->_flag |= _IONBF;
				continue;
			}
			iop->_flag |= _IOMYBUF;
			iop->_bufsiz = (int) size;
			if (iop == stdout && isatty(fileno(stdout))) {
				iop->_flag |= _IOLBF;
				iop->_ptr = base;
				continue;
			}
			rn = n = 0;
		} else if ((rn = n = (int)(iop->_ptr - base)) > 0) {
			iop->_ptr = base;
			n = write(fileno(iop), base, (size_t) n);
		}
		iop->_cnt = iop->_bufsiz - 1;
		*base++ = (char) c;
		iop->_ptr = base;
		break;
	}

	if (rn != n) {
		iop->_flag |= _IOERR;
		return (EOF);
	}
	return (c);
}

int
fflush(FILE *iop)
{
	char *base;
	int n;

	/*
	 * C17 7.21.5.2p3 makes a null stream the flush of every stream whose
	 * flush is defined, which is the walk over the open streams; each of
	 * them reaches the single-stream decision below, where a stream with
	 * nothing pending answers zero.
	 */
	if (iop == NULL)
		return (_fwalk(fflush));

	/*
	 * Bytes are pending output when the stream is in write mode, or when
	 * an r+ stream is in neither mode: a line-buffered r+ stream queues
	 * a partial line through the putc macro without reaching _flsbuf, so
	 * _IOWRT is not yet on while the bytes are already there.
	 */
	if ((iop->_flag & _IONBF) == 0 &&
	    ((iop->_flag & _IOWRT) ||
	    (iop->_flag & (_IORW|_IOREAD)) == _IORW) &&
	    (base = iop->_base) != NULL && (n = (int)(iop->_ptr - base)) > 0) {
		iop->_flag |= _IOWRT;
		iop->_ptr = base;
		/*
		 * A fully buffered stream gets its space back; an r+ stream
		 * gets none, so the next getc() or putc() reaches _filbuf or
		 * _flsbuf and the mode switch is seen rather than a stale
		 * count letting getc() read the output buffer as input.
		 */
		iop->_cnt = (iop->_flag & (_IOLBF|_IONBF|_IORW)) ? 0 : iop->_bufsiz;
		if (write(fileno(iop), base, (size_t) n) != n) {
			iop->_flag |= _IOERR;
			return (EOF);
		}
	}
	return (0);
}

int
fclose(FILE *iop)
{
	int r;

	r = EOF;
	if ((iop->_flag & (_IOREAD|_IOWRT|_IORW)) && (iop->_flag & _IOSTRG) == 0) {
		r = fflush(iop);
		if (close(fileno(iop)) < 0)
			r = EOF;
		if (iop->_flag & _IOMYBUF)
			free(iop->_base);
	}
	iop->_cnt = 0;
	iop->_base = NULL;
	iop->_ptr = NULL;
	iop->_bufsiz = 0;
	iop->_flag = 0;
	iop->_file = 0;
	return (r);
}
