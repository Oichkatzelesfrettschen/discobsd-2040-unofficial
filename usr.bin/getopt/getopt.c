#include <errno.h>
#include <string.h>
#include <unistd.h>

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

static int
write_option(char option, const char *argument)
{
	if (write_text(STDOUT_FILENO, " -") < 0 ||
	    write_all(STDOUT_FILENO, &option, 1) < 0)
		return -1;
	if (argument != NULL &&
	    (write_all(STDOUT_FILENO, " ", 1) < 0 ||
	    write_text(STDOUT_FILENO, argument) < 0))
		return -1;
	return 0;
}

static void
report_invalid(char option, int missing_argument)
{
	(void)write_text(STDERR_FILENO, "getopt: option ");
	(void)write_all(STDERR_FILENO, &option, 1);
	(void)write_text(STDERR_FILENO, missing_argument ?
	    " requires an argument\n" : " is invalid\n");
}

int
main(int argc, char **argv)
{
	const char *argument;
	const char *option_definition;
	const char *option_string;
	const char *option_cursor;
	const char *value;
	char current_option;
	int argument_index;
	int status;

	if (argc < 2) {
		(void)write_text(STDERR_FILENO,
		    "usage: getopt optstring parameters ...\n");
		return 1;
	}
	option_string = argv[1];
	argument_index = 2;
	status = 0;
	while (argument_index < argc) {
		argument = argv[argument_index];
		if (argument[0] != '-' || argument[1] == '\0')
			break;
		if (argument[1] == '-' && argument[2] == '\0') {
			++argument_index;
			break;
		}
		for (option_cursor = argument + 1; *option_cursor != '\0';
		    ++option_cursor) {
			current_option = *option_cursor;
			option_definition = strchr(option_string, current_option);
			if (current_option == ':' || option_definition == NULL) {
				report_invalid(current_option, 0);
				status = 1;
				continue;
			}
			value = NULL;
			if (option_definition[1] == ':') {
				if (option_cursor[1] != '\0') {
					value = option_cursor + 1;
					option_cursor += strlen(option_cursor + 1);
				} else if (argument_index + 1 < argc) {
					value = argv[++argument_index];
				} else {
					report_invalid(current_option, 1);
					status = 1;
					break;
				}
			}
			if (write_option(current_option, value) < 0)
				return 1;
		}
		++argument_index;
	}
	if (write_text(STDOUT_FILENO, " --") < 0)
		return 1;
	for (; argument_index < argc; ++argument_index)
		if (write_all(STDOUT_FILENO, " ", 1) < 0 ||
		    write_text(STDOUT_FILENO, argv[argument_index]) < 0)
			return 1;
	if (write_all(STDOUT_FILENO, "\n", 1) < 0)
		return 1;
	return status;
}
