/*
 * lib/libc/gen/malloc.c over an arena the test supplies.  The hosting
 * library's headers come first so its sbrk keeps its own declaration; the
 * allocator's calls are renamed afterwards.  The allocation names move
 * aside on the command line, which leaves the hosting allocator in place.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void arena_install(char *, size_t);
size_t arena_break(void);
void *arena_sbrk(int);

#define sbrk arena_sbrk

#include "../../lib/libc/gen/malloc.c"

static char *arena_base;
static size_t arena_size;
static size_t arena_used;

/*
 * The allocator links its blocks in ascending address order from the two
 * seed blocks in allocs, so every byte the break supplies has to lie above
 * them.  On the target the break starts at the end of bss.  Here the caller
 * passes storage from main()'s frame, and an arena below the seed blocks is
 * refused, since the allocator would answer it with ENOMEM on every call.
 */
void
arena_install(char *base, size_t size)
{
	size_t skew = (size_t)(-(uintptr_t)base & 15);

	if (size <= skew || base + skew <= (char *)&allocs[1]) {
		fprintf(stderr, "arena: storage lies below the allocator's "
		    "seed blocks; no allocation was measured\n");
		exit(2);
	}
	arena_base = base + skew;
	arena_size = size - skew;
	arena_used = 0;
}

/* The number of arena bytes the allocator has taken from the break. */
size_t
arena_break(void)
{
	return arena_used;
}

void *
arena_sbrk(int increment)
{
	char *previous = arena_base + arena_used;

	if (increment < 0 || (size_t)increment > arena_size - arena_used) {
		errno = ENOMEM;
		return (void *)-1;
	}
	arena_used += (size_t)increment;
	return previous;
}
