/*
 * The negative control for sleep_contract_test.c: sleep() with the two
 * clock readings compared signed, so a sleep spanning the carry out of
 * bit 31 in January 2038 reads the clock as set back and returns 0. The
 * request is slept in chunks, with the signal mask and pselect(), like the
 * shipped function, so only the 2038 cases reject it.
 */
#include <stddef.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>

unsigned int	db_sleep(unsigned int);
int		test_gettimeofday(struct timeval *, struct timezone *);
int		test_pselect(int, fd_set *, fd_set *, fd_set *,
		    const struct timespec *, const sigset_t *);
int		test_sigprocmask(int, const sigset_t *, sigset_t *);

unsigned int
db_sleep(unsigned int seconds)
{
	struct timeval f, s;
	struct timespec t;
	sigset_t all, saved;
	unsigned long chunk, elapsed_seconds, previous_seconds;
	int interrupted, idle;

	if (seconds == 0)
		return 0;
	all = ~(sigset_t)0;
	test_sigprocmask(SIG_BLOCK, &all, &saved);
	test_gettimeofday(&f, NULL);
	elapsed_seconds = 0;
	idle = 0;
	do {
		previous_seconds = elapsed_seconds;
		chunk = seconds - elapsed_seconds;
		if (chunk > 1000000UL)
			chunk = 1000000UL;
		t.tv_sec = chunk;
		t.tv_nsec = 0;
		interrupted = test_pselect(0, NULL, NULL, NULL, &t, &saved) != 0;
		test_gettimeofday(&s, NULL);
		if (s.tv_sec < f.tv_sec) {
			test_sigprocmask(SIG_SETMASK, &saved, NULL);
			return 0;
		}
		elapsed_seconds = (unsigned long)s.tv_sec -
		    (unsigned long)f.tv_sec;
		if (elapsed_seconds >= seconds) {
			test_sigprocmask(SIG_SETMASK, &saved, NULL);
			return 0;
		}
		idle = elapsed_seconds == previous_seconds ? idle + 1 : 0;
	} while (!interrupted && idle < 2);
	test_sigprocmask(SIG_SETMASK, &saved, NULL);
	return seconds - (unsigned int)elapsed_seconds;
}
