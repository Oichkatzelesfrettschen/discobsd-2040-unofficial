/*
 * Production clock, rwip, syncip and sync paths with deterministic I/O
 * failures.
 */
#include "hostkern.h"
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/storage_stats.h>

struct user u;
struct timeval time;
int hz = 100, usechz = 10000, lbolt, adjdelta;
struct mount mount[NMOUNT];
struct inode inode[NINODE];
struct buf buf[NBUF];
const struct cdevsw cdevsw[1] = {{0}};
static struct inode node;
static char data[sizeof(struct fs) + DEV_BSIZE];
static unsigned writes, delayed, asynchronous, flushes, updates;
static int write_error, flush_error, update_error, mount_error;
static unsigned mount_flushes;
/* Delayed buffers bflush() would find, and the block superblock writes use. */
static unsigned dirty_buffers, superblock_gets;
/* iupdat()'s wait argument: syncip() waits, syncinodes() delays. */
static int expected_wait = 1;

void gate_sync(void);

int copyin(const caddr_t src, caddr_t dst, u_int n)
{ bcopy(src, dst, n); return 0; }
int copyout(const caddr_t src, caddr_t dst, u_int n)
{ bcopy(src, dst, n); return 0; }
int suser(void) { return 1; }
void psignal(struct proc *p, int signal) { (void)p; (void)signal; }
void sleep(caddr_t chan, int priority) { (void)chan; (void)priority; }
void itrunc(struct inode *ip, off_t size, int flags)
{ (void)ip; (void)size; (void)flags; }
daddr_t bmap(struct inode *ip, daddr_t block, int rw, int flags)
{ (void)ip; (void)block; (void)rw; (void)flags; return 12; }
struct buf *getblk(dev_t dev, daddr_t block)
{ (void)dev; if (block == SUPERB) superblock_gets++; return &buf[0]; }
struct buf *bread(dev_t dev, daddr_t block) { return getblk(dev, block); }
struct buf *breada(dev_t dev, daddr_t block, daddr_t next)
{ (void)next; return getblk(dev, block); }
struct buf *geteblk(void) { return &buf[0]; }
void brelse(struct buf *bp) { (void)bp; }
int bwrite(struct buf *bp) { if (bp->b_flags & B_ASYNC) asynchronous++; else writes++; return write_error; }
void bdwrite(struct buf *bp) { (void)bp; delayed++; }
int bflush(dev_t dev)
{ (void)dev; mount_flushes++; writes += dirty_buffers; dirty_buffers = 0; return mount_error; }
void iput(struct inode *ip)
{ if (ip >= inode && ip < inode + NINODE) { ip->i_flag &= ~ILOCKED; ip->i_count--; } }
int blkflush(dev_t dev, daddr_t block)
{ (void)dev; (void)block; flushes++; return flush_error; }
/*
 * Like the production iupdat(), a failure leaves the dirty flags set, and a
 * delayed update leaves the inode block for bflush() to write.
 */
int iupdat(struct inode *ip, struct timeval *at, struct timeval *mt, int wait)
{
    (void)at; (void)mt; HK_CHECK(wait == expected_wait); updates++;
    if (update_error)
        return update_error;
    if (!wait && (ip->i_flag & (IUPD|IACC|ICHG|IMOD)))
        dirty_buffers++;
    ip->i_flag &= ~(IUPD|IACC|ICHG|IMOD);
    return 0;
}
int uiomove(caddr_t cp, u_int n, struct uio *io)
{ (void)cp; io->uio_resid -= n; io->uio_offset += n; return 0; }

static void clock_cases(void)
{
    static const long values[] = { -32769, -32768, 0, 32767, 32768 };
    struct { struct timeval *delta, *old; } *args = (void *)u.u_arg;
    struct timeval delta, old;
    unsigned i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        time.tv_sec = 100000; time.tv_usec = 0; lbolt = 0;
        adjdelta = 7; u.u_error = 0;
        delta.tv_sec = values[i] / hz;
        delta.tv_usec = (values[i] % hz) * usechz;
        args->delta = &delta; args->old = &old;
        adjtime();
        HK_CHECK(u.u_error == 0);
        if (values[i] >= -32768 && values[i] <= 32767) {
            HK_CHECK(adjdelta == values[i]);
            HK_CHECK(time.tv_sec == 100000);
            HK_CHECK(old.tv_sec == 0 && old.tv_usec == 70000);
        } else {
            HK_CHECK(adjdelta == 0);
            HK_CHECK(lbolt >= 0 && lbolt < hz);
            HK_CHECK((time.tv_sec - 100000) * hz + lbolt == values[i]);
        }
        HK_CHECK(hk_ipl == 0);
    }
    time.tv_sec = 2147483647L; lbolt = 0; adjdelta = 9;
    delta.tv_sec = 2147483647L; delta.tv_usec = 0;
    args->delta = &delta; args->old = 0; u.u_error = 0;
    adjtime();
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(time.tv_sec == 2147483647L && adjdelta == 9);
    HK_CHECK(hk_ipl == 0);
}

static int write_case(int kind, int mount_flags, int flags, unsigned count)
{
    struct uio io = {0};
    u = (struct user){0}; node = (struct inode){0};
    mount[0] = (struct mount){0}; buf[0] = (struct buf){0};
    writes = delayed = asynchronous = flushes = updates = mount_flushes = 0;
    node.i_mode = kind | 0600; node.i_size = DEV_BSIZE;
    node.i_fs = &mount[0].m_filsys; node.i_dev = 1;
    mount[0].m_filsys.fs_flags = mount_flags;
    u.u_rlimit[RLIMIT_FSIZE].rlim_cur = 100000;
    buf[0].b_addr = data;
    io.uio_rw = UIO_WRITE; io.uio_resid = count;
    return rwip(&node, &io, flags);
}

static void io_cases(void)
{
    unsigned i;
    int mounts[] = { 0, MNT_ASYNC, MNT_SYNCHRONOUS };
    for (i = 0; i < sizeof(mounts) / sizeof(mounts[0]); i++) {
        write_error = flush_error = update_error = 0;
        HK_CHECK(write_case(IFREG, mounts[i], IO_SYNC, 17) == 0);
        HK_CHECK(writes == 1 && delayed == 0 && asynchronous == 0);
        HK_CHECK(flushes == 1 && updates == 1 && mount_flushes == 1);
        HK_CHECK(write_case(IFREG, mounts[i], IO_SYNC, DEV_BSIZE) == 0);
        HK_CHECK(writes == 1 && flushes == 1 && updates == 1);
    }
    HK_CHECK(write_case(IFREG, 0, 0, 17) == 0);
    HK_CHECK(delayed == 1 && writes == 0 && updates == 0);
    HK_CHECK(write_case(IFREG, 0, 0, DEV_BSIZE) == 0);
    HK_CHECK(asynchronous == 1 && writes == 0);
    HK_CHECK(write_case(IFDIR, 0, 0, 17) == 0);
    HK_CHECK(writes == 1 && updates == 1);
    HK_CHECK(write_case(IFDIR, MNT_ASYNC, 0, 17) == 0);
    HK_CHECK(delayed == 1 && updates == 0);
    HK_CHECK(write_case(IFDIR, MNT_ASYNC, IO_SYNC, 17) == 0);
    HK_CHECK(writes == 1 && updates == 1);
    write_error = EIO;
    HK_CHECK(write_case(IFREG, 0, IO_SYNC, DEV_BSIZE * 2) == EIO);
    HK_CHECK(writes == 1 && flushes == 0 && updates == 0);
    write_error = 0; flush_error = EIO;
    HK_CHECK(write_case(IFREG, 0, IO_SYNC, 17) == EIO);
    HK_CHECK(writes == 1 && updates == 0);
    flush_error = 0; update_error = EIO;
    HK_CHECK(write_case(IFREG, 0, IO_SYNC, 17) == EIO);
    HK_CHECK(updates == 1);
    update_error = 0; mount_error = EIO;
    HK_CHECK(write_case(IFREG, 0, IO_SYNC, 17) == EIO);
    HK_CHECK(mount_flushes == 1);
}

/*
 * sync() over a mount whose superblock is clean. An overwrite of an
 * allocated block leaves fs_fmod clear while it dirties a data buffer and
 * the inode, so dirty_buffers and inode[3] stand for that state.
 */
static void sync_setup(unsigned dirty_data, int dirty_inode)
{
    unsigned i;

    u = (struct user){0}; mount[0] = (struct mount){0}; buf[0] = (struct buf){0};
    for (i = 0; i < NINODE; i++)
        inode[i] = (struct inode){0};
    writes = delayed = asynchronous = flushes = updates = mount_flushes = 0;
    superblock_gets = 0;
    write_error = flush_error = update_error = mount_error = 0;
    expected_wait = 0;
    buf[0].b_addr = data;
    mount[0].m_inodp = &node; mount[0].m_dev = 1;
    dirty_buffers = dirty_data;
    inode[3].i_fs = &mount[0].m_filsys; inode[3].i_dev = 1;
    inode[3].i_count = 1;
    inode[3].i_flag = dirty_inode ? IUPD | ICHG : 0;
}

static void sync_cases(void)
{
    struct fs *fs = &mount[0].m_filsys;
    unsigned data_dirty;
    int inode_dirty;

    /* Neither, data, metadata, both: all eligible state is written. */
    for (data_dirty = 0; data_dirty <= 1; data_dirty++)
        for (inode_dirty = 0; inode_dirty <= 1; inode_dirty++) {
            sync_setup(data_dirty, inode_dirty);
            gate_sync();
            HK_CHECK(updates == (unsigned)inode_dirty);
            HK_CHECK(writes == data_dirty + (unsigned)inode_dirty);
            HK_CHECK(dirty_buffers == 0 && inode[3].i_flag == 0);
            HK_CHECK(inode[3].i_count == 1);
            HK_CHECK(superblock_gets == 0 && fs->fs_fmod == 0);
        }

    /* A modified superblock is still written, and only then. */
    sync_setup(0, 0); fs->fs_fmod = 1;
    gate_sync();
    HK_CHECK(superblock_gets == 1 && writes == 1 && fs->fs_fmod == 0);

    /* A locked inode is skipped, stays dirty and is written next time. */
    sync_setup(0, 1); inode[3].i_flag |= ILOCKED;
    gate_sync();
    HK_CHECK(updates == 0 && writes == 0);
    HK_CHECK(inode[3].i_flag == (ILOCKED | IUPD | ICHG));
    inode[3].i_flag &= ~ILOCKED;
    gate_sync();
    HK_CHECK(updates == 1 && writes == 1 && inode[3].i_flag == 0);

    /* Locked superblock lists pass the mount over with its state kept. */
    sync_setup(1, 1); fs->fs_ilock = 1;
    gate_sync();
    HK_CHECK(mount_flushes == 0 && updates == 0 && writes == 0);
    HK_CHECK(dirty_buffers == 1 && inode[3].i_flag == (IUPD | ICHG));
    fs->fs_ilock = 0; fs->fs_flock = 1;
    gate_sync();
    HK_CHECK(mount_flushes == 0 && dirty_buffers == 1);
    fs->fs_flock = 0;
    gate_sync();
    HK_CHECK(writes == 2 && dirty_buffers == 0 && inode[3].i_flag == 0);

    /*
     * A failed inode update keeps its flags, the buffers are flushed anyway,
     * and the superblock waits; the next call retries the inode.
     */
    sync_setup(1, 1); fs->fs_fmod = 1; update_error = EIO;
    gate_sync();
    HK_CHECK(updates == 1 && mount_flushes == 1 && writes == 1);
    HK_CHECK(inode[3].i_flag == (IUPD | ICHG));
    HK_CHECK(superblock_gets == 0 && fs->fs_fmod == 1);
    update_error = 0;
    gate_sync();
    HK_CHECK(updates == 2 && writes == 3 && superblock_gets == 1);
    HK_CHECK(inode[3].i_flag == 0 && fs->fs_fmod == 0);

    /* A latched write error survives the call that cannot report it. */
    sync_setup(0, 0); mount[0].m_write_error = EIO;
    gate_sync();
    HK_CHECK(mount[0].m_write_error == EIO && writes == 0);

    /* A clean read-only mount writes nothing and does not panic. */
    sync_setup(0, 0); mount[0].m_flags = MNT_RDONLY; fs->fs_ronly = 1;
    gate_sync();
    HK_CHECK(writes == 0 && superblock_gets == 0);

    /* The mount's MNT_ASYNC setting is restored after the flush. */
    sync_setup(1, 0); mount[0].m_flags = MNT_ASYNC;
    gate_sync();
    HK_CHECK(writes == 1 && mount[0].m_flags == MNT_ASYNC);

    expected_wait = 1;
}

static void counter_cases(void)
{
    struct storage_stats stats = {0};
    storage_stats_add(&stats, STORAGE_MAP_WRITES, 4);
    HK_CHECK(stats.value[STORAGE_MAP_WRITES] == 4 && !stats.saturated);
    stats.value[STORAGE_MAP_WRITES] = UINT32_MAX - 1;
    storage_stats_add(&stats, STORAGE_MAP_WRITES, 1);
    HK_CHECK(stats.value[STORAGE_MAP_WRITES] == UINT32_MAX && !stats.saturated);
    storage_stats_add(&stats, STORAGE_MAP_WRITES, 1);
    HK_CHECK(stats.value[STORAGE_MAP_WRITES] == UINT32_MAX && stats.saturated);
    HK_CHECK(stats.value[STORAGE_ROOT_WRITES] == 0);
}

int main(void)
{
    clock_cases(); io_cases(); sync_cases(); counter_cases();
    return hk_verdict("time/io/sync/storage");
}
