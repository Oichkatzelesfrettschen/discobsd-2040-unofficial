/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dir.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/uio.h>

/*
 * Bmap defines the structure of file system storage
 * by returning the physical block number on a device given the
 * inode and the logical block number in a file.
 * When convenient, it also leaves the physical
 * block number of the next block of the file in rablock
 * for use in read-ahead.
 */
daddr_t
bmap(register struct inode *ip, daddr_t bn, int rwflg, int flags)
{
    register int i;
    register struct buf *bp;
    struct buf *nbp;
    int j, sh, error;
    daddr_t nb, *bap, ra;
#ifdef V7FS
    daddr_t pb;                 /* the indirect block bp holds */
#endif
    int async = (INODE_FILESYSTEM(ip)->fs_flags & MNT_ASYNC) &&
        (flags & B_SYNC) == 0;

    if (bn < 0) {
        u.u_error = EFBIG;
        return((daddr_t)0);
    }
    ra = rablock = 0;

    /*
     * blocks 0..NADDR-4 are direct blocks
     */
    if (bn < FS_NADDR(INODE_FILESYSTEM(ip))-3) {
        i = bn;
        nb = IADDR(ip, i);
        if (nb == 0) {
            if (rwflg == B_READ || (bp = balloc(ip, flags)) == NULL)
                return((daddr_t)-1);
            nb = FS_ALLOCBN(INODE_FILESYSTEM(ip), bp);
            /* Initialize synchronous allocations before publishing the pointer. */
            if (flags & B_SYNC) {
                error = bwrite(bp);
                if (error)
                    goto write_failed;
            } else
                bdwrite(bp);
            IADDR_SET(ip, i, nb);
            ip->i_flag |= IUPD|ICHG;
        }
        if (i < FS_NADDR(INODE_FILESYSTEM(ip))-4)
            rablock = IADDR(ip, i+1);
        return(nb);
    }

    /*
     * addresses NADDR-3, NADDR-2, and NADDR-1
     * have single, double, triple indirect blocks.
     * the first step is to determine
     * how many levels of indirection.
     */
    sh = 0;
    nb = 1;
    bn -= FS_NADDR(INODE_FILESYSTEM(ip))-3;
    for (j = 3;j > 0;j--) {
        sh += FS_NSHIFT(INODE_FILESYSTEM(ip));
        nb <<= FS_NSHIFT(INODE_FILESYSTEM(ip));
        if (bn < nb)
            break;
        bn -= nb;
    }
    if (j == 0) {
        u.u_error = EFBIG;
        return((daddr_t)0);
    }

    /*
     * fetch the first indirect block
     */
    nb = IADDR(ip, FS_NADDR(INODE_FILESYSTEM(ip))-j);
    if (nb == 0) {
        if (rwflg == B_READ || (bp = balloc(ip, flags | B_CLRBUF)) == NULL)
            return((daddr_t) -1);
        nb = FS_ALLOCBN(INODE_FILESYSTEM(ip), bp);
        /*
         * Write synchronously if requested so that indirect blocks
         * never point at garbage.
         */
        if (async)
            bdwrite(bp);
        else {
            error = bwrite(bp);
            if (error)
                goto write_failed;
        }
        IADDR_SET(ip, FS_NADDR(INODE_FILESYSTEM(ip))-j, nb);
        ip->i_flag |= IUPD|ICHG;
    }

    /*
     * fetch through the indirect blocks
     */
    for(;j <= 3;j++) {
        bp = bread(INODE_DEVICE(ip), FSBTODB(INODE_FILESYSTEM(ip), nb));
        if ((bp->b_flags & B_ERROR) || bp->b_resid) {
            brelse(bp);
            return((daddr_t)0);
        }
        bap = (daddr_t*) FSBADDR(INODE_FILESYSTEM(ip), bp, nb);
#ifdef V7FS
        pb = nb;
#endif
        sh -= FS_NSHIFT(INODE_FILESYSTEM(ip));
        i = (bn>>sh) & FS_NMASK(INODE_FILESYSTEM(ip));
        nb = bap[i];
        /*
         * calculate read-ahead
         */
        if ((u_int)i < FS_NINDIR(INODE_FILESYSTEM(ip))-1)
            ra = bap[i+1];
        if (nb == 0) {
#ifdef V7FS
            /*
             * The block balloc() returns, or a free-chain block it
             * reads, may be another part of the buffer held here.
             */
            if (FS_DBSHIFT(INODE_FILESYSTEM(ip))) {
                brelse(bp);
                bp = NULL;
            }
#endif
            if (rwflg == B_READ || (nbp = balloc(ip, flags | B_CLRBUF)) == NULL) {
                FS_BRELSE_HELD(bp);
                return((daddr_t) -1);
            }
            nb = FS_ALLOCBN(INODE_FILESYSTEM(ip), nbp);
            /* Metadata and explicitly synchronous data must precede the parent. */
            if (!async && (j < 3 || (flags & B_SYNC))) {
                error = bwrite(nbp);
                if (error) {
                    FS_BRELSE_HELD(bp);
                    goto write_failed;
                }
            } else
                bdwrite(nbp);
#ifdef V7FS
            if (bp == NULL) {
                bp = bread(INODE_DEVICE(ip),
                    FSBTODB(INODE_FILESYSTEM(ip), pb));
                if (bp->b_flags & B_ERROR) {
                    brelse(bp);
                    error = EIO;
                    goto write_failed;
                }
                bap = (daddr_t*) FSBADDR(INODE_FILESYSTEM(ip), bp, pb);
            }
#endif
            FS_REMAP(bap, bp);
            bap[i] = nb;
            bdwrite(bp);
        } else
            brelse(bp);
    }
    rablock = ra;
    return(nb);

write_failed:
    /* No pointer owns this allocation until its initialization succeeds. */
    free(ip, nb);
    u.u_error = error;
    return((daddr_t)-1);
}
