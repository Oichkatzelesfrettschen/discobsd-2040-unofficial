#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cleanup_identity_shim.h"

#ifndef CPIO_PATH_LIMIT
#define CPIO_PATH_LIMIT 256
#endif

static int close_failure_complete;
static int create_relocation_complete;
static int directory_relocation_complete;
static int directory_verify_replacement_complete;
static int publication_relocation_complete;
static int publication_stage_replacement_complete;
static int publication_target_replacement_complete;
static int staged_cleanup_replacement_complete;
static int write_failure_complete;
static int staged_root_descriptor = -1;
static int staged_descriptor = -1;
static char staged_path[CPIO_PATH_LIMIT];

static int
same_inode(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static void
record_call(const char *name)
{
    const char *trace_path = getenv("CPIO_TEST_TRACE");
    FILE *trace_stream;

    if (trace_path == NULL)
        return;
    trace_stream = fopen(trace_path, "a");
    if (trace_stream == NULL)
        return;
    (void)fprintf(trace_stream, "%s\n", name);
    (void)fclose(trace_stream);
}

static int
valid_component(const char *component)
{
    return component[0] != '\0' && strcmp(component, ".") != 0 &&
        strcmp(component, "..") != 0;
}

static int
resolve_parent_beneath(int root_descriptor, const char *path,
    int *parent_descriptor, char leaf[CPIO_PATH_LIMIT], dev_t *root_device)
{
    struct stat status;
    char path_copy[CPIO_PATH_LIMIT];
    char *component;
    char *separator;
    size_t path_length;
    int current_descriptor;

    if (path == NULL || path[0] == '\0' || path[0] == '/') {
        errno = EINVAL;
        return -1;
    }
    path_length = strlen(path);
    if (path_length >= sizeof(path_copy)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(path_copy, path, path_length + 1);
    current_descriptor = dup(root_descriptor);
    if (current_descriptor < 0)
        return -1;
    if (fstat(current_descriptor, &status) < 0) {
        int saved_errno = errno;

        (void)close(current_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        (void)close(current_descriptor);
        errno = ENOTDIR;
        return -1;
    }
    *root_device = status.st_dev;
    component = path_copy;
    for (;;) {
        int next_descriptor;

        separator = strchr(component, '/');
        if (separator == NULL)
            break;
        *separator = '\0';
        if (!valid_component(component)) {
            (void)close(current_descriptor);
            errno = EINVAL;
            return -1;
        }
        next_descriptor = openat(current_descriptor, component,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (next_descriptor < 0) {
            int saved_errno = errno;

            (void)close(current_descriptor);
            errno = saved_errno;
            return -1;
        }
        if (fstat(next_descriptor, &status) < 0) {
            int saved_errno = errno;

            (void)close(next_descriptor);
            (void)close(current_descriptor);
            errno = saved_errno;
            return -1;
        }
        if (!S_ISDIR(status.st_mode) || status.st_dev != *root_device) {
            int saved_errno = status.st_dev != *root_device ? EXDEV : ENOTDIR;

            (void)close(next_descriptor);
            (void)close(current_descriptor);
            errno = saved_errno;
            return -1;
        }
        if (close(current_descriptor) < 0) {
            int saved_errno = errno;

            (void)close(next_descriptor);
            errno = saved_errno;
            return -1;
        }
        current_descriptor = next_descriptor;
        component = separator + 1;
    }
    if (!valid_component(component)) {
        (void)close(current_descriptor);
        errno = EINVAL;
        return -1;
    }
    memcpy(leaf, component, strlen(component) + 1);
    *parent_descriptor = current_descriptor;
    return 0;
}

static int
relocate_ancestor(int root_descriptor)
{
    const char *destination = getenv("CPIO_TEST_RELOCATE_DESTINATION");
    const char *replacement = getenv("CPIO_TEST_RELOCATE_REPLACEMENT");
    const char *source = getenv("CPIO_TEST_RELOCATE_ANCESTOR");

    if (source == NULL || destination == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (renameat(root_descriptor, source, root_descriptor, destination) < 0)
        return -1;
    if (replacement != NULL &&
        renameat(root_descriptor, replacement, root_descriptor, source) < 0)
        return -1;
    record_call("relocate_ancestor");
    return 0;
}

static int
write_stage_path_record(const char *path)
{
    const char *record_path = getenv("CPIO_TEST_STAGE_PATH_FILE");
    FILE *record_stream;

    if (record_path == NULL)
        return 0;
    record_stream = fopen(record_path, "w");
    if (record_stream == NULL)
        return -1;
    if (fprintf(record_stream, "%s\n", path) < 0) {
        int saved_errno = errno;

        (void)fclose(record_stream);
        errno = saved_errno;
        return -1;
    }
    if (fclose(record_stream) < 0)
        return -1;
    return 0;
}

static int
replace_staged_entry(void)
{
    static const char replacement[] = "foreign staged replacement\n";
    const char *saved_path = getenv("CPIO_TEST_STAGE_SAVED_PATH");
    int replacement_descriptor;

    if (staged_root_descriptor < 0 || staged_descriptor < 0 ||
        staged_path[0] == '\0' || saved_path == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (renameat(staged_root_descriptor, staged_path,
        staged_root_descriptor, saved_path) < 0)
        return -1;
    replacement_descriptor = openat(staged_root_descriptor, staged_path,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (replacement_descriptor < 0)
        return -1;
    if (write(replacement_descriptor, replacement,
        sizeof(replacement) - 1) != (ssize_t)(sizeof(replacement) - 1) ||
        close(replacement_descriptor) < 0)
        return -1;
    record_call("replace_staged_entry");
    return 0;
}

int
openat_beneath(int root_descriptor, const char *path, int flags, ...)
{
    struct stat status;
    char leaf[CPIO_PATH_LIMIT];
    dev_t root_device;
    mode_t mode = 0;
    int descriptor;
    int parent_descriptor;

    if ((flags & O_CREAT) != 0) {
        va_list arguments;

        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    if (resolve_parent_beneath(root_descriptor, path, &parent_descriptor,
        leaf, &root_device) < 0)
        return -1;
    if ((flags & O_CREAT) != 0)
        descriptor = openat(parent_descriptor, leaf, flags | O_NOFOLLOW,
            mode);
    else
        descriptor = openat(parent_descriptor, leaf, flags | O_NOFOLLOW);
    if (descriptor < 0) {
        int saved_errno = errno;

        (void)close(parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (fstat(descriptor, &status) < 0) {
        int saved_errno = errno;

        (void)close(descriptor);
        (void)close(parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (status.st_dev != root_device) {
        (void)close(descriptor);
        (void)close(parent_descriptor);
        errno = EXDEV;
        return -1;
    }
    if (close(parent_descriptor) < 0) {
        int saved_errno = errno;

        (void)close(descriptor);
        errno = saved_errno;
        return -1;
    }
    return descriptor;
}

int
mkdirat_beneath(int root_descriptor, const char *path, mode_t mode)
{
    char leaf[CPIO_PATH_LIMIT];
    dev_t root_device;
    int parent_descriptor;
    int result;

    if (resolve_parent_beneath(root_descriptor, path, &parent_descriptor,
        leaf, &root_device) < 0)
        return -1;
    (void)root_device;
    result = mkdirat(parent_descriptor, leaf, mode);
    if (result < 0) {
        int saved_errno = errno;

        (void)close(parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (close(parent_descriptor) < 0)
        return -1;
    return 0;
}

int
unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor)
{
    struct stat expected_status;
    struct stat path_status;
    char leaf[CPIO_PATH_LIMIT];
    dev_t root_device;
    int parent_descriptor;
    int result;

    if (fstat(expected_descriptor, &expected_status) < 0)
        return -1;
    if (!S_ISREG(expected_status.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (resolve_parent_beneath(root_descriptor, path, &parent_descriptor,
        leaf, &root_device) < 0)
        return -1;
    (void)root_device;
    if (fstatat(parent_descriptor, leaf, &path_status,
        AT_SYMLINK_NOFOLLOW) < 0) {
        int saved_errno = errno;

        (void)close(parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(path_status.st_mode) ||
        !same_inode(&path_status, &expected_status)) {
        (void)close(parent_descriptor);
        errno = ESTALE;
        return -1;
    }
    result = unlinkat(parent_descriptor, leaf, 0);
    if (result < 0) {
        int saved_errno = errno;

        (void)close(parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (close(parent_descriptor) < 0)
        return -1;
    return 0;
}

int
publishat_beneath(int root_descriptor, const char *staged_name,
    int expected_staged_descriptor, const char *target_path,
    int target_descriptor)
{
    struct stat expected_staged_status;
    struct stat staged_parent_status;
    struct stat staged_path_status;
    struct stat target_parent_status;
    struct stat target_path_status;
    char staged_leaf[CPIO_PATH_LIMIT];
    char target_leaf[CPIO_PATH_LIMIT];
    dev_t staged_root_device;
    dev_t target_root_device;
    int staged_parent_descriptor;
    int target_parent_descriptor;
    int target_result;
    int result;

    if (fstat(expected_staged_descriptor, &expected_staged_status) < 0)
        return -1;
    if (!S_ISREG(expected_staged_status.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (resolve_parent_beneath(root_descriptor, staged_name,
        &staged_parent_descriptor, staged_leaf, &staged_root_device) < 0)
        return -1;
    if (resolve_parent_beneath(root_descriptor, target_path,
        &target_parent_descriptor, target_leaf, &target_root_device) < 0) {
        int saved_errno = errno;

        (void)close(staged_parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (fstat(staged_parent_descriptor, &staged_parent_status) < 0 ||
        fstat(target_parent_descriptor, &target_parent_status) < 0) {
        int saved_errno = errno;

        (void)close(target_parent_descriptor);
        (void)close(staged_parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (staged_root_device != target_root_device ||
        !same_inode(&staged_parent_status, &target_parent_status)) {
        (void)close(target_parent_descriptor);
        (void)close(staged_parent_descriptor);
        errno = EXDEV;
        return -1;
    }
    if (fstatat(staged_parent_descriptor, staged_leaf, &staged_path_status,
        AT_SYMLINK_NOFOLLOW) < 0) {
        int saved_errno = errno;

        (void)close(target_parent_descriptor);
        (void)close(staged_parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(staged_path_status.st_mode) ||
        !same_inode(&staged_path_status, &expected_staged_status)) {
        (void)close(target_parent_descriptor);
        (void)close(staged_parent_descriptor);
        errno = ESTALE;
        return -1;
    }
    target_result = fstatat(target_parent_descriptor, target_leaf,
        &target_path_status, AT_SYMLINK_NOFOLLOW);
    if (target_descriptor < 0) {
        if (target_result == 0) {
            (void)close(target_parent_descriptor);
            (void)close(staged_parent_descriptor);
            errno = EEXIST;
            return -1;
        }
        if (errno != ENOENT) {
            int saved_errno = errno;

            (void)close(target_parent_descriptor);
            (void)close(staged_parent_descriptor);
            errno = saved_errno;
            return -1;
        }
    } else {
        struct stat expected_target_status;

        if (target_result < 0) {
            int saved_errno = errno;

            (void)close(target_parent_descriptor);
            (void)close(staged_parent_descriptor);
            errno = saved_errno;
            return -1;
        }
        if (fstat(target_descriptor, &expected_target_status) < 0) {
            int saved_errno = errno;

            (void)close(target_parent_descriptor);
            (void)close(staged_parent_descriptor);
            errno = saved_errno;
            return -1;
        }
        if (!S_ISREG(expected_target_status.st_mode) ||
            !same_inode(&target_path_status, &expected_target_status)) {
            (void)close(target_parent_descriptor);
            (void)close(staged_parent_descriptor);
            errno = ESTALE;
            return -1;
        }
    }
    result = renameat(staged_parent_descriptor, staged_leaf,
        target_parent_descriptor, target_leaf);
    if (result < 0) {
        int saved_errno = errno;

        (void)close(target_parent_descriptor);
        (void)close(staged_parent_descriptor);
        errno = saved_errno;
        return -1;
    }
    {
        int close_error = 0;

        if (close(target_parent_descriptor) < 0)
            close_error = errno;
        if (close(staged_parent_descriptor) < 0 && close_error == 0)
            close_error = errno;
        if (close_error != 0) {
            errno = close_error;
            return -1;
        }
    }
    return 0;
}

int
cpio_test_open(const char *path, int flags, ...)
{
    mode_t mode = 0;

    if ((flags & O_CREAT) != 0) {
        va_list arguments;

        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        if (getenv("CPIO_TEST_LEGACY_RELOCATE_BEFORE_CREATE") != NULL) {
            const char *destination = getenv("CPIO_TEST_LEGACY_DESTINATION");
            const char *source = getenv("CPIO_TEST_LEGACY_ANCESTOR");

            if (source == NULL || destination == NULL ||
                rename(source, destination) < 0)
                return -1;
            record_call("legacy_relocate_before_create");
        }
        return open(path, flags, mode);
    }
    return open(path, flags);
}

int
cpio_test_openat_beneath(int root_descriptor, const char *path, int flags,
    ...)
{
    mode_t mode = 0;
    int descriptor;

    record_call("openat_beneath");
    if ((flags & O_CREAT) != 0) {
        va_list arguments;

        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        if (getenv("CPIO_TEST_STAGE_SYMLINK_ELOOP") != NULL) {
            struct stat path_status;

            if (fstatat(root_descriptor, path, &path_status,
                AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(path_status.st_mode)) {
                record_call("stage_symlink_eloop");
                errno = ELOOP;
                return -1;
            }
        }
        if (!create_relocation_complete &&
            getenv("CPIO_TEST_RELOCATE_BEFORE_CREATE") != NULL) {
            create_relocation_complete = 1;
            if (relocate_ancestor(root_descriptor) < 0)
                return -1;
        }
        descriptor = openat_beneath(root_descriptor, path, flags, mode);
        if (descriptor >= 0) {
            size_t path_length = strlen(path);

            if (path_length >= sizeof(staged_path)) {
                (void)close(descriptor);
                errno = ENAMETOOLONG;
                return -1;
            }
            staged_root_descriptor = root_descriptor;
            staged_descriptor = descriptor;
            memcpy(staged_path, path, path_length + 1);
            if (write_stage_path_record(path) < 0) {
                int saved_errno = errno;

                (void)close(descriptor);
                errno = saved_errno;
                return -1;
            }
        }
        return descriptor;
    }
    if (!directory_verify_replacement_complete &&
        (flags & O_DIRECTORY) != 0 &&
        getenv("CPIO_TEST_REPLACE_DIRECTORY_VERIFY") != NULL) {
        const char *replacement =
            getenv("CPIO_TEST_DIRECTORY_REPLACEMENT");
        const char *saved = getenv("CPIO_TEST_DIRECTORY_SAVED_PATH");

        directory_verify_replacement_complete = 1;
        if (replacement == NULL || saved == NULL ||
            renameat(root_descriptor, path, root_descriptor, saved) < 0 ||
            renameat(root_descriptor, replacement, root_descriptor, path) < 0)
            return -1;
        record_call("replace_directory_verify");
    }
    return openat_beneath(root_descriptor, path, flags);
}

int
cpio_test_mkdirat_beneath(int root_descriptor, const char *path, mode_t mode)
{
    record_call("mkdirat_beneath");
    if (!directory_relocation_complete &&
        getenv("CPIO_TEST_RELOCATE_BEFORE_DIRECTORY") != NULL) {
        directory_relocation_complete = 1;
        if (relocate_ancestor(root_descriptor) < 0)
            return -1;
    }
    return mkdirat_beneath(root_descriptor, path, mode);
}

int
cpio_test_publishat_beneath(int root_descriptor,
    const char *staged_name, int expected_staged_descriptor,
    const char *target_path, int target_descriptor)
{
    const char *replacement_path;

    record_call("publishat_beneath");
    if (!publication_relocation_complete &&
        getenv("CPIO_TEST_RELOCATE_BEFORE_PUBLICATION") != NULL) {
        publication_relocation_complete = 1;
        if (relocate_ancestor(root_descriptor) < 0)
            return -1;
    }
    replacement_path = getenv("CPIO_TEST_REPLACE_TARGET_PUBLICATION");
    if (!publication_target_replacement_complete &&
        replacement_path != NULL) {
        publication_target_replacement_complete = 1;
        if (renameat(root_descriptor, replacement_path,
            root_descriptor, target_path) < 0)
            return -1;
        record_call("replace_target_publication");
    }
    if (!publication_stage_replacement_complete &&
        getenv("CPIO_TEST_REPLACE_STAGE_PUBLICATION") != NULL) {
        publication_stage_replacement_complete = 1;
        if (replace_staged_entry() < 0)
            return -1;
    }
    return publishat_beneath(root_descriptor, staged_name,
        expected_staged_descriptor, target_path, target_descriptor);
}

int
cpio_test_unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor)
{
    record_call("unlinkat_beneath");
    return unlinkat_beneath(root_descriptor, path, expected_descriptor);
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
    if (descriptor <= STDERR_FILENO || write_failure_complete ||
        getenv("CPIO_TEST_FAIL_WRITE") == NULL)
        return write(descriptor, buffer, byte_count);
    if (!staged_cleanup_replacement_complete &&
        getenv("CPIO_TEST_REPLACE_STAGE_CLEANUP") != NULL) {
        staged_cleanup_replacement_complete = 1;
        if (replace_staged_entry() < 0)
            return -1;
    }
    write_failure_complete = 1;
    errno = ENOSPC;
    return -1;
}
