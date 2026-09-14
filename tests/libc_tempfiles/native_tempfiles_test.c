#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

FILE *discobsd_tmpfile(void);
char *discobsd_tmpnam(char *);

static char collision_path[L_tmpnam];
static int collision_descriptor = -1;

static void
cleanup_collision(void)
{
	int saved_errno;

	saved_errno = errno;
	if (collision_descriptor >= 0)
		(void)close(collision_descriptor);
	if (collision_path[0] != '\0')
		(void)unlink(collision_path);
	errno = saved_errno;
}

static void
fail(const char *message)
{
	fprintf(stderr, "libc native tempfiles test: %s: %s\n", message,
	    strerror(errno));
	exit(1);
}

static void
require(int condition, const char *message)
{
	if (!condition)
		fail(message);
}

int
main(void)
{
	char candidate_path[L_tmpnam];
	char read_buffer[16];
	char *internal_path;
	FILE *stream;
	struct stat status;
	int attempt;
	int close_status;

	if (atexit(cleanup_collision) != 0) {
		errno = ENOMEM;
		fail("register collision cleanup");
	}

	stream = discobsd_tmpfile();
	require(stream != NULL, "tmpfile returned a stream");
	require(fstat(fileno(stream), &status) == 0,
	    "tmpfile descriptor status");
	require(status.st_nlink == 0, "tmpfile name was unlinked");
	require(fputs("durable", stream) >= 0, "tmpfile write");
	rewind(stream);
	require(fgets(read_buffer, sizeof(read_buffer), stream) == read_buffer,
	    "tmpfile read");
	require(strcmp(read_buffer, "durable") == 0, "tmpfile contents");
	close_status = fclose(stream);
	require(close_status == 0, "tmpfile close");

	for (attempt = 0; attempt < 10; ++attempt) {
		internal_path = discobsd_tmpnam(NULL);
		require(internal_path != NULL, "tmpnam internal result");
		strcpy(candidate_path, internal_path);
		collision_descriptor = open(candidate_path,
		    O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (collision_descriptor >= 0) {
			strcpy(collision_path, candidate_path);
			break;
		}
		require(errno == EEXIST, "tmpnam collision setup");
	}
	require(collision_descriptor >= 0, "tmpnam collision file creation");
	internal_path = discobsd_tmpnam(NULL);
	require(internal_path != NULL, "tmpnam collision retry");
	require(strcmp(internal_path, collision_path) != 0,
	    "tmpnam avoids an existing candidate");
	require(close(collision_descriptor) == 0, "tmpnam collision close");
	collision_descriptor = -1;
	require(unlink(collision_path) == 0, "tmpnam collision cleanup");
	collision_path[0] = '\0';

	puts("libc native tempfiles tests passed");
	return 0;
}
