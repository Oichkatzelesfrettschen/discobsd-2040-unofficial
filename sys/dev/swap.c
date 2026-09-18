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

/*
 * Raw flash has only 384 swap blocks.  Sixteen-bit extent fields therefore
 * retain the complete range while making room for an append high-water mark
 * in less SRAM than the former two u_int arrays used.  A newly allocated
 * temporary device may use erase-once append writes only in strict block
 * order from zero; any partial, duplicate, or out-of-order write permanently
 * switches the extent to ordinary copy-through-scratch rewrites.
 */
#define TD_APPEND_DISABLED 0xffffU
struct swtemp {
	u_short	t_start;
	u_short	t_size;
	u_short	t_next;
};
static struct swtemp td[NTMP];

static int
swtemp_write_flags(struct swtemp *temp, u_int block, u_int blocks)
{
	if (blocks == 0 || temp->t_next == TD_APPEND_DISABLED ||
	    block != temp->t_next || block > temp->t_size ||
	    blocks > temp->t_size - block) {
		temp->t_next = TD_APPEND_DISABLED;
		return B_WRITE;
	}
	temp->t_next += blocks;
	return B_WRITE | B_SWAPIMAGE;
}

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

	return td[unit].t_size;
}

int
swcopen(dev_t dev, int mode __unused, int flag __unused)
{
	int unit = minor(dev);

	if (unit >= NTMP) {
		printf("temp%d: Device number out of range\n", minor(dev));
		return ENODEV;
	}

	return 0;
}

int
swcclose(dev_t dev, int mode __unused, int flag __unused)
{
	int unit = minor(dev);

	if (unit >= NTMP)
		return ENODEV;

	return 0;
}

int
swcread(dev_t dev, struct uio *uio, int flag __unused)
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

	if (td[unit].t_start == 0)
		return EIO;

	if (uio->uio_offset >= td[unit].t_size << 10)
		return EIO;

	bp = getnewbuf();

	block = uio->uio_offset >> 10;
	boff = uio->uio_offset - (block << 10);

	rlen = uio->uio_iov->iov_len;

	while ((rlen > 0) && (block < td[unit].t_size)) {
		rsize = MIN(DEV_BSIZE - boff, rlen);
		swap(td[unit].t_start + block, (size_t)bp->b_addr,
		    DEV_BSIZE, B_READ);
		uiomove(bp->b_addr + boff, rsize, uio);
		boff = 0;
		block++;
		rlen -= rsize;
	}

	brelse(bp);

	return 0;
}

int
swcwrite(dev_t dev, struct uio *uio, int flag __unused)
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

	if (td[unit].t_start == 0) {
		printf("temp%d: attempt to write with no allocation\n",
		    unit);
		return EIO;
	}

	if (uio->uio_offset >= td[unit].t_size << 10) {
		printf("temp%d: attempt to write past end of allocation\n",
		    unit);
		return EIO;
	}

	bp = getnewbuf();

	block = uio->uio_offset >> 10;
	boff = uio->uio_offset - (block << 10);

	rlen = uio->uio_iov->iov_len;

	while (rlen > 0 && block < td[unit].t_size) {
		rsize = MIN(DEV_BSIZE - boff, rlen);
		if (boff != 0 || rsize != DEV_BSIZE) {
			td[unit].t_next = TD_APPEND_DISABLED;
			swap(td[unit].t_start + block, (size_t)bp->b_addr,
			    DEV_BSIZE, B_READ);
		}
		uiomove(bp->b_addr + boff, rsize, uio);
		swap(td[unit].t_start + block, (size_t)bp->b_addr,
		    DEV_BSIZE, boff == 0 && rsize == DEV_BSIZE ?
		    swtemp_write_flags(&td[unit], block, 1) : B_WRITE);
		boff = 0;
		block++;
		rlen -= rsize;
	}

	brelse(bp);

	return 0;
}

int
swcioctl(dev_t dev, u_int cmd, caddr_t addr, int flag __unused)
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
		if (td[unit].t_start > 0) {
#ifdef SWAP_IMAGE_ALIGN
			mfree(swapmap,
			    (td[unit].t_size + SWAP_IMAGE_ALIGN - 1) &
			    ~(SWAP_IMAGE_ALIGN - 1), td[unit].t_start);
#else
			mfree(swapmap, td[unit].t_size, td[unit].t_start);
#endif
			td[unit].t_start = 0;
			td[unit].t_size = 0;
			td[unit].t_next = TD_APPEND_DISABLED;
		}

		if (*offtval > 0) {
			requested = *offtval;
#ifdef SWAP_IMAGE_ALIGN
			extent[0] = extent[1] = extent[2] = 0;
			if (malloc3_contiguous_next(swapmap,
			    (size_t)requested, 0, 0, SWAP_IMAGE_ALIGN,
			    &swapnext, extent) != 0)
				td[unit].t_start = extent[0];
#else
			td[unit].t_start = malloc(swapmap, requested);
#endif
			if (td[unit].t_start > 0) {
				td[unit].t_size = requested;
				td[unit].t_next = 0;
#ifdef SWAP_IMAGE_ALIGN
				swap_cursor_publish(swapnext);
#endif
				/* printf("temp%d: allocated %lu blocks\n",
				    unit, td[unit].t_size); */

				return 0;
			}
			td[unit].t_start = 0;
			td[unit].t_next = TD_APPEND_DISABLED;
			*offtval = 0;
			printf("temp%d: failed to allocate %lu blocks\n",
			    unit, (u_long)requested);

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

		if (td[unit].t_start == 0) {
			printf("swap%d: attempt to access unallocated device\n",
			    unit);
			return;
		}

		if (bp->b_blkno >= td[unit].t_size ||
		    btod(bp->b_bcount) > (u_long)(td[unit].t_size - bp->b_blkno)) {
			printf("swap%d: attempt to access past end of allocation\n",
			    unit);
			return;
		}

		if (bp->b_flags & B_READ) {
			swap(td[unit].t_start + bp->b_blkno, (size_t)bp->b_addr,
			    bp->b_bcount, B_READ);
		} else {
			swap(td[unit].t_start + bp->b_blkno, (size_t)bp->b_addr,
			    bp->b_bcount,
			    (bp->b_bcount & (DEV_BSIZE - 1)) == 0 ?
			    swtemp_write_flags(&td[unit], bp->b_blkno,
			    btod(bp->b_bcount)) :
			    (td[unit].t_next = TD_APPEND_DISABLED, B_WRITE));
		}

		biodone(bp);
	}
}
