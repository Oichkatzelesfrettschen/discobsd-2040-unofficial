#include <sys/exec_aout.h>
#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int discobsd_strings_main(int, char **);

static unsigned read_count;
static char diagnostic[64];
static size_t diagnostic_length;

ssize_t
test_read(int descriptor, void *buffer, size_t length)
{
	struct exec header;

	(void)descriptor;
	if (read_count++ == 0) {
		if (length < sizeof(header))
			return -1;
		memset(&header, 0, sizeof(header));
		N_SETMAGIC(header, OMAGIC, MID_ZERO, 0);
		header.a_text = 512;
		memcpy(buffer, &header, sizeof(header));
		return (ssize_t)sizeof(header);
	}
	if (read_count == 2) {
		if (length < 256)
			return -1;
		memset(buffer, 'A', 256);
		return 256;
	}
	return 0;
}

ssize_t
test_write(int descriptor, const void *buffer, size_t length)
{
	if (descriptor == STDOUT_FILENO) {
		errno = EPIPE;
		return -1;
	}
	if (descriptor != STDERR_FILENO ||
	    length > sizeof(diagnostic) - diagnostic_length - 1)
		return -1;
	memcpy(diagnostic + diagnostic_length, buffer, length);
	diagnostic_length += length;
	diagnostic[diagnostic_length] = '\0';
	return (ssize_t)length;
}

int
main(void)
{
	char *arguments[] = { (char *)"strings", NULL };

	if (discobsd_strings_main(1, arguments) != 1) {
		fprintf(stderr, "strings output error test: scan succeeded\n");
		return EXIT_FAILURE;
	}
	if (strcmp(diagnostic, "strings: write failed\n") != 0) {
		fprintf(stderr, "strings output error test: diagnostic <%s>\n",
		    diagnostic);
		return EXIT_FAILURE;
	}
	if (read_count != 2) {
		fprintf(stderr, "strings output error test: read count %u\n",
		    read_count);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
