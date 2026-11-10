/* Exercise the actual pipe file-operation adapter with observed I/O flags. */
#include <sys/param.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/uio.h>

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
    return 0;
}
