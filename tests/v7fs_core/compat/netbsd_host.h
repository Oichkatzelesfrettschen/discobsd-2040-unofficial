/*
 * BSD <sys/cdefs.h> macros that the imported NetBSD v7fs sources use and the
 * host C library does not define. check.sh passes this header with -include
 * so every imported file sees the same definitions.
 */
#ifndef V7FS_CORE_NETBSD_HOST_H
#define V7FS_CORE_NETBSD_HOST_H

#define __RCSID(x)
#define __KERNEL_RCSID(n, x)
#define __COPYRIGHT(x)
#define __dead __attribute__((__noreturn__))
#define __unused __attribute__((__unused__))
#define __packed __attribute__((__packed__))
#define __predict_false(x) __builtin_expect((x) != 0, 0)
#define __predict_true(x) __builtin_expect((x) != 0, 1)

/* NetBSD <sys/bswap.h>, used by v7fs_endian.c for the big-endian order. */
#define bswap16(x) __builtin_bswap16(x)
#define bswap32(x) __builtin_bswap32(x)

#endif /* !V7FS_CORE_NETBSD_HOST_H */
