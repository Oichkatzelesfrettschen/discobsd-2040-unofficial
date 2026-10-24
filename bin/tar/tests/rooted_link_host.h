#ifndef TAR_TESTS_ROOTED_LINK_HOST_H
#define TAR_TESTS_ROOTED_LINK_HOST_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>

#ifndef O_IDENTITY
#define O_IDENTITY 0x00080000
#endif

int openat_beneath(int rootfd, const char *path, int flags, ...);
int unlinkat_beneath(int rootfd, const char *path, int expected_fd);
int publishat_beneath(int rootfd, const char *staged_path, int staged_fd,
    const char *target_path, int target_fd);
int tar_test_path_publish(int rootfd, const char *staged_path, int staged_fd,
    const char *target_path, int target_fd);
int linkat_beneath(int rootfd, const char *source_path,
    int expected_source_fd, const char *target_path);
int tar_test_path_link(const char *source_path, const char *target_path);
ssize_t tar_test_read(int descriptor, void *buffer, size_t byte_count);
ssize_t tar_test_write(int descriptor, const void *buffer, size_t byte_count);

#define read tar_test_read
#define write tar_test_write

#endif
