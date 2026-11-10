/*
 * Host-only declarations for building usr.bin/dc and usr.bin/bc unmodified.
 * The tree's <unistd.h> declares __progname; glibc exports the symbol
 * without declaring it. The tree's <sys/cdefs.h> defines __dead, the
 * OpenBSD spelling dc and bc decorate usage() with; glibc's does not.
 * Passed with -include.
 */

#ifndef DCBC_HOST_COMPAT_H
#define DCBC_HOST_COMPAT_H

extern const char *__progname;

#ifndef __dead
#define __dead _Noreturn
#endif

#endif /* DCBC_HOST_COMPAT_H */
