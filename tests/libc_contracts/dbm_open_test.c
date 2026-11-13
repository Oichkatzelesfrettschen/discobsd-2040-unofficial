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
static mode_t mode;
static unsigned live_descriptors, faults, cases;
static off_t extent;

void *dbm_test_malloc(size_t);
void dbm_test_free(void *);
int dbm_test_open(const char *, int, ...);
int dbm_test_close(int);
int dbm_test_fstat(int, struct stat *);

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
    actual_mode = va_arg(arguments, mode_t);
    va_end(arguments);
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

    name_length = length;
    memset(name, 'a', length);
    name[length] = 0;
    flags = access | O_CREAT | O_EXCL;
    mode = 0640;
    extent = bytes;
    failure = stage;
    allocations = frees = opens = closes = stats = 0;
    live_descriptors = faults = 0;
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
    if (expected_errno) {
        if (db || errno != expected_errno)
            faults++;
    } else if (!db) {
        faults++;
    } else {
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
