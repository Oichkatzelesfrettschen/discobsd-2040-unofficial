/*
 * A fixed pool of block buffers over a file descriptor: the bread, bwrite,
 * getblk and brelse that sys/fs/msdosfs and the makefs forks call, with the
 * contract compat/ffs/buf.h states. The pool holds MSDOSFS_NBUF buffers of
 * MSDOSFS_BUFSIZE bytes in .bss, so the program's buffer memory is fixed at
 * link time; a request larger than one buffer fails with EFBIG, and a
 * request when every buffer is busy stops the program with status 1.
 * The first failed write stays recorded for buf_write_error, because
 * msdosfs_fat.c's updatefats discards the status of the FAT writes.
 */

#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "makefs.h"
#include "ffs/buf.h"

static struct buf pool[MSDOSFS_NBUF];
static unsigned char storage[MSDOSFS_NBUF][MSDOSFS_BUFSIZE];
static int busy, high_water, write_error;

int
buf_high_water(void)
{
	return high_water;
}

int
buf_write_error(void)
{
	return write_error;
}

/* Fail the program: every caller treats getblk's result as valid. */
static void
stop(const char *what, daddr_t blkno)
{
	fprintf(stderr, "msdosfs: buffer %s at block %ld\n", what, (long)blkno);
	exit(1);
}

/*
 * The byte offset of BLKNO in FS, or -1 when it lies before the file
 * system or past the largest offset off_t holds.
 */
static off_t
block_offset(const fsinfo_t *fs, daddr_t blkno, long bytes)
{
	/* off_t is signed and two's complement on every supported host. */
	const unsigned long long off_max =
	    ((unsigned long long)1 << (sizeof(off_t) * CHAR_BIT - 1)) - 1;
	unsigned long long start;

	if (blkno < 0 || fs->offset < 0)
		return -1;
	if ((unsigned long long)blkno > (off_max - (unsigned long long)fs->offset -
	    (unsigned long long)bytes) / (unsigned long long)fs->sectorsize)
		return -1;
	start = (unsigned long long)blkno * (unsigned long long)fs->sectorsize +
	    (unsigned long long)fs->offset;
	return (off_t)start;
}

struct buf *
getblk(struct vnode *vp, daddr_t blkno, int size, int slpflag, int slptimeo)
{
	struct buf *bp = NULL;
	int i;

	(void)slpflag;
	(void)slptimeo;
	if (size <= 0 || size > MSDOSFS_BUFSIZE)
		stop("size out of range", blkno);
	for (i = 0; i < MSDOSFS_NBUF; i++) {
		if (pool[i].b_busy && pool[i].b_blkno == blkno)
			stop("requested twice", blkno);
		if (!pool[i].b_busy && bp == NULL)
			bp = &pool[i];
	}
	if (bp == NULL)
		stop("pool exhausted", blkno);
	bp->b_data = storage[bp - pool];
	bp->b_bufsize = MSDOSFS_BUFSIZE;
	bp->b_bcount = size;
	bp->b_blkno = bp->b_lblkno = blkno;
	bp->b_fs = vp->fs;
	bp->b_busy = 1;
	memset(bp->b_data, 0, (size_t)size);
	if (++busy > high_water)
		high_water = busy;
	return bp;
}

void
brelse(struct buf *bp, int flags)
{
	(void)flags;
	if (bp == NULL || !bp->b_busy)
		stop("released twice", bp != NULL ? bp->b_blkno : -1);
	bp->b_busy = 0;
	busy--;
}

int
bread(struct vnode *vp, daddr_t blkno, int size, int flags, struct buf **bpp)
{
	struct buf *bp;
	off_t offset;
	ssize_t n;
	int error;

	(void)flags;
	*bpp = NULL;
	if (size <= 0 || size > MSDOSFS_BUFSIZE)
		return EFBIG;
	if ((offset = block_offset(vp->fs, blkno, size)) < 0)
		return EINVAL;
	bp = getblk(vp, blkno, size, 0, 0);
	if (lseek(vp->fs->fd, offset, SEEK_SET) == -1) {
		error = errno;
		brelse(bp, 0);
		return error;
	}
	n = read(vp->fs->fd, bp->b_data, (size_t)size);
	if (n != size) {
		error = n < 0 ? errno : EIO;
		brelse(bp, 0);
		return error;
	}
	*bpp = bp;
	return 0;
}

/* The kernel's bwrite releases the buffer whatever the outcome. */
int
bwrite(struct buf *bp)
{
	off_t offset;
	ssize_t n;
	int error = 0;

	offset = block_offset(bp->b_fs, bp->b_blkno, bp->b_bcount);
	if (offset < 0)
		error = EINVAL;
	else if (lseek(bp->b_fs->fd, offset, SEEK_SET) == -1)
		error = errno;
	else if ((n = write(bp->b_fs->fd, bp->b_data, (size_t)bp->b_bcount)) !=
	    bp->b_bcount)
		error = n < 0 ? errno : EIO;
	brelse(bp, 0);
	if (error != 0 && write_error == 0)
		write_error = error;
	return error;
}
