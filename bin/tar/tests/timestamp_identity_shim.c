#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int substitution_complete;

static int
create_replacement(const char *path, const char *kind)
{
    static const char replacement[] = "replacement\n";
    int descriptor;

    if (strcmp(kind, "directory") == 0)
        return mkdir(path, 0700);
    if (strcmp(kind, "file") != 0) {
        errno = EINVAL;
        return -1;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0)
        return -1;
    if (write(descriptor, replacement, sizeof(replacement) - 1) !=
        (ssize_t)(sizeof(replacement) - 1)) {
        int saved_errno = errno;

        (void)close(descriptor);
        errno = saved_errno;
        return -1;
    }
    return close(descriptor);
}

static int
substitute_path(void)
{
    const char *kind;
    const char *moved_path;
    const char *original_path;

    if (substitution_complete)
        return 0;
    original_path = getenv("TAR_TIMESTAMP_ORIGINAL");
    moved_path = getenv("TAR_TIMESTAMP_MOVED");
    kind = getenv("TAR_TIMESTAMP_KIND");
    if (original_path == NULL || moved_path == NULL || kind == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (rename(original_path, moved_path) < 0)
        return -1;
    if (create_replacement(original_path, kind) < 0)
        return -1;
    substitution_complete = 1;
    return 0;
}

int
tar_test_futimes(int descriptor, const struct timeval times[2])
{
    if (substitute_path() < 0)
        return -1;
    return futimes(descriptor, times);
}

int
tar_test_utimes(const char *path, const struct timeval times[2])
{
    const char *original_path = getenv("TAR_TIMESTAMP_ORIGINAL");

    if (original_path == NULL || strcmp(path, original_path) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (substitute_path() < 0)
        return -1;
    return utimes(path, times);
}
