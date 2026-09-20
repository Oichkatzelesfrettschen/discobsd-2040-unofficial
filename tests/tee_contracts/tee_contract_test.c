#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FIXTURE_SIZE 2500
#define OUTPUT_SIZE 4096
#define DESCRIPTOR_LIMIT 32

int discobsd_tee_main(int, char **);

static unsigned char input_data[FIXTURE_SIZE];
static unsigned char output_data[DESCRIPTOR_LIMIT][OUTPUT_SIZE];
static size_t input_offset;
static size_t output_length[DESCRIPTOR_LIMIT];
static int next_descriptor;
static int read_interrupt;
static int read_failure;
static int failed_write_descriptor;
static int zero_write_descriptor;
static int failed_close_descriptor;
static int open_calls;
static int close_calls;
static int signal_calls;

static void
reset_fixture(void)
{
	unsigned int index;

	for (index = 0; index < sizeof(input_data); index++)
		input_data[index] = (unsigned char)(index * 37U + 11U);
	memset(output_data, 0, sizeof(output_data));
	memset(output_length, 0, sizeof(output_length));
	input_offset = 0;
	next_descriptor = 10;
	read_interrupt = 0;
	read_failure = 0;
	failed_write_descriptor = -1;
	zero_write_descriptor = -1;
	failed_close_descriptor = -1;
	open_calls = 0;
	close_calls = 0;
	signal_calls = 0;
}

static int
check(int condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "tee contract test: %s\n", message);
	return 1;
}

ssize_t
test_read(int descriptor, void *buffer, size_t length)
{
	size_t transfer_length;

	if (descriptor != STDIN_FILENO) {
		errno = EBADF;
		return -1;
	}
	if (read_interrupt) {
		read_interrupt = 0;
		errno = EINTR;
		return -1;
	}
	if (read_failure && input_offset >= 1024) {
		errno = EIO;
		return -1;
	}
	if (input_offset == sizeof(input_data))
		return 0;
	transfer_length = sizeof(input_data) - input_offset;
	if (transfer_length > length)
		transfer_length = length;
	memcpy(buffer, input_data + input_offset, transfer_length);
	input_offset += transfer_length;
	return (ssize_t)transfer_length;
}

ssize_t
test_write(int descriptor, const void *buffer, size_t length)
{
	size_t transfer_length = length > 13 ? 13 : length;

	if (descriptor == STDERR_FILENO)
		return (ssize_t)length;
	if (descriptor == failed_write_descriptor) {
		errno = ENOSPC;
		return -1;
	}
	if (descriptor == zero_write_descriptor)
		return 0;
	if (descriptor < 0 || descriptor >= DESCRIPTOR_LIMIT ||
	    output_length[descriptor] + transfer_length > OUTPUT_SIZE) {
		errno = EFBIG;
		return -1;
	}
	memcpy(output_data[descriptor] + output_length[descriptor], buffer,
	    transfer_length);
	output_length[descriptor] += transfer_length;
	return (ssize_t)transfer_length;
}

int
test_open(const char *name, int flags, ...)
{
	va_list arguments;
	int mode;

	open_calls++;
	va_start(arguments, flags);
	mode = va_arg(arguments, int);
	va_end(arguments);
	if ((flags & (O_WRONLY | O_CREAT)) != (O_WRONLY | O_CREAT) ||
	    mode != 0666) {
		errno = EINVAL;
		return -1;
	}
	if (strcmp(name, "open-fail") == 0) {
		errno = EACCES;
		return -1;
	}
	return next_descriptor++;
}

int
test_close(int descriptor)
{
	close_calls++;
	if (descriptor == failed_close_descriptor) {
		errno = EIO;
		return -1;
	}
	return 0;
}

void (*
test_signal(int signal_number, void (*handler)(int)))(int)
{
	(void)handler;
	if (signal_number == SIGINT)
		signal_calls++;
	return SIG_DFL;
}

void (*
__sysv_signal(int signal_number, void (*handler)(int)))(int)
{
	return test_signal(signal_number, handler);
}

static int
complete_copy_case(void)
{
	char *arguments[] = { "tee", "-ai", "first", "second", NULL };
	int failures = 0;
	int status;

	reset_fixture();
	read_interrupt = 1;
	status = discobsd_tee_main(4, arguments);
	failures += check(status == 0, "a complete copy returns failure");
	failures += check(signal_calls == 1, "-i does not ignore SIGINT");
	failures += check(output_length[STDOUT_FILENO] == FIXTURE_SIZE,
	    "standard output is incomplete");
	failures += check(output_length[10] == FIXTURE_SIZE &&
	    output_length[11] == FIXTURE_SIZE, "a named output is incomplete");
	failures += check(memcmp(output_data[10], input_data, FIXTURE_SIZE) == 0 &&
	    memcmp(output_data[11], input_data, FIXTURE_SIZE) == 0,
	    "partial writes change output bytes");
	failures += check(close_calls == 2, "named outputs are not closed");
	return failures;
}

static int
isolated_failure_case(void)
{
	char *arguments[] = { "tee", "bad", "good", NULL };
	int failures = 0;

	reset_fixture();
	failed_write_descriptor = 10;
	failures += check(discobsd_tee_main(3, arguments) == 1,
	    "a write failure returns success");
	failures += check(output_length[11] == FIXTURE_SIZE &&
	    memcmp(output_data[11], input_data, FIXTURE_SIZE) == 0,
	    "one failed destination stops an unaffected destination");
	return failures;
}

static int
error_case(void)
{
	char *arguments[] = { "tee", "open-fail", "close-fail", NULL };
	int failures = 0;

	reset_fixture();
	failed_close_descriptor = 10;
	failures += check(discobsd_tee_main(3, arguments) == 1,
	    "open and close failures return success");
	failures += check(open_calls == 2, "an open failure stops later opens");

	reset_fixture();
	read_failure = 1;
	failures += check(discobsd_tee_main(1, arguments) == 1,
	    "a read failure returns success");

	reset_fixture();
	zero_write_descriptor = STDOUT_FILENO;
	failures += check(discobsd_tee_main(1, arguments) == 1,
	    "a zero-byte write returns success");
	return failures;
}

static int
capacity_case(void)
{
	char *arguments[22];
	char names[20][8];
	int index;
	int failures = 0;

	arguments[0] = "tee";
	for (index = 0; index < 20; index++) {
		(void)snprintf(names[index], sizeof(names[index]), "f%d", index);
		arguments[index + 1] = names[index];
	}
	arguments[21] = NULL;
	reset_fixture();
	failures += check(discobsd_tee_main(21, arguments) == 1,
	    "descriptor exhaustion returns success");
	failures += check(open_calls == 19,
	    "descriptor exhaustion opens beyond the fixed capacity");
	return failures;
}

int
main(void)
{
	int failures = complete_copy_case() + isolated_failure_case() +
	    error_case() + capacity_case();

	if (failures == 0)
		printf("tee I/O contract tests passed\n");
	return failures != 0;
}
