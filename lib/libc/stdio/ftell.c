/*
 * Return file offset.
 * Coordinates with buffering.
 */
#include <stdio.h>
#include <unistd.h>

long
ftell(FILE *iop)
{
	long tres;
	int adjust;

	if (iop->_cnt < 0)
		iop->_cnt = 0;
	if (iop->_flag & _IOREAD)
		adjust = -iop->_cnt;
	else if (iop->_flag & (_IOWRT|_IORW)) {
		adjust = 0;
		if ((iop->_flag & _IOWRT) && iop->_base != NULL &&
		    (iop->_flag & _IONBF) == 0)
			adjust = (int)(iop->_ptr - iop->_base);
	} else
		return (-1);
	tres = lseek(fileno(iop), 0L, SEEK_CUR);
	if (tres < 0)
		return (tres);
	return (tres + adjust);
}
