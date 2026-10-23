/*
 * Host gate for file-owner and file-status control in kern_descrip.c and
 * sys_generic.c.
 *
 * The helper cases pin conversion between the public negative-process-group
 * convention and the nonnegative TIOCSPGRP/TIOCGPGRP payload.  The syscall
 * cases pin descriptor and access checks, user-copy staging, driver failure,
 * and publication only after a successful representable result.  The status
 * cases require driver acceptance before flag publication and exercise the
 * compensating nonblocking transition when F_SETFL cannot set async mode.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/syscall_dispatch.h>

#ifdef HK_ILP32
_Static_assert(sizeof(int) == 4, "int is the target's four bytes");
_Static_assert(sizeof(long) == 4, "long is the target's four bytes");
_Static_assert(sizeof(void *) == 4, "pointers are the target's four bytes");
#endif

#define FD 3

struct user u;
struct timeval time;
int lbolt;
int usechz;

static struct file owner_file;
static struct proc found_process;
static int found_pid;
static unsigned int pfind_calls;

static unsigned int driver_calls;
static u_int driver_command;
static int driver_error;
static int driver_output;
static int driver_writes_output;
static int driver_input;
#define DRIVER_HISTORY 4
static u_int driver_commands[DRIVER_HISTORY];
static int driver_inputs[DRIVER_HISTORY];
static int driver_flags[DRIVER_HISTORY];
static int driver_errors[DRIVER_HISTORY];

static unsigned int copyin_calls;
static unsigned int copyout_calls;
static int copyin_error;
static int copyout_error;

/* Extra bytes keep every host pointer expression inside the actual array. */
static unsigned char user_storage[16];
static u_long user_span_lower;
static u_long user_span_upper;

static int
stub_rw(struct file *file, struct uio *uio)
{
	(void)file;
	(void)uio;
	return EOPNOTSUPP;
}

static int
stub_ioctl(struct file *file, u_int command, char *data)
{
	unsigned int call_index = driver_calls;
	int error;

	driver_calls++;
	driver_command = command;
	if (command == TIOCGPGRP && driver_writes_output)
		bcopy(&driver_output, data, sizeof driver_output);
	if (command == TIOCSPGRP || command == FIONBIO || command == FIOASYNC)
		bcopy(data, &driver_input, sizeof driver_input);
	if (call_index < DRIVER_HISTORY) {
		driver_commands[call_index] = command;
		driver_inputs[call_index] = driver_input;
		driver_flags[call_index] = file->f_flag;
	}
	error = driver_error;
	if (call_index < DRIVER_HISTORY && driver_errors[call_index] != 0)
		error = driver_errors[call_index];
	return error;
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
	stub_rw, stub_ioctl, stub_select, stub_close
};
const struct fileops pipeops = {
	stub_rw, stub_ioctl, stub_select, stub_close
};

struct proc *
pfind(int pid)
{
	pfind_calls++;
	if (pid == found_pid)
		return &found_process;
	return NULL;
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
	if (copyout_error != 0)
		return copyout_error;
	if (baduaddr_span(to, nbytes))
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

static void
reset(int flags)
{
	bzero(&u, sizeof u);
	bzero(&owner_file, sizeof owner_file);
	bzero(&found_process, sizeof found_process);
	bzero(user_storage, sizeof user_storage);
	owner_file.f_flag = flags;
	owner_file.f_type = DTYPE_INODE;
	owner_file.f_count = 1;
	u.u_ofile[FD] = &owner_file;
	u.u_rval = 0x1234;
	found_pid = 71;
	found_process.p_pid = found_pid;
	found_process.p_pgrp = 9;
	pfind_calls = 0;
	driver_calls = 0;
	driver_command = 0;
	driver_error = 0;
	driver_output = 0;
	driver_writes_output = 0;
	driver_input = -1;
	bzero(driver_commands, sizeof driver_commands);
	bzero(driver_inputs, sizeof driver_inputs);
	bzero(driver_flags, sizeof driver_flags);
	bzero(driver_errors, sizeof driver_errors);
	copyin_calls = 0;
	copyout_calls = 0;
	copyin_error = 0;
	copyout_error = 0;
	user_span_lower = (u_long)&user_storage[4];
	user_span_upper = (u_long)&user_storage[12];
	hk_reset_output();
}

static void
store_user_int(caddr_t address, int value)
{
	bcopy(&value, address, sizeof value);
}

static int
load_user_int(caddr_t address)
{
	int value;

	bcopy(address, &value, sizeof value);
	return value;
}

static void
call_fcntl(int fd, int command, int argument)
{
	struct arguments {
		int fdes;
		int cmd;
		int arg;
	} *arguments = (struct arguments *)u.u_arg;

	arguments->fdes = fd;
	arguments->cmd = command;
	arguments->arg = argument;
	fcntl();
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
helper_set_cases(void)
{
	int error;

	reset(FREAD | FWRITE);
	error = fsetown(&owner_file, -SHRT_MAX);
	HK_CHECK(error == 0);
	HK_CHECK(driver_calls == 1 && driver_command == TIOCSPGRP);
	HK_CHECK(driver_input == SHRT_MAX && pfind_calls == 0);

	reset(FREAD | FWRITE);
	error = fsetown(&owner_file, 0);
	HK_CHECK(error == 0 && driver_calls == 1 && driver_input == 0);

	reset(FREAD | FWRITE);
	error = fsetown(&owner_file, found_pid);
	HK_CHECK(error == 0 && pfind_calls == 1 && driver_calls == 1);
	HK_CHECK(driver_input == found_process.p_pgrp);

	reset(FREAD | FWRITE);
	error = fsetown(&owner_file, found_pid + 1);
	HK_CHECK(error == ESRCH && pfind_calls == 1 && driver_calls == 0);

	reset(FREAD | FWRITE);
	found_process.p_pgrp = -1;
	error = fsetown(&owner_file, found_pid);
	HK_CHECK(error == EINVAL && driver_calls == 0);

	reset(FREAD | FWRITE);
	error = fsetown(&owner_file, -SHRT_MAX - 1);
	HK_CHECK(error == EINVAL && driver_calls == 0 && pfind_calls == 0);

	reset(FREAD | FWRITE);
	driver_error = EPERM;
	error = fsetown(&owner_file, -12);
	HK_CHECK(error == EPERM && driver_calls == 1 && driver_input == 12);

	/* The bound check must precede negation, including under UBSan. */
	reset(FREAD | FWRITE);
	error = fsetown(&owner_file, INT_MIN);
	HK_CHECK(error == EINVAL && driver_calls == 0 && pfind_calls == 0);
}

static void
helper_get_cases(void)
{
	int error;
	int result;

	reset(FREAD | FWRITE);
	result = 91;
	driver_error = ENOTTY;
	error = fgetown(&owner_file, &result);
	HK_CHECK(error == ENOTTY && result == 91 && driver_calls == 1);

	reset(FREAD | FWRITE);
	result = 91;
	driver_output = 17;
	driver_writes_output = 1;
	error = fgetown(&owner_file, &result);
	HK_CHECK(error == 0 && result == -17 && driver_calls == 1);

	reset(FREAD | FWRITE);
	result = 91;
	driver_output = -1;
	driver_writes_output = 1;
	error = fgetown(&owner_file, &result);
	HK_CHECK(error == EINVAL && result == 91 && driver_calls == 1);

	reset(FREAD | FWRITE);
	result = 91;
	driver_output = SHRT_MAX + 1;
	driver_writes_output = 1;
	error = fgetown(&owner_file, &result);
	HK_CHECK(error == EINVAL && result == 91 && driver_calls == 1);

	reset(FREAD | FWRITE);
	result = 91;
	driver_output = INT_MIN;
	driver_writes_output = 1;
	error = fgetown(&owner_file, &result);
	HK_CHECK(error == EINVAL && result == 91 && driver_calls == 1);
}

static void
fcntl_cases(void)
{
	reset(FREAD | FWRITE);
	call_fcntl(FD, F_SETOWN, -23);
	HK_CHECK(u.u_error == 0 && driver_calls == 1 && driver_input == 23);

	reset(FREAD | FWRITE);
	call_fcntl(FD, F_SETOWN, INT_MIN);
	HK_CHECK(u.u_error == EINVAL && driver_calls == 0);

	reset(FREAD | FWRITE);
	u.u_rval = 0x1234;
	driver_error = ENOTTY;
	call_fcntl(FD, F_GETOWN, 0);
	HK_CHECK(u.u_error == ENOTTY && u.u_rval == 0x1234);

	reset(FREAD | FWRITE);
	driver_output = 29;
	driver_writes_output = 1;
	call_fcntl(FD, F_GETOWN, 0);
	HK_CHECK(u.u_error == 0 && u.u_rval == -29);

	reset(FREAD | FWRITE);
	call_fcntl(FD + 1, F_SETOWN, -1);
	HK_CHECK(u.u_error == EBADF && driver_calls == 0);
}

static void
helper_status_cases(void)
{
	int error;

	reset(FREAD | FWRITE);
	error = fset(&owner_file, FNONBLOCK, 1);
	HK_CHECK(error == 0 && driver_calls == 1);
	HK_CHECK(driver_command == FIONBIO && driver_input == 1);
	HK_CHECK(driver_flags[0] == (FREAD | FWRITE));
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE | FNONBLOCK));

	reset(FREAD | FWRITE);
	driver_error = ENOTTY;
	error = fset(&owner_file, FNONBLOCK, 1);
	HK_CHECK(error == ENOTTY && driver_calls == 1);
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE));

	reset(FREAD | FWRITE | FNONBLOCK);
	driver_error = EIO;
	error = fset(&owner_file, FNONBLOCK, 0);
	HK_CHECK(error == EIO && driver_calls == 1 && driver_input == 0);
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE | FNONBLOCK));

	reset(FREAD | FWRITE);
	error = fset(&owner_file, FASYNC, 7);
	HK_CHECK(error == 0 && driver_command == FIOASYNC && driver_input == 7);
	HK_CHECK(driver_flags[0] == (FREAD | FWRITE));
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE | FASYNC));
}

static void
fcntl_status_cases(void)
{
	int old_flags = FREAD | FWRITE | FAPPEND;
	int requested = O_RDWR | O_NONBLOCK | O_ASYNC | O_FSYNC;
	int nonblocking_flags = old_flags | FNONBLOCK;

	reset(old_flags);
	call_fcntl(FD, F_SETFL, requested);
	HK_CHECK(u.u_error == 0 && driver_calls == 2);
	HK_CHECK(driver_commands[0] == FIONBIO && driver_inputs[0] != 0);
	HK_CHECK(driver_commands[1] == FIOASYNC && driver_inputs[1] != 0);
	HK_CHECK(driver_flags[0] == old_flags);
	HK_CHECK(driver_flags[1] == (old_flags | FNONBLOCK));
	HK_CHECK(owner_file.f_flag ==
	    (FREAD | FWRITE | FNONBLOCK | FASYNC | FFSYNC));

	reset(old_flags);
	driver_errors[0] = ENOTTY;
	call_fcntl(FD, F_SETFL, requested);
	HK_CHECK(u.u_error == ENOTTY && driver_calls == 1);
	HK_CHECK(owner_file.f_flag == old_flags);

	reset(old_flags);
	driver_errors[1] = EIO;
	call_fcntl(FD, F_SETFL, requested);
	HK_CHECK(u.u_error == EIO && driver_calls == 3);
	HK_CHECK(driver_commands[2] == FIONBIO && driver_inputs[2] == 0);
	HK_CHECK(owner_file.f_flag == old_flags);

	reset(old_flags);
	driver_errors[1] = EIO;
	driver_errors[2] = EPERM;
	call_fcntl(FD, F_SETFL, requested);
	HK_CHECK(u.u_error == EIO && driver_calls == 3);
	HK_CHECK(owner_file.f_flag == (old_flags | FNONBLOCK));

	reset(nonblocking_flags);
	driver_errors[1] = EIO;
	call_fcntl(FD, F_SETFL, O_RDWR | O_ASYNC | O_FSYNC);
	HK_CHECK(u.u_error == EIO && driver_calls == 3);
	HK_CHECK(driver_commands[2] == FIONBIO && driver_inputs[2] != 0);
	HK_CHECK(owner_file.f_flag == nonblocking_flags);
}

static void
ioctl_status_cases(void)
{
	caddr_t unaligned = (caddr_t)&user_storage[5];
	caddr_t crossing = (caddr_t)&user_storage[10];

	reset(FREAD | FWRITE);
	store_user_int(unaligned, 3);
	call_ioctl(FD, FIONBIO, unaligned);
	HK_CHECK(u.u_error == 0 && copyin_calls == 1 && driver_calls == 1);
	HK_CHECK(driver_command == FIONBIO && driver_input == 3);
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE | FNONBLOCK));

	reset(FREAD | FWRITE);
	store_user_int(unaligned, 1);
	copyin_error = EFAULT;
	call_ioctl(FD, FIOASYNC, unaligned);
	HK_CHECK(u.u_error == EFAULT && copyin_calls == 1 && driver_calls == 0);
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE));

	reset(FREAD | FWRITE);
	call_ioctl(FD, FIONBIO, crossing);
	HK_CHECK(u.u_error == EFAULT && copyin_calls == 0 && driver_calls == 0);
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE));

	reset(FREAD | FWRITE | FASYNC);
	store_user_int(unaligned, 0);
	driver_error = ENOTTY;
	call_ioctl(FD, FIOASYNC, unaligned);
	HK_CHECK(u.u_error == ENOTTY && copyin_calls == 1 && driver_calls == 1);
	HK_CHECK(owner_file.f_flag == (FREAD | FWRITE | FASYNC));
}

static void
ioctl_set_cases(void)
{
	caddr_t unaligned = (caddr_t)&user_storage[5];
	caddr_t crossing = (caddr_t)&user_storage[10];

	reset(FREAD | FWRITE);
	store_user_int(unaligned, -31);
	call_ioctl(FD, FIOSETOWN, unaligned);
	HK_CHECK(u.u_error == 0 && driver_calls == 1 && driver_input == 31);
	HK_CHECK(copyin_calls == 1 && copyout_calls == 0);

	reset(FREAD | FWRITE);
	store_user_int(unaligned, -31);
	copyin_error = EFAULT;
	call_ioctl(FD, FIOSETOWN, unaligned);
	HK_CHECK(u.u_error == EFAULT && driver_calls == 0 && copyin_calls == 1);

	reset(FREAD | FWRITE);
	call_ioctl(FD, FIOSETOWN, crossing);
	HK_CHECK(u.u_error == EFAULT && driver_calls == 0);

	reset(0);
	call_ioctl(FD, FIOSETOWN, (caddr_t)&user_storage[3]);
	HK_CHECK(u.u_error == EBADF && driver_calls == 0 && copyin_calls == 0);

	reset(FREAD | FWRITE);
	call_ioctl(FD + 1, FIOSETOWN, (caddr_t)&user_storage[3]);
	HK_CHECK(u.u_error == EBADF && driver_calls == 0 && copyin_calls == 0);
}

static void
ioctl_get_cases(void)
{
	caddr_t unaligned = (caddr_t)&user_storage[5];
	caddr_t crossing = (caddr_t)&user_storage[10];

	reset(FREAD | FWRITE);
	store_user_int(unaligned, 0x2345);
	driver_output = 37;
	driver_writes_output = 1;
	call_ioctl(FD, FIOGETOWN, unaligned);
	HK_CHECK(u.u_error == 0 && load_user_int(unaligned) == -37);
	HK_CHECK(copyout_calls == 1 && copyin_calls == 0 && driver_calls == 1);

	reset(FREAD | FWRITE);
	store_user_int(unaligned, 0x2345);
	driver_error = ENOTTY;
	call_ioctl(FD, FIOGETOWN, unaligned);
	HK_CHECK(u.u_error == ENOTTY && load_user_int(unaligned) == 0x2345);
	HK_CHECK(copyout_calls == 0 && driver_calls == 1);

	reset(FREAD | FWRITE);
	store_user_int(unaligned, 0x2345);
	driver_output = 41;
	driver_writes_output = 1;
	copyout_error = EFAULT;
	call_ioctl(FD, FIOGETOWN, unaligned);
	HK_CHECK(u.u_error == EFAULT && load_user_int(unaligned) == 0x2345);
	HK_CHECK(copyout_calls == 1 && driver_calls == 1);

	reset(FREAD | FWRITE);
	store_user_int(unaligned, 0x2345);
	driver_output = -1;
	driver_writes_output = 1;
	call_ioctl(FD, FIOGETOWN, unaligned);
	HK_CHECK(u.u_error == EINVAL && load_user_int(unaligned) == 0x2345);
	HK_CHECK(copyout_calls == 0 && driver_calls == 1);

	reset(FREAD | FWRITE);
	call_ioctl(FD, FIOGETOWN, crossing);
	HK_CHECK(u.u_error == EFAULT && driver_calls == 0 && copyout_calls == 0);
}

int
main(void)
{
	helper_set_cases();
	helper_get_cases();
	helper_status_cases();
	fcntl_cases();
	fcntl_status_cases();
	ioctl_status_cases();
	ioctl_set_cases();
	ioctl_get_cases();
	return hk_verdict("file control contracts");
}
