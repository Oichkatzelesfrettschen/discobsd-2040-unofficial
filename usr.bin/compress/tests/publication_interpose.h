#ifndef COMPRESS_PUBLICATION_INTERPOSE_H
#define COMPRESS_PUBLICATION_INTERPOSE_H

/*
 * Include host declarations before applying the test renames.  Fortified
 * libc headers may otherwise bind inline wrappers directly to the real
 * symbol and bypass the identity-failure witness.
 */
#include <sys/stat.h>
#include <sys/time.h>

#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int compress_test_openat(int, const char *, int, ...);
int compress_test_publishat_beneath(int, const char *, int, const char *, int);
int compress_test_unlinkat_beneath(int, const char *, int);
int compress_test_fchown(int, uid_t, gid_t);
int compress_test_fchmod(int, mode_t);
int compress_test_futimes(int, const struct timeval [2]);
int compress_test_fclose(FILE *);
int compress_test_fflush(FILE *);
int compress_test_ferror(FILE *);
int compress_test_dup(int);
void *compress_test_malloc(size_t);
size_t compress_test_fwrite(const void *, size_t, size_t, FILE *);
int compress_test_sigprocmask(int, const sigset_t *, sigset_t *);

#define openat compress_test_openat
#define publishat_beneath compress_test_publishat_beneath
#define unlinkat_beneath compress_test_unlinkat_beneath
#define fchown compress_test_fchown
#define fchmod compress_test_fchmod
#define futimes compress_test_futimes
#define fclose compress_test_fclose
#define fflush compress_test_fflush
#define ferror compress_test_ferror
#define dup compress_test_dup
#define malloc compress_test_malloc
#define fwrite compress_test_fwrite
#define sigprocmask compress_test_sigprocmask

#endif
