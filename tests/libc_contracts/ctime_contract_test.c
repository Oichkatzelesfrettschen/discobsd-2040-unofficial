/*
 * gmtime() of the 32-bit view (sys/sys/_time_t.h), where time_t is the
 * kernel's unsigned 32-bit count: every count from 0 to 4294967295 is a
 * UTC instant from 1970-01-01 00:00:00 to 2106-02-07 06:28:15, and a count
 * with bit 31 set is a time after January 2038. The named cases pin each
 * end and the carry out of bit 31; the vectors of calendar32_vectors.py
 * check the fields against CPython's datetime over the whole range.
 */
#include <string.h>
#include <time.h>
#include <unistd.h>

struct tm *db_gmtime(const time_t *);

struct instant32_vector {
	time_t	count;
	int	year, mon, mday, hour, min, sec, wday, yday;
};

#include "calendar32_vectors.h"

static int failure_count;

static void
check(int condition, const char *message)
{
	if (condition)
		return;
	(void)write(STDERR_FILENO, message, strlen(message));
	(void)write(STDERR_FILENO, "\n", 1);
	++failure_count;
}

static void
check_time(time_t timestamp, int year, int month, int month_day,
    int hour, int minute, int second, int week_day, int year_day,
    const char *message)
{
	struct tm *time_value;

	time_value = db_gmtime(&timestamp);
	check(time_value->tm_year == year - 1900 &&
	    time_value->tm_mon == month - 1 &&
	    time_value->tm_mday == month_day &&
	    time_value->tm_hour == hour && time_value->tm_min == minute &&
	    time_value->tm_sec == second && time_value->tm_wday == week_day &&
	    time_value->tm_yday == year_day && time_value->tm_isdst == 0 &&
	    time_value->tm_gmtoff == 0 && strcmp(time_value->tm_zone, "GMT") == 0,
	    message);
}

int
main(void)
{
	size_t index;
	int wrong = 0;

	check((time_t)-1 > 0 && (time_t)0xffffffffUL == ORACLE_COUNT_MAX,
	    "time_t is not an unsigned count reaching 4294967295");
	check_time(0, 1970, 1, 1, 0, 0, 0, 4, 0, "gmtime epoch");
	check_time(951827696UL, 2000, 2, 29, 12, 34, 56, 2, 59,
	    "gmtime Gregorian leap day");
	check_time(2147483647UL, 2038, 1, 19, 3, 14, 7, 2, 18,
	    "gmtime last count below bit 31");
	check_time(2147483648UL, 2038, 1, 19, 3, 14, 8, 2, 18,
	    "gmtime first count with bit 31 set");
	check_time(2415919104UL, 2046, 7, 23, 0, 38, 24, 1, 203,
	    "gmtime count 0x90000000");
	check_time(4294967294UL, 2106, 2, 7, 6, 28, 14, 0, 37,
	    "gmtime count 4294967294");
	check_time((time_t)-1, 2106, 2, 7, 6, 28, 15, 0, 37,
	    "gmtime last count");

	for (index = 0; index < sizeof(instant32_vectors) /
	    sizeof(instant32_vectors[0]); index++) {
		const struct instant32_vector *vector = &instant32_vectors[index];
		struct tm *time_value = db_gmtime(&vector->count);

		if (time_value == 0 || time_value->tm_year != vector->year ||
		    time_value->tm_mon != vector->mon ||
		    time_value->tm_mday != vector->mday ||
		    time_value->tm_hour != vector->hour ||
		    time_value->tm_min != vector->min ||
		    time_value->tm_sec != vector->sec ||
		    time_value->tm_wday != vector->wday ||
		    time_value->tm_yday != vector->yday)
			wrong++;
	}
	check(wrong == 0, "gmtime disagrees with the datetime vectors");
	check(index > 3000, "gmtime vectors: too few instants");
	return failure_count != 0;
}
