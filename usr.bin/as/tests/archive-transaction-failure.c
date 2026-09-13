#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *
selected_failure(void)
{
	const char *operation;

	operation = getenv("ARCHIVE_FAIL_OPERATION");
	return(operation == NULL ? "" : operation);
}

static int
call_next_path_operation(const char *symbol, const char *source_path,
    const char *destination_path)
{
	int (*operation)(const char *, const char *);

	operation = dlsym(RTLD_NEXT, symbol);
	if (operation == NULL)
		_exit(125);
	return(operation(source_path, destination_path));
}

int
rename(const char *source_path, const char *destination_path)
{
	if (strcmp(selected_failure(), "rename") == 0) {
		errno = EIO;
		return(-1);
	}
	return(call_next_path_operation("rename", source_path, destination_path));
}

int
link(const char *source_path, const char *destination_path)
{
	if (strcmp(selected_failure(), "link") == 0) {
		errno = EIO;
		return(-1);
	}
	return(call_next_path_operation("link", source_path, destination_path));
}

int
fsync(int fd)
{
	static unsigned directory_sync_count;
	int (*next_fsync)(int);
	const char *failure;
	struct stat file_stat;

	next_fsync = dlsym(RTLD_NEXT, "fsync");
	if (next_fsync == NULL)
		_exit(125);
	if (fstat(fd, &file_stat) < 0)
		return(next_fsync(fd));

	failure = selected_failure();
	if (S_ISDIR(file_stat.st_mode)) {
		directory_sync_count++;
		if (strcmp(failure, "directory-fsync") == 0 ||
		    (strcmp(failure, "post-commit-directory-fsync") == 0 &&
		    directory_sync_count == 2)) {
			errno = EIO;
			return(-1);
		}
	} else if (strcmp(failure, "file-fsync") == 0) {
		errno = EIO;
		return(-1);
	}
	return(next_fsync(fd));
}
