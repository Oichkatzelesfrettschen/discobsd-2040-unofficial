/* Exercise the actual pipe file-operation adapter with observed I/O flags. */
#include <sys/param.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>

static int observed_kind;
static int observed_flags;
static struct file *observed_file;
static struct uio *observed_uio;
#ifdef SINGLE_UFS_ROOT
struct mount mount[NMOUNT];
#endif

int
readp(struct file *fp, struct uio *uio, int flags)
{
    observed_kind = 1;
    observed_flags = flags;
    observed_file = fp;
    observed_uio = uio;
    return 17;
}

int
writep(struct file *fp, struct uio *uio, int flags)
{
    observed_kind = 2;
    observed_flags = flags;
    observed_file = fp;
    observed_uio = uio;
    return 23;
}

struct user u;
static struct inode *waiting_pipe;
static unsigned sleeps, writes, first_chunk;
static int transfer_error;
static unsigned transfer_limit;

void
sleep(caddr_t chan, int priority)
{
    if (chan != (caddr_t)waiting_pipe + 2 || priority != PPIPE ||
        (waiting_pipe->i_flag & ILOCKED) || ++sleeps > 1)
        __builtin_trap();
    waiting_pipe->i_size = 0;
}

void wakeup(caddr_t chan) { (void)chan; }
void psignal(struct proc *p, int sig) { (void)p; (void)sig; }
void selwakeup(struct proc *p, long coll) { (void)p; (void)coll; }

int
rwip(struct inode *ip, struct uio *io, int flags)
{
    unsigned n = MIN(transfer_limit, io->uio_resid);
    (void)flags;
    if (!(ip->i_flag & ILOCKED) || io->uio_offset != ip->i_size)
        __builtin_trap();
    if (writes++ == 0)
        first_chunk = n;
    ip->i_size += n;
    io->uio_resid -= n;
    return transfer_error;
}

#ifndef PIPE_RW_FUNCTION
#define PIPE_RW_FUNCTION "pipe_rw_function.c"
#endif
#include PIPE_RW_FUNCTION

static const struct fileops dispatch = { .fo_rw = pipe_rw };

int
main(void)
{
    struct fs filesystem = { 0 };
    struct inode inode = { 0 };
    struct file file = { 0 };
    struct uio uio = { 0 };
    unsigned direction, options, mount_sync;
    int expected_flags, expected_result, result;

#ifndef SINGLE_UFS_ROOT
    inode.i_fs = &filesystem;
#else
    (void)filesystem;
#endif
    file.f_data = (caddr_t)&inode;
    for (direction = 0; direction < 2; direction++) {
        for (options = 0; options < 8; options++) {
            for (mount_sync = 0; mount_sync < 2; mount_sync++) {
                file.f_flag = (options & 1 ? FNONBLOCK : 0) |
                    (options & 2 ? FFSYNC : 0) | (options & 4 ? FAPPEND : 0);
                INODE_FILESYSTEM(&inode)->fs_flags = mount_sync ? MNT_SYNCHRONOUS : 0;
                uio.uio_rw = direction ? UIO_WRITE : UIO_READ;
                expected_flags = options & 1 ? IO_NDELAY : 0;
                if (direction && ((options & 2) || mount_sync))
                    expected_flags |= IO_SYNC;
                observed_kind = observed_flags = 0;
                observed_file = NULL;
                observed_uio = NULL;
                result = (*dispatch.fo_rw)(&file, &uio);
                expected_result = direction ? 23 : 17;
                if (result != expected_result || observed_kind != (int)direction + 1 ||
                    observed_flags != expected_flags || observed_file != &file ||
                    observed_uio != &uio)
                    return 1 + (int)(direction * 16 + options * 2 + mount_sync);
            }
        }
    }
    file.f_data = (caddr_t)&inode;
    file.f_flag = FNONBLOCK;
    inode.i_count = 2;
    inode.i_size = 0;
    transfer_limit = PIPE_BUF;
    uio.uio_resid = PIPE_BUF;
    if (bounded_write(&file, &uio, 0) || uio.uio_resid || inode.i_size != PIPE_BUF)
        return 40;
    inode.i_size = 1;
    uio.uio_resid = PIPE_BUF;
    if (bounded_write(&file, &uio, 0) != EAGAIN ||
        uio.uio_resid != PIPE_BUF || inode.i_size != 1)
        return 41;
    file.f_flag = 0;
    waiting_pipe = &inode;
    writes = 0;
    if (bounded_write(&file, &uio, 0) || sleeps != 1 ||
        writes != 1 || first_chunk != PIPE_BUF || uio.uio_resid)
        return 42;
    file.f_flag = FNONBLOCK;
    inode.i_size = 0;
    uio.uio_resid = PIPE_BUF + 1;
    if (bounded_write(&file, &uio, 0) || uio.uio_resid != 1)
        return 43;
    transfer_limit = 0;
    transfer_error = ENOSPC;
    inode.i_size = 0;
    if (bounded_write(&file, &uio, 0) != ENOSPC || uio.uio_resid != 1)
        return 44;
    return 0;
}
