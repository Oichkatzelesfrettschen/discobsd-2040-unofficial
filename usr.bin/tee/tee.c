/*
 * Copy standard input to standard output and each named file.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>

#define IO_SIZE 1024
#define MAX_OUTPUTS 20

struct output {
	int descriptor;
	const char *name;
};

static char transfer_buffer[IO_SIZE];

static void
put_string(const char *string)
{
	size_t length;

	length = 0;
	while (string[length] != '\0')
		length++;
	while (length != 0) {
		ssize_t written = write(STDERR_FILENO, string, length);

		if (written > 0) {
			string += written;
			length -= (size_t)written;
			continue;
		}
		if (written < 0 && errno == EINTR)
			continue;
		break;
	}
}

static void
diagnose(const char *operation, const char *name)
{
	put_string("tee: cannot ");
	put_string(operation);
	put_string(" ");
	put_string(name);
	put_string("\n");
}

static int
write_all(int descriptor, const char *buffer, size_t length)
{
	while (length != 0) {
		ssize_t written = write(descriptor, buffer, length);

		if (written > 0) {
			buffer += written;
			length -= (size_t)written;
			continue;
		}
		if (written < 0 && errno == EINTR)
			continue;
		if (written == 0)
			errno = EIO;
		return -1;
	}
	return 0;
}

static int
usage(void)
{
	put_string("usage: tee [-ai] [file ...]\n");
	return 1;
}

int
main(int argc, char **argv)
{
	struct output outputs[MAX_OUTPUTS];
	int append = 0;
	int ignore_interrupts = 0;
	int output_count = 1;
	int status = 0;
	int argument = 1;

	outputs[0].descriptor = STDOUT_FILENO;
	outputs[0].name = "standard output";
	while (argument < argc && argv[argument][0] == '-' &&
	    argv[argument][1] != '\0') {
		const char *option;

		if (argv[argument][1] == '-' && argv[argument][2] == '\0') {
			argument++;
			break;
		}
		for (option = argv[argument] + 1; *option != '\0'; option++) {
			if (*option == 'a')
				append = 1;
			else if (*option == 'i')
				ignore_interrupts = 1;
			else
				return usage();
		}
		argument++;
	}
	if (ignore_interrupts)
		(void)signal(SIGINT, SIG_IGN);

	for (; argument < argc; argument++) {
		int descriptor;
		int flags;

		if (output_count == MAX_OUTPUTS) {
			diagnose("open after descriptor limit", argv[argument]);
			status = 1;
			continue;
		}
		flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
		descriptor = open(argv[argument], flags, 0666);
		if (descriptor < 0) {
			diagnose("open", argv[argument]);
			status = 1;
			continue;
		}
		outputs[output_count].descriptor = descriptor;
		outputs[output_count].name = argv[argument];
		output_count++;
	}

	for (;;) {
		ssize_t received;
		int output_index;

		do {
			received = read(STDIN_FILENO, transfer_buffer,
			    sizeof(transfer_buffer));
		} while (received < 0 && errno == EINTR);
		if (received == 0)
			break;
		if (received < 0) {
			diagnose("read", "standard input");
			status = 1;
			break;
		}
		for (output_index = 0; output_index < output_count;
		    output_index++) {
			if (outputs[output_index].descriptor < 0)
				continue;
			if (write_all(outputs[output_index].descriptor,
			    transfer_buffer, (size_t)received) == 0)
				continue;
			diagnose("write", outputs[output_index].name);
			status = 1;
			if (output_index != 0) {
				if (close(outputs[output_index].descriptor) < 0)
					diagnose("close", outputs[output_index].name);
			}
			outputs[output_index].descriptor = -1;
		}
	}

	while (--output_count > 0) {
		if (outputs[output_count].descriptor >= 0 &&
		    close(outputs[output_count].descriptor) < 0) {
			diagnose("close", outputs[output_count].name);
			status = 1;
		}
	}
	return status;
}
