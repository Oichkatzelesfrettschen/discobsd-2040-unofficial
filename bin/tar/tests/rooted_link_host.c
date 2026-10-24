#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

#undef read
#undef write
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);

static int substitution_complete;
static int archive_read_calls;
static int archive_write_calls;

static int
same_inode(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

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
openat_beneath(int root_descriptor, const char *path, int flags, ...)
{
    int host_flags = flags;
    mode_t mode = 0;

    if ((flags & O_CREAT) != 0) {
        va_list arguments;

        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    if (getenv("TAR_REGULAR_TRACE") != NULL)
        fputs("tar regular publication: rooted open\n", stderr);
    if ((flags & O_IDENTITY) != 0) {
#if defined(O_PATH)
        host_flags = (flags & ~O_IDENTITY) | O_PATH;
#elif defined(O_EVTONLY)
        host_flags = (flags & ~O_IDENTITY) | O_EVTONLY;
#else
#error "tar identity fixture requires O_PATH or O_EVTONLY"
#endif
    }
    if ((flags & O_CREAT) != 0)
        return openat(root_descriptor, path, host_flags | O_NOFOLLOW, mode);
    return openat(root_descriptor, path, host_flags | O_NOFOLLOW);
}

ssize_t
tar_test_read(int descriptor, void *buffer, size_t byte_count)
{
    if (getenv("TAR_REGULAR_READ_ERROR") == NULL)
        return read(descriptor, buffer, byte_count);
    archive_read_calls++;
    if (archive_read_calls == 1 && byte_count > 1024U)
        byte_count = 1024U;
    else if (archive_read_calls > 1) {
        errno = EIO;
        return -1;
    }
    return read(descriptor, buffer, byte_count);
}

ssize_t
tar_test_write(int descriptor, const void *buffer, size_t byte_count)
{
    if (getenv("TAR_REGULAR_WRITE_ERROR") != NULL &&
        archive_write_calls++ == 0) {
        errno = EIO;
        return -1;
    }
    return write(descriptor, buffer, byte_count);
}

int
unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor)
{
    struct stat expected_status;
    struct stat path_status;

    if (getenv("TAR_REGULAR_TRACE") != NULL)
        fputs("tar regular publication: rooted cleanup\n", stderr);
    if (fstat(expected_descriptor, &expected_status) < 0 ||
        fstatat(root_descriptor, path, &path_status,
        AT_SYMLINK_NOFOLLOW) < 0)
        return -1;
    if (!S_ISREG(path_status.st_mode) ||
        !same_inode(&expected_status, &path_status)) {
        errno = ESTALE;
        return -1;
    }
    return unlinkat(root_descriptor, path, 0);
}

static int
schedule_publication_replacement(int root_descriptor,
    const char *staged_path, const char *target_path,
    int expected_target_descriptor)
{
    const char *moved_stage = getenv("TAR_REGULAR_MOVED_STAGE");
    const char *moved_target = getenv("TAR_REGULAR_MOVED_TARGET");

    if (moved_stage != NULL) {
        if (renameat(root_descriptor, staged_path, root_descriptor,
            moved_stage) < 0 ||
            write_replacement(root_descriptor, staged_path) < 0)
            return -1;
    }
    if (moved_target != NULL && expected_target_descriptor >= 0) {
        if (renameat(root_descriptor, target_path, root_descriptor,
            moved_target) < 0 ||
            write_replacement(root_descriptor, target_path) < 0)
            return -1;
    } else if (getenv("TAR_REGULAR_CREATE_TARGET") != NULL &&
        expected_target_descriptor < 0 &&
        write_replacement(root_descriptor, target_path) < 0) {
        return -1;
    }
    return 0;
}

int
publishat_beneath(int root_descriptor, const char *staged_path,
    int expected_staged_descriptor, const char *target_path,
    int expected_target_descriptor)
{
    struct stat expected_staged_status;
    struct stat expected_target_status;
    struct stat staged_status;
    struct stat target_status;
    int target_result;

    if (getenv("TAR_REGULAR_TRACE") != NULL)
        fputs("tar regular publication: rooted publish\n", stderr);
    if (schedule_publication_replacement(root_descriptor, staged_path,
        target_path, expected_target_descriptor) < 0)
        return -1;
    if (fstat(expected_staged_descriptor, &expected_staged_status) < 0 ||
        fstatat(root_descriptor, staged_path, &staged_status,
        AT_SYMLINK_NOFOLLOW) < 0)
        return -1;
    if (!S_ISREG(staged_status.st_mode) ||
        !same_inode(&expected_staged_status, &staged_status)) {
        errno = ESTALE;
        return -1;
    }
    target_result = fstatat(root_descriptor, target_path, &target_status,
        AT_SYMLINK_NOFOLLOW);
    if (expected_target_descriptor < 0) {
        if (target_result == 0) {
            errno = EEXIST;
            return -1;
        }
        if (errno != ENOENT)
            return -1;
    } else {
        if (fstat(expected_target_descriptor, &expected_target_status) < 0)
            return -1;
        if (target_result < 0 || !S_ISREG(target_status.st_mode) ||
            !same_inode(&expected_target_status, &target_status)) {
            errno = ESTALE;
            return -1;
        }
    }
    return renameat(root_descriptor, staged_path, root_descriptor,
        target_path);
}

int
tar_test_path_publish(int root_descriptor, const char *staged_path,
    int expected_staged_descriptor, const char *target_path,
    int expected_target_descriptor)
{
    (void)expected_staged_descriptor;
    if (getenv("TAR_REGULAR_TRACE") != NULL)
        fputs("tar regular publication: pathname publish\n", stderr);
    if (schedule_publication_replacement(root_descriptor, staged_path,
        target_path, expected_target_descriptor) < 0)
        return -1;
    return renameat(root_descriptor, staged_path, root_descriptor,
        target_path);
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
