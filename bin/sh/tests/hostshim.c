/*
 * Host-build support for bin/sh: see hostshim.h.
 *
 * sbrk(2) here carves a private arena instead of moving the process
 * break, because blok.c owns every byte between brkbegin and brkend
 * and would collide with the libc allocator otherwise.  The board
 * build uses lib/libc/arm/sys/sbrk.c and never reaches this file.
 *
 * sh_host_getdents() answers the raw directory read in expand.c's
 * getdir(), which pwd.c calls too.  Both hand it a plain descriptor and
 * expect read(2) semantics, so it goes straight to getdents64(2).  The
 * compiled layout witness proves that the ILP32 host record consumed by
 * expand.c matches the Linux kernel record before this function passes
 * the bytes through unchanged.  Keeping the kernel's own file offset as
 * the cursor lets pwd.c abandon a directory scan partway and expand.c
 * open the next directory on the same descriptor number without
 * inheriting a stale position.
 */
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

long syscall(long, ...);

#define SH_ARENA_BYTES (4 * 1024 * 1024)

static char sh_arena[SH_ARENA_BYTES];
static long sh_arena_top;

int
sh_host_command_alloc_failure(void)
{
	char *value;

	value = getenv("SH_TEST_COMMAND_BEGINLIST_FAIL");
	return value != NULL && value[0] == '1' && value[1] == '\0';
}

void *
sbrk(intptr_t incr)
{
	char *old = sh_arena + sh_arena_top;

	if (sh_arena_top + incr > SH_ARENA_BYTES || sh_arena_top + incr < 0)
		return (void *)-1;
	sh_arena_top += incr;
	return old;
}

int
brk(void *addr)
{
	long want = (char *)addr - sh_arena;

	if (want < 0 || want > SH_ARENA_BYTES)
		return -1;
	sh_arena_top = want;
	return 0;
}

ssize_t
sh_host_getdents(int fd, void *buf, size_t nbytes)
{
	return (ssize_t)syscall(SYS_getdents64, fd, buf, nbytes);
}
