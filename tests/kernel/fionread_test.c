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
#include <sys/swap.h>
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
static u_int provider_capture_bytes;
static unsigned char provider_capture[16];

static unsigned int copyin_calls;
static u_int copyin_bytes;
static int copyin_error;
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
	if (provider_capture_bytes != 0)
		bcopy(data, provider_capture, provider_capture_bytes);
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

	if (start < user_span_lower || start > user_span_upper)
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
	copyin_bytes = nbytes;
	if (copyin_error != 0)
		return copyin_error;
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
	provider_capture_bytes = 0;
	bzero(provider_capture, sizeof provider_capture);
	copyin_calls = 0;
	copyin_bytes = 0;
	copyin_error = 0;
	copyout_calls = 0;
	copyout_bytes = 0;
	copyout_error = 0;
	user_span_lower = (u_long)&user_storage[USER_LOWER];
	user_span_upper = (u_long)&user_storage[USER_UPPER];
}

static void
store_user_bytes(caddr_t address, u_int nbytes)
{
	u_int index;

	for (index = 0; index < nbytes; index++)
		address[index] = (char)(0x20 + index);
}

static int
same_bytes(const unsigned char *left, const unsigned char *right, u_int nbytes)
{
	u_int index;

	for (index = 0; index < nbytes; index++)
		if (left[index] != right[index])
			return 0;
	return 1;
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
	caddr_t zero_length_arguments[4];
	unsigned int index;
	u_int command;

	reset(DTYPE_INODE, IFREG, FREAD);
	command = IOC_IN | ((u_int)UNKNOWN_GROUP << 8) | UNKNOWN_NUMBER;
	zero_length_arguments[0] = (caddr_t)0;
	zero_length_arguments[1] = (caddr_t)(user_span_lower - 1);
	zero_length_arguments[2] = (caddr_t)user_span_upper;
	zero_length_arguments[3] = (caddr_t)(user_span_upper + 1);
	for (index = 0; index < sizeof zero_length_arguments /
	    sizeof zero_length_arguments[0]; index++) {
		reset(DTYPE_INODE, IFREG, FREAD);
		argument = zero_length_arguments[index];
		call_ioctl(FD, command, argument);
		HK_CHECK(u.u_error == ENOTTY && provider_calls == 1);
		HK_CHECK(provider_command == command && provider_data == argument);
		HK_CHECK(copyin_calls == 0 && copyout_calls == 0);
	}

	reset(DTYPE_INODE, IFREG, FREAD);
	argument = (caddr_t)&user_storage[USER_LOWER];
	command = IOC_IN | (1U << 24) | ((u_int)sizeof(int) << 16) |
	    ((u_int)UNKNOWN_GROUP << 8) | UNKNOWN_NUMBER;
	call_ioctl(FD, command, argument);
	HK_CHECK(u.u_error == ENOTTY);
	HK_CHECK(provider_calls == 1 && provider_command == command);
	HK_CHECK(provider_data == argument);
	HK_CHECK(copyin_calls == 0 && copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	argument = (caddr_t)&user_storage[USER_LOWER + 1];
	store_user_bytes(argument, sizeof(struct winsize));
	command = TIOCSWINSZ | (1U << 24);
	provider_succeeds_without_write = 1;
	call_ioctl(FD, command, argument);
	HK_CHECK(u.u_error == 0 && provider_calls == 1);
	HK_CHECK(provider_command == command && provider_data == argument);
	HK_CHECK(copyin_calls == 0 && copyout_calls == 0);
}

static void
input_ownership_cases(void)
{
	static const struct input_case {
		u_int command;
		u_int nbytes;
		u_int alignment;
	} cases[] = {
		{ TIOCSETD, sizeof(int), _Alignof(int) },
		{ TIOCSETP, sizeof(struct sgttyb), _Alignof(struct sgttyb) },
		{ TIOCSETN, sizeof(struct sgttyb), _Alignof(struct sgttyb) },
		{ TIOCFLUSH, sizeof(int), _Alignof(int) },
		{ TIOCSETC, sizeof(struct tchars), _Alignof(struct tchars) },
		{ TIOCLBIS, sizeof(int), _Alignof(int) },
		{ TIOCLBIC, sizeof(int), _Alignof(int) },
		{ TIOCLSET, sizeof(int), _Alignof(int) },
		{ TIOCSPGRP, sizeof(int), _Alignof(int) },
		{ TIOCSLTC, sizeof(struct ltchars), _Alignof(struct ltchars) },
		{ TIOCSTI, sizeof(char), _Alignof(char) },
		{ TIOCSWINSZ, sizeof(struct winsize), _Alignof(struct winsize) },
	};
	caddr_t unaligned = (caddr_t)&user_storage[USER_LOWER + 1];
	unsigned char original[sizeof(struct winsize)];
	u_int index;

	for (index = 0; index < sizeof cases / sizeof cases[0]; index++) {
		reset(DTYPE_INODE, IFREG, FREAD);
		store_user_bytes(unaligned, cases[index].nbytes);
		bcopy(unaligned, original, cases[index].nbytes);
		provider_capture_bytes = cases[index].nbytes;
		provider_succeeds_without_write = 1;
		call_ioctl(FD, cases[index].command, unaligned);
		HK_CHECK(u.u_error == 0 && provider_calls == 1);
		HK_CHECK(provider_command == cases[index].command);
		HK_CHECK(provider_data != unaligned);
		HK_CHECK((u_long)provider_data % cases[index].alignment == 0);
		HK_CHECK(copyin_calls == 1 && copyin_bytes == cases[index].nbytes);
		HK_CHECK(copyout_calls == 0);
		HK_CHECK(same_bytes(provider_capture, original,
		    cases[index].nbytes));
		HK_CHECK(same_bytes((unsigned char *)unaligned, original,
		    cases[index].nbytes));
	}

	reset(DTYPE_INODE, IFREG, FREAD);
	store_user_bytes(unaligned, sizeof(struct winsize));
	copyin_error = EFAULT;
	call_ioctl(FD, TIOCSWINSZ, unaligned);
	HK_CHECK(u.u_error == EFAULT && copyin_calls == 1);
	HK_CHECK(provider_calls == 0 && copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	call_ioctl(FD, TIOCFLUSH, NULL);
	HK_CHECK(u.u_error == EFAULT && provider_calls == 0);
	HK_CHECK(copyin_calls == 0 && copyout_calls == 0);
}

static void
inout_ownership_cases(void)
{
	caddr_t unaligned = (caddr_t)&user_storage[USER_LOWER + 1];
	off_t requested;

	reset(DTYPE_INODE, IFREG, FREAD);
	requested = 5;
	store_user_off(unaligned, requested);
	provider_capture_bytes = sizeof requested;
	provider_writes_before_error = 1;
	provider_error_value = 0;
	provider_succeeds_without_write = 1;
	call_ioctl(FD, TFALLOC, unaligned);
	HK_CHECK(u.u_error == 0 && provider_calls == 1);
	HK_CHECK(provider_data != unaligned);
	HK_CHECK((u_long)provider_data % _Alignof(off_t) == 0);
	HK_CHECK(copyin_calls == 1 && copyin_bytes == sizeof requested);
	HK_CHECK(copyout_calls == 1 && copyout_bytes == sizeof requested);
	HK_CHECK(load_user_off(unaligned) == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	requested = -3;
	store_user_off(unaligned, requested);
	provider_writes_before_error = 1;
	provider_error_value = 99;
	provider_error = EINVAL;
	call_ioctl(FD, TFALLOC, unaligned);
	HK_CHECK(u.u_error == EINVAL && provider_calls == 1);
	HK_CHECK(load_user_off(unaligned) == requested);
	HK_CHECK(copyin_calls == 1 && copyout_calls == 0);

	reset(DTYPE_INODE, IFREG, FREAD);
	requested = 5;
	store_user_off(unaligned, requested);
	copyin_error = EFAULT;
	call_ioctl(FD, TFALLOC, unaligned);
	HK_CHECK(u.u_error == EFAULT && copyin_calls == 1);
	HK_CHECK(provider_calls == 0 && copyout_calls == 0);
	HK_CHECK(load_user_off(unaligned) == requested);

	reset(DTYPE_INODE, IFREG, FREAD);
	requested = 5;
	store_user_off(unaligned, requested);
	provider_writes_before_error = 1;
	provider_error_value = 0;
	provider_succeeds_without_write = 1;
	copyout_error = EFAULT;
	call_ioctl(FD, TFALLOC, unaligned);
	HK_CHECK(u.u_error == EFAULT && provider_calls == 1);
	HK_CHECK(copyin_calls == 1 && copyout_calls == 1);
	HK_CHECK(load_user_off(unaligned) == requested);
}

int
main(void)
{
	provider_cases();
	failure_cases();
	command_geometry_cases();
	input_ownership_cases();
	inout_ownership_cases();
	return hk_verdict("ioctl and FIONREAD admission");
}
