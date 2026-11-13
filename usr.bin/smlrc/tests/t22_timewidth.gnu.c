/*
 * The 64-bit view of time for t22_timewidth.c, compiled by the cross
 * compiler because smlrc has no 64-bit integer type.
 *
 * gettimeofday() and time() below are the existing entry points with the
 * kernel's layout: a 32-bit second count read as unsigned, and a struct
 * timeval of two 32-bit words. wide_gettimeofday() and wide_time() are the
 * second set of names a 64-bit time_t gives the cross compiler's programs:
 * each calls the narrow entry point into a narrow object and widens the
 * second count zero-extended, which is what keeps a count with bit 31 set a
 * date after 2038. The cross compiler returns the 64-bit count in r0:r1 and
 * aligns the wide struct to eight bytes; neither reaches the smlrc object,
 * which is handed plain ints.
 */

struct narrow_timeval {
	unsigned long tv_sec;
	long tv_usec;
};

struct wide_timeval {
	long long tv_sec;
	long tv_usec;
};

int gettimeofday(struct narrow_timeval *, void *);
unsigned long time(unsigned long *);

int wide_gettimeofday(struct wide_timeval *, void *);
long long wide_time(long long *);
int wide_sizes(void);
int wide_disagreement(unsigned long, long);

int
wide_gettimeofday(struct wide_timeval *tv, void *tz)
{
	struct narrow_timeval narrow;
	int result;

	result = gettimeofday(&narrow, tz);
	if (result == 0 && tv != 0) {
		tv->tv_sec = (long long)(unsigned long long)narrow.tv_sec;
		tv->tv_usec = narrow.tv_usec;
	}
	return result;
}

long long
wide_time(long long *t)
{
	long long now = (long long)(unsigned long long)time(0);

	if (t != 0)
		*t = now;
	return now;
}

/* sizeof(struct wide_timeval) in the high byte, sizeof the count in the low. */
int
wide_sizes(void)
{
	return (int)(sizeof(struct wide_timeval) << 8 | sizeof(long long));
}

/*
 * One bit per way the wide view departs from the narrow reading the caller
 * took of the same clock: 1 the call failed, 2 the seconds differ from the
 * narrow seconds zero-extended, 4 the seconds are not positive, 8 the
 * microseconds differ, 16 a guard word beside the wide object changed, 32
 * wide_time() returned or stored another count.
 */
int
wide_disagreement(unsigned long sec, long usec)
{
	struct {
		unsigned long long before;
		struct wide_timeval tv;
		unsigned long long after;
	} guarded;
	long long stored = 0;
	long long now;
	int wrong = 0;

	guarded.before = 0xa5a5a5a5a5a5a5a5ULL;
	guarded.after = 0x5a5a5a5a5a5a5a5aULL;
	guarded.tv.tv_sec = 0;
	guarded.tv.tv_usec = 0;

	if (wide_gettimeofday(&guarded.tv, 0) != 0)
		wrong |= 1;
	if (guarded.tv.tv_sec != (long long)(unsigned long long)sec)
		wrong |= 2;
	if (guarded.tv.tv_sec <= 0)
		wrong |= 4;
	if (guarded.tv.tv_usec != usec)
		wrong |= 8;
	if (guarded.before != 0xa5a5a5a5a5a5a5a5ULL ||
	    guarded.after != 0x5a5a5a5a5a5a5a5aULL)
		wrong |= 16;
	now = wide_time(&stored);
	if (now != guarded.tv.tv_sec || stored != now)
		wrong |= 32;
	return wrong;
}
