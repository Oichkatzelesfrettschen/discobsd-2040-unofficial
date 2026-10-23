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

#ifndef SYSCALL_ARGS_BLOCK
#define SYSCALL_ARGS_BLOCK "syscall_args_block.c"
#endif

typedef char *caddr_t;
typedef unsigned int u_int;

#define EFAULT 14
#define EINVAL 22
#define XPSR_STKALIGN 0x00000200UL

_Static_assert(sizeof(int) == 4, "syscall arguments are four-byte words");
_Static_assert(sizeof(void *) == 4, "syscall fixture requires ILP32 pointers");
_Static_assert(sizeof(size_t) == 4, "syscall fixture requires ILP32 size_t");

struct trapframe {
	u_int tf_r0;
	u_int tf_r1;
	u_int tf_r2;
	u_int tf_r3;
	u_int tf_sp;
	u_int tf_psr;
};

struct user {
	int u_arg[6];
	int u_error;
	int u_rval;
	int u_qsave;
	struct trapframe *u_frame;
};

static struct user u;
static struct trapframe frame;
static uint8_t syscall_nargs[4];
static const int nsysent = 4;

static unsigned int handler_calls;
static unsigned int handler_slot;
static unsigned int setjmp_calls;
static unsigned int copyin_calls;
static unsigned int baduaddr_calls;
static uintptr_t copyin_source;
static size_t copyin_length;
static uintptr_t logical_user_lower;
static uintptr_t logical_user_upper;
static int force_copyin_failure;
static int failures;

static void
record_handler(unsigned int slot)
{
	handler_calls++;
	handler_slot = slot;
}

static void
handler_zero(void)
{
	record_handler(0);
}

static void
handler_one(void)
{
	record_handler(1);
}

static void
handler_two(void)
{
	record_handler(2);
}

static void
handler_three(void)
{
	record_handler(3);
}

static void (*syscall_handlers[4])(void) = {
	handler_zero,
	handler_one,
	handler_two,
	handler_three,
};

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
	copyin_length = length;
	if (force_copyin_failure)
		return EFAULT;
	if (start < logical_user_lower || start > logical_user_upper ||
	    length > logical_user_upper - start)
		return EFAULT;
	memcpy(destination, source, length);
	return 0;
}

static int
recording_setjmp(void *environment)
{
	(void)environment;
	setjmp_calls++;
	return 0;
}

#define setjmp(environment) recording_setjmp(environment)

static void
exercise_dispatch(int code)
{
#include SYSCALL_ARGS_BLOCK
}

#define CHECK(label, expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s\n", label); \
		failures++; \
	} \
} while (0)

static void
reset_fixture(u_int *stack_words, size_t word_count)
{
	size_t argument_index;

	memset(&frame, 0, sizeof(frame));
	memset(syscall_nargs, 0, sizeof(syscall_nargs));
	for (argument_index = 0; argument_index < 6; argument_index++)
		u.u_arg[argument_index] = (int)0xa5a5a5a5U;
	u.u_error = 0;
	u.u_rval = -1;
	u.u_qsave = 0;
	u.u_frame = &frame;
	frame.tf_r0 = 0x10101010U;
	frame.tf_r1 = 0x20202020U;
	frame.tf_r2 = 0x30303030U;
	frame.tf_r3 = 0x40404040U;
	frame.tf_sp = (u_int)(uintptr_t)stack_words;
	logical_user_lower = (uintptr_t)stack_words;
	logical_user_upper = logical_user_lower + word_count * sizeof(*stack_words);
	handler_calls = 0;
	handler_slot = UINT_MAX;
	setjmp_calls = 0;
	copyin_calls = 0;
	baduaddr_calls = 0;
	copyin_source = 0;
	copyin_length = 0;
	force_copyin_failure = 0;
}

static void
check_five_arguments(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	stack_words[8] = 0x50505050U;
	syscall_nargs[1] = 5;
	exercise_dispatch(1);
	CHECK("five arguments dispatch", handler_calls == 1 && handler_slot == 1);
	CHECK("five register arguments", u.u_arg[0] == (int)0x10101010U &&
	    u.u_arg[1] == (int)0x20202020U &&
	    u.u_arg[2] == (int)0x30303030U &&
	    u.u_arg[3] == (int)0x40404040U);
	CHECK("fifth stack argument", u.u_arg[4] == (int)0x50505050U);
	CHECK("unused sixth argument cleared", u.u_arg[5] == 0);
	CHECK("five argument complete copy", copyin_calls == 1 &&
	    copyin_source == (uintptr_t)&stack_words[8] &&
	    copyin_length == sizeof(stack_words[8]));
}

static void
check_six_arguments(int aligned)
{
	u_int stack_words[12] = { 0 };
	size_t first_argument = aligned ? 9U : 8U;

	reset_fixture(stack_words, 12);
	frame.tf_psr = aligned ? XPSR_STKALIGN : 0;
	stack_words[first_argument] = 0x51515151U;
	stack_words[first_argument + 1] = 0x61616161U;
	syscall_nargs[2] = 6;
	exercise_dispatch(2);
	CHECK(aligned ? "aligned six dispatch" : "six dispatch",
	    handler_calls == 1 && handler_slot == 2);
	CHECK(aligned ? "aligned fifth argument" : "six fifth argument",
	    u.u_arg[4] == (int)0x51515151U);
	CHECK(aligned ? "aligned sixth argument" : "six sixth argument",
	    u.u_arg[5] == (int)0x61616161U);
	CHECK(aligned ? "aligned complete copy" : "six complete copy",
	    copyin_calls == 1 &&
	    copyin_source == (uintptr_t)&stack_words[first_argument] &&
	    copyin_length == 2 * sizeof(stack_words[0]));
}

static void
check_register_convention(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	syscall_nargs[3] = 1;
	exercise_dispatch(3);
	CHECK("one argument dispatch", handler_calls == 1 && handler_slot == 3);
	CHECK("nonzero call publishes four registers",
	    u.u_arg[0] == (int)0x10101010U &&
	    u.u_arg[1] == (int)0x20202020U &&
	    u.u_arg[2] == (int)0x30303030U &&
	    u.u_arg[3] == (int)0x40404040U);
	CHECK("nonzero call clears unused tail", u.u_arg[4] == 0 && u.u_arg[5] == 0);
}

static void
check_zero_and_invalid_calls(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	exercise_dispatch(0);
	CHECK("zero argument dispatch", handler_calls == 1 && handler_slot == 0);
	CHECK("zero argument stale state cleared", u.u_arg[0] == 0 &&
	    u.u_arg[1] == 0 && u.u_arg[2] == 0 && u.u_arg[3] == 0 &&
	    u.u_arg[4] == 0 && u.u_arg[5] == 0);

	reset_fixture(stack_words, 12);
	exercise_dispatch(99);
	CHECK("invalid syscall routes slot zero",
	    handler_calls == 1 && handler_slot == 0);
	CHECK("invalid syscall clears stale state", u.u_arg[0] == 0 &&
	    u.u_arg[5] == 0);
}

static void
check_invalid_first_stack_word(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	logical_user_upper = (uintptr_t)&stack_words[8];
	syscall_nargs[1] = 5;
	exercise_dispatch(1);
	CHECK("invalid first word reports EFAULT", u.u_error == EFAULT);
	CHECK("invalid first word skips dispatch", handler_calls == 0);
	CHECK("invalid first word skips setjmp", setjmp_calls == 0);
	CHECK("invalid first word leaves cleared tail",
	    u.u_arg[4] == 0 && u.u_arg[5] == 0);
}

static void
check_truncated_second_stack_word(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	stack_words[8] = 0x51515151U;
	stack_words[9] = 0x61616161U;
	logical_user_upper = (uintptr_t)&stack_words[9] + 2U;
	syscall_nargs[2] = 6;
	exercise_dispatch(2);
	CHECK("truncated second word reports EFAULT", u.u_error == EFAULT);
	CHECK("truncated second word skips dispatch", handler_calls == 0);
	CHECK("truncated tail publishes no argument",
	    u.u_arg[4] == 0 && u.u_arg[5] == 0);
	CHECK("truncated tail requested complete span",
	    copyin_calls == 1 && copyin_length == 2 * sizeof(stack_words[0]));
}

static void
check_forced_copy_failure(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	stack_words[8] = 0x51515151U;
	stack_words[9] = 0x61616161U;
	force_copyin_failure = 1;
	syscall_nargs[2] = 6;
	exercise_dispatch(2);
	CHECK("forced copy failure reports EFAULT", u.u_error == EFAULT);
	CHECK("forced copy failure skips dispatch", handler_calls == 0);
	CHECK("forced copy failure publishes no tail",
	    u.u_arg[4] == 0 && u.u_arg[5] == 0);
}

static void
check_argument_count_bound(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	syscall_nargs[3] = 7;
	exercise_dispatch(3);
	CHECK("oversized metadata reports EINVAL", u.u_error == EINVAL);
	CHECK("oversized metadata skips copy", copyin_calls == 0);
	CHECK("oversized metadata skips dispatch", handler_calls == 0);
	CHECK("oversized metadata clears arguments", u.u_arg[0] == 0 &&
	    u.u_arg[5] == 0);
}

static void
check_stack_address_wrap(void)
{
	u_int stack_words[12] = { 0 };

	reset_fixture(stack_words, 12);
	frame.tf_sp = UINT_MAX - 31U;
	syscall_nargs[1] = 5;
	exercise_dispatch(1);
	CHECK("wrapped stack address reports EFAULT", u.u_error == EFAULT);
	CHECK("wrapped stack address skips copy", copyin_calls == 0);
	CHECK("wrapped stack address skips dispatch", handler_calls == 0);
}

int
main(void)
{
	(void)&baduaddr;
	(void)&copyin;
	check_five_arguments();
	check_six_arguments(0);
	check_six_arguments(1);
	check_register_convention();
	check_zero_and_invalid_calls();
	check_invalid_first_stack_word();
	check_truncated_second_stack_word();
	check_forced_copy_failure();
	check_argument_count_bound();
	check_stack_address_wrap();
	return failures != 0;
}
