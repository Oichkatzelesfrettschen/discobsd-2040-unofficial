#include <stdio.h>
#include <time.h>
#include <tzfile.h>

char *
asctime_format_bad(const struct tm *time_value)
{
	static const char weekday_names[DAYS_PER_WEEK][4] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char month_names[MONS_PER_YEAR][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	static char result[26];

	(void)sprintf(result, "%.3s %.3s%3d %02d:%02d:%02d %d\n",
	    weekday_names[time_value->tm_wday],
	    month_names[time_value->tm_mon], time_value->tm_mday,
	    time_value->tm_hour, time_value->tm_min, time_value->tm_sec,
	    TM_YEAR_BASE + time_value->tm_year);
	return result;
}
