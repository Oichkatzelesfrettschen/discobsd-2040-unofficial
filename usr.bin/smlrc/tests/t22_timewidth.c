/*
 * Two widths of time in one program. This object is compiled by smlrc, which
 * has no 64-bit integer type, and declares the time interface by hand the way
 * a program compiled on the board does: the existing names, a 32-bit unsigned
 * second count and an eight-byte struct timeval. t22_timewidth.gnu.c is
 * compiled by the cross compiler and reads the same clock through 64-bit
 * types by way of wrappers that call these same entry points and widen.
 *
 * The clock is the fixed instant of the execution tier (qemusys.c), whose
 * second count has bit 31 set. Each line prints PASS when the two views
 * agree and neither wrote outside its own object, so the expected output is
 * the oracle. docs/research/time-representation-past-2038.md states the
 * design this exercises.
 */
int printf(char *fmt, ...);

struct timeval {
	unsigned long tv_sec;
	long tv_usec;
};

/*
 * Hand-written, as on the board, and unsigned: libc's time() returns the
 * 32-bit view's time_t, which is the kernel's unsigned count (utime32_t,
 * sys/sys/_time_t.h). struct timeval's tv_sec is a signed long in libc's
 * headers, since the kernel also passes intervals in it; this object reads
 * the clock from it unsigned, which the shared 32-bit representation and
 * register of the procedure call standard make the same value.
 */
int gettimeofday(struct timeval *tv, void *tz);
unsigned long time(unsigned long *t);

/* The cross-compiled companion. */
int wide_sizes(void);
int wide_disagreement(unsigned long sec, long usec);

struct guarded {
	unsigned long before;
	struct timeval tv;
	unsigned long after;
};

void
chk(char *name, int ok)
{
	if (ok)
		printf("PASS %s\n", name);
	else
		printf("FAIL %s\n", name);
}

int
main(void)
{
	struct guarded g;
	unsigned long now, stored;
	int sizes, wrong;

	g.before = 0xa5a5a5a5UL;
	g.after = 0x5a5a5a5aUL;
	g.tv.tv_sec = 0;
	g.tv.tv_usec = 0;
	stored = 0;

	chk("narrow gettimeofday returns 0", gettimeofday(&g.tv, 0) == 0);
	chk("narrow struct timeval is 8 bytes", sizeof(struct timeval) == 8);
	chk("narrow call wrote 8 bytes only",
	    g.before == 0xa5a5a5a5UL && g.after == 0x5a5a5a5aUL);
	chk("narrow seconds have bit 31 set", g.tv.tv_sec == 0x90000000UL);
	chk("narrow seconds read as after 2038", g.tv.tv_sec > 0x7fffffffUL);
	chk("narrow microseconds", g.tv.tv_usec == 250000);

	now = time(&stored);
	chk("narrow time() returns the seconds", now == g.tv.tv_sec);
	chk("narrow time() stores the seconds", stored == g.tv.tv_sec);

	sizes = wide_sizes();
	chk("wide struct timeval is 16 bytes", (sizes >> 8) == 16);
	chk("wide time_t is 8 bytes", (sizes & 255) == 8);

	wrong = wide_disagreement(g.tv.tv_sec, g.tv.tv_usec);
	chk("wide gettimeofday returns 0", (wrong & 1) == 0);
	chk("wide seconds equal the narrow seconds zero-extended",
	    (wrong & 2) == 0);
	chk("wide seconds are positive", (wrong & 4) == 0);
	chk("wide microseconds equal the narrow ones", (wrong & 8) == 0);
	chk("wide call wrote 16 bytes only", (wrong & 16) == 0);
	chk("wide time() returns and stores the same count", (wrong & 32) == 0);
	return 0;
}
