/*
 * Program: sleep.c
 * Copyright: 1997, sms
 * Author: Steven M. Schultz
 *
 * Version   Date	Modification
 *     1.0  1997/9/25	1. Initial release.
 */

#include <stdio.h>	/* For NULL */
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

/*
 * This implements the sleep(3) function using only 3 system calls instead of
 * the 9 that the old implementation required.  Also this version avoids using
 * signals (with the attendant system overhead) and returns the amount of
 * time left unslept if an interrupt occurs.
 *
 * The error status of gettimeofday is not checked because if that fails the
 * program has scrambled the stack so badly that a sleep() failure is the least
 * problem the program has.  The select() call either completes successfully
 * or is interrupted - no errors to be checked for.
 */
u_int
sleep(seconds)
	u_int seconds;
{
	struct timeval f, s;
	u_long elapsed_seconds;

	if (seconds) {
		gettimeofday(&f, NULL);
		s.tv_sec = seconds;
		s.tv_usec = 0;
		select(0, NULL, NULL, NULL, &s);
		gettimeofday(&s, NULL);
		/*
		 * Each tv_sec is a reading of the kernel's 32-bit clock, which
		 * counts unsigned modulo 2^32 (utime32_t), so the elapsed time
		 * is the difference modulo 2^32 and the carry out of bit 31 in
		 * January 2038 is an ordinary second. A clock set back reads as
		 * an elapsed time near 2^32 and, like an oversleep, returns 0
		 * rather than wrapping the unsigned result.
		 */
		elapsed_seconds = ((u_long)s.tv_sec - (u_long)f.tv_sec) &
		    0xffffffffUL;
		if (elapsed_seconds >= seconds)
			return 0;
		seconds -= (u_int)elapsed_seconds;
	}
	return(seconds);
}
