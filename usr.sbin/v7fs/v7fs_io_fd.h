/*
 * Limits shared by the descriptor backend (v7fs_io_fd.c) and the v7fs
 * front end (v7fs.c).
 */
#ifndef V7FS_IO_FD_H
#define V7FS_IO_FD_H

#include <sys/types.h>

#include <limits.h>
#include <stdint.h>

/* The largest off_t, formed without overflowing the signed type. */
#define	V7FS_FD_OFF_MAX							\
	((((uint64_t)1 << (sizeof(off_t) * CHAR_BIT - 2)) - 1) * 2 + 1)

/*
 * The most 512-byte sectors whose byte offsets fit off_t: 4,194,303 with
 * DiscoBSD's 32-bit off_t, which bounds an image or device below v7fs's own
 * 24-bit block address limit.
 */
#define	V7FS_FD_MAX_SECTORS	(V7FS_FD_OFF_MAX / 512)

#endif /* !V7FS_IO_FD_H */
