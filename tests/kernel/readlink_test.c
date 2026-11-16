/* Exercise the production readlink syscall with controlled inode and I/O. */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/namei.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

struct user u;
static struct inode selected_inode;
static int lookup_missing;
static int io_error;
static unsigned io_calls;
static unsigned release_calls;

void kernel_readlink(void);

struct inode *
readlink_namei(struct nameidata *name)
{
    (void)name;
    if (lookup_missing) {
        u.u_error = ENOENT;
        return NULL;
    }
    return &selected_inode;
}

void
readlink_iput(struct inode *inode)
{
    HK_CHECK(inode == &selected_inode);
    release_calls++;
}

int
rdwri(enum uio_rw operation, struct inode *inode, caddr_t buffer,
    int length, off_t offset, int flags, int *residual)
{
    HK_CHECK(operation == UIO_READ);
    HK_CHECK(inode == &selected_inode);
    HK_CHECK(buffer != NULL);
    HK_CHECK(length == 8);
    HK_CHECK(offset == 0);
    HK_CHECK(flags == IO_UNIT);
    io_calls++;
    *residual = 3;
    return io_error;
}

static void
run_case(int mode, int missing, int failure)
{
    struct args { char *name; char *buffer; int count; };
    char name[] = "entry";
    char buffer[8];
    struct args *arguments;

    u = (struct user){0};
    arguments = (struct args *)u.u_arg;
    arguments->name = name;
    arguments->buffer = buffer;
    arguments->count = sizeof(buffer);
    u.u_rval = 123;
    selected_inode.i_mode = mode;
    lookup_missing = missing;
    io_error = failure;
    io_calls = release_calls = 0;
    kernel_readlink();
    if (missing) {
        HK_CHECK(u.u_error == ENOENT);
        HK_CHECK(u.u_rval == 123);
        HK_CHECK(release_calls == 0);
        HK_CHECK(io_calls == 0);
    } else if (mode != IFLNK) {
        HK_CHECK(u.u_error == EINVAL);
        HK_CHECK(u.u_rval == 123);
        HK_CHECK(release_calls == 1);
        HK_CHECK(io_calls == 0);
    } else {
        HK_CHECK(u.u_error == failure);
        HK_CHECK(u.u_rval == 5);
        HK_CHECK(release_calls == 1);
        HK_CHECK(io_calls == 1);
    }
}

int
main(void)
{
    run_case(IFREG, 1, 0);
    run_case(IFREG, 0, 0);
    run_case(IFLNK, 0, 0);
    run_case(IFLNK, 0, EIO);
    return hk_verdict("readlink error and residual");
}
