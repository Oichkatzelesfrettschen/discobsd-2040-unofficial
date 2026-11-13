#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ndbm.h>

static char name[4096];
static size_t name_length;
static int flags, failure, allocations, frees, opens, closes, stats;
static mode_t mode, observed_mode;
static int observed_flags;
static unsigned live_descriptors, faults, cases;
static off_t extent;

void *dbm_test_malloc(size_t);
void dbm_test_free(void *);
int dbm_test_open(const char *, int, ...);
int dbm_test_close(int);
int dbm_test_fstat(int, struct stat *);

static mode_t
read_mode(va_list *arguments)
{
    /* Unary plus selects the type after the default integer promotions. */
    return (mode_t)_Generic(+(mode_t)0,
        int: va_arg(*arguments, int),
        unsigned int: va_arg(*arguments, unsigned int),
        long: va_arg(*arguments, long),
        unsigned long: va_arg(*arguments, unsigned long),
        long long: va_arg(*arguments, long long),
        unsigned long long: va_arg(*arguments, unsigned long long));
}

void *
dbm_test_malloc(size_t size)
{
    allocations++;
    if (size != sizeof(DBM))
        faults++;
    if (failure == 1) {
        errno = EACCES;
        return NULL;
    }
    return malloc(size);
}

void
dbm_test_free(void *pointer)
{
    frees++;
    free(pointer);
    errno = ENOSPC;
}

int
dbm_test_open(const char *path, int actual_flags, ...)
{
    const char *suffix = opens == 0 ? ".pag" : ".dir";
    int expected_flags = flags, descriptor = opens;
    mode_t actual_mode;
    va_list arguments;

    va_start(arguments, actual_flags);
    actual_mode = read_mode(&arguments);
    va_end(arguments);
    observed_mode = actual_mode;
    observed_flags = actual_flags;
    if ((expected_flags & O_ACCMODE) == O_WRONLY)
        expected_flags = (expected_flags & ~O_ACCMODE) | O_RDWR;
    if (opens > 1 || strlen(path) != name_length + 4 ||
        memcmp(path, name, name_length) || strcmp(path + name_length, suffix) ||
        actual_flags != expected_flags || actual_mode != mode)
        faults++;
    opens++;
    if (failure == opens + 1) {
        errno = opens == 1 ? EIO : EACCES;
        return -1;
    }
    live_descriptors |= 1U << descriptor;
    return descriptor;
}

int
dbm_test_close(int descriptor)
{
    closes++;
    if (descriptor < 0 || descriptor > 1 ||
        !(live_descriptors & (1U << descriptor)))
        faults++;
    else
        live_descriptors &= ~(1U << descriptor);
    errno = EBADF;
    return -1;
}

int
dbm_test_fstat(int descriptor, struct stat *status)
{
    stats++;
    if (descriptor != 1 || live_descriptors != 3)
        faults++;
    memset(status, 0, sizeof(*status));
    status->st_size = extent;
    if (failure == 4) {
        errno = EINTR;
        return -1;
    }
    return 0;
}

static int
exercise(size_t length, int stage, off_t bytes, long expected_max, int access)
{
    DBM *db;
    int expected_errno = 0, expected_opens = 2, expected_closes = 2;
    int expected_allocations = 1, expected_frees = 1, expected_stats = 1;
    const char *label = "success";
    long observed_max = 0, observed_page = 0, observed_directory = 0;
    int observed_readonly = 0, open_errno;

    name_length = length;
    memset(name, 'a', length);
    name[length] = 0;
    flags = access | O_CREAT | O_EXCL;
    mode = 0640;
    extent = bytes;
    failure = stage;
    allocations = frees = opens = closes = stats = 0;
    live_descriptors = faults = 0;
    observed_mode = 0;
    observed_flags = 0;
    cases++;
    if (length > 1019) {
        label = "long-name";
        expected_errno = ENAMETOOLONG;
        expected_opens = expected_closes = expected_allocations = expected_frees = expected_stats = 0;
    } else if (stage == 1) {
        label = "allocation";
        expected_errno = ENOMEM;
        expected_opens = expected_closes = expected_frees = expected_stats = 0;
    } else if (stage == 2) {
        label = "page-open";
        expected_errno = EIO;
        expected_opens = 1;
        expected_closes = expected_stats = 0;
    } else if (stage == 3) {
        label = "directory-open";
        expected_errno = EACCES;
        expected_closes = 1;
        expected_stats = 0;
    } else if (stage == 4) {
        label = "directory-stat";
        expected_errno = EINTR;
    } else if (bytes < 0 || bytes > LONG_MAX / 8 + 1) {
        label = "directory-extent";
        expected_errno = EFBIG;
    }
    errno = 0;
    db = dbm_open(name, flags, mode);
    open_errno = errno;
    if (expected_errno) {
        if (db || errno != expected_errno)
            faults++;
    } else if (!db) {
        faults++;
    } else {
        observed_max = db->dbm_maxbno;
        observed_page = db->dbm_pagbno;
        observed_directory = db->dbm_dirbno;
        observed_readonly = !!dbm_rdonly(db);
        if (db->dbm_maxbno != expected_max || db->dbm_pagbno != -1 ||
            db->dbm_dirbno != -1 || live_descriptors != 3 ||
            !!dbm_rdonly(db) != (access == O_RDONLY))
            faults++;
        dbm_close(db);
    }
    if (faults || live_descriptors || allocations != expected_allocations ||
        frees != expected_frees || opens != expected_opens || closes != expected_closes ||
        stats != expected_stats) {
        printf("FAIL dbm-open %s length=%lu stage=%d bytes=%ld\n", label,
               (unsigned long)length, stage, (long)bytes);
        printf("observed faults=%u live=%u allocations=%d/%d frees=%d/%d "
               "opens=%d/%d closes=%d/%d stats=%d/%d errno=%d/%d\n",
               faults, live_descriptors, allocations, expected_allocations,
               frees, expected_frees, opens, expected_opens, closes, expected_closes,
               stats, expected_stats, open_errno, expected_errno);
        printf("observed max=%ld/%ld page=%ld directory=%ld readonly=%d/%d "
               "flags=%d mode=%lu/%lu\n", observed_max, expected_max,
               observed_page, observed_directory, observed_readonly, access == O_RDONLY,
               observed_flags, (unsigned long)observed_mode, (unsigned long)mode);
        return 1;
    }
    return 0;
}

int
main(void)
{
    size_t length;
    int access, stage;
    static const struct { off_t bytes; long maximum; } extents[] = {
        {0, -1}, {1, 7}, {127, 1015}, {LONG_MAX / 8, LONG_MAX - 8},
        {LONG_MAX / 8 + 1, LONG_MAX}, {LONG_MAX / 8 + 2, 0}, {-1, 0}
    };
    unsigned i;

    for (length = 0; length <= 1024; length++)
        for (access = O_RDONLY; access <= O_RDWR; access++)
            if (exercise(length, 0, 1, 7, access))
                return 1;
    for (stage = 0; stage <= 4; stage++)
        for (i = 0; i < sizeof(extents) / sizeof(extents[0]); i++)
            if (exercise(1019, stage, extents[i].bytes, extents[i].maximum, O_WRONLY) ||
                exercise(1020, stage, extents[i].bytes, extents[i].maximum, O_RDONLY) ||
                exercise(4095, stage, extents[i].bytes, extents[i].maximum, O_RDWR))
                return 1;
    printf("dbm-open: %u cases pass\n", cases);
    return 0;
}
