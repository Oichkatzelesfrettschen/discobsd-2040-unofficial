/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef char *caddr_t;
typedef unsigned int u_int;

#include "../../sys/arch/rp2040/include/frame.h"
#include "../../sys/arch/rp2040/include/signal.h"

#ifndef SIGRETURN_CONTEXT_FUNCTION
#define SIGRETURN_CONTEXT_FUNCTION "sigreturn_context_function.c"
#endif

#define EFAULT 14
#define EJUSTRETURN (-2)
#define SA_ONSTACK 0x0001
#define SYSTRACE_SIGNAL 2
#define SYSTRACE_ON(kind) 0

_Static_assert(sizeof(int) == 4, "signal context requires four-byte int");
_Static_assert(sizeof(long) == 4, "signal context requires four-byte long");
_Static_assert(sizeof(void *) == 4, "signal fixture requires ILP32 pointers");
_Static_assert(sizeof(struct sigcontext) == 72,
    "RP2040 sigcontext ABI is eighteen words");
_Static_assert(_Alignof(struct sigcontext) == 4,
    "RP2040 sigcontext ABI has four-byte alignment");
_Static_assert(offsetof(struct sigcontext, sc_onstack) == 0,
    "sc_onstack ABI offset");
_Static_assert(offsetof(struct sigcontext, sc_mask) == 4,
    "sc_mask ABI offset");
_Static_assert(offsetof(struct sigcontext, sc_r0) == 8,
    "sc_r0 ABI offset");
_Static_assert(offsetof(struct sigcontext, sc_psr) == 36,
    "sc_psr ABI offset");
_Static_assert(offsetof(struct sigcontext, sc_r11) == 68,
    "sc_r11 ABI offset");
_Static_assert(sizeof(struct trapframe) == 64,
    "RP2040 trapframe ABI is sixteen words");
_Static_assert(offsetof(struct trapframe, tf_ip) == 16,
    "tf_sp alias ABI offset");
_Static_assert(offsetof(struct trapframe, tf_psr) == 28,
    "tf_psr ABI offset");

struct proc {
	unsigned int p_pid;
	long p_sigmask;
};

struct signal_stack {
	int ss_flags;
};

struct user {
	int u_error;
	struct signal_stack u_sigstk;
	struct proc *u_procp;
	struct trapframe *u_frame;
};

struct context_storage {
	u_int hardware_frame[8];
	struct sigcontext context;
	u_int tail[2];
};

static struct user u;
static struct proc process;
static struct trapframe frame;
static const long sigcantmask = 0x00000300L;
static uintptr_t logical_user_lower;
static uintptr_t logical_user_upper;
static unsigned int baduaddr_calls;
static unsigned int copyin_calls;
static uintptr_t copyin_source;
static uintptr_t copyin_destination;
static u_int copyin_length;
static int force_copyin_failure;
static int overwrite_user_after_copy;
static int failures;

static int
baduaddr(caddr_t address)
{
	uintptr_t value = (uintptr_t)address;

	baduaddr_calls++;
	return value < logical_user_lower || value >= logical_user_upper;
}

static int
copyin(caddr_t source, caddr_t destination, u_int length)
{
	uintptr_t start = (uintptr_t)source;

	copyin_calls++;
	copyin_source = start;
	copyin_destination = (uintptr_t)destination;
	copyin_length = length;
	if (force_copyin_failure)
		return EFAULT;
	if (start < logical_user_lower || start > logical_user_upper ||
	    length > logical_user_upper - start)
		return EFAULT;
	memcpy(destination, source, length);
	if (overwrite_user_after_copy)
		memset(source, 0x3c, length);
	return 0;
}

#include SIGRETURN_CONTEXT_FUNCTION

#define CHECK(label, expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s\n", label); \
		failures++; \
	} \
} while (0)

static void
fill_context(struct sigcontext *context)
{
	context->sc_onstack = SA_ONSTACK;
	context->sc_mask = 0x00000f0fL;
	context->sc_r0 = 0x10101010U;
	context->sc_r1 = 0x11111111U;
	context->sc_r2 = 0x12121212U;
	context->sc_r3 = 0x13131313U;
	context->sc_sp = 0x20202020U;
	context->sc_lr = 0x21212121U;
	context->sc_pc = 0x22222223U;
	context->sc_psr = 0x61000200U;
	context->sc_r4 = 0x14141414U;
	context->sc_r5 = 0x15151515U;
	context->sc_r6 = 0x16161616U;
	context->sc_r7 = 0x17171717U;
	context->sc_r8 = 0x18181818U;
	context->sc_r9 = 0x19191919U;
	context->sc_r10 = 0x1a1a1a1aU;
	context->sc_r11 = 0x1b1b1b1bU;
}

static void
reset_fixture(u_int frame_base)
{
	memset(&frame, 0xa5, sizeof(frame));
	frame.tf_sp = frame_base;
	u.u_error = 77;
	u.u_sigstk.ss_flags = 0x40;
	u.u_procp = &process;
	u.u_frame = &frame;
	process.p_sigmask = 0x55aa55aaL;
	baduaddr_calls = 0;
	copyin_calls = 0;
	copyin_source = 0;
	copyin_destination = 0;
	copyin_length = 0;
	force_copyin_failure = 0;
	overwrite_user_after_copy = 0;
}

static void
check_restored_context(const struct sigcontext *context)
{
	CHECK("sigreturn publishes EJUSTRETURN", u.u_error == EJUSTRETURN);
	CHECK("sigreturn restores alternate-stack state",
	    (u.u_sigstk.ss_flags & SA_ONSTACK) != 0);
	CHECK("sigreturn preserves unrelated stack flags",
	    (u.u_sigstk.ss_flags & ~SA_ONSTACK) == 0x40);
	CHECK("sigreturn masks unmaskable signals",
	    process.p_sigmask == (context->sc_mask & ~sigcantmask));
	CHECK("sigreturn restores low registers",
	    frame.tf_r0 == context->sc_r0 && frame.tf_r1 == context->sc_r1 &&
	    frame.tf_r2 == context->sc_r2 && frame.tf_r3 == context->sc_r3);
	CHECK("sigreturn restores exception registers",
	    frame.tf_sp == context->sc_sp && frame.tf_lr == context->sc_lr &&
	    frame.tf_pc == context->sc_pc && frame.tf_psr == context->sc_psr);
	CHECK("sigreturn restores high registers",
	    frame.tf_r4 == context->sc_r4 && frame.tf_r5 == context->sc_r5 &&
	    frame.tf_r6 == context->sc_r6 && frame.tf_r7 == context->sc_r7 &&
	    frame.tf_r8 == context->sc_r8 && frame.tf_r9 == context->sc_r9 &&
	    frame.tf_r10 == context->sc_r10 &&
	    frame.tf_r11 == context->sc_r11);
}

static void
check_rejected_state(const char *case_name, const struct trapframe *before,
    int stack_flags, long signal_mask)
{
	CHECK(case_name, u.u_error == EFAULT);
	CHECK("rejection preserves trapframe",
	    memcmp(&frame, before, sizeof(frame)) == 0);
	CHECK("rejection preserves alternate-stack state",
	    u.u_sigstk.ss_flags == stack_flags);
	CHECK("rejection preserves signal mask", process.p_sigmask == signal_mask);
}

static void
check_exact_complete_context(void)
{
	struct context_storage storage;
	struct sigcontext expected;

	memset(&storage, 0, sizeof(storage));
	fill_context(&expected);
	storage.context = expected;
	reset_fixture((u_int)(uintptr_t)&storage);
	logical_user_lower = (uintptr_t)&storage.context;
	logical_user_upper = logical_user_lower + sizeof(storage.context);
	overwrite_user_after_copy = 1;
	sigreturn();
	CHECK("exact context uses one complete copy", copyin_calls == 1);
	CHECK("exact context copies seventy-two bytes",
	    copyin_source == (uintptr_t)&storage.context &&
	    copyin_length == sizeof(storage.context));
	CHECK("copy destination is sigcontext-aligned",
	    copyin_destination % _Alignof(struct sigcontext) == 0);
	CHECK("snapshot path avoids probe-only admission", baduaddr_calls == 0);
	CHECK("copy seam changes user context after snapshot",
	    memcmp(&storage.context, &expected, sizeof(expected)) != 0);
	check_restored_context(&expected);
}

static void
check_misaligned_context(void)
{
	unsigned char storage[32 + sizeof(struct sigcontext) + 2];
	struct sigcontext expected;
	uintptr_t context_address = (uintptr_t)&storage[33];

	fill_context(&expected);
	memcpy((void *)context_address, &expected, sizeof(expected));
	reset_fixture((u_int)(context_address - 32U));
	logical_user_lower = context_address;
	logical_user_upper = context_address + sizeof(expected) + 1U;
	overwrite_user_after_copy = 1;
	sigreturn();
	CHECK("misaligned source uses one complete copy", copyin_calls == 1);
	CHECK("misaligned source address preserved", copyin_source == context_address);
	CHECK("misaligned destination is aligned",
	    copyin_destination % _Alignof(struct sigcontext) == 0);
	CHECK("misaligned user context changes after snapshot",
	    memcmp((void *)context_address, &expected, sizeof(expected)) != 0);
	check_restored_context(&expected);
}

static void
check_invalid_or_truncated_context(int first_byte_invalid)
{
	struct context_storage storage;
	struct trapframe before;
	int stack_flags;
	long signal_mask;

	memset(&storage, 0, sizeof(storage));
	fill_context(&storage.context);
	reset_fixture((u_int)(uintptr_t)&storage);
	logical_user_lower = (uintptr_t)&storage.context;
	logical_user_upper = first_byte_invalid ? logical_user_lower :
	    logical_user_lower + sizeof(storage.context) - 1U;
	before = frame;
	stack_flags = u.u_sigstk.ss_flags;
	signal_mask = process.p_sigmask;
	sigreturn();
	check_rejected_state(first_byte_invalid ?
	    "invalid context reports EFAULT" :
	    "truncated context reports EFAULT", &before, stack_flags,
	    signal_mask);
	CHECK("rejected context requests one complete copy", copyin_calls == 1);
	CHECK("rejected context avoids probe-only admission", baduaddr_calls == 0);
}

static void
check_forced_copy_failure(void)
{
	struct context_storage storage;
	struct trapframe before;
	int stack_flags;
	long signal_mask;

	memset(&storage, 0, sizeof(storage));
	fill_context(&storage.context);
	reset_fixture((u_int)(uintptr_t)&storage);
	logical_user_lower = (uintptr_t)&storage.context;
	logical_user_upper = logical_user_lower + sizeof(storage.context) + 1U;
	force_copyin_failure = 1;
	before = frame;
	stack_flags = u.u_sigstk.ss_flags;
	signal_mask = process.p_sigmask;
	sigreturn();
	check_rejected_state("copy failure reports EFAULT", &before, stack_flags,
	    signal_mask);
	CHECK("forced failure requests one complete copy", copyin_calls == 1);
}

static void
check_context_address_wrap(void)
{
	struct trapframe before;
	int stack_flags;
	long signal_mask;

	reset_fixture(UINT_MAX - 31U);
	logical_user_lower = 1;
	logical_user_upper = UINT_MAX;
	before = frame;
	stack_flags = u.u_sigstk.ss_flags;
	signal_mask = process.p_sigmask;
	sigreturn();
	check_rejected_state("wrapped context reports EFAULT", &before,
	    stack_flags, signal_mask);
	CHECK("wrapped context skips copy", copyin_calls == 0);
	CHECK("wrapped context skips probe", baduaddr_calls == 0);
}

int
main(void)
{
	(void)&baduaddr;
	(void)&copyin;
	check_exact_complete_context();
	check_misaligned_context();
	check_invalid_or_truncated_context(1);
	check_invalid_or_truncated_context(0);
	check_forced_copy_failure();
	check_context_address_wrap();
	return failures != 0;
}
