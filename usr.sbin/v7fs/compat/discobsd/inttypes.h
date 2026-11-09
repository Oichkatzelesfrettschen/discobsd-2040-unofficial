/*
 * The NetBSD v7fs sources include <inttypes.h> for the exact-width integer
 * types alone; they use no PRI or SCN macro and no strto*max function.
 * DiscoBSD's libc ships <stdint.h> and no <inttypes.h>, so the v7fs
 * Makefile puts this directory on the include path for the DiscoBSD build of
 * those files only; host builds use the host's header.
 */
#ifndef V7FS_DISCOBSD_INTTYPES_H
#define V7FS_DISCOBSD_INTTYPES_H

#include <stdint.h>

#endif /* !V7FS_DISCOBSD_INTTYPES_H */
