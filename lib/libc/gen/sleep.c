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

/*
 * The 1997 note above predates the kernel's timeout limits: select() also
 * fails with EINVAL for a timeout it refuses, and a failure is read as the
 * end of the sleep.
 *
 * One select() call carries at most SLEEP_SELECT_MAX seconds. The kernel
 * refuses a timeout over 100,000,000 s with EINVAL (itimerfix(),
 * sys/kern/kern_time.c) and counts one over 0x7fffffff / HZ s as the largest
 * tick count, which expires early with a return of 0 (hzto(),
 * sys/kern/kern_clock.c). A request longer than the bound is slept in chunks
 * of that length; 1,000,000 s stays inside the exact tick range at any HZ up
 * to 1000 and inside a signed 32-bit tv_sec. A request no longer than the
 * bound is one select(), as before.
 *
 * After each chunk the clock decides how much of the request is slept: a
 * select() that returns 0 with time left (a chunk that ended, or an early
 * timeout) goes round again, and a select() that returns -1 (EINTR from a
 * signal, or any failure) ends the sleep and returns the unslept part.
 *
 * A select() that returns 0 without the clock moving a second, twice in a
 * row, ends the sleep as well. The kernel forms a deadline as the clock plus
 * the timeout modulo 2^32 and treats one that wrapped below the clock as
 * already due (select1() and wall_reached(), sys/kern/sys_generic.c and
 * sys/sys/time.h), so within one chunk of the rollover in February 2106 every
 * select() returns 0 at once. One legitimate select() of a second or more
 * cannot leave the second count unmoved twice running, so the pair of
 * unmoved readings marks the stall and the unslept part is returned, as the
 * single select() of earlier versions returned it.
 *
 * Signals. The sleep is not atomic with respect to a signal handler: a
 * handler that runs after one select() returns 0 and before the next enters
 * the kernel does not interrupt the sleep, which then runs to the end of the
 * next chunk, at most SLEEP_SELECT_MAX seconds after the signal. The window
 * is the few instructions between the two calls and recurs once per chunk;
 * the single select() of earlier versions has the same window once, between
 * the first gettimeofday() and the select(), with the whole request as its
 * bound. Closing it needs a blocked signal mask and pselect(), which cost
 * three system calls and more code than the loop (see
 * docs/research/sleep-timeout-bound.md).
 *
 * Each tv_sec is a reading of the kernel's 32-bit clock, which counts
 * unsigned modulo 2^32 (utime32_t), so the elapsed time is the difference
 * modulo 2^32 and the carry out of bit 31 in January 2038 is an ordinary
 * second. A clock set back reads as an elapsed time near 2^32 and, like an
 * oversleep, returns 0 rather than wrapping the unsigned result.
 */
#define	SLEEP_SELECT_MAX	1000000UL

unsigned int
sleep(unsigned int seconds)
{
	struct timeval f, s;
	unsigned long chunk, elapsed_seconds, previous_seconds;
	int interrupted, idle;

	if (seconds == 0)
		return 0;
	gettimeofday(&f, NULL);
	elapsed_seconds = 0;
	idle = 0;
	do {
		previous_seconds = elapsed_seconds;
		chunk = seconds - elapsed_seconds;
		if (chunk > SLEEP_SELECT_MAX)
			chunk = SLEEP_SELECT_MAX;
		s.tv_sec = (long)chunk;
		s.tv_usec = 0;
		interrupted = select(0, NULL, NULL, NULL, &s) != 0;
		gettimeofday(&s, NULL);
		elapsed_seconds = ((unsigned long)s.tv_sec -
		    (unsigned long)f.tv_sec) & 0xffffffffUL;
		if (elapsed_seconds >= seconds)
			return 0;
		idle = elapsed_seconds == previous_seconds ? idle + 1 : 0;
	} while (!interrupted && idle < 2);
	return seconds - (unsigned int)elapsed_seconds;
}
