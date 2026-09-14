#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>

#ifndef NPROC
#define NPROC 25
#endif

static int
write_all(int descriptor, const char *buffer, size_t length)
{
	ssize_t written;

	while (length != 0) {
		written = write(descriptor, buffer, length);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (written == 0)
			return -1;
		buffer += written;
		length -= (size_t)written;
	}
	return 0;
}

static int
write_text(int descriptor, const char *text)
{
	return write_all(descriptor, text, strlen(text));
}

static void
report_file_error(const char *pathname, const char *reason)
{
	(void)write_text(STDERR_FILENO, "users: ");
	(void)write_text(STDERR_FILENO, pathname);
	(void)write_text(STDERR_FILENO, reason);
}

static int
read_record(int descriptor, struct utmp *record)
{
	unsigned char *cursor;
	ssize_t amount;
	size_t remaining;

	cursor = (unsigned char *)record;
	remaining = sizeof(*record);
	while (remaining != 0) {
		amount = read(descriptor, cursor, remaining);
		if (amount < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (amount == 0)
			return remaining == sizeof(*record) ? 0 : -1;
		cursor += amount;
		remaining -= (size_t)amount;
	}
	return 1;
}

int
main(int argc, char **argv)
{
	char names[NPROC][UT_NAMESIZE + 1];
	char candidate[UT_NAMESIZE + 1];
	struct utmp record;
	const char *pathname;
	int descriptor;
	int name_count;
	int position;
	int record_status;
	int scan;

	if (argc > 2) {
		(void)write_text(STDERR_FILENO, "usage: users [file]\n");
		return 1;
	}
	pathname = argc == 2 ? argv[1] : _PATH_UTMP;
	descriptor = open(pathname, O_RDONLY);
	if (descriptor < 0) {
		report_file_error(pathname, ": cannot open\n");
		return 1;
	}
	name_count = 0;
	while ((record_status = read_record(descriptor, &record)) > 0) {
		if (record.ut_name[0] == '\0')
			continue;
		memcpy(candidate, record.ut_name, UT_NAMESIZE);
		candidate[UT_NAMESIZE] = '\0';
		for (position = 0; position < name_count; ++position) {
			scan = strcmp(names[position], candidate);
			if (scan >= 0)
				break;
		}
		if (position < name_count && scan == 0)
			continue;
		if (name_count == NPROC) {
			(void)write_text(STDERR_FILENO,
			    "users: too many distinct users\n");
			(void)close(descriptor);
			return 1;
		}
		for (scan = name_count; scan > position; --scan)
			memcpy(names[scan], names[scan - 1], UT_NAMESIZE + 1);
		memcpy(names[position], candidate, UT_NAMESIZE + 1);
		++name_count;
	}
	(void)close(descriptor);
	if (record_status < 0) {
		report_file_error(pathname, ": malformed input\n");
		return 1;
	}
	for (position = 0; position < name_count; ++position) {
		if (position != 0 && write_all(STDOUT_FILENO, " ", 1) < 0)
			return 1;
		if (write_all(STDOUT_FILENO, names[position],
		    strlen(names[position])) < 0)
			return 1;
	}
	if (write_all(STDOUT_FILENO, "\n", 1) < 0)
		return 1;
	return 0;
}
