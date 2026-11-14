/*
 * Calendar conversion at the 32-bit view of time, compiled by smlrc and
 * linked against the tree's libc. smlrc has no 64-bit integer, so a program
 * it compiles reads the existing names (gmtime, timegm, asctime, time),
 * where time_t is the kernel's unsigned 32-bit count (sys/sys/_time_t.h):
 * a count with bit 31 set is a time after January 2038 and the last count,
 * 4294967295, is 2106-02-07 06:28:15 UTC.
 *
 * Each count prints with the line asctime() gives for it, which the
 * expected output pins to CPython's datetime, and converts back through
 * timegm() and, with TZ set empty so that tzset() selects GMT, through
 * mktime(); consecutive counts order by the count and by the year gmtime()
 * reads. The last count equals the refusal (time_t)-1, so each conversion
 * is told apart from a refusal by the tm_wday sentinel, and the second past
 * the range is refused with the sentinel left. The clock is the execution
 * tier's fixed instant (qemusys.c), 0x90000000. A zone other than GMT stays
 * out: the tier's gettimeofday() reports no zone, and its libc reads no
 * zone file.
 */
#include <time.h>

int printf(char *fmt, ...);
int setenv(char *name, char *value, int rewrite);

static time_t counts[6] = {
	0UL, 2147483647UL, 2147483648UL, 2415919104UL, 4294967294UL,
	4294967295UL
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
	struct tm fields, *tm;
	time_t now, back;
	int i, year, last_year;

	chk("time_t is an unsigned four-byte count",
	    sizeof(time_t) == 4 && (time_t)-1 > 0);

	setenv("TZ", "", 1);
	last_year = -1;
	for (i = 0; i < 6; i++) {
		tm = gmtime(&counts[i]);
		printf("%lu %s", counts[i], asctime(tm));
		year = tm->tm_year;
		fields = *tm;
		fields.tm_wday = -1;
		back = timegm(&fields);
		chk("timegm returns the count", back == counts[i]);
		chk("timegm rewrites the tm_wday sentinel", fields.tm_wday >= 0);
		fields = *tm;
		fields.tm_isdst = -1;
		fields.tm_wday = -1;
		back = mktime(&fields);
		chk("mktime in GMT returns the count", back == counts[i]);
		chk("mktime rewrites the tm_wday sentinel", fields.tm_wday >= 0);
		if (i > 0) {
			chk("the count follows the one before",
			    counts[i] > counts[i - 1]);
			chk("the year is not before the one before",
			    year >= last_year);
		}
		last_year = year;
	}
	chk("the carry out of bit 31 is one second",
	    counts[2] - counts[1] == 1UL);
	chk("0x90000000 is 268435457 seconds after 2147483647",
	    counts[3] - counts[1] == 268435457UL);

	fields = *gmtime(&counts[5]);
	fields.tm_sec++;
	fields.tm_isdst = -1;
	fields.tm_wday = -1;
	chk("mktime refuses the second past 2106-02-07 06:28:15",
	    mktime(&fields) == (time_t)-1 && fields.tm_wday == -1);

	now = time(0);
	printf("clock %lu %s", now, asctime(gmtime(&now)));
	chk("the clock reads after 2038", now > 2147483647UL);
	return 0;
}
