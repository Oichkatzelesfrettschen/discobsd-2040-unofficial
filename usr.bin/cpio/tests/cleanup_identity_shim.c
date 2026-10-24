#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int close_failure_complete;
static int write_failure_complete;

int
unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor)
{
    struct stat expected_status;
    struct stat path_status;

    if (fstat(expected_descriptor, &expected_status) < 0)
        return -1;
    if (!S_ISREG(expected_status.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (fstatat(root_descriptor, path, &path_status,
        AT_SYMLINK_NOFOLLOW) < 0)
        return -1;
    if (!S_ISREG(path_status.st_mode) ||
        path_status.st_dev != expected_status.st_dev ||
        path_status.st_ino != expected_status.st_ino) {
        errno = ESTALE;
        return -1;
    }
    return unlinkat(root_descriptor, path, 0);
}

int
cpio_test_close(int descriptor)
{
    int result = close(descriptor);

    if (descriptor <= STDERR_FILENO || close_failure_complete ||
        getenv("CPIO_TEST_FAIL_CLOSE") == NULL)
        return result;
    close_failure_complete = 1;
    if (result < 0)
        return -1;
    errno = EIO;
    return -1;
}

ssize_t
cpio_test_write(int descriptor, const void *buffer, size_t byte_count)
{
    const char *replacement_path;
    const char *target_path;

    if (descriptor <= STDERR_FILENO || write_failure_complete ||
        getenv("CPIO_TEST_FAIL_WRITE") == NULL)
        return write(descriptor, buffer, byte_count);

    replacement_path = getenv("CPIO_TEST_REPLACEMENT");
    target_path = getenv("CPIO_TEST_TARGET");
    if ((replacement_path == NULL) != (target_path == NULL)) {
        errno = EINVAL;
        return -1;
    }
    if (replacement_path != NULL &&
        rename(replacement_path, target_path) < 0)
        return -1;
    write_failure_complete = 1;
    errno = ENOSPC;
    return -1;
}
