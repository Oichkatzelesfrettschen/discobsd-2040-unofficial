/*
 * What the NetBSD v7fs sources under sys/fs/v7fs and sbin/newfs_v7fs expect
 * from <sys/cdefs.h>, <sys/endian.h> and <sys/bswap.h>, for hosts that lack
 * it: DiscoBSD's libc headers, glibc and macOS each provide a different
 * part. Every build of those sources passes this header with -include; it
 * includes the host's <sys/cdefs.h> first, so each definition below yields
 * to one the host already makes. The byte-order constants come from the
 * compiler's own predefines, whose values match BSD's (1234, 4321, 3412).
 */
#ifndef V7FS_NETBSD_COMPAT_H
#define V7FS_NETBSD_COMPAT_H

#include <sys/cdefs.h>
/*
 * A host with <sys/endian.h> (DiscoBSD, the BSDs) defines the byte-order
 * names below with its own tokens, so it is included ahead of the guarded
 * fallbacks; glibc and macOS lack the header and take the fallbacks.
 */
#if defined(__has_include)
#if __has_include(<sys/endian.h>)
#include <sys/endian.h>
#endif
#endif

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
#ifndef __BEGIN_DECLS
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
#ifndef PDP_ENDIAN
#define PDP_ENDIAN __ORDER_PDP_ENDIAN__
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER __BYTE_ORDER__
#endif
#ifndef _BYTE_ORDER
#define _BYTE_ORDER BYTE_ORDER
#endif

/* NetBSD <sys/bswap.h>, used by v7fs_endian.c for the big-endian order. */
#ifndef bswap16
#define bswap16(x) __builtin_bswap16(x)
#endif
#ifndef bswap32
#define bswap32(x) __builtin_bswap32(x)
#endif

#endif /* !V7FS_NETBSD_COMPAT_H */
