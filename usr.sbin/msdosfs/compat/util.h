/*
 * NetBSD <util.h>'s allocation wrappers, which the makefs forks call:
 * each one exits with status 1 and a message when the allocation fails.
 * msdosfs_env.c defines them.
 */
#ifndef MSDOSFS_COMPAT_UTIL_H
#define MSDOSFS_COMPAT_UTIL_H

#include <stddef.h>

void *ecalloc(size_t, size_t);
void *emalloc(size_t);
void *erealloc(void *, size_t);

#endif /* !MSDOSFS_COMPAT_UTIL_H */
