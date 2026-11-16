/*
 * Measurement prototype: the block backend a kernel-linked v7fs core would
 * use, over the DiscoBSD buffer cache (sys/kern/ufs_bio.c). kernel-cost.sh
 * compiles it with the PICO kernel's code-generation flags to size the
 * backend; no kernel configuration links it.
 *
 * A v7fs sector is 512 bytes and a DiscoBSD buffer DEV_BSIZE (1024), so
 * sector S lives in the half S & 1 of buffer block S >> 1. A read copies
 * that half out; a write reads the buffer, replaces the half and writes
 * it back synchronously, so a failed transfer reaches the core as false.
 * Each of the core's three locks (superblock, inode list, scratch memory)
 * is a sleep lock of its own, because the core holds one while taking
 * another: it runs in process context and bread sleeps, so another
 * process can enter between two transfers.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/systm.h>

#include "v7fs.h"
#include "v7fs_impl.h"

struct bio_io {
	dev_t dev;
	daddr_t sectors;
};

static struct v7fs_self self;
static struct bio_io state;
static int sb_busy, ilist_busy, mem_busy;

static bool
bio_read(void *cookie, uint8_t *data, daddr_t sector)
{
	const struct bio_io *io = cookie;
	struct buf *bp;
	bool ok;

	if (sector < 0 || sector >= io->sectors)
		return false;
	bp = bread(io->dev, sector >> 1);
	ok = (bp->b_flags & B_ERROR) == 0;
	if (ok)
		bcopy(bp->b_addr + (sector & 1) * V7FS_BSIZE, data, V7FS_BSIZE);
	brelse(bp);
	return ok;
}

static bool
bio_write(void *cookie, uint8_t *data, daddr_t sector)
{
	const struct bio_io *io = cookie;
	struct buf *bp;

	if (sector < 0 || sector >= io->sectors)
		return false;
	bp = bread(io->dev, sector >> 1);
	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		return false;
	}
	bcopy(data, bp->b_addr + (sector & 1) * V7FS_BSIZE, V7FS_BSIZE);
	return bwrite(bp) == 0;
}

static void
core_lock(void *cookie)
{
	int *busy = cookie;

	while (*busy)
		sleep(busy, PRIBIO);
	*busy = 1;
}

static void
core_unlock(void *cookie)
{
	int *busy = cookie;

	*busy = 0;
	wakeup(busy);
}

int
v7fs_io_init_bio(struct v7fs_self **fs, dev_t dev, daddr_t sectors)
{
	if (sectors <= 0)
		return EINVAL;
	memset(&self, 0, sizeof(self));
	self.scratch_free = -1;
	self.scratch_remain = V7FS_SELF_NSCRATCH;
	state.dev = dev;
	state.sectors = sectors;
	self.io.cookie = &state;
	self.io.read = bio_read;
	self.io.write = bio_write;
	self.sb_lock.cookie = &sb_busy;
	self.ilist_lock.cookie = &ilist_busy;
	self.mem_lock.cookie = &mem_busy;
	self.sb_lock.lock = self.ilist_lock.lock = self.mem_lock.lock =
	    core_lock;
	self.sb_lock.unlock = self.ilist_lock.unlock = self.mem_lock.unlock =
	    core_unlock;
	*fs = &self;
	return 0;
}
