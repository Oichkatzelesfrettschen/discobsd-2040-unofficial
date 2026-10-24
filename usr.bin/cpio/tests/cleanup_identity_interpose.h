#ifndef CPIO_CLEANUP_IDENTITY_INTERPOSE_H
#define CPIO_CLEANUP_IDENTITY_INTERPOSE_H

/*
 * Include host declarations before applying the test renames.  Fortified
 * libc headers can otherwise bind an inline wrapper to the real symbol and
 * bypass a scheduled namespace transition.
 */
#include <sys/types.h>

#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

int openat_beneath(int, const char *, int, ...);
int mkdirat_beneath(int, const char *, mode_t);
int publishat_beneath(int, const char *, int, const char *, int);
int unlinkat_beneath(int, const char *, int);
int cpio_test_open(const char *, int, ...);
int cpio_test_openat_beneath(int, const char *, int, ...);
int cpio_test_mkdirat_beneath(int, const char *, mode_t);
int cpio_test_publishat_beneath(int, const char *, int, const char *, int);
int cpio_test_unlinkat_beneath(int, const char *, int);
int cpio_test_close(int);
ssize_t cpio_test_write(int, const void *, size_t);

#define open cpio_test_open
#define openat_beneath cpio_test_openat_beneath
#define mkdirat_beneath cpio_test_mkdirat_beneath
#define publishat_beneath cpio_test_publishat_beneath
#define unlinkat_beneath cpio_test_unlinkat_beneath
#define close cpio_test_close
#define write cpio_test_write

#endif
