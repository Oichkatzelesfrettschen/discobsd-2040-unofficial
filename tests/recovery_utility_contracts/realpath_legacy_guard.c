#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>

char *__real_realpath(const char *, char *);

char *
__wrap_realpath(const char *path, char *resolved)
{
	struct stat status;

	if (lstat(path, &status) == 0 && S_ISLNK(status.st_mode)) {
		errno = ELOOP;
		return NULL;
	}
	return __real_realpath(path, resolved);
}
