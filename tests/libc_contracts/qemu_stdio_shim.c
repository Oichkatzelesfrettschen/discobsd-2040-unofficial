/*
 * The libc services the tree's stdio objects call, for a Thumb-1 test linked
 * with those objects under qemu-arm.
 *
 * lib/libc/stdio compiled from the tree defines fflush and fclose, the same
 * names newlib's stdio defines. Any newlib object that reaches newlib's
 * reentrancy structure (malloc, close, fstat, isatty, read and write all do,
 * through errno) pulls in newlib's stdio with it and the link fails on the
 * duplicate definitions. This file supplies those seven services directly so
 * the test links the tree's stdio alone: a bump allocator whose free()
 * releases nothing, which a test of bounded length can afford; close,
 * isatty, fstat and read answers for streams the test never opens; an errno
 * cell; and write as the Linux EABI call. Pair it with -DQEMU_SUPPORT_EXIT_DIRECT on
 * tests/libbn/qemu_support.c, whose exit() would pull the same objects in.
 */

#include <stddef.h>

static char arena[1u << 20] __attribute__((aligned(8)));
static size_t arena_used;
static int errno_cell;

void *
malloc(size_t n)
{
	size_t rounded = (n + 7u) & ~(size_t)7u;
	void *p;

	if (rounded < n || arena_used + rounded > sizeof arena)
		return 0;
	p = arena + arena_used;
	arena_used += rounded;
	return p;
}

void
free(void *p)
{
	(void)p;
}

int *
__errno(void)
{
	return &errno_cell;
}

int
close(int fd)
{
	(void)fd;
	return 0;
}

int
isatty(int fd)
{
	(void)fd;
	return 0;
}

int
fstat(int fd, void *st)
{
	(void)fd;
	(void)st;
	return -1;
}

/* The scanf tests read string streams, which _filbuf ends without a read. */
int
read(int fd, void *buf, size_t n)
{
	(void)fd;
	(void)buf;
	(void)n;
	return 0;
}

int
write(int fd, const void *buf, size_t n)
{
	register long r7 __asm__("r7") = 4;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = (long)n;

	__asm__ volatile("svc #0"
	    : "+r"(r0)
	    : "r"(r7), "r"(r1), "r"(r2)
	    : "memory");
	return (int)r0;
}
