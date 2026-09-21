/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
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

typedef struct fblk *FBLKP;

/*
 * Allocate a block in the file system.
 *
 * alloc will obtain the next available free disk block from the
 * free list of the specified device.  The super block has up to
 * NICFREE remembered free blocks; the last of these is read to
 * obtain NICFREE more...
 */
struct buf *
balloc(struct inode *ip __unused, int flags)
{
    register struct fs *fs;
    register struct buf *bp;
    int async;
    daddr_t bno;

    fs = INODE_FILESYSTEM(ip);
    async = fs->fs_flags & MNT_ASYNC;

    while (fs->fs_flock)
        sleep((caddr_t)&fs->fs_flock, PINOD);
    do {
        if (fs->fs_nfree <= 0)
            goto nospace;
        if (fs->fs_nfree > NICFREE) {
            fserr (fs, "bad free count");
            goto nospace;
        }
        bno = fs->fs_free[--fs->fs_nfree];
        if (bno == 0)
            goto nospace;
    } while (badblock(fs, bno));
    if (fs->fs_nfree <= 0) {
        fs->fs_flock++;
        bp = bread(INODE_DEVICE(ip), bno);
        if (((bp->b_flags&B_ERROR) == 0) && (bp->b_resid==0)) {
            register struct fblk *fbp;

            fbp = (FBLKP) bp->b_addr;
            *((FBLKP)&fs->fs_nfree) = *fbp;
        }
        brelse(bp);
        /*
         * Write the superblock back, synchronously if requested,
         * so that the free list pointer won't point at garbage.
         * We can still end up with dups in free if we then
         * use some of the blocks in this freeblock, then crash
         * without a sync.
         */
        bp = getblk(INODE_DEVICE(ip), SUPERB);
        {
            register struct fs *fps;

            fps = (struct fs*) bp->b_addr;
            *fps = *fs;
            /*
             * fs_fmod and fs_time describe the image on the disk, so
             * they are set in the copy and the in-core superblock keeps
             * its own. Clearing fs_fmod in core would end the refill
             * with the filesystem marked clean while fs_tfree, every
             * dirty inode and every dirty data block still await
             * ufs_sync(); sync() skips a clean filesystem outright
             * (ufs_subr.c), so the flag has to survive this write.
             * Writing the flag set would instead leave a 1 on the disk,
             * which mountfs() keeps on a read-only mount and ufs_sync()
             * answers with panic("sync: rofs").
             */
            fps->fs_fmod = 0;
            fps->fs_time = time.tv_sec;
        }
        if (!async)
            bwrite(bp);
        else
            bdwrite(bp);
        fs->fs_flock = 0;
        wakeup((caddr_t)&fs->fs_flock);
        if (fs->fs_nfree <=0)
            goto nospace;
    }
    bp = getblk(INODE_DEVICE(ip), bno);
    bp->b_resid = 0;
    if (flags & B_CLRBUF)
        bzero (bp->b_addr, MAXBSIZE);
    fs->fs_fmod = 1;
    fs->fs_tfree--;
    return(bp);

nospace:
    /*
     * The counts are corrected in core, which is a superblock change like
     * any other: the flag carries it to ufs_sync(), whose write is the only
     * one that also flushes the inodes and data blocks the failed allocation
     * leaves behind.
     */
    fs->fs_nfree = 0;
    fs->fs_tfree = 0;
    fs->fs_fmod = 1;
    fserr (fs, "file system full");
    /*
     * THIS IS A KLUDGE...
     * SHOULD RATHER SEND A SIGNAL AND SUSPEND THE PROCESS IN A
     * STATE FROM WHICH THE SYSTEM CALL WILL RESTART
     */
    uprintf("\n%s: write failed, file system full\n", fs->fs_fsmnt);
    {
        register int i;

        for (i = 0; i < 5; i++)
            sleep((caddr_t)&lbolt, PRIBIO);
    }
    u.u_error = ENOSPC;
    return(NULL);
}

/*
 * Allocate an inode in the file system.
 *
 * Allocate an unused I node on the specified device.  Used with file
 * creation.  The algorithm keeps up to NICINOD spare I nodes in the
 * super block.  When this runs out, a linear search through the I list
 * is instituted to pick up NICINOD more.
 */
struct inode *
ialloc(struct inode *pip __unused)
{
    register struct fs *fs;
    register struct buf *bp;
    register struct inode *ip;
    int i;
    struct dinode *dp;
    ino_t ino;
    daddr_t adr;
    ino_t inobas;
    int first;
    char    *emsg = "no inodes free";

    fs = INODE_FILESYSTEM(pip);
    while (fs->fs_ilock)
        sleep((caddr_t)&fs->fs_ilock, PINOD);
loop:
    if (fs->fs_ninode > 0) {
        ino = fs->fs_inode[--fs->fs_ninode];
        if (ino <= ROOTINO)
            goto loop;
        ip = iget(INODE_DEVICE(pip), fs, ino);
        if (ip == NULL)
            return(NULL);
        if (ip->i_mode == 0) {
            bzero((caddr_t)ip->i_addr,sizeof(ip->i_addr));
            ip->i_flags = 0;
            fs->fs_fmod = 1;
            fs->fs_tinode--;
            return(ip);
        }
        /*
         * Inode was allocated after all.
         * Look some more.
         */
        iput(ip);
        goto loop;
    }
    fs->fs_ilock++;
    if (fs->fs_nbehind < 4 * NICINOD) {
        first = 1;
        ino = fs->fs_lasti;
#ifdef DIAGNOSTIC
        if (itoo(ino))
            panic("ialloc");
#endif
        adr = itod(ino);
    } else {
fromtop:
        first = 0;
        ino = 1;
        adr = SUPERB+1;
        fs->fs_nbehind = 0;
    }
    inobas = 0;
    for (; (u_long)adr < fs->fs_isize; adr++) {
        inobas = ino;
        bp = bread(INODE_DEVICE(pip), adr);
        if ((bp->b_flags & B_ERROR) || bp->b_resid) {
            brelse(bp);
            ino += INOPB;
            continue;
        }
        dp = (struct dinode*) bp->b_addr;
        for (i = 0;i < INOPB;i++) {
            if (dp->di_mode != 0)
                goto cont;
            if (ifind(INODE_DEVICE(pip), ino))
                goto cont;
            fs->fs_inode[fs->fs_ninode++] = ino;
            if (fs->fs_ninode >= NICINOD)
                break;
        cont:
            ino++;
            dp++;
        }
        brelse(bp);
        if (fs->fs_ninode >= NICINOD)
            break;
    }
    if (fs->fs_ninode < NICINOD && first) {
        /*
         * fs_inode[0 .. fs_ninode) holds distinct inode numbers: neither
         * ifree()'s append nor the pop at loop: tests for membership, so
         * a number recorded twice is handed out, found allocated by
         * iget(), put back and read again. This pass covers
         * [1, fs_isize) where the first covered [fs_lasti, fs_isize), a
         * superset, so it reaches every entry the first pass recorded;
         * keeping the count appends each of them a second time, and
         * discarding it loses no inode for that same reason.
         * tests/kernel/ialloc_test.c is the oracle.
         */
        fs->fs_ninode = 0;
        goto fromtop;
    }
    fs->fs_lasti = inobas;
    fs->fs_ilock = 0;
    wakeup((caddr_t)&fs->fs_ilock);
    if (fs->fs_ninode > 0)
        goto loop;
    fserr (fs, emsg);
    uprintf("\n%s: %s\n", fs->fs_fsmnt, emsg);
    u.u_error = ENOSPC;
    return(NULL);
}

/*
 * Free a block or fragment.
 *
 * Place the specified disk block back on the free list of the
 * specified device.
 */
void
free(struct inode *ip, daddr_t bno)
{
    register struct fs *fs;
    register struct buf *bp;
    struct fblk *fbp;

    fs = INODE_FILESYSTEM(ip);
    if (badblock (fs, bno)) {
        printf("bad block %D, ino %d\n", bno, ip->i_number);
        return;
    }
    while (fs->fs_flock)
        sleep((caddr_t)&fs->fs_flock, PINOD);
    if (fs->fs_nfree <= 0) {
        fs->fs_nfree = 1;
        fs->fs_free[0] = 0;
    }
    if (fs->fs_nfree >= NICFREE) {
        fs->fs_flock++;
        bp = getblk(INODE_DEVICE(ip), bno);
        fbp = (FBLKP) bp->b_addr;
        *fbp = *((FBLKP)&fs->fs_nfree);
        fs->fs_nfree = 0;
        if (fs->fs_flags & MNT_ASYNC)
            bdwrite(bp);
        else
            bwrite(bp);
        fs->fs_flock = 0;
        wakeup((caddr_t)&fs->fs_flock);
    }
    fs->fs_free[fs->fs_nfree++] = bno;
    fs->fs_tfree++;
    fs->fs_fmod = 1;
}

/*
 * Free an inode.
 *
 * Free the specified I node on the specified device.  The algorithm
 * stores up to NICINOD I nodes in the super block and throws away any more.
 */
void
ifree(struct inode *ip __unused, ino_t ino)
{
    register struct fs *fs;

    fs = INODE_FILESYSTEM(ip);
    fs->fs_tinode++;
    if (fs->fs_ilock)
        return;
    if (fs->fs_ninode >= NICINOD) {
        if (fs->fs_lasti > ino)
            fs->fs_nbehind++;
        return;
    }
    fs->fs_inode[fs->fs_ninode++] = ino;
    fs->fs_fmod = 1;
}

/*
 * Fserr prints the name of a file system with an error diagnostic.
 *
 * The form of the error message is:
 *  fs: error message
 */
void
fserr(struct fs *fp, char *cp)
{
    printf ("%s: %s\n", fp->fs_fsmnt, cp);
}
