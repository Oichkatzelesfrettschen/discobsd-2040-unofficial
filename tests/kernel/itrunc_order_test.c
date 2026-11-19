/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Host gate for the order of itrunc() in sys/kern/ufs_inode.c: the shortened
 * inode reaches the device before the first block is freed.
 *
 * The gate compiles the production ufs_inode.c (itrunc, indirtrunc, iupdat)
 * at the target's width in the PICO configuration and links it against a
 * buffer pool and a block store this file owns. free() of ufs_alloc.c is
 * renamed to itrunc_order_free() for the compilation, so every block release
 * appears in the event log beside every buffer write, in submission order.
 *
 * A reset can fall between any two events. The invariant the gate states is
 * the one that makes every such cut safe: when the first block is freed, a
 * synchronous write of the inode block has already been submitted, the
 * dinode it carries holds the new size, and none of the blocks freed by the
 * truncation appears among its addresses. The truncation writes the inode
 * once, so the ordering costs one inode block write. The inode enters each
 * case with clean flags, which iupdat() answers with an early return, so the
 * gate also holds the flags that force the write.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/user.h>

_Static_assert(sizeof(daddr_t) == 4, "the on-disk inode holds four-byte addresses");

#define GATE_DEV	((dev_t)0x0100)
#define FS_ISIZE	4
#define FS_FSIZE	64
#define GATE_INO	ROOTINO
#define NBUFS		4
#define FIRST_DATA	8
#define EV_MAX		128

struct user u;
struct timeval time;
struct mount mount[NMOUNT];
int lbolt;

static char disk[FS_FSIZE][DEV_BSIZE];
static struct buf bufs[NBUFS];
static int buf_busy[NBUFS];

enum event_kind { EV_WRITE_SYNC, EV_WRITE_DELAYED, EV_FREE };

static struct event {
	enum event_kind kind;
	daddr_t blkno;			/* block written, or block freed */
	struct dinode dinode;		/* the gate inode as written */
} events[EV_MAX];
static unsigned nevents;

static void
record(enum event_kind kind, daddr_t blkno)
{
	struct event *ev;

	HK_CHECK(nevents < EV_MAX);
	if (nevents >= EV_MAX)
		return;
	ev = &events[nevents++];
	ev->kind = kind;
	ev->blkno = blkno;
	bzero((caddr_t)&ev->dinode, sizeof(ev->dinode));
	if (kind != EV_FREE && blkno == FS_ITOD(&mount[0].m_filsys, GATE_INO))
		ev->dinode = ((struct dinode *)disk[blkno])[itoo(GATE_INO)];
}

static struct buf *
gate_getbuf(dev_t dev, daddr_t blkno)
{
	int i;

	if (dev != GATE_DEV || blkno < 0 || blkno >= FS_FSIZE)
		panic("itrunc order: block outside the fixture");
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
	panic("itrunc order: no free buffer");
	return NULL;
}

static void
gate_release(struct buf *bp)
{
	int i;

	for (i = 0; i < NBUFS; i++)
		if (&bufs[i] == bp && buf_busy[i]) {
			buf_busy[i] = 0;
			return;
		}
	panic("itrunc order: not a held buffer");
}

struct buf *
bread(dev_t dev, daddr_t blkno)
{
	return gate_getbuf(dev, blkno);
}

/* A private block-sized buffer, which indirtrunc() fills with a copy. */
struct buf *
geteblk(void)
{
	static char scratch[NBUFS][DEV_BSIZE];
	int i;

	for (i = 0; i < NBUFS; i++) {
		if (buf_busy[i])
			continue;
		buf_busy[i] = 1;
		bufs[i].b_flags = B_BUSY;
		bufs[i].b_error = 0;
		bufs[i].b_resid = 0;
		bufs[i].b_dev = NODEV;
		bufs[i].b_blkno = -1;
		bufs[i].b_bcount = DEV_BSIZE;
		bufs[i].b_addr = scratch[i];
		return &bufs[i];
	}
	panic("itrunc order: no free buffer");
	return NULL;
}

int
geterror(struct buf *bp)
{
	return (bp->b_flags & B_ERROR) ? (bp->b_error != 0 ? bp->b_error : EIO) : 0;
}

void
brelse(struct buf *bp)
{
	gate_release(bp);
}

/* Nonzero makes the next synchronous write fail with that error. */
static int fail_write;

int
bwrite(struct buf *bp)
{
	int error = fail_write;

	fail_write = 0;
	if (error == 0)
		record(EV_WRITE_SYNC, bp->b_blkno);
	gate_release(bp);
	return error;
}

void
bdwrite(struct buf *bp)
{
	record(EV_WRITE_DELAYED, bp->b_blkno);
	gate_release(bp);
}

void
itrunc_order_free(struct inode *ip, daddr_t bn)
{
	(void)ip;
	record(EV_FREE, bn);
}

/* Reached from itrunc() only when a file grows or ends inside a block. */
daddr_t
bmap(struct inode *ip, daddr_t bn, int rwflg, int flags)
{
	(void)ip;
	(void)bn;
	(void)rwflg;
	(void)flags;
	panic("itrunc order: bmap");
	return 0;
}

static struct inode gate_ip;
static daddr_t blocks_of_file[NDADDR + NINDIR];
static unsigned nblocks_of_file;

/*
 * A regular file of ndirect direct blocks and, when nindirect is nonzero,
 * one single-indirect block holding nindirect more. The inode's flags are
 * clean, as they are after an update.
 */
static void
build(int ndirect, int nindirect, int async)
{
	struct fs *fs = &mount[0].m_filsys;
	daddr_t next = FIRST_DATA, *entries;
	int i;

	bzero((caddr_t)disk, sizeof(disk));
	bzero((caddr_t)buf_busy, sizeof(buf_busy));
	bzero((caddr_t)mount, sizeof(mount));
	bzero((caddr_t)&gate_ip, sizeof(gate_ip));
	nevents = 0;
	nblocks_of_file = 0;
	fs->fs_magic1 = FSMAGIC1;
	fs->fs_magic2 = FSMAGIC2;
	fs->fs_isize = FS_ISIZE;
	fs->fs_fsize = FS_FSIZE;
	fs->fs_flags = async ? MNT_ASYNC : 0;
	mount[0].m_dev = GATE_DEV;
	mount[0].m_flags = fs->fs_flags;
	gate_ip.i_number = GATE_INO;
	gate_ip.i_mode = IFREG | 0644;
	gate_ip.i_nlink = 1;
	for (i = 0; i < ndirect; i++) {
		IADDR_DB_SET(&gate_ip, i, next);
		blocks_of_file[nblocks_of_file++] = next++;
	}
	if (nindirect != 0) {
		daddr_t indirect = next++;

		IADDR_IB_SET(&gate_ip, 0, indirect);
		blocks_of_file[nblocks_of_file++] = indirect;
		entries = (daddr_t *)disk[indirect];
		for (i = 0; i < nindirect; i++) {
			entries[i] = next;
			blocks_of_file[nblocks_of_file++] = next++;
		}
	}
	gate_ip.i_size = (off_t)(ndirect + nindirect) * DEV_BSIZE;
	/* The on-disk copy starts out agreeing with the in-core inode. */
	((struct dinode *)disk[FS_ITOD(fs, GATE_INO)])[itoo(GATE_INO)].di_ic1 =
	    gate_ip.i_ic1;
	bcopy((caddr_t)gate_ip.i_addr,
	    (caddr_t)((struct dinode *)disk[FS_ITOD(fs, GATE_INO)] +
	    itoo(GATE_INO))->di_addr, NADDR * sizeof(daddr_t));
	u.u_error = 0;
	time.tv_sec = 1000;
}

static int
is_freed(daddr_t bn)
{
	unsigned i;

	for (i = 0; i < nevents; i++)
		if (events[i].kind == EV_FREE && events[i].blkno == bn)
			return 1;
	return 0;
}

/*
 * Truncate the case's file to length and hold the event order. The blocks
 * the truncation frees are those named by the events; the order is judged
 * on them, and the count of freed blocks on expected.
 */
static void
run_case(const char *name, int ndirect, int nindirect, int async,
    off_t length, unsigned expected_frees)
{
	unsigned i, first_free = EV_MAX, inode_writes = 0, frees = 0;
	int first_write = -1, a;
	struct dinode *dp;

	build(ndirect, nindirect, async);
	itrunc(&gate_ip, length, 0);
	HK_CHECK(u.u_error == 0);
	for (i = 0; i < nevents; i++) {
		if (events[i].kind == EV_FREE) {
			frees++;
			if (first_free == EV_MAX)
				first_free = i;
		} else if (events[i].blkno == FS_ITOD(&mount[0].m_filsys, GATE_INO)) {
			inode_writes++;
			if (first_write < 0 && events[i].kind == EV_WRITE_SYNC)
				first_write = (int)i;
		}
	}
	hk_note("%s: %u events, %u frees, inode writes %u, first sync inode "
	    "write at %d, first free at %u\n", name, nevents, frees,
	    inode_writes, first_write, first_free);
	HK_CHECK(frees == expected_frees);
	/* A synchronous inode write precedes every free. */
	HK_CHECK(first_write >= 0);
	HK_CHECK(first_free != EV_MAX && (unsigned)first_write < first_free);
	/* One inode write for the whole truncation. */
	HK_CHECK(inode_writes == 1);
	if (first_write < 0)
		return;
	/* The dinode that write carried holds the new size and none of the
	 * blocks the truncation releases. */
	dp = &events[first_write].dinode;
	HK_CHECK((off_t)dp->di_ic1.ic_size == length);
	for (i = 0; i < nblocks_of_file; i++) {
		if (!is_freed(blocks_of_file[i]))
			continue;
		for (a = 0; a < NADDR; a++)
			HK_CHECK(dp->di_addr[a] != blocks_of_file[i]);
	}
	/* The in-core inode ends clean: nothing is left to write. */
	HK_CHECK((gate_ip.i_flag & (IUPD | IACC | ICHG | IMOD)) == 0);
}

/*
 * The inode write fails: the device still holds every old address, so the
 * truncation reports the error, frees no block and leaves the inode dirty
 * for a later update.
 */
static void
run_failed_write(const char *name, int ndirect, int nindirect)
{
	unsigned i, frees = 0;

	build(ndirect, nindirect, 0);
	fail_write = EIO;
	itrunc(&gate_ip, 0, 0);
	for (i = 0; i < nevents; i++)
		if (events[i].kind == EV_FREE)
			frees++;
	hk_note("%s: error %d, %u frees\n", name, u.u_error, frees);
	HK_CHECK(u.u_error == EIO);
	HK_CHECK(frees == 0);
	HK_CHECK((gate_ip.i_flag & (IUPD | ICHG)) == (IUPD | ICHG));
	HK_CHECK(fail_write == 0);
	u.u_error = 0;
}

int
main(void)
{
	run_case("direct, truncate to 0", NDADDR, 0, 0, 0, NDADDR);
	run_case("direct, async mount", NDADDR, 0, 1, 0, NDADDR);
	run_case("direct, keep two blocks", NDADDR, 0, 0, 2 * DEV_BSIZE, 2);
	run_case("indirect, truncate to 0", NDADDR, 3, 0, 0, NDADDR + 4);
	run_case("indirect, keep the direct blocks", NDADDR, 3, 1,
	    NDADDR * DEV_BSIZE, 4);
	run_failed_write("direct, inode write fails", NDADDR, 0);
	run_failed_write("indirect, inode write fails", NDADDR, 3);
	return hk_verdict("itrunc order");
}
