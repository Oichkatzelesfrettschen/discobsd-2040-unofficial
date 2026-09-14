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

int
main(int argc, char **argv)
{
	const char *word;
	int argument_index;

	for (;;) {
		if (argc == 1) {
			if (write_all(STDOUT_FILENO, "y\n", 2) < 0)
				break;
			continue;
		}
		for (argument_index = 1; argument_index < argc; ++argument_index) {
			word = argv[argument_index];
			if (write_all(STDOUT_FILENO, word, strlen(word)) < 0 ||
			    (argument_index + 1 < argc &&
			    write_all(STDOUT_FILENO, " ", 1) < 0))
				goto write_failed;
		}
		if (write_all(STDOUT_FILENO, "\n", 1) < 0)
			break;
	}
write_failed:
	(void)write_all(STDERR_FILENO, "yes: write failed\n", 18);
	return 1;
}
