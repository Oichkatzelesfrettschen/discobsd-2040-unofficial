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
 * ILP32 host gate for the RLIMIT_DATA and RLIMIT_STACK growth checks:
 * rlimit_data_admits() and rlimit_stack_admits() (sys/kern/kern_resource.c),
 * brk() (sys/kern/kern_mman.c) and user_entry()
 * (sys/arch/rp2040/rp2040/syscall.c), compiled verbatim from the extracted
 * source under a process shim whose window is a host buffer. sendsig()
 * calls the same stack helper and stays outside this gate.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef RLIMIT_GROWTH_FUNCTIONS
#define RLIMIT_GROWTH_FUNCTIONS "rlimit_growth_functions.c"
#endif

_Static_assert(sizeof(void *) == 4, "the shim models the target's 32-bit pointers");

typedef unsigned int u_int;
typedef char *caddr_t;

#define ENOMEM 12
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIM_NLIMITS 6
#define RLIM_INFINITY 0x7fffffff
#define WINDOW (192U * 1024U)
#define MAXMEM WINDOW
#define HWFRAME_BYTES 32U
#define XPSR_STKALIGN 0x200U
#define SA_ONSTACK 0x0001
#define bzero(destination, length) memset((destination), 0, (length))

struct rlimit {
	long rlim_cur;
	long rlim_max;
};

struct sigaltstack {
	char *ss_base;
	int ss_size;
	int ss_flags;
};

struct proc {
	size_t p_daddr;
	size_t p_saddr;
	size_t p_dsize;
	size_t p_ssize;
	size_t p_tsize;
};

struct trapframe {
	u_int tf_sp;
	u_int tf_psr;
};

struct user {
	struct proc *u_procp;
	int u_arg[6];
	int u_error;
	int u_rval;
	size_t u_tsize;
	size_t u_dsize;
	size_t u_ssize;
	struct sigaltstack u_sigstk;
	struct rlimit u_rlimit[RLIM_NLIMITS];
};

/* user_entry() refuses a frame inside the u area, so the frame follows it. */
static struct {
	struct user user;
	struct trapframe frame;
} layout;
#define u layout.user

static _Alignas(8) unsigned char window[WINDOW];
static struct proc process;
static unsigned panics;

#define USER_TOP(p) ((size_t)(window + WINDOW))

static void
panic(const char *message)
{
	(void)message;
	panics++;
}

int rlimit_data_admits(size_t bytes);
int rlimit_stack_admits(size_t depth);
void brk(void);

#include RLIMIT_GROWTH_FUNCTIONS

static unsigned checks, failures;

#define CHECK(name, condition) do {					\
	checks++;							\
	if (!(condition)) {						\
		failures++;						\
		printf("FAIL %s\n", (name));				\
	}								\
} while (0)

static void
reset(size_t text, size_t data, size_t stack)
{
	unsigned i;

	memset(&layout, 0, sizeof(layout));
	memset(&process, 0, sizeof(process));
	process.p_daddr = (size_t)window;
	process.p_tsize = text;
	process.p_dsize = data;
	process.p_ssize = stack;
	process.p_saddr = USER_TOP(&process) - stack;
	u.u_procp = &process;
	u.u_dsize = data;
	u.u_ssize = stack;
	for (i = 0; i < RLIM_NLIMITS; i++)
		u.u_rlimit[i].rlim_cur = u.u_rlimit[i].rlim_max = RLIM_INFINITY;
}

/* Move the break to the window offset end; return the error brk() set. */
static int
break_to(size_t end)
{
	u.u_error = 0;
	u.u_arg[0] = (int)((size_t)window + end);
	brk();
	return u.u_error;
}

static void
check_data_limit(void)
{
	reset(4096, 8192, 1024);
	CHECK("infinite limit admits growth", break_to(65536) == 0 &&
	    u.u_dsize == 65536);

	reset(4096, 8192, 1024);
	u.u_rlimit[RLIMIT_DATA].rlim_cur = 12288;
	CHECK("growth to the limit above the text is admitted",
	    break_to(4096 + 12288) == 0 && u.u_dsize == 16384);
	CHECK("growth one byte past the limit is refused",
	    break_to(4096 + 12289) == ENOMEM && u.u_dsize == 16384);
	CHECK("the text stays outside the data limit",
	    break_to(12288) == 0 && u.u_dsize == 12288);

	reset(4096, 32768, 1024);
	u.u_rlimit[RLIMIT_DATA].rlim_cur = 1024;
	CHECK("a process over a lowered limit can shrink",
	    break_to(16384) == 0 && u.u_dsize == 16384);
	CHECK("and cannot grow again past it",
	    break_to(16385) == ENOMEM && u.u_dsize == 16384);

	reset(0, 8192, 1024);
	u.u_rlimit[RLIMIT_DATA].rlim_cur = -1;
	CHECK("a negative limit admits no growth", break_to(8193) == ENOMEM);
}

/* Enter the kernel with sp at depth below the window top. */
static int
enter_at(size_t depth)
{
	layout.frame.tf_sp = (u_int)(USER_TOP(&process) - depth);
	layout.frame.tf_psr = 0;
	return user_entry(&layout.frame);
}

static void
check_stack_limit(void)
{
	reset(4096, 8192, 1024);
	CHECK("infinite limit records a deeper stack",
	    enter_at(4096) == 0 && process.p_ssize == 4096);

	reset(4096, 8192, 1024);
	u.u_rlimit[RLIMIT_STACK].rlim_cur = 4096;
	CHECK("a depth at the limit is recorded",
	    enter_at(4096) == 0 && process.p_ssize == 4096 &&
	    u.u_ssize == 4096);
	CHECK("a depth past the limit fails and records nothing",
	    enter_at(4104) != 0 && process.p_ssize == 4096);
	CHECK("a shallower entry under a lowered limit passes",
	    enter_at(512) == 0 && process.p_ssize == 4096);
	CHECK("the frame lies outside the u area", panics == 0);
}

int
main(void)
{
	check_data_limit();
	check_stack_limit();
	printf("rlimit growth: %u checks, %u failed\n", checks, failures);
	return failures != 0;
}
