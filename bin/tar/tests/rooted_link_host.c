#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int substitution_complete;

static int
write_replacement(int root_descriptor, const char *path)
{
    static const char replacement[] = "foreign replacement\n";
    const char *kind = getenv("TAR_LINK_REPLACEMENT_KIND");
    int descriptor;

    if (kind != NULL && strcmp(kind, "symlink") == 0)
        return symlinkat("foreign-target", root_descriptor, path);
    if (kind != NULL && strcmp(kind, "regular") != 0) {
        errno = EINVAL;
        return -1;
    }
    descriptor = openat(root_descriptor, path,
        O_WRONLY | O_CREAT | O_EXCL, 0600);
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
substitute_source(int root_descriptor, const char *source_path)
{
    const char *moved_path = getenv("TAR_LINK_MOVED_SOURCE");

    if (moved_path == NULL || substitution_complete)
        return 0;
    if (renameat(root_descriptor, source_path, root_descriptor,
        moved_path) < 0)
        return -1;
    if (write_replacement(root_descriptor, source_path) < 0)
        return -1;
    substitution_complete = 1;
    return 0;
}

int
linkat_beneath(int root_descriptor, const char *source_path,
    int expected_source_descriptor, const char *target_path)
{
    struct stat expected_status;
    struct stat source_status;

    if (getenv("TAR_LINK_TRACE") != NULL)
        fputs("tar hard-link identity: rooted call\n", stderr);
    if (substitute_source(root_descriptor, source_path) < 0)
        return -1;
    if (getenv("TAR_LINK_ABORT_AFTER_SUBSTITUTION") != NULL)
        abort();
    if (fstat(expected_source_descriptor, &expected_status) < 0)
        return -1;
    if (!S_ISREG(expected_status.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (fstatat(root_descriptor, source_path, &source_status,
        AT_SYMLINK_NOFOLLOW) < 0)
        return -1;
    if (S_ISLNK(source_status.st_mode)) {
        errno = ELOOP;
        return -1;
    }
    if (!S_ISREG(source_status.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (source_status.st_dev != expected_status.st_dev ||
        source_status.st_ino != expected_status.st_ino) {
        errno = ESTALE;
        return -1;
    }
    return linkat(root_descriptor, source_path, root_descriptor,
        target_path, 0);
}

int
tar_test_path_link(const char *source_path, const char *target_path)
{
    if (getenv("TAR_LINK_TRACE") != NULL)
        fputs("tar hard-link identity: pathname call\n", stderr);
    if (substitute_source(AT_FDCWD, source_path) < 0)
        return -1;
    return link(source_path, target_path);
}
