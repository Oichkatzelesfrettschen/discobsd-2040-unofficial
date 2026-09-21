/*
 * Contract gate for lib/libc/gen/ctime.c's zone file reader, which is the
 * one part of libc that decodes a file a caller names. TZ may name an
 * absolute path, so the bytes tzload() parses are the caller's, and every
 * count in them bounds a walk over a fixed-size array.
 *
 * The gate writes the files itself, of the first version, which is the one
 * this tree's tzload reads: thirty-two reserved bytes, three counts, then
 * the transition times, their type indices, the local time types, and the
 * abbreviation characters. A file is accepted or refused as a whole, so the
 * cases here differ from a well-formed one in a single field each.
 *
 * Three properties are at stake.
 *
 *   - ttis[] holds TZ_MAX_TYPES entries and ats[] holds TZ_MAX_TIMES, which
 *     is the larger. localtime()'s search for a standard-time type walks
 *     ttis[] and must stop at typecnt; a file whose types are all daylight
 *     and whose transition count exceeds its type count is what tells the
 *     two bounds apart.
 *   - tzname[] holds two pointers and localtime() indexes it with a type's
 *     daylight flag, which tzload reads as a whole byte. A flag outside
 *     {0, 1} therefore writes a pointer past that array, so the file has to
 *     be refused before localtime() sees it.
 *   - Each four-byte value is two's complement and long is wider than that
 *     on some hosts, so an offset west of Greenwich has to arrive negative.
 */
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

int		creat(const char *, mode_t);
void		db_tzset(void);
struct tm      *db_localtime(const time_t *);

static int failure_count;
static int check_count;

static void
check(int condition, const char *message)
{
	++check_count;
	if (condition)
		return;
	(void)write(STDERR_FILENO, "FAIL ", 5);
	(void)write(STDERR_FILENO, message, strlen(message));
	(void)write(STDERR_FILENO, "\n", 1);
	++failure_count;
}

#define ZONE_PATH	"/tmp/discobsd_zone_contract"

/* TZ_MAX_TYPES is 10 and TZ_MAX_TIMES is 370; the gate needs both bounds. */
#define MAX_TYPES	10
#define MAX_TIMES	370

static void
put32(unsigned char *p, long v)
{
	unsigned long u = (unsigned long)v;

	p[0] = (unsigned char)((u >> 24) & 0xff);
	p[1] = (unsigned char)((u >> 16) & 0xff);
	p[2] = (unsigned char)((u >> 8) & 0xff);
	p[3] = (unsigned char)(u & 0xff);
}

/*
 * A zone file with the counts and contents the caller asks for. isdst_of
 * gives each type's daylight flag, so a case can name one the file format
 * permits and the array bound does not.
 */
static int
write_zone(int timecnt, int typecnt, int charcnt, long gmtoff,
    const unsigned char *isdst_of)
{
	unsigned char buf[4096];
	unsigned char *p;
	int fd, i;
	size_t len;

	memset(buf, 0, sizeof(buf));
	memcpy(buf, "TZif", 4);			/* the span tzload skips */
	put32(buf + 32, timecnt);
	put32(buf + 36, typecnt);
	put32(buf + 40, charcnt);
	p = buf + 44;
	for (i = 0; i < timecnt; i++) {		/* transition times */
		put32(p, (long)i * 86400L);
		p += 4;
	}
	for (i = 0; i < timecnt; i++)		/* their type indices */
		*p++ = (unsigned char)(i % (typecnt > 0 ? typecnt : 1));
	for (i = 0; i < typecnt; i++) {		/* the local time types */
		put32(p, gmtoff);
		p += 4;
		*p++ = isdst_of != NULL ? isdst_of[i] : 0;
		*p++ = 0;			/* tt_abbrind */
	}
	for (i = 0; i < charcnt; i++)		/* the abbreviation */
		*p++ = (unsigned char)(i + 1 == charcnt ? '\0' : 'X');
	len = (size_t)(p - buf);

	/*
	 * creat(), not open(): the open flags this tree's fcntl.h spells are
	 * the target kernel's numbers rather than the host kernel's.
	 */
	fd = creat(ZONE_PATH, 0644);
	if (fd == -1)
		return (-1);
	if (write(fd, buf, len) != (ssize_t)len) {
		(void)close(fd);
		return (-1);
	}
	return (close(fd));
}

/* Load the file just written and answer what localtime() made of it. */
static struct tm *
load_and_read(time_t when)
{
	putenv("TZ=" ZONE_PATH);
	db_tzset();
	return (db_localtime(&when));
}

static void
use_gmt(void)
{
	putenv("TZ=");
	db_tzset();
}

#ifdef TZ_ZONEINFO
/*
 * A well-formed file, so the cases that follow differ from something that
 * works rather than from nothing.
 */
static void
test_well_formed_zone(void)
{
	static const unsigned char isdst[2] = { 0, 1 };
	struct tm *tm;
	time_t t = 1000000000L;

	check(write_zone(4, 2, 4, -18000L, isdst) == 0,
	    "fixture: the well-formed zone could not be written");
	tm = load_and_read(t);
	check(tm != 0, "well-formed: localtime refused it");
	if (tm != 0) {
		check(tm->tm_gmtoff == -18000L,
		    "well-formed: the offset was not the file's");
		check(tm->tm_isdst == 0 || tm->tm_isdst == 1,
		    "well-formed: the daylight flag is not a flag");
	}
	use_gmt();
}

/*
 * Every type is daylight and there are more transitions than types, which
 * is the file that tells a walk bounded by typecnt from one bounded by
 * timecnt. The search finds no standard type and must stop at the end of
 * ttis[] rather than at the end of ats[].
 */
static void
test_search_stops_at_typecnt(void)
{
	static unsigned char isdst[MAX_TYPES];
	struct tm *tm;
	int i;

	for (i = 0; i < MAX_TYPES; i++)
		isdst[i] = 1;

	/* An instant before the first transition, which is the branch. */
	check(write_zone(MAX_TIMES, MAX_TYPES, 4, -3600L, isdst) == 0,
	    "fixture: the all-daylight zone could not be written");
	tm = load_and_read(-1000000L);
	check(tm != 0, "all-daylight: localtime refused the file");
	if (tm != 0) {
		/*
		 * Whatever type it settles on has to be one the file
		 * described, so its offset is the one every type carries.
		 */
		check(tm->tm_gmtoff == -3600L,
		    "all-daylight: the offset came from outside ttis[]");
		check(tm->tm_isdst == 0 || tm->tm_isdst == 1,
		    "all-daylight: the daylight flag is not a flag");
	}
	use_gmt();
}

/*
 * A daylight flag the format permits and the array does not. localtime()
 * writes tzname[flag], so the file has to be refused; when it is, tzset()
 * falls back and the zone in force is not the file's.
 */
static void
test_daylight_flag_is_refused(void)
{
	static unsigned char isdst[2];
	struct tm *tm;
	time_t t = 1000000000L;
	unsigned char bad[] = { 2, 7, 64, 200, 255 };
	unsigned i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		isdst[0] = 0;
		isdst[1] = bad[i];
		check(write_zone(4, 2, 4, -18000L, isdst) == 0,
		    "fixture: the bad-flag zone could not be written");
		tm = load_and_read(t);
		check(tm != 0, "bad flag: localtime returned nothing");
		if (tm != 0) {
			check(tm->tm_gmtoff != -18000L,
			    "bad flag: the file was loaded rather than refused");
			check(tm->tm_isdst == 0 || tm->tm_isdst == 1,
			    "bad flag: the daylight flag reached tm_isdst");
		}
		use_gmt();
	}
}

/*
 * An offset west of Greenwich is negative, and it is stored as four bytes
 * of two's complement that a wider long has to be given the sign of.
 */
static void
test_negative_offset(void)
{
	static const unsigned char isdst[1] = { 0 };
	struct tm *tm;
	time_t t = 1000000000L;

	check(write_zone(0, 1, 4, -18000L, isdst) == 0,
	    "fixture: the negative-offset zone could not be written");
	tm = load_and_read(t);
	check(tm != 0, "negative offset: localtime refused the file");
	if (tm != 0) {
		check(tm->tm_gmtoff == -18000L,
		    "negative offset: the sign was not carried");
		/* 2001-09-09 01:46:40 UTC is the evening before, five west. */
		check(tm->tm_year == 101 && tm->tm_mon == 8 &&
		    tm->tm_mday == 8 && tm->tm_hour == 20,
		    "negative offset: the instant landed in the wrong year");
	}
	use_gmt();
}

#endif /* TZ_ZONEINFO */

#ifdef TZ_ZONEINFO
/* A count past the array it addresses is refused outright. */
static void
test_counts_are_bounded(void)
{
	/* Sized for the over-typed case below, which names more than ttis[]. */
	static const unsigned char isdst[MAX_TYPES + 1];
	struct tm *tm;
	time_t t = 0;

	/* More types than ttis[] holds. */
	check(write_zone(0, MAX_TYPES + 1, 4, -18000L, isdst) == 0,
	    "fixture: the over-typed zone could not be written");
	tm = load_and_read(t);
	check(tm == 0 || tm->tm_gmtoff != -18000L,
	    "type count: a count past ttis[] was accepted");
	use_gmt();

	/* No types at all, which leaves nothing for the search to find. */
	check(write_zone(0, 0, 4, -18000L, 0) == 0,
	    "fixture: the untyped zone could not be written");
	tm = load_and_read(t);
	check(tm == 0 || tm->tm_gmtoff != -18000L,
	    "type count: a file naming no types was accepted");
	use_gmt();
}
#endif /* TZ_ZONEINFO */

#ifndef TZ_ZONEINFO
/*
 * The shape that reads no file. tzset() reaches tzsetkernel() and then
 * tzsetgmt(), so a zone file named in TZ is not opened at all -- which is
 * the whole of what this build promises, and the reason the bounds the
 * cases above measure have nothing to bound.
 */
/*
 * The offset the fixture writes is deliberately not a whole number of
 * minutes. tzsetkernel() builds its offset from tz_minuteswest, which
 * gettimeofday(2) reports in minutes, so any offset it can produce is a
 * multiple of sixty and this one is not. That distinguishes "the file was
 * read" from "the kernel happens to report the same offset" -- a distinction
 * that matters because Linux fills struct timezone with zeros while the BSDs
 * report the zone in force, so a fixture naming a plausible offset would
 * pass on one host and fail on another for reasons having nothing to do
 * with the code.
 */
#define UNREACHABLE_OFFSET	(-18001L)

static void
test_no_file_is_read(void)
{
	static const unsigned char isdst[1] = { 0 };
	struct tm *tm;
	time_t t = 1000000000L;

	check(write_zone(0, 1, 4, UNREACHABLE_OFFSET, isdst) == 0,
	    "fixture: the zone could not be written");
	tm = load_and_read(t);
	check(tm != 0, "no file: localtime returned nothing");
	if (tm != 0) {
		check(tm->tm_gmtoff != UNREACHABLE_OFFSET,
		    "no file: a zone file was read by a build without one");
		check(tm->tm_gmtoff % 60 == 0,
		    "no file: the offset did not come from tz_minuteswest");
	}

	/*
	 * Whatever the kernel reports, the fields localtime() gives are
	 * gmtime()'s moved by exactly that offset: one constant, applied
	 * once, with no transition to find.
	 */
	use_gmt();
	tm = db_localtime(&t);
	check(tm != 0, "no file: localtime returned nothing for GMT");
	if (tm != 0) {
		struct tm local = *tm;
		time_t shifted = t + local.tm_gmtoff;
		struct tm *utc = db_gmtime(&shifted);

		check(utc != 0 && utc->tm_year == local.tm_year &&
		    utc->tm_mon == local.tm_mon &&
		    utc->tm_mday == local.tm_mday &&
		    utc->tm_hour == local.tm_hour &&
		    utc->tm_min == local.tm_min &&
		    utc->tm_sec == local.tm_sec,
		    "no file: the fields are not gmtime's moved by the offset");
	}
}
#endif /* !TZ_ZONEINFO */

int
main(void)
{
#ifdef TZ_ZONEINFO
	test_well_formed_zone();
	test_search_stops_at_typecnt();
	test_daylight_flag_is_refused();
	test_negative_offset();
	test_counts_are_bounded();
#else
	test_no_file_is_read();
#endif

	(void)unlink(ZONE_PATH);
	if (failure_count != 0) {
		(void)write(STDERR_FILENO, "zone contract tests failed\n", 27);
		return (1);
	}
	(void)write(STDERR_FILENO, "zone contract tests passed\n", 27);
	return (0);
}
