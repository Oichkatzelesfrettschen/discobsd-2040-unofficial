#include <string.h>
#include <time.h>
#include <unistd.h>

struct tm *db_gmtime(const time_t *);

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
	check_time(0, 1970, 1, 1, 0, 0, 0, 4, 0, "gmtime epoch");
	check_time(-1, 1969, 12, 31, 23, 59, 59, 3, 364,
	    "gmtime pre-epoch second");
	check_time(951827696L, 2000, 2, 29, 12, 34, 56, 2, 59,
	    "gmtime Gregorian leap day");
	check_time(2147483647L, 2038, 1, 19, 3, 14, 7, 2, 18,
	    "gmtime signed maximum");
	check_time(-2147483647L - 1L, 1901, 12, 13, 20, 45, 52, 5, 346,
	    "gmtime signed minimum");
	return failure_count != 0;
}
