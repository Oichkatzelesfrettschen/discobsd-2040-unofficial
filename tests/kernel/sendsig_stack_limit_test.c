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
 * ILP32 host gate for the RLIMIT_STACK call site in sendsig()
 * (sys/arch/rp2040/rp2040/sig_machdep.c). A signal frame deeper than
 * RLIMIT_STACK ends the process with SIGILL through
 * rlimit_stack_admits(USER_TOP(p) - sfp); a frame at the limit is delivered
 * and grows the recorded stack. sendsig() and rlimit_stack_admits()
 * (sys/kern/kern_resource.c) are extracted from the production sources, so
 * the fixture drives the call site against the real decision.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef char *caddr_t;
typedef unsigned int u_int;
typedef unsigned long u_long;

/* user_context.h validates user addresses through this probe; sendsig() does not call it. */
static int baduaddr_span(caddr_t, u_int);

#define USER_DATA_START UINT32_C(0x20000000)
#include "../../sys/arch/rp2040/include/frame.h"
#include "../../sys/arch/rp2040/include/scb.h"
#include "../../sys/arch/rp2040/include/signal.h"
#include "../../sys/arch/rp2040/include/user_context.h"

#ifndef SENDSIG_FUNCTIONS
#define SENDSIG_FUNCTIONS "sendsig_stack_limit_functions.c"
#endif

static int invalid_user_span;

static int
baduaddr_span(caddr_t address, u_int length)
{
	(void)address;
	(void)length;
	return invalid_user_span;
}

#define SA_ONSTACK 0x0001
#define SAS_ALTSTACK 0x02
#define SIGILL 4
#define SIGUSR1 30
#define RLIMIT_STACK 3
#define SYSTRACE_SIGNAL 2
#define SYSTRACE_ON(kind) 0
#define sigmask(m) (1L << ((m) - 1))

typedef void (*sig_t)(int);

_Static_assert(sizeof(void *) == 4, "signal fixture requires ILP32 pointers");

struct rlimit {
	long rlim_cur;
	long rlim_max;
};

struct proc {
	unsigned int p_pid;
	caddr_t p_daddr;
	u_int p_ssize;
	u_int p_saddr;
};

struct signal_stack {
	caddr_t ss_base;
	u_int ss_size;
	int ss_flags;
};

struct user {
	struct proc *u_procp;
	struct trapframe *u_frame;
	struct signal_stack u_sigstk;
	int u_psflags;
	long u_sigonstack;
	int u_code;
	u_int u_sigtramp;
	u_int u_dsize;
	u_int u_ssize;
	struct rlimit u_rlimit[8];
};

#define WINDOW_BYTES 16384U
#define LIMIT_BYTES 4096U
#define RECORDED_STACK 256U
#define DATA_BYTES 1024U
#define FRAME_BYTES 128U

static unsigned char window[WINDOW_BYTES] __attribute__((aligned(8)));
static struct user u;
static struct proc process;
static struct userframe uframe;
#define frame (uframe.uf_tf)
static int fatalsig_calls;
static int fatalsig_signal;
static int failures;

#define USER_TOP(target) ((size_t)(uintptr_t)(window + sizeof(window)))

static void
fatalsig(int signal_number)
{
	fatalsig_calls++;
	fatalsig_signal = signal_number;
}

#include SENDSIG_FUNCTIONS

_Static_assert(sizeof(struct sigframe) == FRAME_BYTES,
    "the fixture frame size matches the production signal frame");

#define CHECK(label, expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s\n", label); \
		failures++; \
	} \
} while (0)

static void
handler(int signal_number)
{
	(void)signal_number;
}

static void
trampoline(void)
{
}

/* Arrange a process whose interrupted sp makes the signal frame depth bytes. */
static void
reset_fixture(u_int depth)
{
	memset(&uframe, 0, sizeof(uframe));
	memset(&process, 0, sizeof(process));
	memset(&u, 0, sizeof(u));
	u.u_procp = &process;
	u.u_frame = &frame;
	u.u_sigtramp = (u_int)(uintptr_t)trampoline;
	u.u_dsize = DATA_BYTES;
	u.u_ssize = RECORDED_STACK;
	u.u_rlimit[RLIMIT_STACK].rlim_cur = LIMIT_BYTES;
	process.p_pid = 2;
	process.p_daddr = (caddr_t)window;
	process.p_ssize = RECORDED_STACK;
	process.p_saddr = USER_TOP(0) - RECORDED_STACK;
	frame.tf_sp = USER_TOP(0) - depth + FRAME_BYTES;
	frame.tf_pc = 0x20000100U;
	frame.tf_psr = 0x01000000U;
	fatalsig_calls = 0;
	fatalsig_signal = 0;
	invalid_user_span = 0;
}

static void
check_alternate_stack(void)
{
	u_int expected;
	struct trapframe before;

	reset_fixture(RECORDED_STACK);
	u.u_psflags = SAS_ALTSTACK;
	u.u_sigonstack = sigmask(SIGUSR1);
	u.u_sigstk.ss_base = (caddr_t)(window + 2048);
	u.u_sigstk.ss_size = 1024;
	expected = (u_int)(uintptr_t)(window + 2048 + 1024 - FRAME_BYTES);
	sendsig(handler, SIGUSR1, 0);
	CHECK("a valid alternate stack receives the frame",
	    fatalsig_calls == 0 && frame.tf_sp == expected &&
	    (u.u_sigstk.ss_flags & SA_ONSTACK) != 0);

	reset_fixture(RECORDED_STACK);
	u.u_psflags = SAS_ALTSTACK;
	u.u_sigonstack = sigmask(SIGUSR1);
	u.u_sigstk.ss_base = (caddr_t)(window + 2048);
	u.u_sigstk.ss_size = 1024;
	invalid_user_span = 1;
	before = frame;
	sendsig(handler, SIGUSR1, 0);
	CHECK("an invalid alternate-stack frame ends the process",
	    fatalsig_calls == 1 && fatalsig_signal == SIGILL);
	CHECK("an invalid alternate stack stays unpublished",
	    (u.u_sigstk.ss_flags & SA_ONSTACK) == 0 &&
	    memcmp(&frame, &before, sizeof(frame)) == 0);
}

static void
check_delivered(const char *label, u_int depth)
{
	u_int sfp = USER_TOP(0) - depth;
	u_int original_sp = frame.tf_sp;

	sendsig(handler, SIGUSR1, 0);
	CHECK(label, fatalsig_calls == 0);
	CHECK("a delivered frame is carved below the interrupted sp",
	    frame.tf_sp == sfp && frame.tf_pc == (u_int)(uintptr_t)handler);
	CHECK("a delivered frame records the interrupted sp",
	    ((struct sigframe *)(uintptr_t)sfp)->sf_sc.sc_sp == original_sp);
	CHECK("a delivered frame grows the recorded stack to its depth",
	    process.p_ssize == depth && u.u_ssize == depth &&
	    process.p_saddr == sfp);
}

static void
check_refused(const char *label, u_int depth)
{
	struct trapframe before = frame;

	sendsig(handler, SIGUSR1, 0);
	CHECK(label, fatalsig_calls == 1 && fatalsig_signal == SIGILL);
	CHECK("a refused frame leaves the trapframe alone",
	    memcmp(&frame, &before, sizeof(frame)) == 0);
	CHECK("a refused frame leaves the recorded stack alone",
	    process.p_ssize == RECORDED_STACK && u.u_ssize == RECORDED_STACK &&
	    process.p_saddr == USER_TOP(0) - RECORDED_STACK);
	(void)depth;
}

int
main(void)
{
	reset_fixture(LIMIT_BYTES / 2U);
	check_delivered("a frame well inside the limit is delivered",
	    LIMIT_BYTES / 2U);

	reset_fixture(LIMIT_BYTES);
	check_delivered("a frame at the limit is delivered", LIMIT_BYTES);

	reset_fixture(LIMIT_BYTES + 8U);
	check_refused("a frame past the limit ends the process",
	    LIMIT_BYTES + 8U);

	reset_fixture(LIMIT_BYTES * 2U);
	check_refused("a frame far past the limit ends the process",
	    LIMIT_BYTES * 2U);

	check_alternate_stack();

	if (failures != 0) {
		fprintf(stderr, "sendsig stack limit: %d failure(s)\n", failures);
		return 1;
	}
	puts("PASS sendsig stack limit");
	return 0;
}
