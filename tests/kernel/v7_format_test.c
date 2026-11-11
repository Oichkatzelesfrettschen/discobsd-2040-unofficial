/*
 * Host gate for the V7 format descriptor: the kernel's own inode layer
 * (ufs_alloc.c, ufs_bmap.c, ufs_inode.c, ufs_namei.c, sys_inode.c and
 * the codecs in ufs_v7.c), compiled with V7FS at ILP32, mounted on a
 * volume image through a four-buffer cache this file owns.
 *
 * Usage: v7_format_test IMAGE OUT EXPECT [PATH=HOSTFILE]...
 *
 * IMAGE is a V7 volume another implementation wrote (usr.sbin/v7fs,
 * the NetBSD core). Each PATH is read through namei() and rdwri() and
 * compared with HOSTFILE, so the kernel reads what the core wrote. The
 * gate then creates, extends, truncates, renames nothing, and removes
 * files and directories, writes the volume to OUT and records the bytes
 * each surviving file must hold under EXPECT, so the caller reads them
 * back with the core and an independent checker.
 *
 * The cache holds NBUF buffers as the kernel does and fails the gate
 * when a caller asks for a buffer it already holds, the fault two V7
 * blocks sharing one buffer make possible, or holds more than the pool.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

/* Host C library, declared here because the kernel headers own the names. */
int hk_host_load(const char *path, unsigned char **data, unsigned long *size);
int hk_host_store(const char *path, const unsigned char *data,
    unsigned long size);
void hk_host_exit(int status);
void *memcpy(void *, const void *, unsigned long);
void *memset(void *, int, unsigned long);
int memcmp(const void *, const void *, unsigned long);
char *strchr(const char *, int);

#define GATE_DEV    ((dev_t)0x0500)
#define POOL        NBUF

struct user u;
struct timeval time;
struct mount mount[NMOUNT];
struct inode inode[NINODE];
struct namecache namecache[NNAMECACHE];
struct file file[1];
int lbolt;
int securelevel;
const struct bdevsw bdevsw[1];
const struct cdevsw cdevsw[1];
const int nblkdev = 0;
const int nchrdev = 0;

static unsigned char *disk;
static unsigned long disk_bytes;

/*
 * Buffer cache.
 */
static struct buf pool[POOL];
static char pool_data[POOL][DEV_BSIZE];
static unsigned pool_age[POOL];
static unsigned pool_clock;
static unsigned held, held_peak;
static daddr_t fail_blkno = -1;    /* a buffer block whose reads fail */
static unsigned long bread_calls;
static int bad_frees = -1;         /* counts refused frees when >= 0 */

static void
die(const char *what, long a, long b)
{
    hk_note("v7_format_test: %s (%ld, %ld)", what, a, b);
    hk_host_exit(1);
}

static void
store(struct buf *bp)
{
    unsigned long at = (unsigned long)bp->b_blkno * DEV_BSIZE;

    if (bp->b_dev != GATE_DEV)
        die("write of a buffer with no identity", bp->b_blkno, 0);
    if (at + DEV_BSIZE / 2 > disk_bytes)
        die("write past the volume", bp->b_blkno, 0);
    /* A device that ends inside the buffer takes the sector it has. */
    memcpy(disk + at, bp->b_addr,
        at + DEV_BSIZE > disk_bytes ? DEV_BSIZE / 2 : DEV_BSIZE);
    bp->b_flags &= ~B_DELWRI;
}

static struct buf *
take(dev_t dev, daddr_t blkno)
{
    struct buf *bp, *victim = NULL;
    int i;

    if (dev != NODEV)
        for (i = 0; i < POOL; i++) {
            bp = &pool[i];
            if (bp->b_dev == dev && bp->b_blkno == blkno) {
                if (bp->b_flags & B_BUSY)
                    die("buffer requested while held", blkno, 0);
                goto got;
            }
        }
    for (i = 0; i < POOL; i++) {
        bp = &pool[i];
        if (bp->b_flags & B_BUSY)
            continue;
        if (victim == NULL || pool_age[i] < pool_age[victim - pool])
            victim = bp;
    }
    if (victim == NULL)
        die("every buffer is held", blkno, 0);
    bp = victim;
    if (bp->b_flags & B_DELWRI)
        store(bp);
    bp->b_flags = 0;
    bp->b_dev = dev;
    bp->b_blkno = blkno;
    /* geteblk() hands out a buffer with the residual its last use left. */
    if (dev != NODEV)
        bp->b_resid = 0;
got:
    bp->b_flags |= B_BUSY;
    bp->b_addr = pool_data[bp - pool];
    pool_age[bp - pool] = ++pool_clock;
    if (++held > held_peak)
        held_peak = held;
    return (bp);
}

struct buf *
getblk(dev_t dev, daddr_t blkno)
{
    return (take(dev, blkno));
}

struct buf *
geteblk(void)
{
    return (take(NODEV, 0));
}

struct buf *
bread(dev_t dev, daddr_t blkno)
{
    struct buf *bp = take(dev, blkno);
    unsigned long at = (unsigned long)blkno * DEV_BSIZE;

    bread_calls++;
    if (blkno == fail_blkno) {
        bp->b_flags |= B_ERROR;
        return (bp);
    }
    if (bp->b_flags & B_DONE)
        return (bp);
    if (blkno < 0 || at + DEV_BSIZE / 2 > disk_bytes) {
        bp->b_flags |= B_ERROR;
        return (bp);
    }
    if (at + DEV_BSIZE > disk_bytes) {
        /*
         * The device ends inside the buffer: the SD driver transfers
         * the sector it has and reports the absent one in b_resid.
         */
        memcpy(bp->b_addr, disk + at, DEV_BSIZE / 2);
        memset(bp->b_addr + DEV_BSIZE / 2, 0, DEV_BSIZE / 2);
        bp->b_resid = DEV_BSIZE / 2;
    } else
        memcpy(bp->b_addr, disk + at, DEV_BSIZE);
    bp->b_flags |= B_DONE;
    return (bp);
}

struct buf *
breada(dev_t dev, daddr_t blkno, daddr_t rablkno)
{
    (void)rablkno;
    return (bread(dev, blkno));
}

void
brelse(struct buf *bp)
{
    if ((bp->b_flags & B_BUSY) == 0)
        die("release of a free buffer", bp->b_blkno, 0);
    bp->b_flags &= ~(B_BUSY | B_ERROR);
    if (bp->b_dev == NODEV)
        bp->b_flags = 0;
    held--;
}

int
bwrite(struct buf *bp)
{
    store(bp);
    bp->b_flags |= B_DONE;
    brelse(bp);
    return (0);
}

void
bdwrite(struct buf *bp)
{
    if (bp->b_dev != GATE_DEV)
        die("delayed write of a buffer with no identity", 0, 0);
    bp->b_flags |= B_DELWRI | B_DONE;
    brelse(bp);
}

static void
flush(void)
{
    int i;

    if (held != 0)
        die("buffers still held at flush", held, 0);
    for (i = 0; i < POOL; i++)
        if (pool[i].b_flags & B_DELWRI)
            store(&pool[i]);
}

int bflush(dev_t dev) { (void)dev; flush(); return (0); }
void binval(dev_t dev) { (void)dev; }
int geterror(struct buf *bp) { (void)bp; return (EIO); }

/*
 * The rest of the kernel the inode layer names.
 */
void sleep(caddr_t chan, int pri) { (void)chan; (void)pri; }
void wakeup(caddr_t chan) { (void)chan; }
int tsleep(caddr_t chan, int pri, u_int timo)
{ (void)chan; (void)pri; (void)timo; return (0); }
void log(int level, char *fmt, ...) { (void)level; (void)fmt; }
void uprintf(char *fmt, ...) { (void)fmt; }
int access(struct inode *ip, int mode) { (void)ip; (void)mode; return (0); }
int suser(void) { return (1); }
void psignal(struct proc *p, int sig) { (void)p; (void)sig; }
void gsignal(int pgrp, int sig) { (void)pgrp; (void)sig; }
struct file *getf(int fd) { (void)fd; u.u_error = EBADF; return (NULL); }
int isdisk(dev_t dev, int type) { (void)dev; (void)type; return (0); }
int iskmemdev(dev_t dev) { (void)dev; return (0); }
int chrtoblk(dev_t dev) { (void)dev; return (NODEV); }
int ufs_mountedon(dev_t dev) { (void)dev; return (0); }
int vn_closefile(struct file *fp) { (void)fp; return (0); }
int ufs_sync(struct mount *mp) { (void)mp; return (0); }
int syncip(struct inode *ip) { (void)ip; return (0); }
int hk_setjmp(label_t *env) { (void)env; return (0); }

int
copyin(const caddr_t from, caddr_t to, u_int n)
{
    memcpy(to, from, n);
    return (0);
}

int
copystr(caddr_t from, caddr_t to, u_int max, u_int *done)
{
    u_int n;

    for (n = 0; n < max; n++) {
        to[n] = from[n];
        if (from[n] == '\0') {
            if (done)
                *done = n + 1;
            return (0);
        }
    }
    return (ENOENT);
}

int
uiomove(caddr_t cp, u_int n, struct uio *uio)
{
    struct iovec *iov = uio->uio_iov;

    if (n > iov->iov_len)
        die("uiomove past the request", n, iov->iov_len);
    if (uio->uio_rw == UIO_READ)
        memcpy(iov->iov_base, cp, n);
    else
        memcpy(cp, iov->iov_base, n);
    iov->iov_base += n;
    iov->iov_len -= n;
    uio->uio_resid -= n;
    uio->uio_offset += n;
    return (0);
}

int
badblock(struct fs *fp, daddr_t bn)
{
    if (bn < 0 || (u_long)bn < fp->fs_isize || (u_long)bn >= fp->fs_fsize) {
        if (bad_frees < 0)
            die("block outside the data area", bn, fp->fs_fsize);
        bad_frees++;
        return (1);
    }
    return (0);
}

/*
 * The operations a system call would perform, over the real namei(),
 * direnter(), dirremove(), ialloc(), rdwri() and itrunc().
 */
static struct mount *const mp = &mount[0];

static struct inode *
lookup(char *path)
{
    struct nameidata nd;

    u.u_error = 0;
    NAMEI_INIT_KERNEL(&nd, LOOKUP, FOLLOW, path);
    return (namei(&nd));
}

static int
kcreate(char *path, int mode, struct inode **ipp)
{
    struct nameidata nd;
    struct inode *ip, *dp;

    u.u_error = 0;
    NAMEI_INIT_KERNEL(&nd, CREATE, NOFOLLOW, path);
    ip = namei(&nd);
    if (ip != NULL) {
        iput(ip);
        return (EEXIST);
    }
    if (u.u_error)
        return (u.u_error);
    dp = nd.ni_pdir;
    ip = ialloc(dp);
    if (ip == NULL) {
        iput(dp);
        return (u.u_error);
    }
    ip->i_flag |= IACC|IUPD|ICHG;
    ip->i_mode = mode;
    ip->i_nlink = (mode & IFMT) == IFDIR ? 2 : 1;
    ip->i_uid = 3;
    ip->i_gid = 4;
    ip->i_size = 0;
    if ((mode & IFMT) == IFDIR) {
        /* mkdir's template, written before the name appears. */
        static const struct dirtemplate master = {
            0, 12, 1, ".",
            0, DIRBLKSIZ - 12, 2, "..",
        };
        struct dirtemplate t = master;

        t.dot_ino = ip->i_number;
        t.dotdot_ino = dp->i_number;
        u.u_error = rdwri(UIO_WRITE, ip, (caddr_t)&t, sizeof(t), (off_t)0,
            IO_UNIT|IO_SYNC, (int *)0);
        HK_CHECK(u.u_error == 0);
        ip->i_size = DIRBLKSIZ;
        dp->i_nlink++;
        dp->i_flag |= ICHG;
    }
    u.u_error = iupdat(ip, &time, &time, 1);
    HK_CHECK(u.u_error == 0);
    u.u_error = direnter(ip, &nd);     /* releases dp */
    if (u.u_error) {
        ip->i_nlink = 0;
        iput(ip);
        return (u.u_error);
    }
    *ipp = ip;
    return (0);
}

static int
kremove(char *path)
{
    struct nameidata nd;
    struct inode *ip, *dp;

    u.u_error = 0;
    NAMEI_INIT_KERNEL(&nd, DELETE, LOCKPARENT, path);
    ip = namei(&nd);
    if (ip == NULL)
        return (u.u_error);
    dp = nd.ni_pdir;
    if ((ip->i_mode & IFMT) == IFDIR) {
        if (!dirempty(ip, dp->i_number)) {
            iput(ip);
            iput(dp);
            return (ENOTEMPTY);
        }
        dp->i_nlink--;
        dp->i_flag |= ICHG;
        ip->i_nlink--;
    }
    if (dirremove(&nd)) {
        ip->i_nlink--;
        ip->i_flag |= ICHG;
    }
    if (dp == ip)
        irele(ip);
    else
        iput(ip);
    iput(dp);
    return (u.u_error);
}

/* Byte i of the pattern a file created with seed holds. */
static unsigned char
pattern(unsigned seed, unsigned long i)
{
    return ((unsigned char)(seed * 131 + i * 7 + (i >> 9) * 13 + (i >> 16)));
}

static unsigned char scratch[200 * 1024];
static const char *expect_dir;

static void
expect(const char *path, unsigned long size)
{
    char name[128];
    unsigned n = 0, i;

    for (i = 0; expect_dir[i] != '\0' && n < sizeof(name) - 40; i++)
        name[n++] = expect_dir[i];
    name[n++] = '/';
    for (i = 1; path[i] != '\0' && n < sizeof(name) - 1; i++)
        name[n++] = path[i] == '/' ? '%' : path[i];
    name[n] = '\0';
    HK_CHECK(hk_host_store(name, scratch, size) == 0);
}

static void
fill(char *path, unsigned seed, unsigned long size, unsigned chunk)
{
    struct inode *ip;
    unsigned long at, n;
    int error;

    HK_CHECK(size <= sizeof(scratch));
    error = kcreate(path, IFREG | 0644, &ip);
    HK_CHECK(error == 0);
    if (error)
        return;
    for (at = 0; at < size; at++)
        scratch[at] = pattern(seed, at);
    for (at = 0; at < size; at += n) {
        n = size - at < chunk ? size - at : chunk;
        error = rdwri(UIO_WRITE, ip, (caddr_t)scratch + at, n, (off_t)at,
            IO_UNIT, (int *)0);
        HK_CHECK(error == 0);
    }
    HK_CHECK((unsigned long)ip->i_size == size);
    iput(ip);
    expect(path, size);
}

static void
verify(char *path, const unsigned char *want, unsigned long size)
{
    static unsigned char got[sizeof(scratch)];
    struct inode *ip = lookup(path);
    int resid = -1;

    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    HK_CHECK((unsigned long)ip->i_size == size);
    if ((unsigned long)ip->i_size == size && size <= sizeof(got)) {
        /* Odd-sized reads cross block and buffer boundaries. */
        unsigned long at, n;

        for (at = 0; at < size; at += n) {
            n = size - at < 777 ? size - at : 777;
            HK_CHECK(rdwri(UIO_READ, ip, (caddr_t)got + at, n, (off_t)at,
                IO_UNIT, &resid) == 0 && resid == 0);
        }
        HK_CHECK(memcmp(got, want, size) == 0);
    }
    iput(ip);
}

/* The live names of a directory, as libc's readdir() would see them. */
static int
names(char *path, char *out, unsigned max)
{
    static char blk[DIRBLKSIZ];
    struct inode *ip = lookup(path);
    struct direct *d;
    unsigned n = 0;
    int count = 0, resid, at;
    off_t off;

    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return (-1);
    for (off = 0; off < ip->i_size; off += DIRBLKSIZ) {
        HK_CHECK(rdwri(UIO_READ, ip, blk, DIRBLKSIZ, off, 0, &resid) == 0);
        for (at = 0; at < DIRBLKSIZ - resid; at += d->d_reclen) {
            d = (struct direct *)(blk + at);
            HK_CHECK(d->d_reclen >= 12 && (d->d_reclen & 3) == 0);
            if (d->d_reclen < 12)
                break;
            if (d->d_ino == 0)
                continue;
            HK_CHECK(d->d_name[d->d_namlen] == '\0');
            if (n + d->d_namlen + 2 < max) {
                memcpy(out + n, d->d_name, d->d_namlen);
                n += d->d_namlen;
                out[n++] = ' ';
            }
            count++;
        }
    }
    out[n] = '\0';
    iput(ip);
    return (count);
}

static void
resize(char *path, unsigned seed, unsigned long size)
{
    struct inode *ip = lookup(path);
    unsigned long at;

    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    itrunc(ip, (off_t)size, 0);
    HK_CHECK(u.u_error == 0 && (unsigned long)ip->i_size == size);
    iput(ip);
    for (at = 0; at < size; at++)
        scratch[at] = pattern(seed, at);
    expect(path, size);
}

/*
 * The address escape: an address the 16-bit slot cannot hold lives in
 * the disk inode, survives the inode leaving and re-entering core, and
 * leaves its neighbors alone.
 */
static void
escape(void)
{
    struct inode *ip = lookup("/kernel-small");
    daddr_t before[13];
    ino_t ino;
    int i;

    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    ino = ip->i_number;
    for (i = 0; i < 13; i++)
        before[i] = v7_addr_get(ip, i);
    v7_addr_set(ip, 7, (daddr_t)0x00fedcbaL);
    v7_addr_set(ip, 12, (daddr_t)0xffffL);
    HK_CHECK(ip->i_a16[7] == 0xffff && ip->i_a16[12] == 0xffff);
    HK_CHECK(v7_addr_get(ip, 7) == 0x00fedcbaL);
    ip->i_flag |= ICHG;
    iput(ip);
    for (i = 0; i < NINODE; i++)       /* force the inode out of core */
        if (inode[i].i_number == ino && inode[i].i_count == 0)
            inode[i].i_number = 0;
    cinvalall();                       /* and its name out of the cache */
    ip = lookup("/kernel-small");
    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    HK_CHECK(v7_addr_get(ip, 7) == 0x00fedcbaL);
    HK_CHECK(v7_addr_get(ip, 12) == 0xffffL);
    for (i = 0; i < 13; i++)
        if (i != 7 && i != 12)
            HK_CHECK(v7_addr_get(ip, i) == before[i]);
    v7_addr_set(ip, 7, before[7]);
    v7_addr_set(ip, 12, before[12]);
    ip->i_flag |= ICHG;
    iput(ip);
}

/* Write every delayed buffer and forget what the cache holds. */
static void
evict(void)
{
    int i;

    flush();
    for (i = 0; i < POOL; i++) {
        pool[i].b_flags = 0;
        pool[i].b_dev = NODEV;
    }
}

/*
 * A V7 volume is foreign media and a device can fail: what the layer
 * does with an address outside the data area, an inode number outside
 * the table, an owner the format cannot hold, a wide address whose disk
 * inode does not read, and a device that ends inside a buffer.
 */
static void
hostile(void)
{
    struct fs *fs = &mp->m_filsys;
    static unsigned char saved[512], got[16];
    struct inode *ip;
    unsigned long bytes;
    daddr_t root, last, before;
    u_short keep;
    int i;

    /* A direct address that names the superblock. */
    ip = lookup("/kernel-small");
    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    evict();
    memcpy(saved, disk + 512, 512);
    keep = ip->i_a16[0];
    ip->i_a16[0] = 1;
    HK_CHECK(rdwri(UIO_WRITE, ip, (caddr_t)"x", 1, (off_t)0, IO_UNIT,
        (int *)0) == EIO);
    u.u_error = 0;
    HK_CHECK(rdwri(UIO_READ, ip, (caddr_t)got, 1, (off_t)0, IO_UNIT,
        (int *)0) == EIO);
    u.u_error = 0;
    evict();
    HK_CHECK(memcmp(saved, disk + 512, 512) == 0);
    ip->i_a16[0] = keep;

    /* A wide address whose disk inode does not read is no hole. */
    before = v7_addr_get(ip, 0);
    v7_addr_set(ip, 0, (daddr_t)0x00012345L);
    HK_CHECK(ip->i_a16[0] == 0xffff);
    evict();
    fail_blkno = FSBTODB(fs, FS_ITOD(fs, ip->i_number));
    HK_CHECK(rdwri(UIO_WRITE, ip, (caddr_t)"x", 1, (off_t)0, IO_UNIT,
        (int *)0) == EIO);
    fail_blkno = -1;
    u.u_error = 0;
    HK_CHECK(ip->i_a16[0] == 0xffff);
    HK_CHECK(v7_addr_get(ip, 0) == 0x00012345L);
    v7_addr_set(ip, 0, before);
    ip->i_flag |= ICHG;
    iput(ip);

    /* An indirect root outside the data area. */
    ip = lookup("/kernel-double");
    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    root = v7_addr_get(ip, 10);
    HK_CHECK(root != 0);
    keep = ip->i_a16[10];
    ip->i_a16[10] = (u_short)(fs->fs_isize - 1);
    HK_CHECK(rdwri(UIO_READ, ip, (caddr_t)got, 1, (off_t)5120, IO_UNIT,
        (int *)0) == EIO);
    u.u_error = 0;
    ip->i_a16[10] = keep;

    /*
     * The device ends after the first part of its last buffer, and that
     * part is the file's indirect block.
     */
    evict();
    last = (daddr_t)fs->fs_fsize - 2;
    memcpy(saved, disk + last * 512, 512);
    memcpy(disk + last * 512, disk + root * 512, 512);
    bytes = disk_bytes;
    disk_bytes -= 512;
    fs->fs_fsize--;
    ip->i_a16[10] = (u_short)last;
    HK_CHECK(rdwri(UIO_READ, ip, (caddr_t)got, sizeof(got), (off_t)5120,
        IO_UNIT, (int *)0) == 0);
    for (i = 0; i < (int)sizeof(got); i++)
        HK_CHECK(got[i] == pattern(7, 5120 + i));
    u.u_error = 0;
    ip->i_a16[10] = keep;
    evict();
    fs->fs_fsize++;
    disk_bytes = bytes;
    memcpy(disk + last * 512, saved, 512);
    iput(ip);

    /* An inode number past the table. */
    HK_CHECK(iget(GATE_DEV, fs,
        (ino_t)((fs->fs_isize - 2) * 8 + 1)) == NULL);
    HK_CHECK(u.u_error == EIO);
    u.u_error = 0;

    /* An owner the 16-bit field cannot hold creates nothing. */
    u.u_uid = 0x10000;
    i = kcreate("/wide-owner", IFREG | 0644, &ip);
    HK_CHECK(i == EOVERFLOW);
    if (i == 0)
        iput(ip);
    u.u_uid = 0;
    ip = lookup("/wide-owner");
    HK_CHECK(ip == NULL);
    if (ip != NULL)
        iput(ip);
    u.u_error = 0;
}

/*
 * More of the same: fields a V7 volume's writer chose that reach a
 * write, an allocation or a count, and the device extent the superblock
 * claims.
 */
static void
foreign(void)
{
    struct fs *fs = &mp->m_filsys;
    static unsigned char saved[512], got[16], raw[64];
    static struct inode scratch_inode;
    struct inode *ip;
    struct buf *bp;
    unsigned long bytes, calls, tfree;
    daddr_t last, kept;
    off_t size;
    u_short keep;
    int i, resid;

    /*
     * Truncation inside the indirect range, with the indirect root
     * naming the superblock: nothing is read, cleared or freed there.
     */
    ip = lookup("/kernel-double");
    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    evict();
    memcpy(saved, disk + 512, 512);
    size = ip->i_size;
    keep = ip->i_a16[10];
    tfree = fs->fs_tfree;
    ip->i_a16[10] = 1;
    bad_frees = 0;
    itrunc(ip, (off_t)(60 * 512), 0);
    HK_CHECK(u.u_error == EIO);
    u.u_error = 0;
    HK_CHECK(bad_frees == 0);
    bad_frees = -1;
    evict();
    HK_CHECK(memcmp(saved, disk + 512, 512) == 0);
    HK_CHECK(fs->fs_tfree == tfree);
    ip->i_a16[10] = keep;
    ip->i_size = size;
    ip->i_flag |= ICHG;

    /* A wide address that cannot be stored leaves the slot as it was. */
    keep = ip->i_a16[0];
    evict();
    fail_blkno = FSBTODB(fs, FS_ITOD(fs, ip->i_number));
    HK_CHECK(v7_addr_set(ip, 0, (daddr_t)0x00012345L) == EIO);
    fail_blkno = -1;
    u.u_error = 0;
    HK_CHECK(ip->i_a16[0] == keep);
    iput(ip);

    /*
     * The superblock claims one block more than the device holds. The
     * mount check refuses the volume; an allocation that reaches the
     * absent block anyway retires it and fails.
     */
    HK_CHECK(v7_extent(mp) == 0);
    evict();
    bytes = disk_bytes;
    last = (daddr_t)fs->fs_fsize - 1;
    HK_CHECK((last & 1) == 1 && fs->fs_nfree >= 2);
    disk_bytes -= 512;
    HK_CHECK(v7_extent(mp) == EINVAL);
    kept = fs->fs_free[fs->fs_nfree - 1];
    fs->fs_free[fs->fs_nfree - 1] = last;
    tfree = fs->fs_tfree;
    bp = balloc(rootdir, 0);
    HK_CHECK(bp == NULL);
    HK_CHECK(u.u_error == EIO);
    if (bp != NULL)
        brelse(bp);
    u.u_error = 0;
    HK_CHECK(fs->fs_tfree == tfree - 1);
    fs->fs_tfree = tfree;
    fs->fs_free[fs->fs_nfree++] = kept;
    /* An odd volume ends inside its last buffer and mounts. */
    fs->fs_fsize--;
    HK_CHECK(v7_extent(mp) == 0);
    fs->fs_fsize++;
    evict();
    disk_bytes -= 512;
    HK_CHECK(v7_extent(mp) == EINVAL);
    evict();
    disk_bytes = bytes;

    /* A free total of 0 beside a usable cache stays 0. */
    tfree = fs->fs_tfree;
    fs->fs_tfree = 0;
    bp = balloc(rootdir, 0);
    HK_CHECK(bp != NULL);
    HK_CHECK(fs->fs_tfree == 0);
    if (bp != NULL) {
        kept = mp->m_lastalloc;
        brelse(bp);
        free(rootdir, kept);
        HK_CHECK(fs->fs_tfree == 1);
    }
    fs->fs_tfree = tfree;
    u.u_error = 0;

    /*
     * An allocated inode whose size is no positive off_t, or whose
     * link count is negative in the format's signed field, does not
     * read; a free inode carrying the same bytes does.
     */
    memset(raw, 0, sizeof(raw));
    raw[0] = 0xa4; raw[1] = 0x81;               /* IFREG | 0644 */
    raw[2] = 1;
    HK_CHECK(v7_iread(&scratch_inode, raw) == 0);
    raw[11] = 0x80;                             /* size 0x80000000 */
    HK_CHECK(v7_iread(&scratch_inode, raw) == EIO);
    raw[11] = 0;
    raw[2] = 0; raw[3] = 0x80;                  /* link count 0x8000 */
    HK_CHECK(v7_iread(&scratch_inode, raw) == EIO);
    raw[2] = 0xff; raw[3] = 0x7f;
    HK_CHECK(v7_iread(&scratch_inode, raw) == 0);
    raw[0] = raw[1] = 0;
    raw[3] = 0x80; raw[11] = 0x80;
    HK_CHECK(v7_iread(&scratch_inode, raw) == 0);

    /* An inode number past the table is refused before any read. */
    calls = bread_calls;
    HK_CHECK(iget(GATE_DEV, fs, (ino_t)0xffff) == NULL);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(bread_calls == calls);
    u.u_error = 0;

    /*
     * A hole reads as zeros from a buffer whose last transfer was
     * short: geteblk() does not clear the residual.
     */
    HK_CHECK(kcreate("/foreign-hole", IFREG | 0644, &ip) == 0);
    HK_CHECK(rdwri(UIO_WRITE, ip, (caddr_t)"x", 1, (off_t)2048, IO_UNIT,
        (int *)0) == 0);
    iput(ip);
    ip = lookup("/foreign-hole");
    HK_CHECK(ip != NULL);
    if (ip == NULL)
        return;
    evict();
    for (i = 0; i < POOL; i++)
        pool[i].b_resid = DEV_BSIZE / 2;
    memset(got, 0xff, sizeof(got));
    resid = -1;
    HK_CHECK(rdwri(UIO_READ, ip, (caddr_t)got, sizeof(got), (off_t)0,
        IO_UNIT, &resid) == 0);
    HK_CHECK(resid == 0);
    for (i = 0; i < (int)sizeof(got); i++)
        HK_CHECK(got[i] == 0);
    u.u_error = 0;
    iput(ip);
    HK_CHECK(kremove("/foreign-hole") == 0);
}

int
main(int argc, char **argv)
{
    static char list[4096];
    struct inode *ip;
    char name[32];
    int i, error;

    if (argc < 4)
        return (2);
    if (hk_host_load(argv[1], &disk, &disk_bytes) != 0)
        return (2);
    expect_dir = argv[3];
    for (i = 0; i < POOL; i++)
        pool[i].b_dev = NODEV;
    time.tv_sec = 1000000000;
    u.u_rlimit[RLIMIT_FSIZE].rlim_cur = 0x7fffffff;
    ihinit();
    nchinit();

    /* Mount, as mountfs() does after the device opens. */
    {
        struct buf *bp = bread(GATE_DEV, (daddr_t)0);

        mp->m_dev = GATE_DEV;
        mp->m_inodp = (struct inode *)1;
        mp->m_geom = &fs_geom_table[FS_FORMAT_NATIVE];
        error = v7_sbload(&mp->m_filsys, (u_char *)bp->b_addr + 512);
        brelse(bp);
        HK_CHECK(error == 0);
        if (error)
            return (hk_verdict("v7 format"));
        mp->m_geom = &fs_geom_table[FS_FORMAT_V7];
        mp->m_filsys.fs_flags = MNT_V7;
        HK_CHECK(v7_extent(mp) == 0);
    }
    rootdir = iget(GATE_DEV, &mp->m_filsys, (ino_t)ROOTINO);
    HK_CHECK(rootdir != NULL);
    iunlock(rootdir);
    u.u_cdir = rootdir;
    rootdir->i_count++;

    /* What the other implementation wrote. */
    for (i = 4; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        unsigned char *want;
        unsigned long size;

        if (eq == NULL)
            return (2);
        *eq = '\0';
        if (hk_host_load(eq + 1, &want, &size) != 0)
            return (2);
        verify(argv[i], want, size);
    }

    /* Files across the direct, single and double indirect ranges. */
    fill("/kernel-empty", 1, 0, 512);
    fill("/kernel-small", 2, 1, 512);
    fill("/kernel-block", 3, 512, 512);
    fill("/kernel-odd", 4, 513, 100);
    fill("/kernel-direct", 5, 5120, 4096);
    fill("/kernel-single", 6, 5121 + 30000, 1000);
    fill("/kernel-double", 7, 71000 + 60000, 8192);
    fill("/doomed-double", 8, 90000, 3000);

    /* A name the format cannot hold, and one of exactly its length. */
    error = kcreate("/fifteen-letters", IFREG | 0644, &ip);
    HK_CHECK(error == ENAMETOOLONG);
    fill("/fourteen-chars", 9, 77, 512);

    /* Directories, and enough names to need a second directory block. */
    HK_CHECK(kcreate("/kdir", IFDIR | 0755, &ip) == 0);
    iput(ip);
    HK_CHECK(kcreate("/kdir/sub", IFDIR | 0755, &ip) == 0);
    iput(ip);
    fill("/kdir/sub/deep", 10, 3000, 512);

    /* A symbolic link is a file of the volume's block size. */
    HK_CHECK(kcreate("/klink", IFLNK | 0777, &ip) == 0);
    HK_CHECK(rdwri(UIO_WRITE, ip, "kdir/sub/deep", 13, (off_t)0, IO_UNIT,
        (int *)0) == 0);
    iput(ip);
    for (i = 0; i < 40; i++) {
        memcpy(name, "/kdir/n00", 10);
        name[7] = '0' + i / 10;
        name[8] = '0' + i % 10;
        fill(name, 100 + i, 10 + i, 512);
    }
    HK_CHECK(names("/kdir", list, sizeof(list)) == 2 + 1 + 40);
    HK_CHECK(hk_contains(list, ". .. sub n00 n01 "));

    /* Removal frees entries in place; a later name takes the first. */
    HK_CHECK(kremove("/kdir/n05") == 0);
    HK_CHECK(kremove("/kdir/n00") == 0);
    HK_CHECK(kremove("/kdir/n39") == 0);
    HK_CHECK(kremove("/kdir/n05") == ENOENT);
    fill("/kdir/again", 11, 600, 512);
    HK_CHECK(names("/kdir", list, sizeof(list)) == 2 + 1 + 38);
    HK_CHECK(hk_contains(list, ". .. sub again n01 "));
    HK_CHECK(kremove("/kdir/sub") == ENOTEMPTY);
    HK_CHECK(kremove("/doomed-double") == 0);

    /* Truncation inside each range. */
    resize("/kernel-double", 7, 70000);
    resize("/kernel-single", 6, 5000);
    resize("/kernel-direct", 5, 300);
    escape();
    hostile();
    foreign();

    /* What this run wrote, read back through the same layer. */
    for (i = 0; i < 300; i++)
        scratch[i] = pattern(5, i);
    verify("/kernel-direct", scratch, 300);
    for (i = 0; i < 3000; i++)
        scratch[i] = pattern(10, i);
    verify("/kdir/sub/deep", scratch, 3000);
    verify("/klink", scratch, 3000);

    /* Unmount: every inode, then the superblock. */
    iput(rootdir);
    irele(rootdir);
    for (i = 0; i < NINODE; i++)
        HK_CHECK(inode[i].i_count == 0);
    HK_CHECK(v7_sbwrite(mp, 1) == 0);
    flush();
    HK_CHECK(held_peak <= POOL);
    hk_note("v7 format: at most %u of %u buffers held", held_peak, POOL);
    HK_CHECK(hk_host_store(argv[2], disk, disk_bytes) == 0);
    return (hk_verdict("v7 format"));
}
