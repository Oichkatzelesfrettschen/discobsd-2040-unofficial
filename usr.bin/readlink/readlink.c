/* See LICENSE file for copyright and license details. */
#include <sys/param.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *program)
{
	fprintf(stderr, "usage: %s [-n] file\n", program);
}

int
main(int argc, char *argv[])
{
	char target[MAXPATHLEN];
	const char *program = argv[0];
	ssize_t length;
	int ch;
	int newline = 1;

	opterr = 0;
	while ((ch = getopt(argc, argv, "n")) != -1) {
		switch (ch) {
		case 'n':
			newline = 0;
			break;
		default:
			usage(program);
			return 1;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usage(program);
		return 1;
	}

	length = readlink(argv[0], target, sizeof(target));
	if (length < 0) {
		fprintf(stderr, "%s: %s: %s\n", program, argv[0],
		    strerror(errno));
		return 1;
	}
	if ((size_t)length == sizeof(target)) {
		fprintf(stderr, "%s: %s: symbolic link target is too long\n",
		    program, argv[0]);
		return 1;
	}
	if (fwrite(target, 1, (size_t)length, stdout) != (size_t)length ||
	    (newline && fputc('\n', stdout) == EOF) || fflush(stdout) == EOF) {
		fprintf(stderr, "%s: stdout: %s\n", program, strerror(errno));
		return 1;
	}
	return 0;
}
