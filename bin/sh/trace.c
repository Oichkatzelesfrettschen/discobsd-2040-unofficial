#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "dup.h"

/*
 * Debugging routine, compiled only when added to OBJS by hand. It appends
 * the format string, unexpanded, to TRACEF; the arguments are accepted and
 * ignored.
 */
#define TRACEF "/usr/abs/sh/trace"

static int fp = (-1);
unsigned was_traced = 0;

void
trace(const char *fmt, ...)
{
	va_list args;
	char buf[256];

	if( fp < 0){
		fp = creat( TRACEF, 0644 );
		if( fp < 0 ) exit(13);
		fcntl( fp, F_SETFD, EXCLOSE );
	}

	va_start( args, fmt );
/*        vsprintf( buf, fmt, args );   */
	strncpy( buf, fmt, sizeof(buf) - 1 );
	buf[sizeof(buf) - 1] = '\0';
	write( fp, buf, strlen(buf));
	va_end( args );
	was_traced++;
}
