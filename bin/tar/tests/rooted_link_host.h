#ifndef TAR_TESTS_ROOTED_LINK_HOST_H
#define TAR_TESTS_ROOTED_LINK_HOST_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * The test builds rename read and write to the tar_test_* shims below.
 * Under _FORTIFY_SOURCE, glibc's <unistd.h> instead supplies an always-inline
 * read() that calls libc's read through an assembler alias, so the shim
 * never runs; Ubuntu's gcc defines _FORTIFY_SOURCE by default. This header is
 * included before any system header, so it turns fortification off for
 * every translation unit that uses the shims.
 */
#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0

#include <sys/types.h>
#include <errno.h>

/*
 * The DiscoBSD kernel refuses an O_IDENTITY open of a non-regular inode with
 * EFTYPE (vn_open_at() in sys/kern/vfs_vnops.c). glibc has no EFTYPE, so the
 * fixture names a value outside the host's errno range.
 */
#ifndef EFTYPE
#define EFTYPE 4079
#endif

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
