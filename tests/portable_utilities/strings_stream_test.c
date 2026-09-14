#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int discobsd_strings_main(int, char **);

static const char first_chunk[] = "LIVE";
static char captured_output[sizeof(first_chunk) + 1];
static size_t captured_length;
static unsigned read_count;
static int first_chunk_was_emitted;

ssize_t
test_read(int descriptor, void *buffer, size_t length)
{
	(void)descriptor;
	if (read_count++ == 0) {
		if (length < sizeof(first_chunk) - 1)
			return -1;
		memcpy(buffer, first_chunk, sizeof(first_chunk) - 1);
		return sizeof(first_chunk) - 1;
	}
	first_chunk_was_emitted =
	    captured_length == sizeof(first_chunk) - 1 &&
	    memcmp(captured_output, first_chunk, sizeof(first_chunk) - 1) == 0;
	return 0;
}

ssize_t
test_write(int descriptor, const void *buffer, size_t length)
{
	if (descriptor != STDOUT_FILENO ||
	    length > sizeof(captured_output) - captured_length)
		return -1;
	memcpy(captured_output + captured_length, buffer, length);
	captured_length += length;
	return (ssize_t)length;
}

int
main(void)
{
	char *arguments[] = { (char *)"strings", (char *)"-a", NULL };

	if (discobsd_strings_main(2, arguments) != 0) {
		fprintf(stderr, "strings stream test: scan failed\n");
		return EXIT_FAILURE;
	}
	if (!first_chunk_was_emitted) {
		fprintf(stderr,
		    "strings stream test: first short read was buffered\n");
		return EXIT_FAILURE;
	}
	if (captured_length != sizeof(first_chunk) ||
	    memcmp(captured_output, "LIVE\n", sizeof(first_chunk)) != 0) {
		fprintf(stderr, "strings stream test: output mismatch\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
