/*
 * The negative control for sleep_contract_test.c: sleep() with the two
 * clock readings compared signed, so a sleep spanning the carry out of
 * bit 31 in January 2038 reads the clock as set back and returns 0. The
 * request is slept in chunks like the shipped function, so only the 2038
 * cases reject it.
 */
#include <stddef.h>
#include <sys/time.h>
#include <sys/select.h>

unsigned int	db_sleep(unsigned int);
int		test_gettimeofday(struct timeval *, struct timezone *);
int		test_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

unsigned int
db_sleep(unsigned int seconds)
{
	struct timeval f, s;
	unsigned long chunk, elapsed_seconds;
	int interrupted;

	if (seconds == 0)
		return 0;
	test_gettimeofday(&f, NULL);
	elapsed_seconds = 0;
	do {
		chunk = seconds - elapsed_seconds;
		if (chunk > 1000000UL)
			chunk = 1000000UL;
		s.tv_sec = (long)chunk;
		s.tv_usec = 0;
		interrupted = test_select(0, NULL, NULL, NULL, &s) != 0;
		test_gettimeofday(&s, NULL);
		if (s.tv_sec < f.tv_sec)
			return 0;
		elapsed_seconds = (unsigned long)s.tv_sec -
		    (unsigned long)f.tv_sec;
		if (elapsed_seconds >= seconds)
			return 0;
	} while (!interrupted);
	return seconds - (unsigned int)elapsed_seconds;
}
