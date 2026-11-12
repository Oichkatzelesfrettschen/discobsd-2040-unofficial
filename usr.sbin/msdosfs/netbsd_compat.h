/*
 * What the NetBSD msdosfs sources under sys/fs/msdosfs, the makefs forks
 * under usr.sbin/makefs/msdos and sbin/newfs_msdos/mkfs_msdos.c expect from
 * NetBSD's <sys/cdefs.h> and <sys/types.h>, for hosts that lack it:
 * DiscoBSD's libc headers, glibc and macOS each provide a different part.
 * Every build of those sources passes this header with -include; it
 * includes the host's <sys/cdefs.h> and <sys/types.h> first, so each
 * definition below yields to one the host already makes.
 */
#ifndef MSDOSFS_NETBSD_COMPAT_H
#define MSDOSFS_NETBSD_COMPAT_H

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <stdint.h>
/*
 * <sys/endian.h> is the host's where it has one. DiscoBSD's supplies the
 * byte-order conversions and no unaligned decoders, and it precedes the
 * compat directory on the include path, so the compat header is named
 * here by path to add what the host's lacks.
 */
#include <sys/endian.h>
#include "compat/sys/endian.h"

#ifndef __RCSID
#define __RCSID(x)
#endif
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, x)
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(x)
#endif
#ifndef __USE
#define __USE(x) ((void)(x))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif
#ifndef __BEGIN_DECLS
#define __BEGIN_DECLS
#define __END_DECLS
#endif

/*
 * NetBSD's BSD integer names. DiscoBSD and glibc define u_int and u_long;
 * the sized names come from <stdint.h> on every host.
 */
#define u_int8_t uint8_t
#define u_int16_t uint16_t
#define u_int32_t uint32_t
#define u_int64_t uint64_t

/* DiscoBSD's <stdint.h> stops at the exact-width types. */
#ifndef INTMAX_MAX
typedef long long intmax_t;
typedef unsigned long long uintmax_t;
#define INTMAX_MAX __LONG_LONG_MAX__
#endif

/* NetBSD <sys/param.h>. */
#ifndef powerof2
#define powerof2(x) ((((x) - 1) & (x)) == 0)
#endif

#endif /* !MSDOSFS_NETBSD_COMPAT_H */
