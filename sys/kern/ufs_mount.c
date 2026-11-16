/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/proc.h>

/*
 * A successful device lookup returns a referenced, unlocked block inode, so
 * mount and unmount can retain the unit guard through their driver calls.
 * A failed lookup records u.u_error and returns NULL.
 */
static struct inode *
getmdev(dev_t *pdev, caddr_t fname)
{
    register dev_t dev;
    register struct inode *ip;
    struct  nameidata nd;
    register struct nameidata *ndp = &nd;

    if (!suser())
        return NULL;
    NAMEI_INIT_USER (ndp, LOOKUP, FOLLOW, fname);
    ip = namei(ndp);
    if (ip == NULL) {
        if (u.u_error == ENOENT)
            u.u_error = ENODEV; /* needs translation */
        return NULL;
    }
    if ((ip->i_mode&IFMT) != IFBLK) {
        iput(ip);
        u.u_error = ENOTBLK;
        return NULL;
    }
    dev = (dev_t)ip->i_rdev;
    if (major(dev) >= nblkdev) {
        iput(ip);
        u.u_error = ENXIO;
        return NULL;
    }
    IUNLOCK(ip);
    *pdev = dev;
    return ip;
}

void
mount_updname(struct fs *fs, char *on, char *from __unused, int lenon, int lenfrom __unused)
{
    int fslen = lenon;
#ifndef SINGLE_UFS_ROOT
    struct  mount   *mp;
#else
#endif

    bzero (fs->fs_fsmnt, sizeof (fs->fs_fsmnt));
    if (fslen < 0)
        fslen = 0;
    else if ((u_int)fslen >= sizeof (fs->fs_fsmnt))
        fslen = sizeof (fs->fs_fsmnt) - 1;
    bcopy (on, fs->fs_fsmnt, fslen);
#ifndef SINGLE_UFS_ROOT
    mp = (struct mount*) ((int) fs - offsetof (struct mount, m_filsys));
    bzero (mp->m_mnton, sizeof (mp->m_mnton));
    bzero (mp->m_mntfrom, sizeof (mp->m_mntfrom));
    if (lenon < 0)
        lenon = 0;
    else if ((u_int)lenon >= sizeof (mp->m_mnton))
        lenon = sizeof (mp->m_mnton) - 1;
    bcopy (on, mp->m_mnton, lenon);
    if (lenfrom < 0)
        lenfrom = 0;
    else if ((u_int)lenfrom >= sizeof (mp->m_mntfrom))
        lenfrom = sizeof (mp->m_mntfrom) - 1;
    bcopy (from, mp->m_mntfrom, lenfrom);
#endif
}

void
smount(void)
{
    register struct a {
        char    *fspec;
        char    *freg;
        int flags;
    } *uap = (struct a *)u.u_arg;
    dev_t dev = 0;
    register struct inode *ip;
    struct inode *device_inode;
    register struct fs *fs;
    struct  nameidata nd;
    struct  nameidata *ndp = &nd;
    struct  mount   *mp;
    u_int lenon, lenfrom;
    int error = 0;
    char    mnton[MNAMELEN], mntfrom[MNAMELEN];

    device_inode = getmdev(&dev, uap->fspec);
    if (device_inode == NULL)
        return;
    bdevlock(device_inode);
    NAMEI_INIT_USER (ndp, LOOKUP, FOLLOW, uap->freg);
    if ((ip = namei(ndp)) == NULL)
        goto release_device;
    if ((ip->i_mode&IFMT) != IFDIR) {
        error = ENOTDIR;
        goto    cmnout;
    }
    copystr (uap->freg, mnton, sizeof (mnton) - 1, &lenon);
    copystr (uap->fspec, mntfrom, sizeof (mntfrom) - 1, &lenfrom);

    if (uap->flags & MNT_UPDATE) {
        fs = INODE_FILESYSTEM(ip);
        mp = (struct mount *)
            ((int)fs - offsetof(struct mount, m_filsys));
        if (ip->i_number != ROOTINO) {
            error = EINVAL;     /* Not a mount point */
            goto cmnout;
        }
        /*
         * Check that the device passed in is the same one that is in the mount
         * table entry for this mount point.
         */
        if (dev != mp->m_dev) {
            error = EINVAL;     /* not right mount point */
            goto cmnout;
        }
        /*
         * This is where the RW to RO transformation would be done.  It is, for now,
         * too much work to port pages of code to do (besides which most
         * programs get very upset at having access yanked out from under them).
         */
        if (fs->fs_ronly == 0 && (uap->flags & MNT_RDONLY)) {
            error = EPERM;      /* ! RW to RO updates */
            goto cmnout;
        }
        /*
         * However, going from RO to RW is easy.  Then merge in the new
         * flags (async, sync, nodev, etc) passed in from the program.
         */
        if (fs->fs_ronly && ((uap->flags & MNT_RDONLY) == 0)) {
            fs->fs_ronly = 0;
            mp->m_flags &= ~MNT_RDONLY;
        }
#define _MF (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC | MNT_ASYNC | MNT_SYNCHRONOUS | MNT_NOATIME)
        mp->m_flags &= ~_MF;
        mp->m_flags |= (uap->flags & _MF);
#undef _MF
        iput(ip);
        u.u_error = 0;
        goto updname;
    } else {
        /*
         * This is where a new mount (not an update of an existing mount point) is
         * done.
         *
         * The directory being mounted on can have no other references AND can not
         * currently be a mount point.  Mount points have an inode number of (you
         * guessed it) ROOTINO which is 2.
         */
        if (ip->i_count != 1 || (ip->i_number == ROOTINO)) {
            error = EBUSY;
            goto cmnout;
        }
        fs = mountfs (dev, uap->flags, ip);
        if (fs == 0)
            goto release_device;
    }
    /*
     * Lastly, both for new mounts and updates of existing mounts, update the
     * mounted-on and mounted-from fields.
     */
updname:
    mount_updname(fs, mnton, mntfrom, lenon, lenfrom);
    u.u_error = 0;
    goto release_device;
cmnout:
    iput(ip);
    u.u_error = error;
release_device:
    error = u.u_error;
    bdevunlock(device_inode);
    irele(device_inode);
    u.u_error = error;
}

/*
 * Mount a filesystem on the given directory inode.
 *
 * Runtime callers hold bdevlock() through publication or cleanup. The initial
 * root mount runs before concurrent processes and has no device inode yet.
 */
struct fs *
mountfs(dev_t dev, int flags, struct inode *ip)
{
    register struct mount *mp = 0;
    struct buf *tp = 0;
    register struct fs *fs;
    register int error;
    int ronly = flags & MNT_RDONLY;
    int needclose = 0;
    int priority;

    error = bdev_driver_open(dev, ronly ? FREAD : (FREAD | FWRITE));
    if (error)
        goto out;

    needclose = 1;
    tp = bread (dev, SUPERB);
    if (tp->b_flags & B_ERROR)
        goto out;
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp != 0 && dev == mp->m_dev) {
            mp = 0;
            error = EBUSY;
            needclose = 0;
            goto out;
        }
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp == 0 && mp->m_device_lock == 0)
            goto found;
    mp = 0;
    error = EMFILE;     /* needs translation */
    goto out;
found:
    priority = splbio();
    mp->m_write_error = bdev_write_error(dev);
    mp->m_dev = dev;
    mp->m_inodp = ip;   /* reserve slot */
    splx(priority);
    fs = &mp->m_filsys;
#ifdef V7FS
    mp->m_geom = &fs_geom_table[FS_FORMAT_NATIVE];
    if (flags & MNT_V7) {
        /* The V7 superblock is the second 512 bytes of the volume. */
        error = v7_sbload(fs, (u_char *)tp->b_addr + 512);
        brelse (tp);
        tp = 0;
        if (error)
            goto out;
        mp->m_geom = &fs_geom_table[FS_FORMAT_V7];
        error = v7_extent(mp);
        if (error)
            goto out;
    } else
#endif
    {
    bcopy (tp->b_addr, (caddr_t)fs, sizeof(struct fs));
    brelse (tp);
    tp = 0;
    if (fs->fs_magic1 != FSMAGIC1 || fs->fs_magic2 != FSMAGIC2) {
        error = EINVAL;
        goto out;
    }
    }
    fs->fs_ronly = (ronly != 0);
    if (ronly == 0)
        fs->fs_fmod = 1;
    fs->fs_ilock = 0;
    fs->fs_flock = 0;
    fs->fs_nbehind = 0;
    fs->fs_lasti = 1;
    fs->fs_flags = flags;
    if (ip) {
        ip->i_flag |= IMOUNT;
        cacheinval(ip);
        IUNLOCK(ip);
    }
    return (fs);
out:
    if (error == 0)
        error = EIO;
    if (ip)
        iput(ip);
    if (mp)
        mp->m_inodp = 0;
    if (tp)
        brelse(tp);
    if (needclose) {
        /* Last close only: a descriptor on dev keeps the driver open. */
        if (iflush(dev) == 0)
            dev_driver_close(dev,
                ronly? FREAD : FREAD|FWRITE, S_IFBLK);
        binval(dev);
    }
    u.u_error = error;
    return (0);
}

/*
 * Ask the driver whether the medium behind dev is gone, so that the
 * write-back unmount1() could not finish will never be delivered. A driver
 * without DIOCMEDIAGONE, or one that fails it, answers no.
 */
static int
media_gone(dev_t dev)
{
    int gone = 0;

    if ((*bdevsw[major(dev)].d_ioctl)(dev, DIOCMEDIAGONE,
        (caddr_t)&gone, 0) != 0)
        return (0);
    return (gone != 0);
}

static int
unmount1(caddr_t fname)
{
    dev_t dev = 0;
    register struct mount *mp;
    register struct inode *ip;
    struct inode *device_inode;
    register int error;
    int aflag, nopen, flush_error, close_error = 0;

    device_inode = getmdev(&dev, fname);
    if (device_inode == NULL)
        return u.u_error;
    bdevlock(device_inode);
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp != NULL && dev == mp->m_dev)
            goto found;
    error = EINVAL;
    goto release_device;
found:
    bdev_mount_take_lock(mp, device_inode);
    irele(device_inode);
    device_inode = NULL;
    nchinval (dev); /* flush the name cache */
    aflag = mp->m_flags & MNT_ASYNC;
    mp->m_flags &= ~MNT_ASYNC;  /* Don't want async when unmounting */
    error = ufs_sync(mp);
    flush_error = bdevsync(dev);
    if (error == 0)
        error = flush_error;
    if (error) {
        /*
         * A write error on media that is still there stays fatal. Media
         * that was removed cannot take the write-back; ufs_sync() has
         * already dropped the buffers it failed to write, binval() below
         * invalidates the rest, and the latched error describes writes
         * nothing can deliver.
         */
        if (! media_gone(dev)) {
            mp->m_flags |= aflag;
            goto release_device;
        }
        mp->m_write_error = 0;
    }

    nopen = iflush(dev);
    if (nopen < 0) {
        mp->m_flags |= aflag;
        error = EBUSY;
        goto release_device;
    }
    ip = mp->m_inodp;
    ip->i_flag &= ~IMOUNT;
    irele(ip);
    mp->m_inodp = 0;
    /*
     * iflush() counted the open block-special inodes that name dev. A driver
     * sees only the last close, the rule closei() applies, so a descriptor
     * that is still open releases the device when it closes.
     */
    if (nopen == 0)
        close_error = dev_driver_close(dev, 0, S_IFBLK);
    binval(dev);
    error = close_error;
release_device:
    if (device_inode != NULL) {
        bdevunlock(device_inode);
        irele(device_inode);
    } else
        bdev_mount_unlock(mp);
    return error;
}

void
umount(void)
{
    struct a {
        char    *fspec;
    } *uap = (struct a *)u.u_arg;

    u.u_error = unmount1 (uap->fspec);
}
