#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <tzfile.h>

enum {
	ASCTIME_LENGTH = 26,
	ASCTIME_YEAR_MIN = 1000,
	ASCTIME_YEAR_MAX = 9999
};

char *
db_asctime(const struct tm *time_value)
{
	static const char weekday_names[DAYS_PER_WEEK][4] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char month_names[MONS_PER_YEAR][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	static char result[ASCTIME_LENGTH];
	int length;

	if (time_value == 0) {
		errno = EINVAL;
		return 0;
	}
	if (time_value->tm_wday < 0 || time_value->tm_wday >= DAYS_PER_WEEK ||
	    time_value->tm_mon < 0 || time_value->tm_mon >= MONS_PER_YEAR ||
	    time_value->tm_mday < 1 || time_value->tm_mday > 31 ||
	    time_value->tm_hour < 0 ||
	    time_value->tm_min < 0 || time_value->tm_min >= MINS_PER_HOUR ||
	    time_value->tm_sec < 0 || time_value->tm_sec > SECS_PER_MIN ||
	    time_value->tm_year < ASCTIME_YEAR_MIN - TM_YEAR_BASE ||
	    time_value->tm_year > ASCTIME_YEAR_MAX - TM_YEAR_BASE) {
		errno = ERANGE;
		return 0;
	}

	length = snprintf(result, sizeof(result),
	    "%.3s %.3s%3d %02d:%02d:%02d %d\n",
	    weekday_names[time_value->tm_wday],
	    month_names[time_value->tm_mon], time_value->tm_mday,
	    time_value->tm_hour, time_value->tm_min, time_value->tm_sec,
	    time_value->tm_year + TM_YEAR_BASE);
	if (length != ASCTIME_LENGTH - 1) {
		errno = ERANGE;
		return 0;
	}
	return result;
}
