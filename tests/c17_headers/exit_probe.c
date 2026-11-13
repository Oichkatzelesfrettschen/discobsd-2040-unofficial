/*
 * lib/libc/stdlib/iso_exit.c under the name db_Exit.  The process registers a
 * handler that exits with another status and leaves bytes in a fully
 * buffered stream, so status 7 with empty output shows that no handler ran
 * and no stream was flushed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

_Noreturn void db_Exit(int);

static void
handler(void)
{
	_exit(99);
}

int
main(void)
{
	static char buffer[BUFSIZ];

	if (atexit(handler) != 0 ||
	    setvbuf(stdout, buffer, _IOFBF, sizeof(buffer)) != 0)
		return 98;
	fputs("unflushed", stdout);
	db_Exit(7);
}
