/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

char *
setbrk(int incr)
{
	register char *a = sbrk(incr);

	/* A refused request leaves the break, and so brkend, where it was. */
	if (a != (char *)-1)
		brkend = a + incr;
	return(a);
}
