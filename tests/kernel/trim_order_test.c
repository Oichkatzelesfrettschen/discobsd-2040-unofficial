/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Trim order of the filesystem's block release against the flash root's
 * reset semantics.
 *
 * The production ufs_inode.c (itrunc, indirtrunc, iupdat, iget, iput),
 * ufs_alloc.c (alloc, free, ialloc), ufs_bmap.c, ufs_bio.c, ufs_subr.c and
 * ufs_syscalls2.c (ufs_sync, bdev_trim, bdev_checkpoint) run at the
 * target's width in the PICO configuration (SINGLE_UFS_ROOT) over a block
 * device whose strategy routine reads and writes the vendored Dhara map
 * and whose ioctl serves DIOCTRIM with dhara_map_trim() and DIOCCHECKPOINT
 * with dhara_map_sync() (trim_dhara.c), as flash.c does. The device logs
 * every request in submission order.
 *
 * Dhara restores, after a reset, the state of its last checkpoint, and a
 * checkpoint covers a prefix of the request order. Every prefix of a
 * recorded truncate is therefore a state a reset can leave; the test
 * rebuilds each one (restore the flash bytes from before the truncate,
 * replay the first k requests, checkpoint, reset) and requires every block
 * the restored inode references, directly or through its indirect block,
 * to read back the data written to it, never 0xff.
 *
 * The filesystem is an unpartitioned image tools/fsutil makes, loaded the
 * way tools/flashimg writes one (free blocks other than chain blocks left
 * unmapped). Usage: trim_order_test IMAGE [measure].
 */

#include "hostkern.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/user.h>

#include "trim_dhara.h"

/* Kernel state the linked sources name. */
struct buf buf[NBUF];
struct buf bfreelist[BQUEUES];
char bufdata[NBUF * MAXBSIZE];
struct inode inode[NINODE];
struct mount mount[NMOUNT];
struct timeval time;
const struct cdevsw cdevsw[1];
struct user u;
int lbolt;
daddr_t rablock;

int copyin(const caddr_t a, caddr_t b, u_int n) { (void)a; (void)b; (void)n; return EFAULT; }
int copyout(const caddr_t a, caddr_t b, u_int n) { (void)a; (void)b; (void)n; return EFAULT; }
struct file *getf(int fd) { (void)fd; return NULL; }
struct inode *getinode(int fd) { (void)fd; return NULL; }
struct inode *namei(struct nameidata *ndp) { (void)ndp; return NULL; }
int ufs_setattr(struct inode *ip, struct vattr *vap) { (void)ip; (void)vap; return EROFS; }
void uprintf(char *fmt, ...) { (void)fmt; }
void wakeup(caddr_t chan) { (void)chan; }
void log(int level, char *fmt, ...) { (void)level; (void)fmt; }
void cacheinval(struct inode *ip) { (void)ip; }
int chrtoblk(dev_t dev) { (void)dev; return NODEV; }
int blkdev_same_unit(dev_t a, dev_t b) { return a == b; }

/* Nothing here waits: one process, and the device completes at once. */
void
sleep(caddr_t chan, int priority)
{
	(void)chan;
	(void)priority;
	panic("trim order: sleep");
}

/* The request log. */
#define	LOG_MAX		2048
#define	REQ_WRITE	1
#define	REQ_TRIM	2
static struct request {
	int	kind;
	daddr_t	blkno;
	u_char	data[DEV_BSIZE];
} reqlog[LOG_MAX];
static int nlog, log_on;
static daddr_t fail_write = -1;

static void
log_request(int kind, daddr_t blkno, const u_char *data)
{
	if (!log_on)
		return;
	HK_CHECK(nlog < LOG_MAX);
	if (nlog >= LOG_MAX)
		return;
	reqlog[nlog].kind = kind;
	reqlog[nlog].blkno = blkno;
	if (data != NULL)
		bcopy(data, reqlog[nlog].data, DEV_BSIZE);
	nlog++;
}

static int
tdopen(dev_t dev, int flag, int mode)
{
	(void)dev; (void)flag; (void)mode;
	return 0;
}

static void
tdstrategy(struct buf *bp)
{
	bp->b_resid = 0;
	if (bp->b_flags & B_READ) {
		if (td_read((unsigned)bp->b_blkno, (u_char *)bp->b_addr) < 0) {
			bp->b_error = EIO;
			bp->b_flags |= B_ERROR;
		}
	} else if (bp->b_blkno == fail_write) {
		fail_write = -1;
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
	} else {
		log_request(REQ_WRITE, bp->b_blkno, (u_char *)bp->b_addr);
		if (td_write((unsigned)bp->b_blkno, (u_char *)bp->b_addr) < 0) {
			bp->b_error = EIO;
			bp->b_flags |= B_ERROR;
		}
	}
	biodone(bp);
}

static daddr_t
tdsize(dev_t dev)
{
	(void)dev;
	return 0;
}

static int
tdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	(void)dev;
	switch (cmd) {
	case DIOCTRIM:
		if (flag != 0)
			return EPERM;
		log_request(REQ_TRIM, *(daddr_t *)addr, NULL);
		return td_trim((unsigned)*(daddr_t *)addr) < 0 ? EIO : 0;
	case DIOCCHECKPOINT:
		*(int *)addr = td_sync() < 0 ? EIO : 0;
		return 0;
	}
	return EINVAL;
}

const struct bdevsw bdevsw[] = {
	{ tdopen, tdopen, tdstrategy, tdsize, tdioctl, 0 },
	{ 0 },
};

#define	DEV	makedev(0, 0)

/* binit() of sys/kern/init_main.c, which keeps it static. */
static void
init_buffers(void)
{
	struct buf *bp;
	int i;

	for (bp = bfreelist; bp < &bfreelist[BQUEUES]; bp++)
		bp->av_forw = bp->av_back = bp;
	for (i = 0; i < NBUF; i++) {
		bp = &buf[i];
		bp->b_dev = NODEV;
		bp->b_bcount = 0;
		bp->b_addr = bufdata + MAXBSIZE * i;
		bp->b_flags = B_BUSY | B_INVAL;
		brelse(bp);
	}
}

static struct fs *fs;

/* Boot over the device's current state: what mountfs() does for the root. */
static void
mount_root(int async)
{
	struct buf *bp;

	bzero(buf, sizeof(buf));
	bzero(inode, sizeof(inode));
	bzero(mount, sizeof(mount));
	ihinit();
	init_buffers();
	mount[0].m_dev = DEV;
	mount[0].m_inodp = (struct inode *)1;
	fs = &mount[0].m_filsys;
	bp = bread(DEV, SUPERB);
	HK_CHECK((bp->b_flags & B_ERROR) == 0);
	bcopy(bp->b_addr, (caddr_t)fs, sizeof(struct fs));
	brelse(bp);
	HK_CHECK(fs->fs_magic1 == FSMAGIC1 && fs->fs_magic2 == FSMAGIC2);
	fs->fs_ronly = 0;
	fs->fs_fmod = 1;
	fs->fs_ilock = fs->fs_flock = 0;
	fs->fs_nbehind = 0;
	fs->fs_lasti = 1;
	fs->fs_flags = async ? MNT_ASYNC : 0;
	mount[0].m_flags = fs->fs_flags;
	u.u_error = 0;
}

/* Flush and checkpoint, as sync(2) does for the root. */
static void
sync_root(void)
{
	HK_CHECK(ufs_sync(&mount[0]) == 0);
}

/* A reset: the device returns to its last checkpoint and RAM is lost. */
static void
reset(int async)
{
	td_reset();
	mount_root(async);
}

static void
pattern(u_char *data, ino_t ino, daddr_t lbn)
{
	int i;

	for (i = 0; i < DEV_BSIZE; i++)
		data[i] = (u_char)(ino * 7 + lbn * 13 + i);
	if (data[0] == 0xff)
		data[0] = 0;
}

/* A regular file of nblocks blocks, each holding pattern(ino, lbn). */
static struct inode *
make_file(int nblocks)
{
	struct inode *root, *ip;
	struct buf *bp;
	daddr_t bn;
	int lbn;

	root = iget(DEV, fs, ROOTINO);
	HK_CHECK(root != NULL);
	ip = ialloc(root);
	HK_CHECK(ip != NULL);
	iput(root);
	if (ip == NULL)
		return NULL;
	ip->i_mode = IFREG | 0644;
	ip->i_nlink = 1;
	ip->i_flag |= IUPD | ICHG;
	for (lbn = 0; lbn < nblocks; lbn++) {
		bn = bmap(ip, lbn, B_WRITE, 0);
		HK_CHECK(bn > 0 && u.u_error == 0);
		bp = getblk(DEV, bn);
		pattern((u_char *)bp->b_addr, ip->i_number, lbn);
		bwrite(bp);
	}
	ip->i_size = (off_t)nblocks * DEV_BSIZE;
	ip->i_flag |= IUPD | ICHG;
	iupdat(ip, &time, &time, 1);
	return ip;
}

static int
is_erased(const u_char *data)
{
	int i;

	for (i = 0; i < DEV_BSIZE; i++)
		if (data[i] != 0xff)
			return 0;
	return 1;
}

/*
 * Every block the device's copy of inode ino references reads back its
 * pattern. Returns the number of referenced blocks that do not.
 */
static int
check_references(ino_t ino)
{
	u_char block[DEV_BSIZE], want[DEV_BSIZE];
	const struct dinode *dp;
	const daddr_t *ind;
	daddr_t addr[NADDR];
	int i, bad = 0;

	if (td_read((unsigned)itod(ino), block) < 0)
		return 1;
	dp = (const struct dinode *)block + itoo(ino);
	bcopy(dp->di_addr, addr, sizeof(addr));
	for (i = 0; i < NDADDR; i++) {
		if (addr[i] == 0)
			continue;
		if (td_read((unsigned)addr[i], block) < 0)
			return bad + 1;
		pattern(want, ino, i);
		if (is_erased(block) || bcmp(block, want, DEV_BSIZE) != 0)
			bad++;
	}
	if (addr[NDADDR] != 0) {
		u_char indirect[DEV_BSIZE];

		if (td_read((unsigned)addr[NDADDR], indirect) < 0)
			return bad + 1;
		if (is_erased(indirect))
			return bad + 1;
		ind = (const daddr_t *)indirect;
		for (i = 0; i < (int)NINDIR; i++) {
			if (ind[i] == 0)
				continue;
			if (td_read((unsigned)ind[i], block) < 0)
				return bad + 1;
			pattern(want, ino, NDADDR + i);
			if (is_erased(block) || bcmp(block, want, DEV_BSIZE) != 0)
				bad++;
		}
	}
	return bad;
}

static int
first_index(int kind, daddr_t blkno)
{
	int i;

	for (i = 0; i < nlog; i++)
		if (reqlog[i].kind == kind && reqlog[i].blkno == blkno)
			return i;
	return -1;
}

/*
 * a. The order the driver saw: the inode block written before the first
 * trim, a zeroed indirect block written before any of its children is
 * trimmed, and each freed block trimmed once and before any write of it.
 */
static void
check_order(ino_t ino, const daddr_t *freed, int nfreed, daddr_t indirect)
{
	int i, trim, inode_write = -1, first_trim = nlog;

	for (i = 0; i < nlog; i++) {
		if (reqlog[i].kind == REQ_WRITE &&
		    reqlog[i].blkno == itod(ino) && inode_write < 0)
			inode_write = i;
		if (reqlog[i].kind == REQ_TRIM && i < first_trim)
			first_trim = i;
	}
	HK_CHECK(inode_write >= 0 && inode_write < first_trim);
	for (i = 0; i < nfreed; i++) {
		trim = first_index(REQ_TRIM, freed[i]);
		if (trim < 0)
			hk_note("block %ld freed without a trim\n", (long)freed[i]);
		HK_CHECK(trim >= 0);
		if (indirect != 0 && freed[i] != indirect)
			HK_CHECK(first_index(REQ_WRITE, indirect) < trim);
		/* Only the indirect block's own zeroing write precedes its trim. */
		if (freed[i] != indirect)
			HK_CHECK(first_index(REQ_WRITE, freed[i]) < 0 ||
			    first_index(REQ_WRITE, freed[i]) > trim);
	}
}

/* The blocks of ip, read from its in-core addresses. */
static int
file_blocks(struct inode *ip, daddr_t *out, daddr_t *indirect)
{
	struct buf *bp;
	int i, n = 0;

	for (i = 0; i < NDADDR; i++)
		if (IADDR(ip, i) != 0)
			out[n++] = IADDR(ip, i);
	*indirect = IADDR(ip, NDADDR);
	if (*indirect != 0) {
		out[n++] = *indirect;
		bp = bread(DEV, *indirect);
		for (i = 0; i < (int)NINDIR; i++)
			if (((daddr_t *)bp->b_addr)[i] != 0)
				out[n++] = ((daddr_t *)bp->b_addr)[i];
		brelse(bp);
	}
	return n;
}

/*
 * b. Every prefix of the recorded requests, made durable by a checkpoint
 * and restored by a reset, leaves each block the inode references
 * readable. Returns the first failing cut, or -1.
 */
static int
check_every_cut(ino_t ino)
{
	int cut, i;

	for (cut = 0; cut <= nlog; cut++) {
		td_restore();
		for (i = 0; i < cut; i++) {
			if (reqlog[i].kind == REQ_WRITE)
				HK_CHECK(td_write((unsigned)reqlog[i].blkno,
				    reqlog[i].data) == 0);
			else
				HK_CHECK(td_trim((unsigned)reqlog[i].blkno) == 0);
		}
		HK_CHECK(td_sync() == 0);
		td_reset();
		if (check_references(ino) != 0) {
			hk_note("cut %d of %d: a referenced block reads wrong\n",
			    cut, nlog);
			for (i = 0; i < nlog && i < 64; i++)
				hk_note("  %d %s %ld\n", i, reqlog[i].kind ==
				    REQ_WRITE ? "write" : "trim",
				    (long)reqlog[i].blkno);
			return cut;
		}
	}
	return -1;
}

static const char *image;

static void
fresh(int async)
{
	unsigned units, skipped;

	HK_CHECK(td_load(image, &units, &skipped) == 0);
	mount_root(async);
	nlog = 0;
	log_on = 0;
}

/* a and b: truncate to zero and to a partial length, sync and async. */
static void
test_truncate(int async, off_t length)
{
	daddr_t freed[NDADDR + NINDIR + 1], indirect;
	struct inode *ip;
	ino_t ino;
	int nfreed, keep, i, j;

	fresh(async);
	ip = make_file(NDADDR + 20);
	if (ip == NULL)
		return;
	ino = ip->i_number;
	sync_root();
	HK_CHECK(check_references(ino) == 0);
	nfreed = file_blocks(ip, freed, &indirect);
	keep = (int)((length + DEV_BSIZE - 1) / DEV_BSIZE);
	/* The blocks a truncate to length keeps are not freed. */
	for (i = j = 0; i < nfreed; i++) {
		if (freed[i] == indirect && keep > NDADDR)
			continue;
		/* file_blocks() lists the indirect block before its children. */
		if (freed[i] != indirect &&
		    (indirect != 0 && i > NDADDR ? i - 1 : i) < keep)
			continue;
		freed[j++] = freed[i];
	}
	nfreed = j;
	td_save();
	nlog = 0;
	log_on = 1;
	itrunc(ip, length, 0);
	log_on = 0;
	HK_CHECK(u.u_error == 0);
	check_order(ino, freed, nfreed, keep > NDADDR ? 0 : indirect);
	HK_CHECK(check_every_cut(ino) < 0);
	iput(ip);
}

/*
 * c. Enough frees to fill the in-core list make a freed block a chain
 * node; after a checkpoint and reset the free list walks to its end and
 * every chain block holds a valid count.
 */
static void
test_chain(void)
{
	struct inode *ip;
	u_char block[DEV_BSIZE];
	u_int nfree, chains = 0, entries[NICFREE + 1];
	struct fs *dfs;
	daddr_t link;

	fresh(0);
	ip = make_file(NDADDR + 230);
	if (ip == NULL)
		return;
	sync_root();
	nlog = 0;
	log_on = 1;
	itrunc(ip, 0, 0);
	log_on = 0;
	iput(ip);
	sync_root();
	reset(0);
	HK_CHECK(td_read(SUPERB, block) == 0);
	dfs = (struct fs *)block;
	nfree = dfs->fs_nfree;
	bcopy(dfs->fs_free, entries, sizeof(dfs->fs_free));
	while (nfree != 0) {
		HK_CHECK(nfree <= NICFREE);
		if (nfree > NICFREE)
			return;
		link = entries[0];
		if (link == 0)
			break;
		chains++;
		HK_CHECK(td_read((unsigned)link, block) == 0);
		HK_CHECK(!is_erased(block));
		if (is_erased(block))
			return;		/* alloc() would read a broken chain. */
		nfree = *(u_int *)block;
		bcopy(block + sizeof(u_int), entries, NICFREE * sizeof(u_int));
	}
	HK_CHECK(chains > 0);
	/* alloc() reads the chain: allocate everything the list holds. */
	while (fs->fs_tfree > 0 && fs->fs_nfree > 0) {
		struct buf *bp = balloc(ip, 0);

		if (bp == NULL)
			break;
		brelse(bp);
	}
	HK_CHECK(u.u_error == 0);
}

/* d. A trimmed block allocated again and written survives a reset. */
static void
test_reuse(void)
{
	struct inode *ip;
	daddr_t freed, again;
	u_char block[DEV_BSIZE], want[DEV_BSIZE];
	ino_t ino;

	fresh(0);
	ip = make_file(1);
	if (ip == NULL)
		return;
	freed = IADDR(ip, 0);
	sync_root();
	nlog = 0;
	log_on = 1;
	itrunc(ip, 0, 0);
	HK_CHECK(first_index(REQ_TRIM, freed) >= 0);
	again = bmap(ip, 0, B_WRITE, 0);
	log_on = 0;
	HK_CHECK(again == freed);
	ino = ip->i_number;
	{
		struct buf *bp = getblk(DEV, again);

		pattern((u_char *)bp->b_addr, ino, 0);
		bwrite(bp);
	}
	ip->i_size = DEV_BSIZE;
	ip->i_flag |= IUPD | ICHG;
	iupdat(ip, &time, &time, 1);
	iput(ip);
	sync_root();
	reset(0);
	HK_CHECK(td_read((unsigned)again, block) == 0);
	pattern(want, ino, 0);
	HK_CHECK(bcmp(block, want, DEV_BSIZE) == 0);
	HK_CHECK(check_references(ino) == 0);
}

/*
 * e. Create and delete files in a loop on the sparse image: with trim the
 * map's live count returns to its starting value.
 */
static unsigned live_start, live_end, loop_pages;

static void
test_live_share(void)
{
	struct inode *ip;
	unsigned pages, writes;
	int round;

	fresh(0);
	sync_root();
	live_start = td_live();
	pages = td_pages();
	writes = 0;
	for (round = 0; round < 10; round++) {
		ip = make_file(NDADDR + 12);
		if (ip == NULL)
			return;
		ip->i_nlink = 0;
		iput(ip);
		sync_root();
	}
	live_end = td_live();
	loop_pages = td_pages() - pages;
	writes = 10 * (NDADDR + 12);
	hk_note("live share: %u sectors before, %u after ten create and "
	    "delete rounds; %u Dhara pages programmed for %u file blocks "
	    "(%u.%02u per block)\n", live_start, live_end, loop_pages, writes,
	    loop_pages / writes, loop_pages * 100 / writes % 100);
	HK_CHECK(live_end == live_start);
}

/*
 * Create-and-delete rounds with a static file holding most of a
 * capacity-sized filesystem live, the shipped root's situation: printed,
 * not asserted.
 * Each trim journals one Dhara page (try_delete() copies a neighbor's
 * page), so trim pays only where it spares garbage-collection copies.
 */
static void
measure_occupancy(int temporaries, int blocks, int rounds)
{
	struct inode *ip[8], *keep;
	unsigned pages, live0, peak = 0;
	int round, k;

	fresh(0);
	keep = make_file(720);
	if (keep == NULL)
		return;
	sync_root();
	live0 = td_live();
	pages = td_pages();
	for (round = 0; round < rounds; round++) {
		for (k = 0; k < temporaries; k++) {
			ip[k] = make_file(blocks);
			if (ip[k] == NULL)
				return;
		}
		sync_root();
		if (td_live() > peak)
			peak = td_live();
		for (k = 0; k < temporaries; k++) {
			ip[k]->i_nlink = 0;
			iput(ip[k]);
		}
		sync_root();
	}
	pages = td_pages() - pages;
	hk_note("occupancy loop, %d rounds of %d files of %d blocks: %u live "
	    "sectors with the static file, peak %u, %u after; %u Dhara pages "
	    "for %u file blocks (%u.%02u per block)\n", rounds, temporaries,
	    blocks, live0, peak, td_live(), pages,
	    (unsigned)(rounds * temporaries * blocks),
	    pages / (unsigned)(rounds * temporaries * blocks),
	    pages * 100 / (unsigned)(rounds * temporaries * blocks) % 100);
	iput(keep);
}

/* f. Unlink of an open file: the truncate waits for the last reference. */
static void
test_unlink_open(void)
{
	daddr_t freed[NDADDR + NINDIR + 1], indirect;
	struct inode *ip;
	ino_t ino;
	int nfreed;

	fresh(1);
	ip = make_file(NDADDR + 3);
	if (ip == NULL)
		return;
	ino = ip->i_number;
	sync_root();
	nfreed = file_blocks(ip, freed, &indirect);
	ip->i_count++;
	ip->i_nlink = 0;
	ip->i_flag |= ICHG;
	td_save();
	nlog = 0;
	log_on = 1;
	iput(ip);
	HK_CHECK(first_index(REQ_TRIM, freed[0]) < 0);
	iput(ip);
	log_on = 0;
	check_order(ino, freed, nfreed, indirect);
	HK_CHECK(check_every_cut(ino) < 0);
}

/*
 * f. A failed initialization write rolls the allocation back through
 * free(); the failed write latched in the mount, so the block is not
 * trimmed, and it is back on the free list.
 */
static void
test_failed_allocation(void)
{
	struct inode *ip;
	daddr_t bn, next;

	fresh(0);
	ip = make_file(1);
	if (ip == NULL)
		return;
	sync_root();
	next = fs->fs_free[fs->fs_nfree - 1];
	fail_write = next;
	nlog = 0;
	log_on = 1;
	bn = bmap(ip, 1, B_WRITE, B_SYNC);
	log_on = 0;
	HK_CHECK(bn < 0 && u.u_error == EIO);
	HK_CHECK(mount[0].m_write_error == EIO);
	HK_CHECK(first_index(REQ_TRIM, next) < 0);
	HK_CHECK(fs->fs_free[fs->fs_nfree - 1] == next);
	u.u_error = 0;
	mount[0].m_write_error = 0;
	iput(ip);
}

int
main(int argc, char **argv)
{
	if (argc != 2 && !(argc == 3 && hk_streq(argv[2], "measure"))) {
		hk_note("usage: trim_order_test IMAGE [measure]\n");
		return 2;
	}
	image = argv[1];
	test_truncate(0, 0);
	test_truncate(1, 0);
	test_truncate(0, 2 * DEV_BSIZE + 100);
	test_truncate(1, (NDADDR + 5) * DEV_BSIZE);
	test_chain();
	test_reuse();
	test_live_share();
	/*
	 * The measurements follow a passing run, or any run given "measure"
	 * (the trim-skipped mutant's figures): a broken free list hangs them.
	 */
	if (hk_failures == 0 || argc == 3) {
		measure_occupancy(1, NDADDR + 12, 150);
		measure_occupancy(6, 30, 20);
	}
	test_unlink_open();
	test_failed_allocation();
	HK_CHECK(td_faults() == 0);
	return hk_verdict("trim order");
}
