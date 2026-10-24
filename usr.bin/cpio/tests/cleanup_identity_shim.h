#ifndef CPIO_CLEANUP_IDENTITY_SHIM_H
#define CPIO_CLEANUP_IDENTITY_SHIM_H

#include <sys/types.h>

int openat_beneath(int root_descriptor, const char *path, int flags, ...);
int mkdirat_beneath(int root_descriptor, const char *path, mode_t mode);
int publishat_beneath(int root_descriptor, const char *staged_path,
    int staged_descriptor, const char *target_path, int target_descriptor);
int unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor);
int cpio_test_open(const char *path, int flags, ...);
int cpio_test_openat_beneath(int root_descriptor, const char *path,
    int flags, ...);
int cpio_test_mkdirat_beneath(int root_descriptor, const char *path,
    mode_t mode);
int cpio_test_publishat_beneath(int root_descriptor,
    const char *staged_path, int staged_descriptor,
    const char *target_path, int target_descriptor);
int cpio_test_unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor);
int cpio_test_close(int descriptor);
ssize_t cpio_test_write(int descriptor, const void *buffer,
    size_t byte_count);

#endif
