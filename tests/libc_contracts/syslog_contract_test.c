#include <sys/types.h>
#include <sys/uio.h>
#include <syslog.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CAPTURE_SIZE 640
#define LOG_FD 7

void db_syslog(int, const char *, ...);
void db_openlog(const char *, int, int);
void db_closelog(void);

const char *__progname = "contract";
int syslog_test_errno;
struct _iobuf _iob[3];

static char log_capture[CAPTURE_SIZE];
static char stderr_capture[CAPTURE_SIZE];
static size_t log_length;
static size_t stderr_length;
static int checks;
static int failures;
static int messages_open_attempts;
static int messages_open_failures;
static pid_t process_id = 42;
static const char *error_text = "error % marker";

static void
check(int condition, const char *message)
{
	checks++;
	if (!condition) {
		failures++;
		(void)write(STDERR_FILENO, message, strlen(message));
		(void)write(STDERR_FILENO, "\n", 1);
	}
}

static int
same_string(const char *left, const char *right)
{
	while (*left == *right && *left != '\0') {
		left++;
		right++;
	}
	return *left == *right;
}

static void
copy_capture(char *capture, size_t *capture_length, const void *source,
    size_t length)
{
	const char *input = source;
	size_t available;

	available = CAPTURE_SIZE - 1 - *capture_length;
	if (length > available)
		length = available;
	memcpy(capture + *capture_length, input, length);
	*capture_length += length;
	capture[*capture_length] = '\0';
}

static void
reset_transport(void)
{
	db_closelog();
	log_length = 0;
	stderr_length = 0;
	log_capture[0] = '\0';
	stderr_capture[0] = '\0';
	messages_open_attempts = 0;
	messages_open_failures = 0;
	process_id = 42;
	error_text = "error % marker";
}

int
test_open(const char *path, int flags, ...)
{
	(void)flags;
	if (same_string(path, "/var/log/messages")) {
		messages_open_attempts++;
		if (messages_open_failures > 0) {
			messages_open_failures--;
			return -1;
		}
		return LOG_FD;
	}
	return -1;
}

ssize_t
test_write(int fd, const void *buffer, size_t length)
{
	if (fd == LOG_FD) {
		copy_capture(log_capture, &log_length, buffer, length);
		return (ssize_t)length;
	}
	return -1;
}

ssize_t
test_writev(int fd, const struct iovec *vectors, int vector_count)
{
	ssize_t total = 0;
	int index;

	if (fd != STDERR_FILENO)
		return -1;
	for (index = 0; index < vector_count; index++) {
		copy_capture(stderr_capture, &stderr_length,
		    vectors[index].iov_base, vectors[index].iov_len);
		total += (ssize_t)vectors[index].iov_len;
	}
	return total;
}

int
test_fcntl(int fd, int command, ...)
{
	(void)fd;
	(void)command;
	return 0;
}

int
test_close(int fd)
{
	(void)fd;
	return 0;
}

time_t
test_time(time_t *result)
{
	if (result != NULL)
		*result = 0;
	return 0;
}

struct tm *
test_localtime(const time_t *value)
{
	static struct tm result;

	(void)value;
	return &result;
}

size_t
test_strftime(char *destination, size_t capacity, const char *format,
    const struct tm *value)
{
	static const char timestamp[] = "Jan  2 03:04:05 ";
	size_t length = sizeof(timestamp) - 1;

	(void)format;
	(void)value;
	if (capacity <= length)
		return 0;
	memcpy(destination, timestamp, sizeof(timestamp));
	return length;
}

pid_t
test_getpid(void)
{
	return process_id;
}

pid_t
test_vfork(void)
{
	return 99;
}

int
test_waitpid(int pid, int *status, int options)
{
	(void)status;
	(void)options;
	return pid;
}

void
test_exit(int status)
{
	(void)status;
}

char *
test_strerror(int error_number)
{
	(void)error_number;
	return (char *)error_text;
}

static void
test_max_pid(void)
{
	reset_transport();
	process_id = (pid_t)INT_MAX;
	db_openlog("app", LOG_PID | LOG_NDELAY, LOG_USER);
	db_syslog(LOG_INFO, "body");
	check(same_string(log_capture,
	    "<14>Jan  2 03:04:05 app[2147483647]: body\r\n"),
	    "syslog contract: maximum target PID fits its fixed field");
}

static void
test_fixed_fields(void)
{
	reset_transport();
	db_openlog("app", LOG_PID | LOG_PERROR | LOG_NDELAY, LOG_USER);
	db_syslog(LOG_INFO, "value=%d", 9);
	check(same_string(log_capture,
	    "<14>Jan  2 03:04:05 app[42]: value=9\r\n"),
	    "syslog contract: priority, timestamp, tag, PID and CRLF");
	check(same_string(stderr_capture, "app[42]: value=9\n"),
	    "syslog contract: LOG_PERROR omits the transport prefix");
}

static void
test_percent_m(void)
{
	reset_transport();
	db_openlog("app", LOG_NDELAY, LOG_USER);
	syslog_test_errno = EIO;
	db_syslog(LOG_ERR, "failed: %m; again: %m; %%m; %s", "tail");
	check(same_string(log_capture,
	    "<11>Jan  2 03:04:05 app: failed: error % marker; again: "
	    "error % marker; %m; tail\r\n"),
	    "syslog contract: percent-m expansion remains bounded format data");
}

static void
test_long_inputs(void)
{
	char long_input[700];
	size_t index;

	for (index = 0; index < sizeof(long_input) - 1; index++)
		long_input[index] = 'x';
	long_input[sizeof(long_input) - 1] = '\0';

	reset_transport();
	db_openlog(long_input, LOG_NDELAY, LOG_USER);
	db_syslog(LOG_INFO, "body");
	check(log_length == 511,
	    "syslog contract: long tag stops before reserved CRLF storage");
	check(log_capture[log_length - 2] == '\r' &&
	    log_capture[log_length - 1] == '\n',
	    "syslog contract: long tag retains CRLF");

	reset_transport();
	db_openlog("app", LOG_NDELAY, LOG_USER);
	db_syslog(LOG_INFO, "%s", long_input);
	check(log_length == 511,
	    "syslog contract: long message stops before reserved CRLF storage");
	check(log_capture[log_length - 2] == '\r' &&
	    log_capture[log_length - 1] == '\n',
	    "syslog contract: long message retains CRLF");

	reset_transport();
	db_openlog("app", LOG_NDELAY, LOG_USER);
	db_syslog(LOG_INFO, long_input);
	check(log_length == 511,
	    "syslog contract: long literal format stays in the record");
	check(log_capture[log_length - 2] == '\r' &&
	    log_capture[log_length - 1] == '\n',
	    "syslog contract: long literal format retains CRLF");

	reset_transport();
	db_openlog("app", LOG_NDELAY, LOG_USER);
	error_text = long_input;
	db_syslog(LOG_ERR, "%m");
	check(log_length == 511,
	    "syslog contract: long percent-m expansion stays in the record");
	check(log_capture[log_length - 2] == '\r' &&
	    log_capture[log_length - 1] == '\n',
	    "syslog contract: long percent-m expansion retains CRLF");
}

static void
test_open_retry(void)
{
	reset_transport();
	messages_open_failures = 1;
	db_openlog("app", 0, LOG_USER);
	db_syslog(LOG_NOTICE, "first");
	db_syslog(LOG_NOTICE, "second");
	check(messages_open_attempts == 2,
	    "syslog contract: failed logfile open is retried");
	check(strstr(log_capture, "second\r\n") != NULL,
	    "syslog contract: retry delivers the later message");
}

int
main(void)
{
	test_fixed_fields();
	test_max_pid();
	test_percent_m();
	test_long_inputs();
	test_open_retry();
	db_closelog();

	if (failures != 0) {
		(void)write(STDERR_FILENO, "syslog contract tests failed\n", 29);
		return 1;
	}
	(void)write(STDOUT_FILENO, "syslog contract tests passed\n", 29);
	return 0;
}
