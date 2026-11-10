#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "malloc_store.h"

/*
 * C17 7.22.3.1.  malloc() aligns a block to WORD, so a wider alignment is
 * carved out of a block that is larger by the alignment: the first aligned
 * address past the block's start receives a header of its own, and the
 * words in front of it return to the arena as a free block.  The result is
 * an ordinary arena block that free() and realloc() accept.
 *
 * An alignment that is zero or has more than one bit set is EINVAL.  A zero
 * size is NULL with errno untouched, which is malloc(0)'s answer.
 */
/*
 * The header word lies outside the object malloc() returned, so its address
 * is formed from the integer value of the block address.
 */
static union store *
header_of(union store *block)
{
	return (union store *)((INT)block - WORD);
}

void *
aligned_alloc(size_t alignment, size_t size)
{
	union store *block, *aligned, *header;

	if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
		errno = EINVAL;
		return NULL;
	}
	if (alignment <= WORD || size == 0)
		return malloc(size);
	if (alignment > SIZE_MAX - size) {
		errno = ENOMEM;
		return NULL;
	}
	block = malloc(size + alignment);
	if (block == NULL)
		return NULL;
	if (((INT)block & (alignment - 1)) != 0) {
		/*
		 * Blocks and alignments are both multiples of WORD, so the
		 * aligned address lies at least WORD above an unaligned block
		 * and at most alignment - WORD above it: its header fits at
		 * or above the block's first word, and size bytes fit below
		 * the block's end.
		 */
		aligned = (union store *)(((INT)block + alignment - 1) &
		    ~(INT)(alignment - 1));
		header = header_of(block);
		header_of(aligned)->ptr = header->ptr;
		header->ptr = setbusy(header_of(aligned));
		free(block);
		block = aligned;
	}
	/* Shrinking in place splits the surplus tail off as a free block. */
	return realloc(block, size);
}
