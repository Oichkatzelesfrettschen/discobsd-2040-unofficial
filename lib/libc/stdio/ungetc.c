#include <stdio.h>

/*
 * Push one character back onto a stream, as C17 7.21.7.10 defines.
 *
 * The buffer is the stream's own for a file, but for the _IOSTRG stream
 * that sscanf builds it is the caller's string, which is commonly a
 * string literal. Storing into it there writes through a const pointer:
 * on this target read-only data is part of the RAM-resident process image
 * and the store lands silently, while a host that maps its literals
 * read-only faults. A pushback of the character already in that position,
 * which is every pushback a scanner performs, therefore restores the
 * position without storing. A pushback of a different character still has
 * to store, so it keeps the older behavior and its exposure; a separate
 * pushback buffer, not a conditional store, is what removes that case.
 */
int
ungetc(int c, FILE *iop)
{
	if (c == EOF || (iop->_flag & (_IOREAD|_IORW)) == 0 ||
	    iop->_ptr == NULL || iop->_base == NULL)
		return (EOF);

	if (iop->_ptr == iop->_base) {
		if (iop->_cnt == 0)
			iop->_ptr++;
		else
			return (EOF);
	}

	iop->_cnt++;
	--iop->_ptr;
	if (*iop->_ptr != (char) c)
		*iop->_ptr = (char) c;

	return (c);
}
