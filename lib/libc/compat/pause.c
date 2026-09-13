/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <signal.h>

/*
 * Backwards compatible pause. sigsuspend(2) only returns once a handler
 * has run, always -1 with errno EINTR, and pause(2) reports the same.
 */
int
pause()
{
	sigset_t set;

	(void)sigemptyset(&set);
	return (sigsuspend(&set));
}
