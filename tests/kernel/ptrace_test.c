/*
 * Host and ILP32 contract gate for the ptrace child-side request boundary.
 *
 * The gate includes the exact production admission helpers and procxmt().
 * Logical user addresses exercise complete-span and wrap checks without
 * requiring the host to map the RP2040 process window.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/ptrace.h>

#include <machine/frame.h>

#define USER_LOWER UINT32_C(0x20000000)
#define USER_UPPER UINT32_C(0x20000100)
#define CHILD_PID 71

#ifndef PTRACE_FUNCTIONS
#define PTRACE_FUNCTIONS "ptrace_functions.c"
#endif

#ifdef HK_ILP32
_Static_assert(sizeof(int) == 4, "int is the target's four bytes");
_Static_assert(sizeof(void *) == 4, "pointers are the target's four bytes");
#endif

struct user u;
struct ipc ipc;

static struct proc child;
static struct proc parent;
static struct trapframe *frame;
static unsigned char user_storage[USER_UPPER - USER_LOWER];
static unsigned int wakeup_calls;
static unsigned int copyin_calls;
static unsigned int copyout_calls;
static int copyin_error;
static int copyout_error;
static u_int partial_copy_bytes;
static int found_pid;
static unsigned int pfind_calls;
static unsigned int setrun_calls;

static size_t
user_storage_offset(u_int address)
{
	return (size_t)(address - USER_LOWER);
}

int
baduaddr_span(caddr_t address, u_int nbytes)
{
	u_int start = (u_int)(u_long)address;

	if (start < USER_LOWER || start > USER_UPPER)
		return 1;
	return nbytes > USER_UPPER - start;
}

int
baduaddr(caddr_t address)
{
	return baduaddr_span(address, 1);
}

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	u_int start = (u_int)(u_long)from;
	u_int copied = partial_copy_bytes;

	copyin_calls++;
	if (baduaddr_span(from, nbytes))
		return EFAULT;
	if (copied > nbytes)
		copied = nbytes;
	if (copied != 0)
		bcopy(user_storage + user_storage_offset(start), to, copied);
	if (copyin_error != 0)
		return copyin_error;
	bcopy(user_storage + user_storage_offset(start), to, nbytes);
	return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	u_int start = (u_int)(u_long)to;

	copyout_calls++;
	if (copyout_error != 0)
		return copyout_error;
	if (baduaddr_span(to, nbytes))
		return EFAULT;
	bcopy(from, user_storage + user_storage_offset(start), nbytes);
	return 0;
}

void
wakeup(caddr_t channel)
{
	(void)channel;
	wakeup_calls++;
}

void
ptrace_process_exit(int status)
{
	(void)status;
	panic("ptrace test unexpectedly reached exit");
}

#define exit ptrace_process_exit
#include PTRACE_FUNCTIONS
#undef exit

struct proc *
pfind(int pid)
{
	pfind_calls++;
	if (pid == found_pid)
		return &child;
	return NULL;
}

void
sleep(caddr_t channel, int priority)
{
	(void)channel;
	(void)priority;
	panic("ptrace test unexpectedly reached sleep");
}

void
setrun(struct proc *process)
{
	struct proc *saved_process = u.u_procp;
	struct trapframe *saved_frame = u.u_frame;

	setrun_calls++;
	u.u_procp = process;
	u.u_frame = frame;
	(void)procxmt();
	u.u_procp = saved_process;
	u.u_frame = saved_frame;
}

static int
frame_offset(const u_int *word)
{
	return (int)((const char *)word - (const char *)&u);
}

static void
store_user_int(u_int address, int value)
{
	bcopy(&value, user_storage + user_storage_offset(address), sizeof value);
}

static int
load_user_int(u_int address)
{
	int value;

	bcopy(user_storage + user_storage_offset(address), &value, sizeof value);
	return value;
}

static void
reset(void)
{
	bzero(&u, sizeof u);
	bzero(&child, sizeof child);
	bzero(&parent, sizeof parent);
	bzero(user_storage, sizeof user_storage);
	/* u_signal provides aligned in-object storage for the 64-byte frame. */
	frame = (struct trapframe *)(void *)u.u_signal;
	bzero(frame, sizeof *frame);
	u.u_frame = frame;
	u.u_procp = &child;
	child.p_pid = CHILD_PID;
	child.p_ppid = 23;
	child.p_ptracesig = 9;
	parent.p_pid = 23;
	ipc.ip_lock = CHILD_PID;
	ipc.ip_req = 0;
	ipc.ip_addr = NULL;
	ipc.ip_data = 0;
	wakeup_calls = 0;
	copyin_calls = 0;
	copyout_calls = 0;
	copyin_error = 0;
	copyout_error = 0;
	partial_copy_bytes = 0;
	found_pid = CHILD_PID;
	pfind_calls = 0;
	setrun_calls = 0;
}

static int
run_request(int request, u_int address, int data)
{
	ipc.ip_req = request;
	ipc.ip_addr = (int *)(u_long)address;
	ipc.ip_data = data;
	return procxmt();
}

static void
check_lock_ownership(void)
{
	reset();
	ipc.ip_lock = CHILD_PID + 1;
	ipc.ip_req = PT_READ_D;
	ipc.ip_data = 0x12345678;
	HK_CHECK(procxmt() == 0);
	HK_CHECK(ipc.ip_req == PT_READ_D);
	HK_CHECK(ipc.ip_data == 0x12345678);
	HK_CHECK(wakeup_calls == 0);
	HK_CHECK(copyin_calls == 0);
}

static void
call_ptrace(int request, int pid, u_int address, int data)
{
	struct ptrace_args *arguments = (struct ptrace_args *)(void *)u.u_arg;

	arguments->request = request;
	arguments->pid = pid;
	arguments->address = (int *)(u_long)address;
	arguments->data = data;
	ptrace();
}

static void
check_parent_admission_and_result(void)
{
	reset();
	u.u_procp = &parent;
	ipc.ip_lock = 0;
	call_ptrace(PT_TRACE_ME, 0, 0, 0);
	HK_CHECK((parent.p_flag & P_TRACED) != 0);
	HK_CHECK(pfind_calls == 0);

	reset();
	u.u_procp = &parent;
	ipc.ip_lock = 0;
	call_ptrace(-1, CHILD_PID, 0, 0);
	HK_CHECK(u.u_error == EIO);
	HK_CHECK((parent.p_flag & P_TRACED) == 0);
	HK_CHECK(pfind_calls == 0);

	reset();
	u.u_procp = &parent;
	ipc.ip_lock = 0;
	found_pid = -1;
	call_ptrace(PT_READ_D, CHILD_PID, USER_LOWER, 0);
	HK_CHECK(u.u_error == ESRCH);
	HK_CHECK(setrun_calls == 0);

	reset();
	u.u_procp = &parent;
	ipc.ip_lock = 0;
	child.p_stat = SSTOP;
	child.p_flag = P_TRACED | P_WAITED;
	store_user_int(USER_LOWER + 4, -1);
	call_ptrace(PT_READ_D, CHILD_PID, USER_LOWER + 4, 0);
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rval == -1);
	HK_CHECK(setrun_calls == 1);
	HK_CHECK((child.p_flag & P_WAITED) == 0);
	HK_CHECK(ipc.ip_lock == 0);
	HK_CHECK(ipc.ip_req == 0);

	reset();
	u.u_procp = &parent;
	ipc.ip_lock = 0;
	child.p_stat = SSTOP;
	child.p_flag = P_TRACED;
	call_ptrace(100, CHILD_PID, USER_LOWER, 0x1234);
	HK_CHECK(u.u_error == EIO);
	HK_CHECK(u.u_rval == 0x1234);
	HK_CHECK(ipc.ip_lock == 0);
}

static void
check_user_word_transfers(void)
{
	reset();
	store_user_int(USER_LOWER + 4, 0x12345678);
	HK_CHECK(run_request(PT_READ_I, USER_LOWER + 4, 0) == 0);
	HK_CHECK(ipc.ip_req == 0);
	HK_CHECK(ipc.ip_data == 0x12345678);
	HK_CHECK(copyin_calls == 1);

	reset();
	store_user_int(USER_LOWER + 5, 0x23456789);
	HK_CHECK(run_request(PT_READ_D, USER_LOWER + 5, 0) == 0);
	HK_CHECK(ipc.ip_data == 0x23456789);

	reset();
	HK_CHECK(run_request(PT_READ_D, USER_UPPER - 3, 0x13579bdf) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(ipc.ip_data == 0x13579bdf);

	reset();
	store_user_int(USER_LOWER + 8, 0x11223344);
	copyin_error = EFAULT;
	partial_copy_bytes = 2;
	HK_CHECK(run_request(PT_READ_D, USER_LOWER + 8, 0x55667788) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(ipc.ip_data == 0x55667788);

	reset();
	HK_CHECK(run_request(PT_WRITE_I, USER_LOWER + 3, 0x76543210) == 0);
	HK_CHECK(ipc.ip_req == 0);
	HK_CHECK(load_user_int(USER_LOWER + 3) == 0x76543210);
	HK_CHECK(copyout_calls == 1);

	reset();
	store_user_int(USER_UPPER - 4, 0x24681357);
	HK_CHECK(run_request(PT_WRITE_D, USER_UPPER - 3, 0x10203040) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(load_user_int(USER_UPPER - 4) == 0x24681357);
}

static void
check_user_area_reads(void)
{
	int offset;

	reset();
	u.u_rval = 0x1234abcd;
	offset = (int)((char *)&u.u_rval - (char *)&u);
	HK_CHECK(run_request(PT_READ_U, (u_int)offset, 0) == 0);
	HK_CHECK(ipc.ip_data == 0x1234abcd);

	reset();
	HK_CHECK(run_request(PT_READ_U, (u_int)(offset + 1), 0x2468ace0) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(ipc.ip_data == 0x2468ace0);
	HK_CHECK(ptrace_user_word_offset_valid(-4) == 0);
	HK_CHECK(ptrace_user_word_offset_valid(USIZE - 3) == 0);
	HK_CHECK(ptrace_user_word_offset_valid(USIZE - (int)sizeof(int)) == 1);
	HK_CHECK(ptrace_user_word_offset_valid(USIZE) == 0);
}

static void
check_general_register_writes(void)
{
	u_int *registers[] = {
		&frame->tf_r0, &frame->tf_r1, &frame->tf_r2, &frame->tf_r3,
		&frame->tf_lr, &frame->tf_r4, &frame->tf_r5, &frame->tf_r6,
		&frame->tf_r7, &frame->tf_r8, &frame->tf_r9, &frame->tf_r10,
		&frame->tf_r11,
	};
	u_int index;

	reset();
	for (index = 0; index < sizeof registers / sizeof registers[0]; index++) {
		int value = (int)(0x10000000U + index);

		HK_CHECK(run_request(PT_WRITE_U,
		    (u_int)frame_offset(registers[index]), value) == 0);
		HK_CHECK(ipc.ip_req == 0);
		HK_CHECK(*registers[index] == (u_int)value);
	}

	u.u_uid = 17;
	HK_CHECK(run_request(PT_WRITE_U,
	    (u_int)((char *)&u.u_uid - (char *)&u), 0x5678) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(u.u_uid == 17);
	HK_CHECK(run_request(PT_WRITE_U, 1, 0x5678) == 0);
	HK_CHECK(ipc.ip_req == -1);
}

static void
check_control_register_writes(void)
{
	u_int original;

	reset();
	frame->tf_sp = USER_LOWER + 64;
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_sp),
	    USER_LOWER + 96) == 0);
	HK_CHECK(frame->tf_sp == USER_LOWER + 96);
	original = frame->tf_sp;
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_sp),
	    USER_LOWER + 97) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_sp == original);
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_sp),
	    USER_UPPER - 24) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_sp == original);

	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_pc),
	    USER_LOWER + 8) == 0);
	HK_CHECK(frame->tf_pc == USER_LOWER + 8);
	original = frame->tf_pc;
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_pc),
	    USER_LOWER + 9) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_pc == original);
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_pc),
	    USER_UPPER) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_pc == original);

	frame->tf_psr = UINT32_C(0x0100023f);
	HK_CHECK(run_request(PT_WRITE_U, (u_int)frame_offset(&frame->tf_psr),
	    (int)UINT32_C(0xa0ffffff)) == 0);
	HK_CHECK(frame->tf_psr == UINT32_C(0xa100023f));
}

static void
check_continue_and_step(void)
{
	reset();
	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_CONTINUE, USER_LOWER + 8, SIGTERM) == 1);
	HK_CHECK(frame->tf_pc == USER_LOWER + 8);
	HK_CHECK(child.p_ptracesig == SIGTERM);

	reset();
	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_CONTINUE, USER_LOWER + 8, -1) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_pc == USER_LOWER + 2);
	HK_CHECK(child.p_ptracesig == 9);

	reset();
	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_CONTINUE, USER_LOWER + 8, NSIG) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_pc == USER_LOWER + 2);
	HK_CHECK(child.p_ptracesig == 9);

	reset();
	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_CONTINUE, USER_LOWER + 9, SIGTERM) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_pc == USER_LOWER + 2);
	HK_CHECK(child.p_ptracesig == 9);

	reset();
	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_CONTINUE, 1, 0) == 1);
	HK_CHECK(frame->tf_pc == USER_LOWER + 2);
	HK_CHECK(child.p_ptracesig == 0);

	reset();
	frame->tf_pc = USER_LOWER + 2;
	HK_CHECK(run_request(PT_STEP, 1, 0) == 0);
	HK_CHECK(ipc.ip_req == -1);
	HK_CHECK(frame->tf_pc == USER_LOWER + 2);
	HK_CHECK(child.p_ptracesig == 9);

	reset();
	HK_CHECK(run_request(100, USER_LOWER, 0) == 0);
	HK_CHECK(ipc.ip_req == -1);
}

int
main(void)
{
	check_lock_ownership();
	check_parent_admission_and_result();
	check_user_word_transfers();
	check_user_area_reads();
	check_general_register_writes();
	check_control_register_writes();
	check_continue_and_step();
	return hk_verdict("ptrace address and register contracts");
}
