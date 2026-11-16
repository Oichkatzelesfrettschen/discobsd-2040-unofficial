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
#include <signal.h>
#include <unistd.h>

int	pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *,
	    const sigset_t *);

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
 * Signals. The caller's signal mask is saved and every signal is blocked
 * from before the first clock reading until the sleep ends, and each chunk
 * waits in pselect() with the saved mask, which unblocks the signals only
 * for the wait itself. A handler therefore runs inside a pselect() and ends
 * it with EINTR, and a signal that arrives between two chunks waits blocked
 * and ends the next pselect() at once; none is lost. (A handler that leaves
 * by _longjmp() instead of returning leaves the all-blocked mask in force, as
 * it would after sigsuspend().) pselect() has no declaration in the narrow
 * view of time; its entry point takes the kernel's 32-bit timespec.
 *
 * SLEEP_SELECT_MAX stays 1,000,000 s although a kernel that waits out a
 * saturated tick count (select1(), sys/kern/sys_generic.c) and accepts a
 * timeout up to LONG_MAX would carry a longer chunk: this function also
 * runs over older kernels, which refuse a timeout above 100,000,000 s and
 * wake early above 2,147,483 s. The clock decides how much is slept, so the
 * loop is correct over either kernel, and a chunk costs one system call per
 * 11.6 days.
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
	struct timespec t;
	sigset_t all, saved;
	unsigned long chunk, elapsed_seconds, previous_seconds;
	int interrupted, idle;

	if (seconds == 0)
		return 0;
	all = ~(sigset_t)0;
	sigprocmask(SIG_BLOCK, &all, &saved);
	gettimeofday(&f, NULL);
	elapsed_seconds = 0;
	idle = 0;
	do {
		previous_seconds = elapsed_seconds;
		chunk = seconds - elapsed_seconds;
		if (chunk > SLEEP_SELECT_MAX)
			chunk = SLEEP_SELECT_MAX;
		t.tv_sec = chunk;
		t.tv_nsec = 0;
		interrupted = pselect(0, NULL, NULL, NULL, &t, &saved) != 0;
		gettimeofday(&s, NULL);
		elapsed_seconds = ((unsigned long)s.tv_sec -
		    (unsigned long)f.tv_sec) & 0xffffffffUL;
		if (elapsed_seconds >= seconds) {
			elapsed_seconds = seconds;
			break;
		}
		idle = elapsed_seconds == previous_seconds ? idle + 1 : 0;
	} while (!interrupted && idle < 2);
	sigprocmask(SIG_SETMASK, &saved, NULL);
	return seconds - (unsigned int)elapsed_seconds;
}
