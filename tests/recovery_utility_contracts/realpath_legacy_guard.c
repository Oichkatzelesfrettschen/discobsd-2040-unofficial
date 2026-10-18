#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>

#undef realpath

char *realpath(const char *, char *);

char *
discobsd_test_realpath(const char *path, char *resolved)
{
	struct stat status;

	if (lstat(path, &status) == 0 && S_ISLNK(status.st_mode)) {
		errno = ELOOP;
		return NULL;
	}
	return realpath(path, resolved);
}
