/*
 * Host gate for balloc() and free() in sys/kern/ufs_alloc.c, linked from the
 * kernel source against a synthetic free list and a synthetic disk this file
 * owns, so the refill and the exhaustion exit are constructed rather than
 * waited for.
 *
 * balloc() hands out blocks from fs_free[0 .. fs_nfree). The last entry is
 * the address of the next free-list chunk, so popping it empties the cache
 * and the block is read back as a struct fblk to refill it. That refill
 * writes the superblock, and the invariant this gate states is what the
 * write means: the image on the disk is clean and carries the current time,
 * while the in-core superblock stays modified. sync() (sys/kern/ufs_subr.c)
 * passes over a filesystem whose fs_fmod is zero without flushing an inode
 * or a data block, so a refill that cleared the flag in core would end with
 * fs_tfree, the dirty inodes and the dirty buffers held back until the next
 * allocation or free set it again. The disk image takes the opposite
 * constraint: mountfs() (sys/kern/ufs_mount.c) leaves the on-disk fs_fmod
 * alone on a read-only mount, and ufs_sync() (sys/kern/ufs_syscalls2.c)
 * answers a set flag on a read-only filesystem with panic("sync: rofs").
 *
 * fs_time is the port's clock across a reboot: there is no time-of-day
 * hardware, and main() in sys/kern/init_main.c seeds time.tv_sec from the
 * root superblock's fs_time once mountfs() returns. ufs_sync() is the write
 * that stamps it, because it is the one that flushes the inodes and the data
 * blocks beside the superblock; the refill copies the value it finds.
 *
 * struct fs is an on-disk layout that fills exactly one DEV_BSIZE block, so
 * "*fps = *fs" writes one block only where daddr_t, ino_t, time_t and int
 * are four bytes. The static assertions below hold the gate to that, and the
 * Makefile builds it at ILP32 for the same reason.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/fs.h>
#include <sys/dir.h>
#include <sys/inode.h>
#include <sys/buf.h>
#include <sys/user.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/systm.h>

_Static_assert(sizeof(struct fs) == DEV_BSIZE,
    "the superblock fills exactly one block");
_Static_assert(sizeof(struct fblk) <= DEV_BSIZE,
    "a free-list chunk fits in one block");
_Static_assert(sizeof(time_t) == 4, "fs_time is a four-byte on-disk field");

/* The kernel's own globals, which machdep.c and kern_clock.c define. */
struct user u;
struct timeval time;
int lbolt;

#define GATE_DEV	((dev_t)0x0100)

/*
 * The disk: block 0 is the superblock, blocks 1 through FS_ISIZE-1 are the
 * i-list, and FS_ISIZE through FS_FSIZE-1 carry data. badblock() accepts a
 * block number in that last range and no other.
 */
#define FS_ISIZE	4
#define FS_FSIZE	24

/*
 * The fixture's free list. The superblock holds LINKB, BLK1 and BLK2;
 * popping LINKB empties the cache and reads the chunk stored in that block,
 * which holds the end-of-list zero, BLK3 and BLK4. LINKB is itself handed
 * back as the allocated block, so the fixture yields five blocks in all.
 */
#define LINKB		10
#define BLK1		5
#define BLK2		6
#define BLK3		7
#define BLK4		8
#define FIXTURE_BLOCKS	5

/*
 * The in-core fs_time, which the refill copies, and the clock the refill
 * stamps into the image. Two distinct values separate the two writes.
 */
#define FS_TIME_MOUNT	((time_t)0x40000000)
#define GATE_NOW	((time_t)0x50000000)

static char disk[FS_FSIZE][DEV_BSIZE];

#ifdef SINGLE_UFS_ROOT
struct mount mount[NMOUNT];
#define GATE_FS		(&mount[0].m_filsys)
#else
static struct fs gate_fs;
#define GATE_FS		(&gate_fs)
#endif

static struct inode gate_ip;

/* What the gate counts: every call that leaves the translation unit. */
static unsigned sb_writes;		/* superblock writes, either kind */
static unsigned sb_sync_writes;		/* of those, through bwrite() */
static unsigned sb_delayed_writes;	/* of those, through bdwrite() */
static unsigned data_writes;		/* writes to any other block */
static daddr_t last_data_write;
static unsigned wakeup_calls;
static unsigned sleep_calls;
static unsigned badblock_calls;
static unsigned uprintf_calls;

/* The buffer pool. balloc() holds one buffer at a time except for the one it
 * returns, so a pool this size leaves room for a leak to show. */
#define NBUFS		4
static struct buf bufs[NBUFS];
static int buf_busy[NBUFS];

static int
buffers_held(void)
{
	int i, n = 0;

	for (i = 0; i < NBUFS; i++)
		if (buf_busy[i])
			n++;
	return n;
}

static struct buf *
gate_getbuf(dev_t dev, daddr_t blkno)
{
	int i;

	if (dev != GATE_DEV)
		panic("getbuf: wrong device");
	if (blkno < 0 || blkno >= FS_FSIZE)
		panic("getbuf: block outside the fixture");
	for (i = 0; i < NBUFS; i++) {
		if (buf_busy[i])
			continue;
		buf_busy[i] = 1;
		bufs[i].b_flags = 0;
		bufs[i].b_error = 0;
		bufs[i].b_resid = 0;
		bufs[i].b_dev = dev;
		bufs[i].b_blkno = blkno;
		bufs[i].b_bcount = DEV_BSIZE;
		bufs[i].b_addr = (caddr_t)disk[blkno];
		return &bufs[i];
	}
	panic("getbuf: no free buffer");
	return NULL;
}

struct buf *
bread(dev_t dev, daddr_t blkno)
{
	return gate_getbuf(dev, blkno);
}

struct buf *
getblk(dev_t dev, daddr_t blkno)
{
	return gate_getbuf(dev, blkno);
}

static void
gate_release(struct buf *bp, char *who)
{
	int i;

	for (i = 0; i < NBUFS; i++) {
		if (&bufs[i] != bp || !buf_busy[i])
			continue;
		buf_busy[i] = 0;
		return;
	}
	panic(who);
}

void
brelse(struct buf *bp)
{
	gate_release(bp, "brelse: not a held buffer");
}

static void
gate_record_write(struct buf *bp)
{
	if (bp->b_blkno == SUPERB)
		sb_writes++;
	else {
		data_writes++;
		last_data_write = bp->b_blkno;
	}
}

int
bwrite(struct buf *bp)
{
	gate_record_write(bp);
	if (bp->b_blkno == SUPERB)
		sb_sync_writes++;
	gate_release(bp, "bwrite: not a held buffer");
	return 0;
}

void
bdwrite(struct buf *bp)
{
	gate_record_write(bp);
	if (bp->b_blkno == SUPERB)
		sb_delayed_writes++;
	gate_release(bp, "bdwrite: not a held buffer");
}

/*
 * badblock()'s range predicate, which sys/kern/ufs_subr.c states over
 * fs_isize and fs_fsize. The kernel's copy also reports the block through
 * printf() and fserr(); the gate counts the call instead, so the text a test
 * reads back from the harness is the allocator's own.
 */
int
badblock(struct fs *fp, daddr_t bn)
{
	badblock_calls++;
	return (bn < 0 || (u_long)bn < fp->fs_isize ||
	    (u_long)bn >= fp->fs_fsize);
}

/* The allocator sleeps on fs_flock, which no test leaves held, and five times
 * on lbolt at the exhaustion exit, which is what sleep_calls records. */
void
sleep(caddr_t chan __unused, int pri __unused)
{
	sleep_calls++;
}

void
wakeup(caddr_t chan __unused)
{
	wakeup_calls++;
}

void
uprintf(char *fmt __unused, ...)
{
	uprintf_calls++;
}

/* Reached from ialloc(), which shares the translation unit and which
 * tests/kernel/ialloc_test.c drives. */
struct inode *
iget(dev_t dev __unused, struct fs *fs __unused, ino_t ino __unused)
{
	panic("iget: unreachable from balloc");
	return NULL;
}

void
iput(struct inode *ip __unused)
{
	panic("iput: unreachable from balloc");
}

struct inode *
ifind(dev_t dev __unused, ino_t ino __unused)
{
	panic("ifind: unreachable from balloc");
	return NULL;
}

/*
 * Fixture construction. The superblock carries nfree entries from free_list,
 * the chunk in LINKB carries the rest of the fixture, and fs_tfree counts
 * every block the list can still yield.
 */
static struct fs *
build(const daddr_t *free_list, int nfree, u_int tfree, u_int flags)
{
	struct fs *fs = GATE_FS;
	struct fblk *chunk;
	int i;

	bzero((caddr_t)disk, sizeof(disk));

	chunk = (struct fblk *)disk[LINKB];
	chunk->df_nfree = 3;
	chunk->df_free[0] = 0;		/* end of the free list */
	chunk->df_free[1] = BLK3;
	chunk->df_free[2] = BLK4;

	bzero((caddr_t)fs, sizeof(*fs));
	fs->fs_magic1 = FSMAGIC1;
	fs->fs_magic2 = FSMAGIC2;
	fs->fs_isize = FS_ISIZE;
	fs->fs_fsize = FS_FSIZE;
	fs->fs_nfree = nfree;
	for (i = 0; i < nfree; i++)
		fs->fs_free[i] = free_list[i];
	fs->fs_tfree = tfree;
	fs->fs_time = FS_TIME_MOUNT;
	fs->fs_fmod = 1;		/* mountfs() marks a writable mount */
	fs->fs_flags = flags;
	fs->fs_fsmnt[0] = '/';
	fs->fs_fsmnt[1] = '\0';

	bzero((caddr_t)buf_busy, sizeof(buf_busy));
	sb_writes = 0;
	sb_sync_writes = 0;
	sb_delayed_writes = 0;
	data_writes = 0;
	last_data_write = 0;
	wakeup_calls = 0;
	sleep_calls = 0;
	badblock_calls = 0;
	uprintf_calls = 0;

	bzero((caddr_t)&gate_ip, sizeof(gate_ip));
	gate_ip.i_number = ROOTINO;
#ifdef SINGLE_UFS_ROOT
	mount[0].m_dev = GATE_DEV;
#else
	gate_ip.i_dev = GATE_DEV;
	gate_ip.i_fs = fs;
#endif
	u.u_error = 0;
	time.tv_sec = GATE_NOW;
	time.tv_usec = 0;
	hk_reset_output();
	return fs;
}

/* The fixture's superblock free list, top of stack last. */
static const daddr_t fixture[] = { LINKB, BLK1, BLK2 };
#define FIXTURE_N	((int)(sizeof(fixture) / sizeof(fixture[0])))

static struct fs *
build_fixture(u_int flags)
{
	return build(fixture, FIXTURE_N, FIXTURE_BLOCKS, flags);
}

/* The superblock image the refill left on the disk. */
static struct fs *
disk_superblock(void)
{
	return (struct fs *)disk[SUPERB];
}

/* Allocate until the allocator refuses, releasing each buffer as a caller
 * that has written its block does. */
static int
exhaust(daddr_t *out, int max)
{
	struct buf *bp;
	int n = 0;

	while (n < max) {
		bp = balloc(&gate_ip, 0);
		if (bp == NULL)
			break;
		out[n++] = bp->b_blkno;
		brelse(bp);
	}
	return n;
}

/*
 * An allocation that does not empty the cache touches no disk block but the
 * one it hands back, and marks the superblock modified.
 */
static void
test_alloc_without_refill(void)
{
	struct fs *fs = build_fixture(0);
	struct buf *bp;

	bp = balloc(&gate_ip, 0);
	HK_CHECK(bp != NULL);
	if (bp == NULL)
		return;
	HK_CHECK(bp->b_blkno == (daddr_t)BLK2);
	HK_CHECK(fs->fs_nfree == FIXTURE_N - 1);
	HK_CHECK(fs->fs_tfree == FIXTURE_BLOCKS - 1);
	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(fs->fs_time == FS_TIME_MOUNT);
	HK_CHECK(sb_writes == 0);
	brelse(bp);
	HK_CHECK(buffers_held() == 0);
}

/*
 * The refill. The superblock reaches the disk clean and stamped with the
 * current time, the in-core copy stays modified and keeps the fs_time
 * ufs_sync() last wrote, and the chunk read from LINKB is what the image
 * carries.
 */
static void
test_refill_writes_a_clean_current_superblock(void)
{
	struct fs *fs = build_fixture(0);
	struct fs *image;
	struct buf *bp;
	int i;

	for (i = 0; i < FIXTURE_N - 1; i++) {
		bp = balloc(&gate_ip, 0);
		HK_CHECK(bp != NULL);
		if (bp == NULL)
			return;
		brelse(bp);
	}
	HK_CHECK(sb_writes == 0);

	bp = balloc(&gate_ip, 0);
	HK_CHECK(bp != NULL);
	if (bp == NULL)
		return;

	/* The emptied entry is the chunk address, and it becomes the block. */
	HK_CHECK(bp->b_blkno == (daddr_t)LINKB);

	HK_CHECK(sb_writes == 1);
	HK_CHECK(sb_sync_writes == 1);
	HK_CHECK(sb_delayed_writes == 0);

	image = disk_superblock();
	HK_CHECK(image->fs_fmod == 0);
	HK_CHECK(image->fs_time == GATE_NOW);
	HK_CHECK(image->fs_magic1 == FSMAGIC1);
	HK_CHECK(image->fs_magic2 == FSMAGIC2);
	HK_CHECK(image->fs_nfree == 3);
	HK_CHECK(image->fs_free[0] == 0);
	HK_CHECK(image->fs_free[1] == (daddr_t)BLK3);
	HK_CHECK(image->fs_free[2] == (daddr_t)BLK4);

	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(fs->fs_time == FS_TIME_MOUNT);
	HK_CHECK(fs->fs_flock == 0);
	HK_CHECK(wakeup_calls == 1);
	HK_CHECK(sleep_calls == 0);

	brelse(bp);
	HK_CHECK(buffers_held() == 0);
}

/* MNT_ASYNC turns the refill's superblock write into a delayed one. */
static void
test_refill_under_async_is_delayed(void)
{
	struct fs *fs = build_fixture(MNT_ASYNC);
	struct buf *bp;
	int i;

	for (i = 0; i < FIXTURE_N; i++) {
		bp = balloc(&gate_ip, 0);
		HK_CHECK(bp != NULL);
		if (bp == NULL)
			return;
		brelse(bp);
	}
	HK_CHECK(sb_writes == 1);
	HK_CHECK(sb_delayed_writes == 1);
	HK_CHECK(sb_sync_writes == 0);
	HK_CHECK(disk_superblock()->fs_fmod == 0);
	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(buffers_held() == 0);
}

/*
 * Exhaustion. The list yields every block once, and the refusal corrects
 * fs_nfree and fs_tfree in core, which is a superblock change and so leaves
 * the filesystem modified for ufs_sync() to carry out.
 */
static void
test_exhaustion_marks_the_superblock_modified(void)
{
	struct fs *fs = build_fixture(0);
	daddr_t got[FIXTURE_BLOCKS + 1];
	int n, i, j;

	n = exhaust(got, FIXTURE_BLOCKS + 1);

	HK_CHECK(n == FIXTURE_BLOCKS);
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			HK_CHECK(got[i] != got[j]);

	HK_CHECK(u.u_error == ENOSPC);
	HK_CHECK(hk_contains(hk_printf_text, "file system full"));
	HK_CHECK(uprintf_calls == 1);
	HK_CHECK(sleep_calls == 5);
	HK_CHECK(fs->fs_nfree == 0);
	HK_CHECK(fs->fs_tfree == 0);
	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(buffers_held() == 0);
	hk_note("  exhaustion: %d blocks, refill writes %u", n, sb_writes);
}

/*
 * The same refusal reached from a filesystem ufs_sync() has just cleaned.
 * Nothing between the clean and the refusal sets fs_fmod, so this is the
 * path on which a lost flag would strand the inodes and data blocks.
 */
static void
test_refusal_after_a_sync_marks_the_superblock_modified(void)
{
	struct fs *fs = build_fixture(0);
	daddr_t got[FIXTURE_BLOCKS];
	struct buf *bp;
	int n;

	n = exhaust(got, FIXTURE_BLOCKS);
	HK_CHECK(n == FIXTURE_BLOCKS);

	/* ufs_sync() writes the superblock and clears the flag. */
	fs->fs_fmod = 0;
	sb_writes = 0;

	bp = balloc(&gate_ip, 0);
	HK_CHECK(bp == NULL);
	HK_CHECK(u.u_error == ENOSPC);
	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(sb_writes == 0);
	HK_CHECK(buffers_held() == 0);
}

/*
 * The refill that empties the list. The chunk read back holds no block, so
 * the allocation fails on the far side of a superblock write that has
 * already gone to the disk. Nothing after it sets fs_fmod, which makes this
 * the path where a flag cleared in core strands fs_tfree, the dirty inodes
 * and the dirty buffers until the next allocation or free.
 */
static void
test_refill_into_exhaustion_leaves_the_superblock_modified(void)
{
	struct fs *fs = build_fixture(0);
	struct fblk *chunk;
	struct buf *bp;
	int i;

	chunk = (struct fblk *)disk[LINKB];
	chunk->df_nfree = 0;

	for (i = 0; i < FIXTURE_N - 1; i++) {
		bp = balloc(&gate_ip, 0);
		HK_CHECK(bp != NULL);
		if (bp == NULL)
			return;
		brelse(bp);
	}

	bp = balloc(&gate_ip, 0);
	HK_CHECK(bp == NULL);
	HK_CHECK(u.u_error == ENOSPC);

	/* The write reached the disk before the refusal, and it is clean. */
	HK_CHECK(sb_writes == 1);
	HK_CHECK(disk_superblock()->fs_fmod == 0);
	HK_CHECK(disk_superblock()->fs_time == GATE_NOW);

	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(fs->fs_time == FS_TIME_MOUNT);
	HK_CHECK(fs->fs_nfree == 0);
	HK_CHECK(fs->fs_tfree == 0);
	HK_CHECK(fs->fs_flock == 0);
	HK_CHECK(wakeup_calls == 1);
	HK_CHECK(buffers_held() == 0);
}

/*
 * A free-list entry outside [fs_isize, fs_fsize) is discarded and the next
 * one taken, so the block handed back is always addressable.
 */
static void
test_bad_block_is_skipped(void)
{
	static const daddr_t poisoned[] = { LINKB, BLK1, 2 };
	struct fs *fs = build(poisoned, 3, FIXTURE_BLOCKS, 0);
	struct buf *bp;

	bp = balloc(&gate_ip, 0);
	HK_CHECK(bp != NULL);
	if (bp == NULL)
		return;
	HK_CHECK(bp->b_blkno == (daddr_t)BLK1);
	HK_CHECK(badblock_calls == 2);
	HK_CHECK(fs->fs_nfree == 1);
	brelse(bp);
	HK_CHECK(buffers_held() == 0);
}

/* free() returns a block to the cache and marks the superblock modified. */
static void
test_free_marks_the_superblock_modified(void)
{
	struct fs *fs = build_fixture(0);

	fs->fs_fmod = 0;
	free(&gate_ip, (daddr_t)BLK3);

	HK_CHECK(fs->fs_nfree == FIXTURE_N + 1);
	HK_CHECK(fs->fs_free[FIXTURE_N] == (daddr_t)BLK3);
	HK_CHECK(fs->fs_tfree == FIXTURE_BLOCKS + 1);
	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(sb_writes == 0);
	HK_CHECK(data_writes == 0);
	HK_CHECK(buffers_held() == 0);
}

/*
 * A free arriving at a full cache spills the cache into the block being
 * freed, which becomes the next chunk in the chain. The superblock is not
 * written here: only the flag records the change.
 */
static void
test_free_spills_a_full_cache(void)
{
	struct fs *fs = build_fixture(0);
	struct fblk *chunk;
	int i;

	for (i = 0; i < NICFREE; i++)
		fs->fs_free[i] = (daddr_t)BLK4;
	fs->fs_nfree = NICFREE;
	fs->fs_fmod = 0;

	free(&gate_ip, (daddr_t)BLK3);

	HK_CHECK(data_writes == 1);
	HK_CHECK(last_data_write == (daddr_t)BLK3);
	HK_CHECK(sb_writes == 0);

	chunk = (struct fblk *)disk[BLK3];
	HK_CHECK(chunk->df_nfree == NICFREE);
	HK_CHECK(chunk->df_free[0] == (daddr_t)BLK4);

	HK_CHECK(fs->fs_nfree == 1);
	HK_CHECK(fs->fs_free[0] == (daddr_t)BLK3);
	HK_CHECK(fs->fs_fmod == 1);
	HK_CHECK(fs->fs_flock == 0);
	HK_CHECK(wakeup_calls == 1);
	HK_CHECK(buffers_held() == 0);
}

int
main(void)
{
	test_alloc_without_refill();
	test_refill_writes_a_clean_current_superblock();
	test_refill_under_async_is_delayed();
	test_exhaustion_marks_the_superblock_modified();
	test_refusal_after_a_sync_marks_the_superblock_modified();
	test_refill_into_exhaustion_leaves_the_superblock_modified();
	test_bad_block_is_skipped();
	test_free_marks_the_superblock_modified();
	test_free_spills_a_full_cache();
	return hk_verdict("balloc");
}
