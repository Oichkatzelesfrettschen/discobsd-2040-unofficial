/*
 * The buffer interface sys/fs/msdosfs and the makefs forks compile against
 * outside the kernel (their #include <ffs/buf.h>), with the kernel-only
 * names they use. msdosfs_buf.c implements bread, bwrite, getblk and brelse
 * over a fixed pool of MSDOSFS_NBUF buffers of MSDOSFS_BUFSIZE bytes and a
 * file descriptor, in place of NetBSD's heap-allocated usr.sbin/makefs/ffs
 * shim.
 *
 * Block numbers count DEV_BSIZE (512-byte) units from fsinfo_t's offset,
 * as NetBSD's de_bn2kb() produces them. A buffer stays busy from getblk or
 * bread until brelse or bwrite; a second getblk of a busy block, which in
 * the kernel would wait on the first holder forever, stops the program.
 */
#ifndef MSDOSFS_COMPAT_FFS_BUF_H
#define MSDOSFS_COMPAT_FFS_BUF_H

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "makefs.h"

#ifndef MSDOSFS_NBUF
#define MSDOSFS_NBUF 4
#endif
#ifndef MSDOSFS_BUFSIZE
#define MSDOSFS_BUFSIZE 4096
#endif

/*
 * NetBSD counts device blocks in 512-byte units; DiscoBSD's <sys/param.h>
 * makes DEV_BSHIFT 10, which would double every de_bn2kb() result.
 */
#undef DEV_BSHIFT
#define DEV_BSHIFT 9
#undef DEV_BSIZE
#define DEV_BSIZE (1 << DEV_BSHIFT)

/*
 * The FAT block size for FAT16 and FAT32 (msdosfs_mount's
 * pm_fatblocksize), fixed so every host reads the FAT in the same chunks.
 */
#undef MAXBSIZE
#define MAXBSIZE 1024

struct componentname {
	const char *cn_nameptr;
	size_t cn_namelen;
};

struct vnode {
	struct makefs_fsinfo *fs;
	void *v_data;
};

#define vput(a) ((void)(a))

struct buf {
	void *b_data;
	long b_bufsize;
	long b_bcount;
	daddr_t b_blkno;
	daddr_t b_lblkno;
	struct makefs_fsinfo *b_fs;
	int b_busy;
};

struct kauth_cred;
int bread(struct vnode *, daddr_t, int, int, struct buf **);
void brelse(struct buf *, int);
int bwrite(struct buf *);
struct buf *getblk(struct vnode *, daddr_t, int, int, int);
/* The most buffers held at once since the program started. */
int buf_high_water(void);
/*
 * The first error any bwrite returned, or 0. msdosfs_fat.c's updatefats
 * returns void and drops the status of its FAT writes, so the caller reads
 * this before it reports success.
 */
int buf_write_error(void);

#define bdwrite(bp) bwrite(bp)
#define clrbuf(bp) memset((bp)->b_data, 0, (size_t)(bp)->b_bcount)

#define B_MODIFY 0
#define BC_AGE 0
#define IO_SYNC 1
#define NOCRED NULL

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#define min(a, b) MIN((a), (b))

static inline unsigned int
uimin(unsigned int a, unsigned int b)
{
	return a < b ? a : b;
}

static inline unsigned int
uimax(unsigned int a, unsigned int b)
{
	return a > b ? a : b;
}

/*
 * msdosfs_clusteralloc seeds its starting cluster from the microseconds;
 * a stampst timestamp makes the seed, and so the image, reproducible.
 */
static inline void
microtime(struct timeval *tv)
{
	if (stampst.st_ino) {
		tv->tv_sec = stampst.st_mtime;
		tv->tv_usec = 0;
	} else
		gettimeofday(tv, NULL);
}

#define KASSERT(a) ((void)0)

#endif /* !MSDOSFS_COMPAT_FFS_BUF_H */
