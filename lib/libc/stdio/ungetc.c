#include <stdio.h>

/*
 * Push one character back onto a stream, as C17 7.21.7.10 defines.
 *
 * Three cases, in the order tried. A byte equal to the one just read is
 * satisfied by stepping the cursor back, which stores nothing and so is
 * safe over any buffer, including the string literal sscanf reads from.
 * A differing byte on a string stream goes into the stream's own _ub slot:
 * _cnt is parked in _bufsiz, which a string stream never refills from, and
 * _cnt drops to zero so the next getc() reaches _filbuf, which hands the
 * slot back and restores the count. The buffer of a file stream is the
 * stream's own or one the caller gave setbuf, both writable, so a
 * differing byte there is stored in place as before.
 *
 * The slot holds one byte: a second differing pushback before a read is
 * refused, which is the one-character guarantee C17 makes and no more.
 */
int
ungetc(int c, FILE *iop)
{
	if (c == EOF || (iop->_flag & (_IOREAD|_IORW)) == 0 ||
	    iop->_ptr == NULL || iop->_base == NULL)
		return (EOF);
	if (iop->_flag & _IOUNGET)
		return (EOF);

	/*
	 * An r+ stream still in write mode: the pushback is the first act
	 * of input, so the switch _filbuf would make happens here, writing
	 * pending output and emptying the buffer, or the byte parked below
	 * would be flushed as output by the next refill.
	 */
	if ((iop->_flag & (_IORW|_IOWRT)) == (_IORW|_IOWRT)) {
		if (fflush(iop) == EOF)
			return (EOF);
		iop->_flag &= ~_IOWRT;
		iop->_ptr = iop->_base;
		iop->_cnt = 0;
	}

	/*
	 * A pushback is input: C17 7.21.7.10p2 clears the end-of-file
	 * indicator, and on an r+ stream read mode is on from here, so a
	 * stream that had reached end of file, and so was in neither mode,
	 * does not have the consumed byte mistaken for queued output.
	 */
	iop->_flag &= ~_IOEOF;
	if (iop->_flag & _IORW)
		iop->_flag |= _IOREAD;

	if (iop->_ptr > iop->_base && iop->_ptr[-1] == (char) c) {
		iop->_ptr--;
		iop->_cnt++;
		return (c);
	}

	if (iop->_flag & _IOSTRG) {
		iop->_ub[0] = (unsigned char) c;
		iop->_bufsiz = iop->_cnt;
		iop->_cnt = 0;
		iop->_flag |= _IOUNGET;
		return (c);
	}

	if (iop->_ptr == iop->_base) {
		if (iop->_cnt != 0)
			return (EOF);
		iop->_ptr++;	/* an empty buffer: the byte goes at its base */
	}
	iop->_cnt++;
	*--iop->_ptr = (char) c;
	return (c);
}
