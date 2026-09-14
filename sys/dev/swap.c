/*
 * Simple proxy for swap partition.
 *
 * Forwards requests for /dev/swap on to the
 * device specified by swapdev.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/dk.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/map.h>
#include <sys/swap.h>
#include <sys/disk.h>

#ifndef NTMP
#define NTMP 3
#endif

extern struct buf *getnewbuf(void);

static u_int	tdsize[NTMP];		/* Number of blocks allocated */
static u_int	tdstart[NTMP];		/* Starting location in map */

extern dev_t	swapdev;

extern int	physio(void (*strat)(struct buf *),
    struct buf *bp, dev_t dev, int rw, struct uio *uio);

extern void	swap(size_t blkno, size_t coreaddr, int count, int rdflg);

int
swopen(dev_t dev, int mode, int flag)
{
	int unit = minor(dev);

	if (unit == 64)
		return bdevsw[major(swapdev)].d_open(swapdev, mode, flag);

	if (unit >= NTMP)
		return ENODEV;

	return 0;
}

int
swclose(dev_t dev, int mode, int flag)
{
	int unit = minor(dev);

	if (unit == 64)
		return bdevsw[major(swapdev)].d_close(swapdev, mode, flag);

	if (unit >= NTMP)
		return ENODEV;

	return 0;
}

daddr_t
swsize(dev_t dev)
{
	int unit = minor(dev);

	if (unit == 64)
		return bdevsw[major(dev)].d_psize(dev);

	if (unit >= NTMP)
		return ENODEV;

	return tdsize[unit];
}

int
swcopen(dev_t dev, int mode, int flag)
{
	int unit = minor(dev);

	if (unit >= NTMP) {
		printf("temp%d: Device number out of range\n", minor(dev));
		return ENODEV;
	}

	return 0;
}

int
swcclose(dev_t dev, int mode, int flag)
{
	int unit = minor(dev);

	if (unit >= NTMP)
		return ENODEV;

	return 0;
}

int
swcread(dev_t dev, struct uio *uio, int flag)
{
	u_int		 block;
	u_int		 boff;
	struct buf	*bp;
	u_int		 rsize;
	u_int		 rlen;

	int unit = minor(dev);

	if (unit >= NTMP) {
		printf("temp%d: Device number out of range\n", minor(dev));
		return ENODEV;
	}

	if (tdstart[unit] == 0)
		return EIO;

	if (uio->uio_offset >= tdsize[unit] << 10)
		return EIO;

	bp = getnewbuf();

	block = uio->uio_offset >> 10;
	boff = uio->uio_offset - (block << 10);

	rsize = DEV_BSIZE - boff;
	rlen = uio->uio_iov->iov_len;

	while ((rlen > 0) && (block < tdsize[unit])) {
		swap(tdstart[unit] + block, (size_t)bp->b_addr,
		    DEV_BSIZE, B_READ);
		uiomove(bp->b_addr + boff, rsize, uio);
		boff = 0;
		block++;
		rlen -= rsize;
		rsize = rlen >= DEV_BSIZE ? DEV_BSIZE : rlen;
	}

	brelse(bp);

	return 0;
}

int
swcwrite(dev_t dev, struct uio *uio, int flag)
{
	u_int		 block;
	u_int		 boff;
	struct buf	*bp;
	u_int		 rsize;
	u_int		 rlen;

	int unit = minor(dev);

	if (unit >= NTMP) {
		printf("temp%d: Device number out of range\n", minor(dev));
		return ENODEV;
	}

	if (tdstart[unit] == 0) {
		printf("temp%d: attempt to write with no allocation\n",
		    unit);
		return EIO;
	}

	if (uio->uio_offset >= tdsize[unit] << 10) {
		printf("temp%d: attempt to write past end of allocation\n",
		    unit);
		return EIO;
	}

	bp = getnewbuf();

	block = uio->uio_offset >> 10;
	boff = uio->uio_offset - (block << 10);

	rsize = DEV_BSIZE - boff;
	rlen = uio->uio_iov->iov_len;

	while (rlen > 0 && block < tdsize[unit]) {
		uiomove(bp->b_addr + boff, rsize, uio);
		swap(tdstart[unit] + block, (size_t)bp->b_addr,
		    DEV_BSIZE, B_WRITE);
		boff = 0;
		block++;
		rlen -= rsize;
		rsize = rlen >= DEV_BSIZE ? DEV_BSIZE : rlen;
	}

	brelse(bp);

	return 0;
}

int
swcioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	u_int		*uival;
	off_t		*offtval;
	off_t		 requested;
#ifdef SWAP_IMAGE_ALIGN
	size_t		 extent[3];
#endif

	int unit = minor(dev);

	if (unit >= NTMP) {
		printf("temp%d: Device number out of range\n", minor(dev));
		return ENODEV;
	}

	uival = (u_int *)addr;
	offtval = (off_t *)addr;

	switch (cmd) {
	case TFALLOC:
		if (tdstart[unit] > 0) {
#ifdef SWAP_IMAGE_ALIGN
			mfree(swapmap,
			    (tdsize[unit] + SWAP_IMAGE_ALIGN - 1) &
			    ~(SWAP_IMAGE_ALIGN - 1), tdstart[unit]);
#else
			mfree(swapmap, tdsize[unit], tdstart[unit]);
#endif
			tdstart[unit] = 0;
			tdsize[unit] = 0;
		}

		if (*offtval > 0) {
			requested = *offtval;
#ifdef SWAP_IMAGE_ALIGN
			extent[0] = extent[1] = extent[2] = 0;
			if (malloc3_contiguous(swapmap,
			    (size_t)requested, 0, 0, SWAP_IMAGE_ALIGN,
			    extent) != 0)
				tdstart[unit] = extent[0];
#else
			tdstart[unit] = malloc(swapmap, requested);
#endif
			if (tdstart[unit] > 0) {
				tdsize[unit] = requested;
				/* printf("temp%d: allocated %lu blocks\n",
				    unit, tdsize[unit]); */

				return 0;
			}
			tdstart[unit] = 0;
			*offtval = 0;
			printf("temp%d: failed to allocate %lu blocks\n",
			    (u_long)requested);

			return 0;
		} else {
			/* printf("temp%d: released allocation\n", unit); */
		}
		break;

	case DIOCGETMEDIASIZE:
		*uival = swsize(dev);
		break;
	}

	return EINVAL;
}

void
swstrategy(struct buf *bp)
{
	int unit = minor(bp->b_dev);

	if (unit == 64) {
		dev_t od = bp->b_dev;
		bp->b_dev = swapdev;
		bdevsw[major(swapdev)].d_strategy(bp);
		bp->b_dev = od;
	} else {
		if (unit >= NTMP)
			return;

		if (tdstart[unit] == 0) {
			printf("swap%d: attempt to access unallocated device\n",
			    unit);
			return;
		}

		if (bp->b_blkno > tdsize[unit]) {
			printf("swap%d: attempt to access past end of allocation\n",
			    unit);
			return;
		}

		if (bp->b_flags & B_READ) {
			swap(tdstart[unit] + bp->b_blkno, (size_t)bp->b_addr,
			    bp->b_bcount, B_READ);
		} else {
			swap(tdstart[unit] + bp->b_blkno, (size_t)bp->b_addr,
			    bp->b_bcount, B_WRITE);
		}

		biodone(bp);
	}
}
