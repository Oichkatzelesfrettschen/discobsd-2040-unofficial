/* See LICENSE file for copyright and license details. */
#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *program)
{
	fprintf(stderr, "usage: %s [-E|-e] file\n", program);
}

static int
join_path(char *result, const char *directory, const char *name)
{
	size_t directory_length = strlen(directory);
	size_t name_length = strlen(name);
	int separator = directory_length != 0 &&
	    directory[directory_length - 1] != '/';

	if (directory_length + (size_t)separator + name_length + 1 >
	    MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(result, directory, directory_length);
	if (separator)
		result[directory_length++] = '/';
	memcpy(result + directory_length, name, name_length + 1);
	return 0;
}

static int
resolve_missing_final(const char *path, char *resolved)
{
	char current[MAXPATHLEN];
	char *slash;
	const char *base;
	struct stat status;
	ssize_t length;
	size_t path_length;
	unsigned int followed_links = 0;
	int directory_required = 0;

	path_length = strlen(path);
	if (path_length == 0 || path_length >= sizeof(current)) {
		errno = path_length == 0 ? ENOENT : ENAMETOOLONG;
		return -1;
	}
	memcpy(current, path, path_length + 1);
	while (path_length > 1 && current[path_length - 1] == '/') {
		directory_required = 1;
		current[--path_length] = '\0';
	}

	for (;;) {
		if (lstat(current, &status) == 0) {
			if (S_ISLNK(status.st_mode)) {
				if (followed_links >= MAXSYMLINKS) {
					errno = ELOOP;
					return -1;
				}
				length = readlink(current, resolved, MAXPATHLEN - 1);
				if (length < 0)
					return -1;
				if (length >= MAXPATHLEN - 1) {
					errno = ENAMETOOLONG;
					return -1;
				}
				resolved[length] = '\0';
				followed_links++;
				if (resolved[0] == '/') {
					memcpy(current, resolved, (size_t)length + 1);
					continue;
				}

				slash = strrchr(current, '/');
				if (slash == NULL)
					memcpy(current, ".", 2);
				else if (slash == current)
					current[1] = '\0';
				else
					*slash = '\0';
				if (join_path(current, current, resolved) < 0)
					return -1;
				continue;
			}
		} else if (errno != ENOENT) {
			return -1;
		}

		if (realpath(current, resolved) != NULL) {
			if (directory_required &&
			    (stat(resolved, &status) < 0 ||
			    !S_ISDIR(status.st_mode))) {
				errno = ENOTDIR;
				return -1;
			}
			return 0;
		}
		if (errno != ENOENT)
			return -1;

		slash = strrchr(current, '/');
		if (slash == NULL) {
			base = current;
			if (realpath(".", resolved) == NULL)
				return -1;
		} else {
			base = slash + 1;
			if (slash == current)
				current[1] = '\0';
			else
				*slash = '\0';
			if (realpath(current, resolved) == NULL)
				return -1;
		}
		if (*base == '\0') {
			errno = ENOENT;
			return -1;
		}
		if (stat(resolved, &status) < 0 || !S_ISDIR(status.st_mode)) {
			if (errno == 0)
				errno = ENOTDIR;
			return -1;
		}
		return join_path(resolved, resolved, base);
	}
}

int
main(int argc, char *argv[])
{
	char resolved[MAXPATHLEN];
	const char *program = argv[0];
	struct stat status;
	int ch;
	int require_existing = 0;

	opterr = 0;
	while ((ch = getopt(argc, argv, "Ee")) != -1) {
		switch (ch) {
		case 'E':
			require_existing = 0;
			break;
		case 'e':
			require_existing = 1;
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

	if ((require_existing && stat(argv[0], &status) < 0) ||
	    resolve_missing_final(argv[0], resolved) < 0) {
		fprintf(stderr, "%s: %s: %s\n", program, argv[0],
		    strerror(errno));
		return 1;
	}
	if (fputs(resolved, stdout) == EOF || fputc('\n', stdout) == EOF ||
	    fflush(stdout) == EOF) {
		fprintf(stderr, "%s: stdout: %s\n", program, strerror(errno));
		return 1;
	}
	return 0;
}
