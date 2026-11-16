/*
 * A subroutine version of the macro getchar.
 */
#define	USE_STDIO_MACROS
#include <stdio.h>

#undef getchar

int
getchar(void)
{
	return getc(stdin);
}
