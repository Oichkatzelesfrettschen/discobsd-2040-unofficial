/*
 * Host-width contract for futimes(2). The gate links the production syscall
 * definition and observes user-copy, descriptor, locking and publication
 * behavior at the target's ILP32 width.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/user.h>

_Static_assert(sizeof(struct timeval) == 8,
    "RP2040 timeval must contain two four-byte values");

struct user u;
struct timeval time;

static struct inode test_inode;
static struct timeval *expected_source;
static struct vattr published_attributes;
static int expected_descriptor;
static int copy_calls;
static int descriptor_calls;
static int lock_calls;
static int unlock_calls;
static int setattr_calls;
static int copy_error;
static int descriptor_error;
static int setattr_error;

extern void futimes(void);

int
copyin(const caddr_t source, caddr_t destination, u_int length)
{
    copy_calls++;
    HK_CHECK(source == (const caddr_t)expected_source);
    HK_CHECK(length == sizeof(struct timeval) * 2);
    if (copy_error != 0)
        return copy_error;
    bcopy(source, destination, length);
    return 0;
}

struct inode *
getinode(int descriptor)
{
    descriptor_calls++;
    HK_CHECK(descriptor == expected_descriptor);
    if (descriptor_error != 0) {
        u.u_error = descriptor_error;
        return NULL;
    }
    return &test_inode;
}

void
ilock(struct inode *inode)
{
    HK_CHECK(inode == &test_inode);
    lock_calls++;
}

void
iunlock(struct inode *inode)
{
    HK_CHECK(inode == &test_inode);
    unlock_calls++;
}

int
ufs_setattr(struct inode *inode, struct vattr *attributes)
{
    HK_CHECK(inode == &test_inode);
    HK_CHECK(lock_calls == 1);
    HK_CHECK(unlock_calls == 0);
    published_attributes = *attributes;
    setattr_calls++;
    return setattr_error;
}

static void
reset_fixture(void)
{
    u = (struct user){0};
    time = (struct timeval){ .tv_sec = 4567, .tv_usec = 890 };
    test_inode = (struct inode){0};
    expected_source = NULL;
    published_attributes = (struct vattr){0};
    expected_descriptor = 7;
    copy_calls = 0;
    descriptor_calls = 0;
    lock_calls = 0;
    unlock_calls = 0;
    setattr_calls = 0;
    copy_error = 0;
    descriptor_error = 0;
    setattr_error = 0;
}

static void
call_futimes(int descriptor, struct timeval *times)
{
    struct {
        int fd;
        struct timeval *tptr;
    } arguments = { descriptor, times };

    _Static_assert(sizeof(arguments) == sizeof(int) * 2,
        "futimes syscall arguments must occupy two words");
    bcopy(&arguments, u.u_arg, sizeof(arguments));
    futimes();
}

static void
explicit_times_are_staged_before_publication(void)
{
    struct timeval times[2] = {
        { .tv_sec = 1234, .tv_usec = 5 },
        { .tv_sec = 2345, .tv_usec = 6 },
    };

    reset_fixture();
    expected_source = times;
    call_futimes(expected_descriptor, times);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(copy_calls == 1);
    HK_CHECK(descriptor_calls == 1);
    HK_CHECK(lock_calls == 1 && unlock_calls == 1);
    HK_CHECK(setattr_calls == 1);
    HK_CHECK(published_attributes.va_atime == 1234);
    HK_CHECK(published_attributes.va_mtime == 2345);
    HK_CHECK((published_attributes.va_vaflags & VA_UTIMES_NULL) == 0);
}

static void
null_times_use_kernel_time(void)
{
    reset_fixture();
    call_futimes(expected_descriptor, NULL);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(copy_calls == 0);
    HK_CHECK(descriptor_calls == 1);
    HK_CHECK(setattr_calls == 1);
    HK_CHECK(published_attributes.va_atime == time.tv_sec);
    HK_CHECK(published_attributes.va_mtime == time.tv_sec);
    HK_CHECK((published_attributes.va_vaflags & VA_UTIMES_NULL) != 0);
}

static void
copy_failure_prevents_descriptor_lookup(void)
{
    struct timeval times[2] = { {0}, {0} };

    reset_fixture();
    expected_source = times;
    copy_error = EFAULT;
    call_futimes(expected_descriptor, times);
    HK_CHECK(u.u_error == EFAULT);
    HK_CHECK(copy_calls == 1);
    HK_CHECK(descriptor_calls == 0);
    HK_CHECK(lock_calls == 0 && unlock_calls == 0);
    HK_CHECK(setattr_calls == 0);
}

static void
descriptor_failure_prevents_publication(void)
{
    struct timeval times[2] = { {0}, {0} };

    reset_fixture();
    expected_source = times;
    descriptor_error = EBADF;
    call_futimes(expected_descriptor, times);
    HK_CHECK(u.u_error == EBADF);
    HK_CHECK(copy_calls == 1);
    HK_CHECK(descriptor_calls == 1);
    HK_CHECK(lock_calls == 0 && unlock_calls == 0);
    HK_CHECK(setattr_calls == 0);
}

static void
setattr_failure_unlocks_and_propagates(void)
{
    struct timeval times[2] = { {0}, {0} };

    reset_fixture();
    expected_source = times;
    setattr_error = EPERM;
    call_futimes(expected_descriptor, times);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(lock_calls == 1 && unlock_calls == 1);
    HK_CHECK(setattr_calls == 1);
}

int
main(void)
{
    explicit_times_are_staged_before_publication();
    null_times_use_kernel_time();
    copy_failure_prevents_descriptor_lookup();
    descriptor_failure_prevents_publication();
    setattr_failure_unlocks_and_propagates();
    return hk_verdict("futimes descriptor timestamp contract");
}
