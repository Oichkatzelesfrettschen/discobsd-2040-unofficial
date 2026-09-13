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
 * expect read(2) semantics, so it goes straight to getdents64(2) and
 * repacks each record into the struct dirent that <sys/dir.h> makes
 * expand.c's struct direct an alias of.  Keeping the kernel's own file
 * offset as the cursor is what lets pwd.c abandon a directory scan
 * partway and expand.c open the next directory on the same descriptor
 * number without inheriting a stale position.
 */
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

long syscall(long, ...);

#define SH_ARENA_BYTES (4 * 1024 * 1024)

static char sh_arena[SH_ARENA_BYTES];
static long sh_arena_top;

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

struct linux_dirent64 {
	uint64_t	d_ino;
	int64_t		d_off;
	unsigned short	d_reclen;
	unsigned char	d_type;
	char		d_name[1];
};

long
sh_host_getdents(int fd, void *buf, unsigned long nbytes)
{
	char raw[8192];
	unsigned long rawlen, rawoff, used = 0;
	long got;

	if (nbytes > sizeof(raw))
		nbytes = sizeof(raw);
	got = syscall(SYS_getdents64, fd, raw, nbytes);
	if (got <= 0)
		return got;

	rawlen = (unsigned long)got;
	for (rawoff = 0; rawoff < rawlen; ) {
		struct linux_dirent64 *src;
		struct dirent out;
		size_t namelen, reclen;

		src = (struct linux_dirent64 *)(raw + rawoff);
		rawoff += src->d_reclen;

		namelen = strlen(src->d_name);
		reclen = (offsetof(struct dirent, d_name) + namelen + 4) & ~3UL;
		if (used + reclen > nbytes)
			break;

		memset(&out, 0, sizeof(out));
		out.d_ino = (ino_t)src->d_ino;
		out.d_off = (off_t)src->d_off;
		out.d_reclen = (unsigned short)reclen;
		out.d_type = src->d_type;
		memcpy(out.d_name, src->d_name, namelen + 1);
		memcpy((char *)buf + used, &out, reclen);
		used += reclen;
	}
	return (long)used;
}
