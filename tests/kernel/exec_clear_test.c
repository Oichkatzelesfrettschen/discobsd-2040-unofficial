/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ILP32 host gate for the contract between exec_clear() and exec_finish().
 * exec_clear() (sys/kern/exec_subr.c) marks the close-on-exec closes pending
 * and leaves them to exec_finish() (sys/kern/kern_exec.c), which runs them
 * once the executable's inode is released. A close made inside exec_clear()
 * runs under that inode lock, can sleep in a stopped terminal's drain, and
 * leaves the swapper asleep on the same lock in exec_text_restore(). Both
 * functions are extracted from the production sources, so the flag set here
 * is the flag exec_finish() reads, with no hand-set state between them.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef EXEC_CLEAR_FUNCTIONS
#define EXEC_CLEAR_FUNCTIONS "exec_clear_functions.c"
#endif

typedef char *caddr_t;

#define DEBUG(...) ((void)0)
#define NO_ADDR ((caddr_t)(uintptr_t)UINT_MAX)
#define bzero(pointer, length) ((void)memset((pointer), 0, (length)))
#define P_TRACED 0x0010
#define SIGTRAP 5
#define SIGKILL 9
#define EINTR 4
#define EXEC_INTERPRETED 0x01
#define EXEC_COMMITTED_FAILURE 0x02
#define EXEC_CLOSE_PENDING 0x04

_Static_assert(sizeof(void *) == 4, "exec fixture requires ILP32 pointers");

struct inode {
	int i_dummy;
};

struct proc {
	int p_flag;
	int p_uid;
};

struct memsect {
	caddr_t vaddr;
	unsigned int len;
};

struct exec_params {
	unsigned char flags;
	int uid;
	int gid;
	struct memsect text;
	struct memsect data;
	struct memsect bss;
	struct memsect heap;
	struct memsect stack;
};

struct trapframe {
	int tf_r0, tf_r1, tf_r2, tf_r3, tf_ip, tf_lr, tf_pc, tf_psr;
	int tf_r4, tf_r5, tf_r6, tf_r7, tf_r8, tf_r9, tf_r10, tf_r11;
};

struct user {
	struct proc *u_procp;
	struct trapframe *u_frame;
	int u_error;
	int u_uid;
	int u_svuid;
	int u_svgid;
	int u_groups[1];
	unsigned int u_tsize;
	unsigned int u_dsize;
	unsigned int u_ssize;
};

static struct user u;
static struct proc process;
static struct trapframe frame;
static struct inode executable_inode;
static int failures;
static int sequence;
static int in_clear;
static int close_calls;
static int close_calls_inside_clear;
static int close_order;
static int inode_release_order;
static int signal_calls;
static int execsigs_calls;

static void
execsigs(struct proc *target)
{
	(void)target;
	execsigs_calls++;
}

static void
psignal(struct proc *target, int signal_number)
{
	(void)target;
	(void)signal_number;
	signal_calls++;
}

static void
exec_alloc_freeall(struct exec_params *parameters)
{
	(void)parameters;
	sequence++;
}

static void
iput(struct inode *inode)
{
	(void)inode;
	inode_release_order = ++sequence;
}

/* A close that a signal interrupts leaves its error in u.u_error. */
static void
fdcloseexec(void)
{
	close_calls++;
	if (in_clear)
		close_calls_inside_clear++;
	close_order = ++sequence;
	u.u_error = EINTR;
}

static void
kernel_exit(int status)
{
	(void)status;
	failures++;
}

#define exit kernel_exit
#include EXEC_CLEAR_FUNCTIONS
#undef exit

#define CHECK(label, expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s\n", label); \
		failures++; \
	} \
} while (0)

#define WINDOW_BYTES 4096U
#define BSS_OFFSET 256U
#define BSS_BYTES 64U
#define STACK_OFFSET 3072U
#define STACK_BYTES 1024U
#define FILL 0xa5U

static unsigned char window[WINDOW_BYTES];

static void
reset_fixture(struct exec_params *parameters)
{
	memset(parameters, 0, sizeof(*parameters));
	memset(window, FILL, sizeof(window));
	memset(&frame, 0x7f, sizeof(frame));
	memset(&process, 0, sizeof(process));
	memset(&u, 0, sizeof(u));
	u.u_procp = &process;
	u.u_frame = &frame;
	parameters->uid = 1000;
	parameters->gid = 100;
	parameters->text.len = 0x400;
	parameters->data.len = 0x100;
	parameters->bss.vaddr = (caddr_t)&window[BSS_OFFSET];
	parameters->bss.len = BSS_BYTES;
	parameters->heap.vaddr = NO_ADDR;
	parameters->heap.len = 0;
	parameters->stack.vaddr = (caddr_t)&window[STACK_OFFSET];
	parameters->stack.len = STACK_BYTES;
	sequence = 0;
	in_clear = 0;
	close_calls = 0;
	close_calls_inside_clear = 0;
	close_order = 0;
	inode_release_order = 0;
	signal_calls = 0;
	execsigs_calls = 0;
}

static void
run_clear(struct exec_params *parameters)
{
	in_clear = 1;
	exec_clear(parameters);
	in_clear = 0;
}

static int
all_equal(const unsigned char *start, size_t length, unsigned char value)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (start[index] != value)
			return 0;
	return 1;
}

static void
check_clear_contract(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	parameters.flags = EXEC_INTERPRETED;
	run_clear(&parameters);
	CHECK("exec_clear marks the closes pending",
	    (parameters.flags & EXEC_CLOSE_PENDING) != 0);
	CHECK("exec_clear leaves the other flags as they were",
	    (parameters.flags & ~EXEC_CLOSE_PENDING) == EXEC_INTERPRETED);
	CHECK("exec_clear does not close descriptors itself",
	    close_calls_inside_clear == 0 && close_calls == 0);
	CHECK("exec_clear resets signal handlers", execsigs_calls == 1);
	CHECK("exec_clear installs the new image's credentials",
	    u.u_uid == 1000 && process.p_uid == 1000 && u.u_groups[0] == 100 &&
	    u.u_svuid == 1000 && u.u_svgid == 100);
	CHECK("exec_clear records the image sizes",
	    u.u_tsize == 0x400U && u.u_dsize == 0x100U + BSS_BYTES &&
	    u.u_ssize == STACK_BYTES);
	CHECK("exec_clear zeroes the registers",
	    frame.tf_r0 == 0 && frame.tf_pc == 0 && frame.tf_r11 == 0);
	CHECK("exec_clear zeroes the bss, the gap and the stack",
	    all_equal(&window[BSS_OFFSET],
	    STACK_OFFSET + STACK_BYTES - BSS_OFFSET, 0));
	CHECK("exec_clear leaves bytes below the bss alone",
	    all_equal(window, BSS_OFFSET, FILL));
	CHECK("exec_clear leaves bytes above the stack alone",
	    all_equal(&window[STACK_OFFSET + STACK_BYTES],
	    WINDOW_BYTES - STACK_OFFSET - STACK_BYTES, FILL));
}

static void
check_finish_performs_the_closes(void)
{
	struct exec_params parameters;
	int error = 0;

	reset_fixture(&parameters);
	run_clear(&parameters);
	u.u_error = error;
	exec_finish(&parameters, &executable_inode);
	CHECK("exec_finish closes the pending descriptors once",
	    close_calls == 1);
	CHECK("exec_finish releases the inode before the closes",
	    inode_release_order != 0 && close_order > inode_release_order);
	CHECK("exec_finish keeps an interrupted close from failing the exec",
	    u.u_error == error);
}

static void
check_traced_image_keeps_credentials(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	process.p_flag = P_TRACED;
	process.p_uid = 7;
	u.u_uid = 7;
	run_clear(&parameters);
	CHECK("a traced exec keeps its credentials and receives SIGTRAP",
	    u.u_uid == 7 && process.p_uid == 7 && signal_calls == 1);
	CHECK("a traced exec still marks the closes pending",
	    (parameters.flags & EXEC_CLOSE_PENDING) != 0);
}

int
main(void)
{
	check_clear_contract();
	check_finish_performs_the_closes();
	check_traced_image_keeps_credentials();
	if (failures != 0) {
		fprintf(stderr, "exec_clear contract: %d failure(s)\n", failures);
		return 1;
	}
	puts("PASS exec_clear contract");
	return 0;
}
