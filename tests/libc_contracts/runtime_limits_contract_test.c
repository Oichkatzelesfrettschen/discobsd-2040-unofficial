#include <errno.h>
#include <paths.h>
#include <unistd.h>

long db_sysconf(int name);
size_t db_confstr(int name, char *buffer, size_t length);

int runtime_limits_errno;
static int failures;

#define CHECK(condition) do { \
	if (!(condition)) \
		failures++; \
} while (0)

static void
fill_bytes(char *buffer, size_t length, unsigned char value)
{
	size_t index;

	for (index = 0; index < length; index++)
		buffer[index] = (char)value;
}

static int
bytes_equal(const char *left, const char *right, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (left[index] != right[index])
			return 0;
	return 1;
}

static int
strings_equal(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static void
check_sysconf(void)
{
	runtime_limits_errno = 37;
	CHECK(db_sysconf(_SC_ARG_MAX) == 5120);
	CHECK(runtime_limits_errno == 37);
	CHECK(db_sysconf(_SC_CLK_TCK) == 60);
	CHECK(db_sysconf(_SC_NGROUPS_MAX) == 16);
	CHECK(db_sysconf(_SC_OPEN_MAX) == 30);
	CHECK(db_sysconf(_SC_JOB_CONTROL) == 1);

	runtime_limits_errno = 0;
	CHECK(db_sysconf(2) == -1);
	CHECK(runtime_limits_errno == EINVAL);
	runtime_limits_errno = 0;
	CHECK(db_sysconf(-1) == -1);
	CHECK(runtime_limits_errno == EINVAL);
	runtime_limits_errno = 0;
	CHECK(db_sysconf(0x7fffffff) == -1);
	CHECK(runtime_limits_errno == EINVAL);
}

static void
check_confstr(void)
{
	static const char expected[] = "/usr/bin:/bin:/usr/sbin:/sbin";
	char buffer[sizeof(expected) + 4];
	char untouched[sizeof(buffer)];
	size_t required;
	size_t length;
	size_t index;

	fill_bytes(buffer, sizeof(buffer), 0xa5);
	fill_bytes(untouched, sizeof(untouched), 0xa5);
	runtime_limits_errno = 41;
	required = db_confstr(_CS_PATH, NULL, 0);
	CHECK(required == sizeof(expected));
	CHECK(runtime_limits_errno == 41);
	CHECK(db_confstr(_CS_PATH, buffer, 0) == required);
	CHECK(bytes_equal(buffer, untouched, sizeof(buffer)));
	CHECK(db_confstr(_CS_PATH, NULL, sizeof(buffer)) == required);

	fill_bytes(buffer, sizeof(buffer), 0xa5);
	CHECK(db_confstr(_CS_PATH, buffer, sizeof(buffer)) == required);
	CHECK(strings_equal(buffer, expected));
	CHECK((unsigned char)buffer[required] == 0xa5);

	for (length = 1; length < required; length++) {
		fill_bytes(buffer, sizeof(buffer), 0xa5);
		CHECK(db_confstr(_CS_PATH, buffer, length) == required);
		CHECK(buffer[length - 1] == '\0');
		for (index = 0; index + 1 < length; index++)
			CHECK(buffer[index] == expected[index]);
		CHECK((unsigned char)buffer[length] == 0xa5);
	}

	fill_bytes(buffer, sizeof(buffer), 0x5a);
	fill_bytes(untouched, sizeof(untouched), 0x5a);
	runtime_limits_errno = 0;
	CHECK(db_confstr(2, buffer, sizeof(buffer)) == 0);
	CHECK(runtime_limits_errno == EINVAL);
	CHECK(bytes_equal(buffer, untouched, sizeof(buffer)));
	runtime_limits_errno = 0;
	CHECK(db_confstr(-1, buffer, sizeof(buffer)) == 0);
	CHECK(runtime_limits_errno == EINVAL);
	CHECK(bytes_equal(buffer, untouched, sizeof(buffer)));
	runtime_limits_errno = 0;
	CHECK(db_confstr(0x7fffffff, buffer, sizeof(buffer)) == 0);
	CHECK(runtime_limits_errno == EINVAL);
	CHECK(bytes_equal(buffer, untouched, sizeof(buffer)));
	CHECK(strings_equal(_PATH_STDPATH, expected));
}

int
main(void)
{
	CHECK(_SC_ARG_MAX == 1);
	CHECK(_SC_CLK_TCK == 3);
	CHECK(_SC_NGROUPS_MAX == 4);
	CHECK(_SC_OPEN_MAX == 5);
	CHECK(_SC_JOB_CONTROL == 6);
	CHECK(_CS_PATH == 1);
	check_sysconf();
	check_confstr();
	return failures != 0;
}
