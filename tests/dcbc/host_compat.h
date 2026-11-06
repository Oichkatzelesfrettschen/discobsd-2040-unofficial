/*
 * Host-only declarations for building usr.bin/dc and usr.bin/bc unmodified.
 * The tree's <unistd.h> declares __progname; glibc exports the symbol
 * without declaring it. Passed with -include.
 */

#ifndef DCBC_HOST_COMPAT_H
#define DCBC_HOST_COMPAT_H

extern const char *__progname;

#endif /* DCBC_HOST_COMPAT_H */
