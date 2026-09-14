#include <sys/exec_aout.h>
#include <sys/exec_hsaout.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_MINIMUM 4
#define MAXIMUM_MINIMUM 255
#define READ_BUFFER_SIZE 256

struct string_state {
	unsigned char pending[MAXIMUM_MINIMUM];
	unsigned minimum;
	unsigned pending_length;
	int emitting;
	int failed;
};

static int
write_all(int descriptor, const void *data, size_t length)
{
	const unsigned char *cursor;
	ssize_t written;

	cursor = (const unsigned char *)data;
	while (length != 0) {
		written = write(descriptor, cursor, length);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (written == 0)
			return -1;
		cursor += written;
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
report_file_error(const char *name, const char *reason)
{
	(void)write_text(STDERR_FILENO, "strings: ");
	(void)write_text(STDERR_FILENO, name);
	(void)write_text(STDERR_FILENO, reason);
}

static void
consume_byte(struct string_state *state, unsigned char byte)
{
	if (byte >= 0x20 && byte <= 0x7e) {
		if (state->emitting) {
			if (write_all(STDOUT_FILENO, &byte, 1) < 0)
				state->failed = 1;
			return;
		}
		state->pending[state->pending_length++] = byte;
		if (state->pending_length == state->minimum) {
			if (write_all(STDOUT_FILENO, state->pending,
			    state->pending_length) < 0)
				state->failed = 1;
			state->emitting = 1;
		}
		return;
	}
	if (state->emitting && write_all(STDOUT_FILENO, "\n", 1) < 0)
		state->failed = 1;
	state->pending_length = 0;
	state->emitting = 0;
}

static void
consume_bytes(struct string_state *state, const unsigned char *data,
    size_t length)
{
	size_t index;

	for (index = 0; index < length && !state->failed; ++index)
		consume_byte(state, data[index]);
}

static int
read_prefix(int descriptor, unsigned char *prefix, size_t prefix_size)
{
	ssize_t amount;
	size_t length;

	length = 0;
	while (length < prefix_size) {
		amount = read(descriptor, prefix + length, prefix_size - length);
		if (amount < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (amount == 0)
			break;
		length += (size_t)amount;
	}
	return (int)length;
}

static int
scan_descriptor(int descriptor, const char *name, unsigned minimum,
    int scan_all)
{
	struct string_state state;
	struct exec header;
	unsigned char prefix[sizeof(header)];
	unsigned char buffer[READ_BUFFER_SIZE];
	unsigned remaining;
	ssize_t amount;
	int prefix_length;
	int raw_aout;

	memset(&state, 0, sizeof(state));
	state.minimum = minimum;
	raw_aout = 0;
	remaining = 0;
	prefix_length = 0;
	if (!scan_all) {
		prefix_length = read_prefix(descriptor, prefix, sizeof(prefix));
		if (prefix_length < 0) {
			report_file_error(name, ": read failed\n");
			return 1;
		}
		if ((size_t)prefix_length == sizeof(header)) {
			memcpy(&header, prefix, sizeof(header));
			if (N_GETMAGIC(header) == OMAGIC &&
			    N_GETMID(header) == MID_ZERO) {
				if (N_GETFLAG(header) == EX_HSPACK) {
					report_file_error(name,
					    ": packed a.out requires -a\n");
					return 1;
				}
				if (N_GETFLAG(header) == 0 &&
				    header.a_text <= 0xffffffffU - header.a_data) {
					raw_aout = 1;
					remaining = header.a_text + header.a_data;
				}
			}
		}
	}
	if (!raw_aout && prefix_length != 0)
		consume_bytes(&state, prefix, (size_t)prefix_length);
	while (!state.failed && (!raw_aout || remaining != 0)) {
		size_t request;

		request = sizeof(buffer);
		if (raw_aout && remaining < request)
			request = remaining;
		amount = read(descriptor, buffer, request);
		if (amount < 0) {
			if (errno == EINTR)
				continue;
			report_file_error(name, ": read failed\n");
			return 1;
		}
		if (amount == 0)
			break;
		consume_bytes(&state, buffer, (size_t)amount);
		if (raw_aout)
			remaining -= (unsigned)amount;
	}
	if (state.failed) {
		(void)write_text(STDERR_FILENO, "strings: write failed\n");
		return 1;
	}
	if (raw_aout && remaining != 0) {
		report_file_error(name, ": truncated a.out\n");
		return 1;
	}
	consume_byte(&state, 0);
	if (state.failed) {
		(void)write_text(STDERR_FILENO, "strings: write failed\n");
		return 1;
	}
	return 0;
}

static void
usage(void)
{
	(void)write_text(STDERR_FILENO,
	    "usage: strings [-a] [-n length] [file ...]\n");
}

int
main(int argc, char **argv)
{
	char *number_end;
	const char *minimum_argument;
	unsigned long parsed_minimum;
	unsigned minimum;
	int argument_index;
	int descriptor;
	int scan_all;
	int status;

	minimum = DEFAULT_MINIMUM;
	scan_all = 0;
	argument_index = 1;
	while (argument_index < argc && argv[argument_index][0] == '-' &&
	    argv[argument_index][1] != '\0') {
		if (strcmp(argv[argument_index], "--") == 0) {
			++argument_index;
			break;
		}
		if (strcmp(argv[argument_index], "-a") == 0) {
			scan_all = 1;
			++argument_index;
			continue;
		}
		if (strcmp(argv[argument_index], "-n") == 0) {
			if (++argument_index == argc) {
				usage();
				return 1;
			}
			minimum_argument = argv[argument_index];
		} else if (strncmp(argv[argument_index], "-n", 2) == 0) {
			minimum_argument = argv[argument_index] + 2;
		} else {
			usage();
			return 1;
		}
		errno = 0;
		parsed_minimum = strtoul(minimum_argument, &number_end, 10);
		if (errno != 0 || *minimum_argument == '\0' ||
		    *number_end != '\0' || parsed_minimum == 0 ||
		    parsed_minimum > MAXIMUM_MINIMUM) {
			usage();
			return 1;
		}
		minimum = (unsigned)parsed_minimum;
		++argument_index;
	}
	status = 0;
	if (argument_index == argc)
		return scan_descriptor(STDIN_FILENO, "standard input", minimum,
		    scan_all);
	for (; argument_index < argc; ++argument_index) {
		descriptor = open(argv[argument_index], O_RDONLY);
		if (descriptor < 0) {
			report_file_error(argv[argument_index], ": cannot open\n");
			status = 1;
			continue;
		}
		if (scan_descriptor(descriptor, argv[argument_index], minimum,
		    scan_all))
			status = 1;
		(void)close(descriptor);
	}
	return status;
}
