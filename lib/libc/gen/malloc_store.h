#ifndef _LIBC_GEN_MALLOC_STORE_H_
#define _LIBC_GEN_MALLOC_STORE_H_

#include <stdint.h>

/*
 * The block header of the malloc.c arena.  Each block is preceded by one
 * union store whose ptr names the header of the next block, and bit 0 of
 * that pointer is set while the block is in use.
 *
 * The tag lives in bit 0 of a pointer value, so the integer that carries a
 * pointer has to hold every bit of it: uintptr_t, not int. A 32-bit target
 * cannot tell the two apart; a 64-bit host running this file as a test can,
 * and truncating a pointer through int there returns a different pointer.
 */
#define	INT		uintptr_t
#define	ALIGN		int
#define	NALIGN		1
#define	WORD		sizeof(union store)

#define	BUSY		1

#define	testbusy(p)	((INT)(p)&BUSY)
#define	setbusy(p)	(union store *)((INT)(p)|BUSY)
#define	clearbusy(p)	(union store *)((INT)(p)&~(INT)BUSY)

union store {
	union store	*ptr;
	ALIGN		dummy[NALIGN];
	int		calloc;	/* calloc clears an array of integers */
};

#endif /* !_LIBC_GEN_MALLOC_STORE_H_ */
