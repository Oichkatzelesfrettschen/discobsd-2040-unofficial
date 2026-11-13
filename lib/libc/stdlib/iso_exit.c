#include <stdlib.h>
#include <unistd.h>

/*
 * C17 7.22.4.5: termination that runs no atexit handler and flushes no
 * stream, which is the _exit system call.
 */
_Noreturn void
_Exit(int status)
{
	_exit(status);
	for (;;)
		continue;
}
