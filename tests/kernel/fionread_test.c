/*
 * Host gate for ioctl command geometry and FIONREAD result ownership.
 *
 * The syscall must limit payload length to the public eight-bit size field,
 * admit its complete span, give every FIONREAD provider aligned kernel
 * storage, then publish the complete off_t only after provider success.  The
 * production inode provider supplies regular-file, directory and pipe results
 * behind a small observing adapter so the gate can distinguish kernel storage
 * from the caller-selected destination.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/kernel.h>
#include <sys/syscall_dispatch.h>

#ifdef HK_ILP32
_Static_assert(sizeof(off_t) == 4, "off_t is the target's four bytes");
_Static_assert(sizeof(void *) == 4, "pointers are the target's four bytes");
#endif
_Static_assert(sizeof(off_t) == sizeof(long),
    "FIONREAD and ino_ioctl use one result width");

#define FD 3
#define USER_BYTES 32
#define USER_LOWER 4
#define USER_UPPER 28
#define USER_FILL 0x5a
#define UNKNOWN_GROUP 'z'
#define UNKNOWN_NUMBER 1U

struct user u;
struct timeval time;
int lbolt;
int usechz;

static struct file test_file;
static struct inode test_inode;
static unsigned char user_storage[USER_BYTES];
static u_long user_span_lower;
static u_long user_span_upper;

static unsigned int provider_calls;
static u_int provider_command;
static caddr_t provider_data;
static int provider_error;
static int provider_writes_before_error;
static int provider_succeeds_without_write;
static off_t provider_error_value;

static unsigned int copyin_calls;
static unsigned int copyout_calls;
static u_int copyout_bytes;
static int copyout_error;

static int
stub_rw(struct file *file, struct uio *uio)
{
	(void)file;
	(void)uio;
	return EOPNOTSUPP;
}

static int
observed_ioctl(struct file *file, u_int command, char *data)
{
	provider_calls++;
	provider_command = command;
	provider_data = data;
	if (provider_writes_before_error)
		bcopy(&provider_error_value, data, sizeof provider_error_value);
	if (provider_error != 0)
		return provider_error;
	if (provider_succeeds_without_write)
		return 0;
	return ino_ioctl(file, command, data);
}

static int
stub_select(struct file *file, int flag)
{
	(void)file;
	(void)flag;
	return 0;
}

static int
stub_close(struct file *file)
{
	(void)file;
	return 0;
}

const struct fileops inodeops = {
	stub_rw, observed_ioctl, stub_select, stub_close
};
const struct fileops pipeops = {
	stub_rw, observed_ioctl, stub_select, stub_close
};
const struct fileops *const Fops[] = {
	NULL, &inodeops, NULL, &pipeops
};

static int
device_ioctl(dev_t device, u_int command, caddr_t data, int flags)
{
	(void)device;
	(void)command;
	(void)data;
	(void)flags;
	return ENOTTY;
}

const struct bdevsw bdevsw[] = {
	{ .d_ioctl = device_ioctl }
};
const struct cdevsw cdevsw[] = {
	{ .d_ioctl = device_ioctl }
};

int
fionread_setjmp(label_t *environment)
{
	(void)environment;
	return 0;
}

struct file *
getf(int fd)
{
	struct file *file;

	if ((unsigned int)fd >= NOFILE || (file = u.u_ofile[fd]) == NULL) {
		u.u_error = EBADF;
		return NULL;
	}
	return file;
}

int
fset(struct file *file, int bit, int value)
{
	(void)file;
	(void)bit;
	(void)value;
	return EOPNOTSUPP;
}

int
fsetown(struct file *file, int value)
{
	(void)file;
	(void)value;
	return EOPNOTSUPP;
}

int
fgetown(struct file *file, int *value)
{
	(void)file;
	(void)value;
	return EOPNOTSUPP;
}

int
baduaddr_span(caddr_t address, u_int nbytes)
{
	u_long start = (u_long)address;

	if (nbytes == 0)
		return 0;
	if (start < user_span_lower || start >= user_span_upper)
		return 1;
	return (u_long)nbytes > user_span_upper - start;
}

int
baduaddr(caddr_t address)
{
	return baduaddr_span(address, 1);
}

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	copyin_calls++;
	if (baduaddr_span(from, nbytes))
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	copyout_calls++;
	copyout_bytes = nbytes;
	if (copyout_error != 0)
		return copyout_error;
	if (baduaddr_span(to, nbytes))
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

static void
reset(int file_type, int mode, int flags)
{
	unsigned int index;

	bzero(&u, sizeof u);
	bzero(&test_file, sizeof test_file);
	bzero(&test_inode, sizeof test_inode);
	for (index = 0; index < sizeof user_storage; index++)
		user_storage[index] = USER_FILL;
	test_file.f_flag = flags;
	test_file.f_type = file_type;
	test_file.f_count = 1;
	test_file.f_data = (caddr_t)&test_inode;
	test_file.f_offset = 20;
	test_inode.i_mode = mode;
	test_inode.i_size = 73;
	u.u_ofile[FD] = &test_file;
	u.u_rval = 0x1234;
	provider_calls = 0;
	provider_command = 0;
	provider_data = NULL;
	provider_error = 0;
	provider_writes_before_error = 0;
	provider_succeeds_without_write = 0;
	provider_error_value = 0;
	copyin_calls = 0;
	copyout_calls = 0;
	copyout_bytes = 0;
	copyout_error = 0;
	user_span_lower = (u_long)&user_storage[USER_LOWER];
	user_span_upper = (u_long)&user_storage[USER_UPPER];
}

static off_t
load_user_off(caddr_t address)
{
	off_t value;

	bcopy(address, &value, sizeof value);
	return value;
}

static void
store_user_off(caddr_t address, off_t value)
{
	bcopy(&value, address, sizeof value);
}

static void
call_ioctl(int fd, u_int command, caddr_t argument)
{
	struct arguments {
		int fdes;
		long cmd;
		caddr_t cmarg;
	} *arguments = (struct arguments *)u.u_arg;

	arguments->fdes = fd;
	arguments->cmd = command;
	arguments->cmarg = argument;
	ioctl();
}

static void
success_case(int file_type, int mode, int flags, off_t expected)
{
	caddr_t unaligned = (caddr_t)&user_storage[USER_LOWER + 1];
	unsigned int after = USER_LOWER + 1 + sizeof(off_t);

	reset(file_type, mode, flags);
	call_ioctl(FD, FIONREAD, unaligned);
	HK_CHECK(u.u_error == 0);
	HK_CHECK(provider_calls == 1 && provider_command == FIONREAD);
	HK_CHECK(provider_data != unaligned);
	HK_CHECK((u_long)provider_data % _Alignof(off_t) == 0);
	HK_CHECK(copyin_calls == 0 && copyout_calls == 1);
	HK_CHECK(copyout_bytes == sizeof(off_t));
	HK_CHECK(load_user_off(unaligned) == expected);
	HK_CHECK(user_storage[USER_LOWER] == USER_FILL);
	HK_CHECK(user_storage[after] == USER_FILL);
}

static void
provider_cases(void)
{
	success_case(DTYPE_INODE, IFREG, FREAD, 53);
	success_case(DTYPE_INODE, IFDIR, FREAD, 53);
	success_case(DTYPE_PIPE, IFREG, FREAD, 53);
	success_case(DTYPE_PIPE, IFREG, FWRITE, 0);
}

static void
failure_cases(void)
{
	caddr_t unaligned = (caddr_t)&user_storage[USER_LOWER + 1];
	caddr_t crossing;

	reset(DTYPE_INODE, IFREG, FREAD);
	store_user_off(unaligned, 1234);
	provider_error = EIO;
	provider_writes_before_error = 1;
	provider_error_value = 999;
	call_ioctl(FD, FIONREAD, unaligned);
	HK_CHECK(u.u_error == EIO && provider_calls == 1);
	HK_CHECK(load_user_off(unaligned) == 1234);
	HK_CHECK(copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	store_user_off(unaligned, 1234);
	copyout_error = EFAULT;
	call_ioctl(FD, FIONREAD, unaligned);
	HK_CHECK(u.u_error == EFAULT && provider_calls == 1);
	HK_CHECK(load_user_off(unaligned) == 1234);
	HK_CHECK(copyout_calls == 1);

	reset(DTYPE_INODE, IFREG, FREAD);
	store_user_off(unaligned, 1234);
	provider_succeeds_without_write = 1;
	call_ioctl(FD, FIONREAD, unaligned);
	HK_CHECK(u.u_error == 0 && provider_calls == 1);
	HK_CHECK(load_user_off(unaligned) == 0);
	HK_CHECK(copyout_calls == 1);

	reset(DTYPE_INODE, IFREG, FREAD);
	crossing = (caddr_t)(user_span_upper - sizeof(off_t) + 1);
	call_ioctl(FD, FIONREAD, crossing);
	HK_CHECK(u.u_error == EFAULT && provider_calls == 0);
	HK_CHECK(copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, 0);
	call_ioctl(FD, FIONREAD, unaligned);
	HK_CHECK(u.u_error == EBADF && provider_calls == 0);
	HK_CHECK(copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	call_ioctl(FD + 1, FIONREAD, unaligned);
	HK_CHECK(u.u_error == EBADF && provider_calls == 0);
	HK_CHECK(copyout_calls == 0);
}

static void
command_geometry_cases(void)
{
	caddr_t argument = (caddr_t)&user_storage[USER_LOWER];
	u_int command;

	reset(DTYPE_INODE, IFREG, FREAD);
	command = IOC_IN | ((u_int)UNKNOWN_GROUP << 8) | UNKNOWN_NUMBER;
	call_ioctl(FD, command, argument);
	HK_CHECK(u.u_error == ENOTTY && provider_calls == 1);
	HK_CHECK(provider_command == command && provider_data == argument);
	HK_CHECK(copyin_calls == 0 && copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	command = IOC_IN | (1U << 24) | ((u_int)sizeof(int) << 16) |
	    ((u_int)UNKNOWN_GROUP << 8) | UNKNOWN_NUMBER;
	call_ioctl(FD, command, argument);
	HK_CHECK(u.u_error == ENOTTY);
	HK_CHECK(provider_calls == 1 && provider_command == command);
	HK_CHECK(provider_data == argument);
	HK_CHECK(copyin_calls == 0 && copyout_calls == 0);
}

int
main(void)
{
	provider_cases();
	failure_cases();
	command_geometry_cases();
	return hk_verdict("ioctl and FIONREAD admission");
}
