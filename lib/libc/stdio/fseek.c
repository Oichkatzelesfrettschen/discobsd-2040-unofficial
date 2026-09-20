/*
 * Seek for standard library.  Coordinates with buffering.
 */
#include <stdio.h>
#include <unistd.h>

/*
 * Reposition a stream. A seek inside the current read buffer moves the
 * cursor without a system call; any other seek discards the buffer,
 * writing pending output first. On an r+ stream a seek is the C17
 * 7.21.5.3p7 boundary between the two modes, so both mode flags drop and
 * the next getc() or putc() re-enters through _filbuf or _flsbuf.
 */
int
fseek(FILE *iop, long offset, int whence)
{
	int resync, c;
	long p = -1;

	iop->_flag &= ~_IOEOF;
	if (iop->_flag & _IOREAD) {
		if (whence < SEEK_END && iop->_base != NULL &&
		    (iop->_flag & _IONBF) == 0) {
			c = iop->_cnt;
			p = offset;
			if (whence == SEEK_SET) {
				long curpos = lseek(fileno(iop), 0L, SEEK_CUR);

				if (curpos == -1)
					return (-1);
				p += c - curpos;
			} else
				offset -= c;
			if ((iop->_flag & _IORW) == 0 && c > 0 && p <= c &&
			    p >= iop->_base - iop->_ptr) {
				iop->_ptr += (int) p;
				iop->_cnt -= (int) p;
				return (0);
			}
			resync = (int)(offset & 01);
		} else
			resync = 0;
		if (iop->_flag & _IORW) {
			iop->_ptr = iop->_base;
			iop->_flag &= ~_IOREAD;
			resync = 0;
		}
		p = lseek(fileno(iop), offset - resync, whence);
		iop->_cnt = 0;
		if (resync && p != -1)
			if (getc(iop) == EOF)
				p = -1;
	} else if (iop->_flag & (_IOWRT|_IORW)) {
		p = fflush(iop);
		if (iop->_flag & _IORW) {
			iop->_cnt = 0;
			iop->_flag &= ~_IOWRT;
			iop->_ptr = iop->_base;
		}
		return (lseek(fileno(iop), offset, whence) == -1 || p == EOF ?
		    -1 : 0);
	}
	return (p == -1 ? -1 : 0);
}
