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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

long syscall(long, ...);

#define SH_ARENA_BYTES (4 * 1024 * 1024)

static char sh_arena[SH_ARENA_BYTES];
static long sh_arena_top;

struct linux_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[1];
};

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
	char *trace;
	ssize_t received;

	received = (ssize_t)syscall(SYS_getdents64, fd, buf, nbytes);
	trace = getenv("SH_TEST_DIRECTORY_TRACE");
	if ((trace != NULL && trace[0] == '1' && trace[1] == '\0') ||
	    access(".sh-directory-trace", F_OK) == 0) {
		size_t offset;

		fprintf(stderr, "getdents64 fd=%d requested=%zu received=%zd\n",
		    fd, nbytes, received);
		for (offset = 0; received > 0 && offset < (size_t)received; ) {
			struct linux_dirent64 *entry;

			entry = (struct linux_dirent64 *)((char *)buf + offset);
			fprintf(stderr,
			    "dirent offset=%zu inode=%llu cursor=%lld reclen=%u type=%u name=%s\n",
			    offset, (unsigned long long)entry->d_ino,
			    (long long)entry->d_off, entry->d_reclen,
			    entry->d_type, entry->d_name);
			if (entry->d_reclen == 0)
				break;
			offset += entry->d_reclen;
		}
	}
	return received;
}
