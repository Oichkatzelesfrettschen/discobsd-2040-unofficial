/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/systm.h>

int updlock;        /* lock for sync */

/*
 * Call ufs_sync() for each mounted filesystem to write back modified inodes,
 * delayed data blocks and, when fs_fmod is set, the superblock. fs_fmod
 * records superblock changes only: an overwrite of an allocated block leaves
 * it clear while dirtying a buffer and the inode, so it cannot select which
 * filesystems to visit. A clean filesystem costs an inode-table and free-list
 * scan with no device I/O. A filesystem whose superblock lists are locked
 * (fs_ilock, fs_flock) is passed over and keeps its dirty state for the next
 * call, as do inodes syncinodes() finds locked and buffers bflush() finds
 * busy. Errors are not returned here: a failed write latches in the mount's
 * m_write_error, which ufs_sync() reports to fsync, IO_SYNC writes and
 * unmount.
 */
void
sync(void)
{
    register struct mount *mp;
    register struct fs *fs;
    int async;

    if (updlock)
        return;
    updlock++;
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++) {
        if (mp->m_inodp == NULL || mp->m_dev == NODEV)
            continue;
        fs = &mp->m_filsys;
        if (fs->fs_ilock || fs->fs_flock)
            continue;
        async = mp->m_flags & MNT_ASYNC;
        mp->m_flags &= ~MNT_ASYNC;
        ufs_sync(mp);
        mp->m_flags |= async;
    }
    updlock = 0;
}

/*
 * Flush all the blocks associated with an inode.
 * There are two strategies based on the size of the file;
 * large files are those with more than NBUF/2 blocks.
 * Large files
 *  Walk through the buffer pool and push any dirty pages
 *  associated with the device on which the file resides.
 * Small files
 *  Look up each block in the file to see if it is in the
 *  buffer pool writing any that are found to disk.
 *  Note that we make a more stringent check of
 *  writing out any block in the buffer pool that may
 *  overlap the inode. This brings the inode up to
 *  date with recent mods to the cooked device.
 */
int
syncip(struct inode *ip)
{
    register struct buf *bp;
    register struct buf *lastbufp;
    long lbn, lastlbn;
    register int s;
    daddr_t blkno, mapped_block;
    int error;

    lastlbn = howmany(ip->i_size, DEV_BSIZE);
    if (lastlbn < NBUF / 2) {
        for (lbn = 0; lbn < lastlbn; lbn++) {
            mapped_block = bmap(ip, lbn, B_READ, 0);
            if (mapped_block < 0)
                return (u.u_error ? u.u_error : EIO);
            blkno = fsbtodb(mapped_block);
            error = blkflush(INODE_DEVICE(ip), blkno);
            if (error)
                return (error);
        }
    } else {
        lastbufp = &buf[NBUF];
        for (bp = buf; bp < lastbufp; bp++) {
            if (bp->b_dev != INODE_DEVICE(ip) ||
                (bp->b_flags & B_DELWRI) == 0)
                continue;
            s = splbio();
            if (bp->b_flags & B_BUSY) {
                bp->b_flags |= B_WANTED;
                sleep((caddr_t)bp, PRIBIO+1);
                splx(s);
                bp--;
                continue;
            }
            splx(s);
            notavail(bp);
            error = bwrite(bp);
            if (error)
                return (error);
        }
    }
    ip->i_flag |= ICHG;
    return (iupdat(ip, &time, &time, 1));
}

/*
 * Check that a specified block number is in range.
 */
int
badblock(register struct fs *fp, daddr_t bn)
{
    if (bn < 0 || (u_long)bn < fp->fs_isize ||
        (u_long)bn >= fp->fs_fsize) {
        printf("bad block %D, ",bn);
        fserr(fp, "bad block");
        return (1);
    }
    return (0);
}

/*
 * Getfs maps a device number into a pointer to the incore super block.
 *
 * The algorithm is a linear search through the mount table. A
 * consistency check of the super block magic number is performed.
 *
 * panic: no fs -- the device is not mounted.
 *  this "cannot happen"
 */
struct fs *
getfs(dev_t dev)
{
    register struct mount *mp;
    register struct fs *fs;

    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++) {
        if (mp->m_inodp == NULL || mp->m_dev != dev)
            continue;
        fs = &mp->m_filsys;
        if (fs->fs_nfree > NICFREE || fs->fs_ninode > NICINOD) {
            fserr(fs, "bad count");
            fs->fs_nfree = fs->fs_ninode = 0;
        }
        return(fs);
    }
    printf("no fs on dev (%u,%u)\n", (u_int)major(dev),
        (u_int)minor(dev));
    return((struct fs *) NULL);
}
