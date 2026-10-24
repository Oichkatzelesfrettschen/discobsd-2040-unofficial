/* Host witness for the target's descriptor-conditioned namespace calls. */
#include <sys/stat.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int flush_failure_active;
static int staged_root_descriptor = -1;
static char staged_path[256];

static int
valid_leaf(const char *path)
{
    return path != NULL && path[0] != '\0' && strcmp(path, ".") != 0 &&
        strcmp(path, "..") != 0 && strchr(path, '/') == NULL;
}

static int
same_inode(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int
environment_selects(const char *name, const char *value)
{
    const char *selected = getenv(name);

    return selected != NULL && strcmp(selected, value) == 0;
}

static void
replace_after_open(int root_descriptor, const char *path)
{
    char replacement[256];
    char opened[256];
    size_t path_length = strlen(path);

    if (path_length > sizeof(replacement) - sizeof(".replacement") ||
        path_length > sizeof(opened) - sizeof(".opened"))
        _exit(120);
    memcpy(replacement, path, path_length);
    memcpy(replacement + path_length, ".replacement",
        sizeof(".replacement"));
    memcpy(opened, path, path_length);
    memcpy(opened + path_length, ".opened", sizeof(".opened"));
    if (renameat(root_descriptor, path, root_descriptor, opened) < 0 ||
        renameat(root_descriptor, replacement, root_descriptor, path) < 0)
        _exit(121);
}

static void
replace_staged_entry(void)
{
    static const char replacement_bytes[] = "foreign staged replacement\n";
    static int replaced;
    char opened[sizeof(staged_path)];
    size_t path_length;
    int descriptor;

    if (replaced)
        return;
    replaced = 1;
    path_length = strlen(staged_path);
    if (staged_root_descriptor < 0 || path_length == 0 ||
        path_length > sizeof(opened) - sizeof(".opened"))
        _exit(122);
    memcpy(opened, staged_path, path_length);
    memcpy(opened + path_length, ".opened", sizeof(".opened"));
    if (renameat(staged_root_descriptor, staged_path,
        staged_root_descriptor, opened) < 0)
        _exit(123);
    descriptor = openat(staged_root_descriptor, staged_path,
        O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0 ||
        write(descriptor, replacement_bytes,
        sizeof(replacement_bytes) - 1) !=
        (ssize_t)(sizeof(replacement_bytes) - 1) || close(descriptor) < 0)
        _exit(124);
}

static void
record_call(const char *name)
{
    const char *trace_path = getenv("COMPRESS_TEST_TRACE");
    FILE *trace_stream;

    if (trace_path == NULL)
        return;
    trace_stream = fopen(trace_path, "a");
    if (trace_stream == NULL)
        return;
    (void)fprintf(trace_stream, "%s\n", name);
    (void)fclose(trace_stream);
}

int
unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor)
{
    struct stat expected_status;
    struct stat path_status;

    record_call("unlinkat_beneath");
    if (!valid_leaf(path)) {
        errno = EINVAL;
        return -1;
    }
    if (fstat(expected_descriptor, &expected_status) < 0 ||
        fstatat(root_descriptor, path, &path_status, AT_SYMLINK_NOFOLLOW) < 0)
        return -1;
    if (!S_ISREG(expected_status.st_mode) ||
        !same_inode(&expected_status, &path_status)) {
        errno = ESTALE;
        return -1;
    }
    return unlinkat(root_descriptor, path, 0);
}

int
publishat_beneath(int root_descriptor, const char *staged_path,
    int staged_descriptor, const char *target_path, int target_descriptor)
{
    struct stat staged_status;
    struct stat staged_path_status;
    struct stat expected_target_status;
    struct stat target_path_status;
    int target_result;

    record_call("publishat_beneath");
    if (!valid_leaf(staged_path) || !valid_leaf(target_path)) {
        errno = EINVAL;
        return -1;
    }
    if (fstat(staged_descriptor, &staged_status) < 0 ||
        fstatat(root_descriptor, staged_path, &staged_path_status,
        AT_SYMLINK_NOFOLLOW) < 0)
        return -1;
    if (!S_ISREG(staged_status.st_mode) ||
        !same_inode(&staged_status, &staged_path_status)) {
        errno = ESTALE;
        return -1;
    }
    target_result = fstatat(root_descriptor, target_path,
        &target_path_status, AT_SYMLINK_NOFOLLOW);
    if (target_descriptor < 0) {
        if (target_result == 0) {
            errno = EEXIST;
            return -1;
        }
        if (errno != ENOENT)
            return -1;
    } else {
        if (target_result < 0 ||
            fstat(target_descriptor, &expected_target_status) < 0)
            return -1;
        if (!S_ISREG(expected_target_status.st_mode) ||
            !same_inode(&expected_target_status, &target_path_status)) {
            errno = ESTALE;
            return -1;
        }
    }
    return renameat(root_descriptor, staged_path, root_descriptor,
        target_path);
}

int
compress_test_openat(int root_descriptor, const char *path, int flags, ...)
{
    static int input_replaced;
    static int stage_open_signaled;
    static int target_replaced;
    mode_t mode = 0;
    int descriptor;

    if (flags & O_CREAT) {
        va_list arguments;

        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        descriptor = openat(root_descriptor, path, flags, mode);
    } else {
        descriptor = openat(root_descriptor, path, flags);
    }
    if (descriptor < 0)
        return descriptor;
    if ((flags & O_CREAT) != 0) {
        size_t path_length = strlen(path);

        if (path_length >= sizeof(staged_path))
            _exit(125);
        staged_root_descriptor = root_descriptor;
        memcpy(staged_path, path, path_length + 1);
    }
    if (!stage_open_signaled && (flags & O_CREAT) != 0 &&
        getenv("COMPRESS_TEST_SIGNAL_STAGE_OPEN") != NULL) {
        stage_open_signaled = 1;
        (void)raise(SIGINT);
    }
    if (!input_replaced &&
        environment_selects("COMPRESS_TEST_REPLACE_INPUT", path)) {
        input_replaced = 1;
        record_call("replace_input");
        replace_after_open(root_descriptor, path);
    } else if (!target_replaced &&
        environment_selects("COMPRESS_TEST_REPLACE_TARGET", path)) {
        target_replaced = 1;
        replace_after_open(root_descriptor, path);
    }
    return descriptor;
}

int
compress_test_sigprocmask(int how, const sigset_t *set, sigset_t *old_set)
{
    static int failure_injected;

    if (!failure_injected &&
        ((how == SIG_BLOCK &&
        getenv("COMPRESS_TEST_FAIL_MASK_BLOCK") != NULL) ||
        (how == SIG_SETMASK &&
        getenv("COMPRESS_TEST_FAIL_MASK_RESTORE") != NULL))) {
        failure_injected = 1;
        errno = EIO;
        return -1;
    }
    return sigprocmask(how, set, old_set);
}

int
compress_test_publishat_beneath(int root_descriptor,
    const char *staged_path, int staged_descriptor,
    const char *target_path, int target_descriptor)
{
    if (getenv("COMPRESS_TEST_REPLACE_STAGE_PUBLICATION") != NULL)
        replace_staged_entry();
    if (environment_selects("COMPRESS_TEST_REPLACE_TARGET_PUBLICATION",
        target_path))
        replace_after_open(root_descriptor, target_path);
    if (getenv("COMPRESS_TEST_FAIL_PUBLICATION") != NULL) {
        errno = EIO;
        return -1;
    }
    return publishat_beneath(root_descriptor, staged_path,
        staged_descriptor, target_path, target_descriptor);
}

int
compress_test_unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor)
{
    if (strncmp(path, ".cmp", 4) == 0 &&
        getenv("COMPRESS_TEST_REPLACE_STAGE_CLEANUP") != NULL)
        replace_staged_entry();
    if (strncmp(path, ".cmp", 4) != 0 &&
        environment_selects("COMPRESS_TEST_REPLACE_INPUT_REMOVAL", path))
        replace_after_open(root_descriptor, path);
    if (strncmp(path, ".cmp", 4) == 0 &&
        getenv("COMPRESS_TEST_FAIL_CLEANUP") != NULL) {
        errno = EIO;
        return -1;
    }
    if (strncmp(path, ".cmp", 4) != 0 &&
        getenv("COMPRESS_TEST_FAIL_REMOVAL") != NULL) {
        errno = EIO;
        return -1;
    }
    return unlinkat_beneath(root_descriptor, path, expected_descriptor);
}

int
compress_test_fchown(int descriptor, uid_t owner, gid_t group)
{
    if (getenv("COMPRESS_TEST_REPLACE_STAGE_METADATA") != NULL)
        replace_staged_entry();
    if (getenv("COMPRESS_TEST_FAIL_FCHOWN") != NULL ||
        getenv("COMPRESS_TEST_FAIL_METADATA") != NULL) {
        errno = EIO;
        return -1;
    }
    return fchown(descriptor, owner, group);
}

int
compress_test_fchmod(int descriptor, mode_t mode)
{
    if (getenv("COMPRESS_TEST_FAIL_FCHMOD") != NULL) {
        errno = EIO;
        return -1;
    }
    return fchmod(descriptor, mode);
}

int
compress_test_futimes(int descriptor, const struct timeval times[2])
{
    if (getenv("COMPRESS_TEST_FAIL_FUTIMES") != NULL) {
        errno = EIO;
        return -1;
    }
    return futimes(descriptor, times);
}

int
compress_test_fflush(FILE *stream)
{
    static int failure_injected;

    if (!failure_injected && getenv("COMPRESS_TEST_FAIL_FLUSH") != NULL) {
        failure_injected = 1;
        flush_failure_active = 1;
        errno = ENOSPC;
        return EOF;
    }
    return fflush(stream);
}

int
compress_test_ferror(FILE *stream)
{
    if (flush_failure_active)
        return 1;
    return ferror(stream);
}

int
compress_test_fclose(FILE *stream)
{
    static unsigned call_count;
    const char *failure_text = getenv("COMPRESS_TEST_FAIL_FCLOSE_CALL");
    unsigned failure_call = 0;
    int result = fclose(stream);

    call_count++;
    if (getenv("COMPRESS_TEST_FAIL_CLOSE") != NULL)
        failure_call = 1;
    else if (failure_text != NULL)
        failure_call = (unsigned)strtoul(failure_text, NULL, 10);
    if (call_count == failure_call) {
        errno = EIO;
        return EOF;
    }
    return result;
}

int
compress_test_dup(int descriptor)
{
    static unsigned call_count;
    const char *failure_text = getenv("COMPRESS_TEST_FAIL_DUP_CALL");

    call_count++;
    if (failure_text != NULL &&
        call_count == (unsigned)strtoul(failure_text, NULL, 10)) {
        errno = EMFILE;
        return -1;
    }
    return dup(descriptor);
}

void *
compress_test_malloc(size_t size)
{
    static unsigned call_count;
    const char *failure_text = getenv("COMPRESS_TEST_FAIL_MALLOC_CALL");

    call_count++;
    if (failure_text != NULL &&
        call_count == (unsigned)strtoul(failure_text, NULL, 10)) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}

size_t
compress_test_fwrite(const void *bytes, size_t size, size_t count,
    FILE *stream)
{
    static int failure_injected;

    if (!failure_injected && getenv("COMPRESS_TEST_FAIL_WRITE") != NULL) {
        failure_injected = 1;
        errno = ENOSPC;
        return 0;
    }
    if (!failure_injected && getenv("COMPRESS_TEST_SIGNAL_WRITE") != NULL) {
        failure_injected = 1;
        (void)raise(SIGINT);
        errno = EINTR;
        return 0;
    }
    return fwrite(bytes, size, count, stream);
}
