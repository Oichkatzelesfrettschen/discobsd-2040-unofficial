/*
 * Host gate for ialloc() in sys/kern/ufs_alloc.c, linked from the kernel
 * source against a synthetic i-list this file owns, so the exhaustion case is
 * constructed rather than waited for.
 *
 * ialloc() hands out inodes from fs_inode[0 .. fs_ninode), a cache of at most
 * NICINOD numbers that ifree() appends to and the allocator pops from, neither
 * of them testing for membership. The cache therefore holds distinct inode
 * numbers, and this gate is where that invariant is stated.
 *
 * Refilling it scans the i-list twice. The first pass covers [fs_lasti,
 * fs_isize) and the second covers [1, fs_isize), a superset, so the second
 * pass reaches every inode the first one recorded. It stops early once the
 * cache is full, which is why the duplicate is visible only on a filesystem
 * holding fewer than NICINOD free inodes in total: below that bound the second
 * pass runs past fs_lasti with room still left. The fixture is built at that
 * size deliberately.
 *
 * struct dinode is an on-disk layout, and INOPB is MAXBSIZE divided by its
 * size, so bread() may hand back a block of exactly INOPB inodes only where
 * off_t and time_t are four bytes. The static assertions below hold the gate
 * to that, and the Makefile builds it at ILP32 for the same reason.
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

_Static_assert(sizeof(struct dinode) == 64, "dinode is the 64-byte on-disk inode");
_Static_assert(INOPB * sizeof(struct dinode) == MAXBSIZE, "INOPB fills one block");

/* The kernel's own globals, which machdep.c and kern_clock.c define. */
struct user u;
struct timeval time;
int lbolt;

#define GATE_DEV	((dev_t)0x0100)

/*
 * The i-list: ILIST_BLOCKS blocks of INOPB inodes at disk addresses 1 through
 * ILIST_BLOCKS, which is how itod() numbers them, and fs_isize names the first
 * block past them.
 */
#define ILIST_BLOCKS	3
#define FS_ISIZE	(ILIST_BLOCKS + 1)
#define NINODES		(ILIST_BLOCKS * INOPB)

/*
 * A scan starts at fs_lasti and reads the block itod() maps it to, so the two
 * agree only where fs_lasti is the first inode of a block. A DIAGNOSTIC kernel
 * panics on any other value, and the gate builds in that shape as well.
 */
#define LASTI		(INOPB + 1)

static struct dinode dinodes[NINODES];

#ifdef SINGLE_UFS_ROOT
struct mount mount[NMOUNT];
#define GATE_FS		(&mount[0].m_filsys)
#else
static struct fs gate_fs;
#define GATE_FS		(&gate_fs)
#endif

static struct inode gate_pip;

/* Call counts, which are what separate a cache of distinct entries from one
 * carrying duplicates: every duplicate popped costs one iget() and one iput()
 * and yields no inode. */
static unsigned bread_calls;
static unsigned iget_calls;
static unsigned iput_calls;
static unsigned wakeup_calls;

/* Blocks bread() reports a read error for, indexed by disk address. */
static int block_fails[FS_ISIZE + 1];

/* Inodes ifind() reports in core, whether the gate placed them there or an
 * iget() the allocator made still holds them. */
#define NHELD	4
static struct inode held[NHELD];
static int held_busy[NHELD];
static ino_t pinned[NINODES + 1];

static struct buf gate_buf;
static int buf_busy;

struct buf *
bread(dev_t dev, daddr_t blkno)
{
	bread_calls++;
	if (dev != GATE_DEV)
		panic("bread: wrong device");
	if (blkno < 1 || blkno >= FS_ISIZE)
		panic("bread: outside the i-list");
	if (buf_busy)
		panic("bread: buffer already held");
	buf_busy = 1;
	gate_buf.b_flags = 0;
	gate_buf.b_resid = 0;
	gate_buf.b_dev = dev;
	gate_buf.b_blkno = blkno;
	gate_buf.b_bcount = MAXBSIZE;
	gate_buf.b_addr = (caddr_t)&dinodes[(blkno - 1) * INOPB];
	if (block_fails[blkno]) {
		gate_buf.b_flags = B_ERROR;
		gate_buf.b_error = EIO;
	}
	return &gate_buf;
}

void
brelse(struct buf *bp)
{
	if (bp != &gate_buf || !buf_busy)
		panic("brelse: not the gate buffer");
	buf_busy = 0;
}

struct inode *
iget(dev_t dev, struct fs *fs, ino_t ino)
{
	int i;

	iget_calls++;
	if (dev != GATE_DEV)
		panic("iget: wrong device");
	if (fs != GATE_FS)
		panic("iget: wrong filesystem");
	if (ino < 1 || ino > NINODES)
		panic("iget: inode outside the i-list");
	for (i = 0; i < NHELD; i++) {
		if (held_busy[i])
			continue;
		held_busy[i] = 1;
		bzero((caddr_t)&held[i], sizeof(held[i]));
		held[i].i_number = ino;
		held[i].i_mode = dinodes[ino - 1].di_mode;
#ifndef SINGLE_UFS_ROOT
		held[i].i_dev = dev;
		held[i].i_fs = fs;
#endif
		held[i].i_count = 1;
		return &held[i];
	}
	panic("iget: no free inode slot");
	return NULL;
}

/*
 * iput() writes the in-core mode back, which is how an inode the caller filled
 * in becomes an allocated inode on disk and so is skipped by the next scan.
 */
void
iput(struct inode *ip)
{
	int i;

	iput_calls++;
	for (i = 0; i < NHELD; i++) {
		if (&held[i] != ip || !held_busy[i])
			continue;
		dinodes[ip->i_number - 1].di_mode = ip->i_mode;
		held_busy[i] = 0;
		return;
	}
	panic("iput: not a held inode");
}

struct inode *
ifind(dev_t dev, ino_t ino)
{
	int i;

	if (dev != GATE_DEV)
		panic("ifind: wrong device");
	if (pinned[ino])
		return &held[0];
	for (i = 0; i < NHELD; i++)
		if (held_busy[i] && held[i].i_number == ino)
			return &held[i];
	return NULL;
}

/*
 * The scan runs under fs_ilock and the gate is the only caller, so a sleep on
 * that lock means the allocator left it set on a path that returns.
 */
void
sleep(caddr_t chan __unused, int pri __unused)
{
	panic("sleep: the gate is the only caller");
}

void
wakeup(caddr_t chan __unused)
{
	wakeup_calls++;
}

void
uprintf(char *fmt, ...)
{
	(void)fmt;
}

/* Reached from balloc() and free(), which share the translation unit and
 * which no test here calls. */
int
badblock(struct fs *fp __unused, daddr_t bn __unused)
{
	panic("badblock: unreachable from ialloc");
	return 0;
}

struct buf *
getblk(dev_t dev __unused, daddr_t blkno __unused)
{
	panic("getblk: unreachable from ialloc");
	return NULL;
}

int
bwrite(struct buf *bp __unused)
{
	panic("bwrite: unreachable from ialloc");
	return 0;
}

void
bdwrite(struct buf *bp __unused)
{
	panic("bdwrite: unreachable from ialloc");
}

/*
 * Fixture construction. free_list names the inodes whose di_mode is zero;
 * every other inode in the i-list is allocated, as inodes 1 and ROOTINO always
 * are on a mounted filesystem.
 */
static struct fs *
build(const ino_t *free_list, int nfree, ino_t lasti)
{
	struct fs *fs = GATE_FS;
	int i;

	bzero((caddr_t)dinodes, sizeof(dinodes));
	for (i = 0; i < NINODES; i++)
		dinodes[i].di_mode = IFREG | 0600;
	for (i = 0; i < nfree; i++)
		dinodes[free_list[i] - 1].di_mode = 0;

	bzero((caddr_t)fs, sizeof(*fs));
	fs->fs_isize = FS_ISIZE;
	fs->fs_lasti = lasti;
	fs->fs_ninode = 0;
	fs->fs_nbehind = 0;
	fs->fs_tinode = nfree;
	fs->fs_fsmnt[0] = '/';
	fs->fs_fsmnt[1] = '\0';

	bzero((caddr_t)held, sizeof(held));
	bzero((caddr_t)held_busy, sizeof(held_busy));
	bzero((caddr_t)pinned, sizeof(pinned));
	bzero((caddr_t)block_fails, sizeof(block_fails));
	buf_busy = 0;
	bread_calls = 0;
	iget_calls = 0;
	iput_calls = 0;
	wakeup_calls = 0;

	bzero((caddr_t)&gate_pip, sizeof(gate_pip));
#ifdef SINGLE_UFS_ROOT
	mount[0].m_dev = GATE_DEV;
#else
	gate_pip.i_dev = GATE_DEV;
	gate_pip.i_fs = fs;
#endif
	u.u_error = 0;
	hk_reset_output();
	return fs;
}

/* Entries in fs_inode[0 .. fs_ninode) that appear more than once. */
static int
duplicates(struct fs *fs)
{
	int i, j, found = 0;

	for (i = 0; i < fs->fs_ninode; i++)
		for (j = i + 1; j < fs->fs_ninode; j++)
			if (fs->fs_inode[i] == fs->fs_inode[j])
				found++;
	return found;
}

static int
cached(struct fs *fs, ino_t ino)
{
	int i;

	for (i = 0; i < fs->fs_ninode; i++)
		if (fs->fs_inode[i] == ino)
			return 1;
	return 0;
}

/*
 * Allocate until the filesystem refuses, the way a caller creating files
 * does: ialloc() hands back an inode whose mode is zero, the caller fills the
 * mode in, and iput() writes it through.
 */
#define MAXALLOC	(NINODES + 1)

static int
exhaust(struct fs *fs, ino_t *out, int *dup_seen)
{
	struct inode *ip;
	int n = 0;

	*dup_seen = 0;
	while (n < MAXALLOC) {
		ip = ialloc(&gate_pip);
		if (ip == NULL)
			break;
		out[n++] = ip->i_number;
		ip->i_mode = IFREG | 0600;
		iput(ip);
		*dup_seen += duplicates(fs);
	}
	return n;
}

static int
contains(const ino_t *list, int n, ino_t ino)
{
	int i;

	for (i = 0; i < n; i++)
		if (list[i] == ino)
			return 1;
	return 0;
}

/*
 * A population of nine free inodes, six of them at or above fs_lasti. The
 * first pass records those six, the second pass reaches all nine, and nine is
 * below NICINOD, so the second pass still has room when it runs past fs_lasti.
 */
static const ino_t tiny[] = { 3, 4, 5, 20, 21, 22, 40, 41, 42 };
#define TINY_N		((int)(sizeof(tiny) / sizeof(tiny[0])))
#define TINY_ABOVE	6	/* tiny[] entries at or above LASTI */

static void
test_rescan_is_distinct(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	struct inode *ip;
	int i;

	ip = ialloc(&gate_pip);
	HK_CHECK(ip != NULL);
	if (ip == NULL)
		return;

	/*
	 * The refill recorded every free inode once and the caller popped one,
	 * so the cache holds one fewer than the population. Carrying the first
	 * pass's count into the second leaves TINY_ABOVE further entries.
	 */
	HK_CHECK(duplicates(fs) == 0);
	HK_CHECK(fs->fs_ninode == TINY_N - 1);
	for (i = 0; i < fs->fs_ninode; i++)
		HK_CHECK(contains(tiny, TINY_N, fs->fs_inode[i]));
	HK_CHECK(contains(tiny, TINY_N, ip->i_number));
	HK_CHECK(!cached(fs, ip->i_number));

	/* The refill released the i-list lock and announced it. */
	HK_CHECK(fs->fs_ilock == 0);
	HK_CHECK(wakeup_calls == 1);
	HK_CHECK(buf_busy == 0);

	ip->i_mode = IFREG | 0600;
	iput(ip);
}

static void
test_exhaustion_allocates_each_once(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	ino_t got[MAXALLOC];
	int n, dup_seen, i, j;

	n = exhaust(fs, got, &dup_seen);

	HK_CHECK(n == TINY_N);
	HK_CHECK(dup_seen == 0);
	for (i = 0; i < n; i++) {
		HK_CHECK(contains(tiny, TINY_N, got[i]));
		for (j = i + 1; j < n; j++)
			HK_CHECK(got[i] != got[j]);
	}
	for (i = 0; i < TINY_N; i++)
		HK_CHECK(contains(got, n, tiny[i]));

	/* fs_tinode counts the free inodes the allocator has handed out. */
	HK_CHECK(fs->fs_tinode == 0);
}

/*
 * The cost of a duplicate. Each entry popped is read through iget(), and an
 * entry whose inode is already allocated is put back and the next one tried,
 * so one iget() per allocation is what a cache of distinct entries spends.
 */
static void
test_exhaustion_reads_each_inode_once(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	ino_t got[MAXALLOC];
	int n, dup_seen;

	n = exhaust(fs, got, &dup_seen);

	HK_CHECK(n == TINY_N);
	HK_CHECK(iget_calls == (unsigned)n);
	HK_CHECK(iput_calls == (unsigned)n);
	hk_note("  exhaustion: %d inodes, %u iget, %u bread", n, iget_calls,
	    bread_calls);
}

static void
test_full_filesystem_refuses(void)
{
	struct fs *fs = build(tiny, 0, LASTI);
	struct inode *ip;

	ip = ialloc(&gate_pip);
	HK_CHECK(ip == NULL);
	HK_CHECK(u.u_error == ENOSPC);
	HK_CHECK(hk_contains(hk_printf_text, "no inodes free"));
	HK_CHECK(fs->fs_ninode == 0);
	HK_CHECK(fs->fs_ilock == 0);
	HK_CHECK(buf_busy == 0);
}

/*
 * A block the driver failed on advances the inode number by INOPB, so the
 * running number stays aligned with the disk address; inodes in that block are
 * skipped and the rest of the i-list is unaffected.
 */
static void
test_read_error_skips_one_block(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	ino_t got[MAXALLOC];
	int n, dup_seen, i;

	block_fails[2] = 1;
	n = exhaust(fs, got, &dup_seen);

	HK_CHECK(dup_seen == 0);
	for (i = 0; i < n; i++) {
		/* Block 2 carries inodes INOPB+1 through 2*INOPB. */
		HK_CHECK(got[i] <= (ino_t)INOPB || got[i] > (ino_t)(2 * INOPB));
		HK_CHECK(contains(tiny, TINY_N, got[i]));
	}
	HK_CHECK(n == TINY_N - 3);	/* 20, 21 and 22 sit in block 2 */
	HK_CHECK(buf_busy == 0);
}

/* An inode already in core belongs to a caller, so a scan passes over it. */
static void
test_in_core_inode_is_skipped(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	ino_t got[MAXALLOC];
	int n, dup_seen;

	pinned[21] = 21;
	n = exhaust(fs, got, &dup_seen);

	HK_CHECK(dup_seen == 0);
	HK_CHECK(n == TINY_N - 1);
	HK_CHECK(!contains(got, n, 21));
	HK_CHECK(contains(got, n, 20));
	HK_CHECK(contains(got, n, 22));
}

/*
 * The path that never rescans. Every inode above fs_lasti is free, so the
 * first pass fills the cache and returns with first still set but the count at
 * NICINOD, and the second pass is not reached.
 */
static void
test_full_first_pass_does_not_rescan(void)
{
	ino_t many[NINODES];
	struct fs *fs;
	struct inode *ip;
	int nfree = 0, i;

	for (i = LASTI; i <= NINODES; i++)
		many[nfree++] = i;
	fs = build(many, nfree, LASTI);

	ip = ialloc(&gate_pip);
	HK_CHECK(ip != NULL);
	if (ip == NULL)
		return;
	HK_CHECK(duplicates(fs) == 0);
	HK_CHECK(fs->fs_ninode == NICINOD - 1);
	for (i = 0; i < fs->fs_ninode; i++)
		HK_CHECK(fs->fs_inode[i] >= (ino_t)LASTI);
	/* One pass over as many blocks as NICINOD entries need, and no more. */
	HK_CHECK(bread_calls == NICINOD / INOPB);
	ip->i_mode = IFREG | 0600;
	iput(ip);
}

/*
 * The regime above the cache bound, where no entry is ever duplicated because
 * the second pass fills the cache before it reaches fs_lasti. What the reset
 * changes here is which inodes are cached and what the pass costs: the cache
 * takes the NICINOD lowest free inodes rather than the first pass's yield
 * topped up from the bottom, and reaching that many from inode 1 reads one
 * more block than topping up would. The refill pays that read once and hands
 * back the same NICINOD inodes, while the duplicates it avoids would each
 * cost an iget() and an iput() at allocation time.
 */
static void
test_rescan_refills_from_the_top(void)
{
	ino_t many[NINODES];
	struct fs *fs;
	struct inode *ip;
	int nfree = 0, i;

	/* Inodes 1 and ROOTINO are allocated; every other inode is free. */
	for (i = ROOTINO + 1; i <= NINODES; i++)
		many[nfree++] = i;
	fs = build(many, nfree, 2 * INOPB + 1);

	ip = ialloc(&gate_pip);
	HK_CHECK(ip != NULL);
	if (ip == NULL)
		return;
	HK_CHECK(duplicates(fs) == 0);
	HK_CHECK(fs->fs_ninode == NICINOD - 1);
	/* The NICINOD lowest free inodes: ROOTINO+1 through ROOTINO+NICINOD. */
	HK_CHECK(ip->i_number == (ino_t)(ROOTINO + NICINOD));
	for (i = 0; i < fs->fs_ninode; i++) {
		HK_CHECK(fs->fs_inode[i] > ROOTINO);
		HK_CHECK(fs->fs_inode[i] < (ino_t)(ROOTINO + NICINOD));
	}
	/* One block in the first pass, and three to reach NICINOD from the top. */
	HK_CHECK(bread_calls == 1 + ILIST_BLOCKS);
	ip->i_mode = IFREG | 0600;
	iput(ip);
}

/*
 * fs_nbehind counts free inodes that ifree() saw below fs_lasti while the
 * cache was full. Past 4 * NICINOD the allocator enters at fromtop with first
 * clear, so it makes one pass from inode 1 and resets the estimate.
 */
static void
test_nbehind_enters_at_the_top(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	struct inode *ip;

	fs->fs_nbehind = 4 * NICINOD;
	ip = ialloc(&gate_pip);
	HK_CHECK(ip != NULL);
	if (ip == NULL)
		return;
	HK_CHECK(duplicates(fs) == 0);
	HK_CHECK(fs->fs_ninode == TINY_N - 1);
	HK_CHECK(fs->fs_nbehind == 0);
	HK_CHECK(bread_calls == ILIST_BLOCKS);
	ip->i_mode = IFREG | 0600;
	iput(ip);
}

/*
 * ifree() appends to the same array the refill fills, so the two agree on its
 * bound: a free arriving while the cache is full is counted in fs_nbehind
 * instead, and one arriving below the bound is cached and handed back next.
 */
static void
test_ifree_shares_the_cache(void)
{
	struct fs *fs = build(tiny, TINY_N, LASTI);
	struct inode *ip;
	ino_t freed;

	ip = ialloc(&gate_pip);
	HK_CHECK(ip != NULL);
	if (ip == NULL)
		return;
	freed = ip->i_number;
	ip->i_mode = 0;
	iput(ip);
	ifree(&gate_pip, freed);

	HK_CHECK(fs->fs_ninode == TINY_N);
	HK_CHECK(duplicates(fs) == 0);
	HK_CHECK(fs->fs_inode[fs->fs_ninode - 1] == freed);
	HK_CHECK(fs->fs_tinode == TINY_N);
}

int
main(void)
{
	test_rescan_is_distinct();
	test_exhaustion_allocates_each_once();
	test_exhaustion_reads_each_inode_once();
	test_full_filesystem_refuses();
	test_read_error_skips_one_block();
	test_in_core_inode_is_skipped();
	test_full_first_pass_does_not_rescan();
	test_rescan_refills_from_the_top();
	test_nbehind_enters_at_the_top();
	test_ifree_shares_the_cache();
	return hk_verdict("ialloc");
}
