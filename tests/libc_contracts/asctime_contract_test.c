#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int asctime_test_errno;
char *db_asctime(const struct tm *);

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

static struct tm
valid_time(void)
{
	struct tm time_value = { 0 };

	time_value.tm_sec = 52;
	time_value.tm_min = 3;
	time_value.tm_hour = 1;
	time_value.tm_mday = 16;
	time_value.tm_mon = 8;
	time_value.tm_year = 73;
	time_value.tm_wday = 0;
	return time_value;
}

static void
check_rejection(struct tm *time_value, const char *message)
{
	struct tm baseline_time;
	char expected_result[26];
	char *static_result;

	baseline_time = valid_time();
	static_result = db_asctime(&baseline_time);
	(void)memcpy(expected_result, static_result, sizeof(expected_result));
	asctime_test_errno = 0;
	check(db_asctime(time_value) == 0, message);
	check(asctime_test_errno == ERANGE, "asctime rejection errno");
	check(strcmp(static_result, expected_result) == 0,
	    "asctime rejection preserves static result");
}

int
main(void)
{
	struct tm time_value;
	char *result;

	time_value = valid_time();
	result = db_asctime(&time_value);
	check(result != 0 &&
	    strcmp(result, "Sun Sep 16 01:03:52 1973\n") == 0,
	    "asctime canonical value");
	time_value.tm_mday = 6;
	check(strcmp(db_asctime(&time_value), "Sun Sep  6 01:03:52 1973\n") == 0,
	    "asctime one-digit day padding");
	time_value = valid_time();
	time_value.tm_year = -900;
	check(strcmp(db_asctime(&time_value), "Sun Sep 16 01:03:52 1000\n") == 0,
	    "asctime lower year boundary");
	time_value.tm_year = 8099;
	time_value.tm_sec = 60;
	check(strcmp(db_asctime(&time_value), "Sun Sep 16 01:03:60 9999\n") == 0,
	    "asctime upper year and leap-second boundary");

	time_value = valid_time();
	time_value.tm_hour = 24;
	check_rejection(&time_value, "asctime rejects hour 24");
	time_value = valid_time();
	time_value.tm_hour = -1;
	check_rejection(&time_value, "asctime rejects hour -1");
	time_value = valid_time();
	time_value.tm_wday = 7;
	check_rejection(&time_value, "asctime rejects weekday 7");
	time_value = valid_time();
	time_value.tm_wday = -1;
	check_rejection(&time_value, "asctime rejects weekday -1");
	time_value = valid_time();
	time_value.tm_mon = -1;
	check_rejection(&time_value, "asctime rejects month -1");
	time_value = valid_time();
	time_value.tm_mon = 12;
	check_rejection(&time_value, "asctime rejects month 12");
	time_value = valid_time();
	time_value.tm_mday = 32;
	check_rejection(&time_value, "asctime rejects day 32");
	time_value = valid_time();
	time_value.tm_mday = 0;
	check_rejection(&time_value, "asctime rejects day 0");
	time_value = valid_time();
	time_value.tm_min = 60;
	check_rejection(&time_value, "asctime rejects minute 60");
	time_value = valid_time();
	time_value.tm_min = -1;
	check_rejection(&time_value, "asctime rejects minute -1");
	time_value = valid_time();
	time_value.tm_sec = 61;
	check_rejection(&time_value, "asctime rejects second 61");
	time_value = valid_time();
	time_value.tm_sec = -1;
	check_rejection(&time_value, "asctime rejects second -1");
	time_value = valid_time();
	time_value.tm_year = -901;
	check_rejection(&time_value, "asctime rejects year 999");
	time_value = valid_time();
	time_value.tm_year = 8100;
	check_rejection(&time_value, "asctime rejects year 10000");

	asctime_test_errno = 0;
	check(db_asctime(0) == 0, "asctime rejects a null input");
	check(asctime_test_errno == EINVAL, "asctime null-input errno");
	return failure_count != 0;
}
