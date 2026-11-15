/*
 * The RP2040 kernel's break, for a host build of bin/sh.
 *
 * On the board the MPU grants the whole 192 KB process window
 * (sys/arch/rp2040/doc/MPU.md), so a store between the break and the
 * stack succeeds instead of raising the SIGSEGV that fault.c answers by
 * growing the break. brk() in sys/kern/kern_mman.c then zeroes each range
 * it grants, and swap carries only the bytes below the break. A Linux
 * break has page granularity and an unmapped page above it, so the host
 * shell never meets either effect.
 *
 * board-break.sh compiles setbrk.c with -Dsbrk=sh_board_sbrk and links
 * this file in its place. The arena is a fixed array in which every store
 * succeeds, growth zeroes [old, new) as kern_mman.c does, shrinking only
 * lowers the break, and a request past the arena fails with ENOMEM.
 */
#include <errno.h>
#include <stddef.h>
#include <string.h>

#define BOARD_ARENA_SIZE (64 * 1024)

void *sh_board_sbrk(int incr);

static char board_arena[BOARD_ARENA_SIZE] __attribute__((aligned(8)));
static char *board_break = board_arena;

void *
sh_board_sbrk(int incr)
{
	char *old = board_break;
	ptrdiff_t offset = old - board_arena;

	if ((incr < 0 && -(ptrdiff_t)incr > offset) ||
	    (incr > 0 && (ptrdiff_t)incr > BOARD_ARENA_SIZE - offset)) {
		errno = ENOMEM;
		return (void *)-1;
	}
	if (incr > 0)
		memset(old, 0, (size_t)incr);
	board_break = old + incr;
	return old;
}
