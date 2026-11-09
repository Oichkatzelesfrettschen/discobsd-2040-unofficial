/*
 * BSD <sys/cdefs.h> macros that the imported NetBSD v7fs sources use. glibc
 * defines none of them and macOS defines some; this header includes the
 * host's <sys/cdefs.h> first, so each definition below yields to a host
 * definition. check.sh passes it with -include, so every imported file sees
 * the same set.
 */
#ifndef V7FS_CORE_NETBSD_HOST_H
#define V7FS_CORE_NETBSD_HOST_H

#include <sys/cdefs.h>

#ifndef __RCSID
#define __RCSID(x)
#endif
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, x)
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(x)
#endif
#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __predict_false
#define __predict_false(x) __builtin_expect((x) != 0, 0)
#endif
#ifndef __predict_true
#define __predict_true(x) __builtin_expect((x) != 0, 1)
#endif

/* NetBSD <sys/bswap.h>, used by v7fs_endian.c for the big-endian order. */
#ifndef bswap16
#define bswap16(x) __builtin_bswap16(x)
#endif
#ifndef bswap32
#define bswap32(x) __builtin_bswap32(x)
#endif

#endif /* !V7FS_CORE_NETBSD_HOST_H */
