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

#include <stddef.h>
#include <stdint.h>

#include <machine/usercopy.h>

#ifndef USERCOPY_FUNCTIONS
#define USERCOPY_FUNCTIONS "usercopy_functions.c"
#endif

typedef char *caddr_t;
typedef unsigned int u_int;

#define EFAULT 14
#define __user_data_start ((char *)(uintptr_t)0x20000000U)
#define USER_TOP(process) user_top_value

struct proc {
	int unused;
};

struct user {
	struct proc *u_procp;
};

static struct proc current_process;
struct user u = { &current_process };
static unsigned int user_top_value = 0x20024000U;

static unsigned int recorded_copy_calls;
static const void *recorded_copy_source;
static void *recorded_copy_destination;
static size_t recorded_copy_length;

static void
recording_bcopy(const void *source, void *destination, size_t length)
{
	recorded_copy_calls++;
	recorded_copy_source = source;
	recorded_copy_destination = destination;
	recorded_copy_length = length;
}

#define bcopy recording_bcopy
#include USERCOPY_FUNCTIONS

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) \
		failures++; \
} while (0)

static caddr_t
address(unsigned int value)
{
	return (caddr_t)(uintptr_t)value;
}

static void
reset_recording(void)
{
	recorded_copy_calls = 0;
	recorded_copy_source = NULL;
	recorded_copy_destination = NULL;
	recorded_copy_length = 0;
}

static void
check_single_address_admission(void)
{
	user_top_value = 0x20024000U;
	CHECK(baduaddr(address(0x20000000U)) == 0);
	CHECK(baduaddr(address(0x20023fffU)) == 0);
	CHECK(baduaddr(address(0x1fffffffU)) == 1);
	CHECK(baduaddr(address(0x20024000U)) == 1);
	user_top_value = 0x20028000U;
	CHECK(baduaddr(address(0x20024000U)) == 0);
}

static void
check_zero_length_admission(void)
{
	user_top_value = 0x20024000U;
	reset_recording();
	CHECK(copyin(address(0xffffffffU), address(0U), 0U) == 0);
	CHECK(copyout(address(0U), address(0xffffffffU), 0U) == 0);
	CHECK(recorded_copy_calls == 0);
}

static void
check_valid_spans(void)
{
	char kernel_bytes[4] = { 1, 2, 3, 4 };

	user_top_value = 0x20024000U;
	reset_recording();
	CHECK(copyin(address(0x20000000U), kernel_bytes, 0x24000U) == 0);
	CHECK(recorded_copy_calls == 1);
	CHECK(recorded_copy_source == address(0x20000000U));
	CHECK(recorded_copy_destination == kernel_bytes);
	CHECK(recorded_copy_length == 0x24000U);

	reset_recording();
	CHECK(copyout(kernel_bytes, address(0x20023fffU), 1U) == 0);
	CHECK(recorded_copy_calls == 1);
	CHECK(recorded_copy_source == kernel_bytes);
	CHECK(recorded_copy_destination == address(0x20023fffU));
	CHECK(recorded_copy_length == 1U);
}

static void
check_rejected_spans(void)
{
	char destination[4] = { 1, 2, 3, 4 };

	user_top_value = 0x20024000U;
	reset_recording();
	CHECK(copyin(address(0x20023fffU), destination, 2U) == EFAULT);
	CHECK(copyout(destination, address(0x1fffffffU), 1U) == EFAULT);
	CHECK(copyin(address(0x20024000U), destination, 1U) == EFAULT);
	CHECK(copyout(destination, address(0x20001000U), 0xfffff001U) ==
	    EFAULT);
	CHECK(copyin(address(0x20001000U), destination, 0xfffff001U) ==
	    EFAULT);
	CHECK(recorded_copy_calls == 0);
	CHECK(destination[0] == 1 && destination[1] == 2 &&
	    destination[2] == 3 && destination[3] == 4);

	user_top_value = 0x1fffffffU;
	CHECK(copyin(address(0x20000000U), destination, 1U) == EFAULT);
	CHECK(recorded_copy_calls == 0);
}

static void
check_conditional_extension(void)
{
	char kernel_byte;

	user_top_value = 0x20024000U;
	reset_recording();
	CHECK(copyin(address(0x20024000U), &kernel_byte, 1U) == EFAULT);
	CHECK(recorded_copy_calls == 0);

	user_top_value = 0x20028000U;
	CHECK(copyin(address(0x20024000U), &kernel_byte, 1U) == 0);
	CHECK(recorded_copy_calls == 1);
	CHECK(recorded_copy_length == 1U);
}

int
main(void)
{
	check_single_address_admission();
	check_zero_length_admission();
	check_valid_spans();
	check_rejected_spans();
	check_conditional_extension();
	return failures != 0;
}
