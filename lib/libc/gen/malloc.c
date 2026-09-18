#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef debug
#include <sys/types.h>
#include <sys/uio.h>

#define ASSERT(p) if(!(p))botch("p")

/*
 * Can't use 'printf' below because that can call malloc().  If the malloc
 * arena is corrupt malloc() calls botch() which calls printf which calls malloc
 * ... result is a recursive loop which underflows the stack.
*/

static botch(s)
char *s;
{
	struct	iovec	iov[3];
	register struct iovec *v = iov;
	char	*ab = "assertion botched: ";

	v->iov_base = ab;
	v->iov_len = strlen(ab);
	v++;
	v->iov_base = s;
	v->iov_len = strlen(s);
	v++;
	v->iov_base = "\n";
	v->iov_len = 1;

	writev(STDOUT_FILENO, iov, 3);
	abort();
}
#else
#define ASSERT(p)
#endif	/* debug */

/*
 * The origins of the following ifdef are lost.  The only comment attached
 * to it, "avoid break bug", probably has something to do with a bug in
 * an older PDP-11 kernel.  Maybe it's still a bug in the current kernel.
 * We'll probably never know ...
 */
#ifdef pdp11
#	define GRANULE 64
#else
#	define GRANULE 0
#endif

/*
 * C storage allocator
 *
 * Uses circular first-fit strategy.  Works with a noncontiguous, but
 * monotonically linked, arena.  Each block is preceded by a ptr to the
 * pointer of the next following block.  Blocks are exact number of words
 * long aligned to the data type requirements of ALIGN.
 *
 * Bit 0 (LSB) of pointers is used to indicate whether the block associated
 * with the pointer is in use.  A 1 indicates a busy block and a 0 a free
 * block (obviously pointers can't point at odd addresses).  Gaps in arena
 * are merely noted as busy blocks.  The last block of the arena (pointed
 * to by alloct) is empty and has a pointer to first.  Idle blocks are
 * coalesced during space search
 *
 * Different implementations may need to redefine ALIGN, NALIGN, BLOCK,
 * BUSY, INT where INT is integer type to which a pointer can be cast.
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
#define	BLOCK		1024	/* a multiple of WORD */

#define	BUSY		1

#define	testbusy(p)	((INT)(p)&BUSY)
#define	setbusy(p)	(union store *)((INT)(p)|BUSY)
#define	clearbusy(p)	(union store *)((INT)(p)&~(INT)BUSY)

/*
 * The largest request the arithmetic below can carry. A request is rounded
 * up to whole words, given a header word, and the arena extension that
 * serves it is rounded up to BLOCK and handed to sbrk() as an int. Every one
 * of those steps stays inside size_t and int for nbytes at or under
 * MAXBYTES, so the bound is tested once, at entry, and the arena is touched
 * only by a request whose every intermediate value is representable.
 * Anything larger is ENOMEM before the search starts: the address space
 * that would hold it does not exist on the target either.
 */
#define	MAXBYTES	((size_t)INT_MAX / 2 - BLOCK - 2 * WORD)

union store {
	union store	*ptr;
	ALIGN		dummy[NALIGN];
	int		calloc;	/* calloc clears an array of integers */
};

static union store	allocs[2];	/* initial arena */
static union store	*allocp;	/* search ptr */
static union store	*alloct;	/* arena top */
static union store	*allocx;	/* for benefit of realloc */

/*
 * A zero-byte request returns NULL with errno untouched, and a failed
 * request returns NULL with errno set to ENOMEM. The first is the arena's
 * historical answer and every caller in the tree is written against it; the
 * second is what the C standard and POSIX name, and what a caller that
 * reports the failure prints. tests/libc_contracts/malloc_test.c pins both.
 */
void *
malloc(nbytes)
	size_t nbytes;
{
	register union store *p, *q;
	register size_t nw;
	static size_t temp;	/* coroutines assume no auto */

	if (nbytes == 0)
		return(NULL);
	if (nbytes > MAXBYTES) {
		errno = ENOMEM;
		return(NULL);
	}
	if (allocs[0].ptr == 0) {	/* first time */
		allocs[0].ptr = setbusy(&allocs[1]);
		allocs[1].ptr = setbusy(&allocs[0]);
		alloct = &allocs[1];
		allocp = &allocs[0];
	}
	nw = (nbytes+WORD+WORD-1)/WORD;
	ASSERT(allocp >= allocs && allocp <= alloct);
	ASSERT(allock());
	for (p = allocp; ; ) {
		for (temp = 0; ; ) {
			if (!testbusy(p->ptr)) {
				while(!testbusy((q = p->ptr)->ptr)) {
					ASSERT(q > p && q < alloct);
					p->ptr = q->ptr;
				}
				if ((size_t)(q - p) >= nw)
					goto found;
			}
			q = p;
			p = clearbusy(p->ptr);
			if (p > q)
				ASSERT(p <= alloct);
			else if (q != alloct || p != allocs) {
				ASSERT(q == alloct && p == allocs);
				errno = ENOMEM;
				return(NULL);
			} else if (++temp > 1)
				break;
		}
		q = (union store *)sbrk(0);
		/*
		 * Line up on page boundry so we can get the last drip at
		 * the end ...
		 */
		temp = ((((INT)q + WORD*nw + BLOCK-1)/BLOCK)*BLOCK
			- (INT)q) / WORD;
		if ((INT)q + (temp+GRANULE)*WORD < (INT)q) {
			errno = ENOMEM;
			return(NULL);
		}
		q = (union store *)sbrk((int)(temp*WORD));
		if (q == (union store *)-1) {
			errno = ENOMEM;
			return(NULL);
		}
		ASSERT(q > alloct);
		alloct->ptr = q;
		if (q != alloct+1)
			alloct->ptr = setbusy(alloct->ptr);
		alloct = q->ptr = q+temp-1;
		alloct->ptr = setbusy(allocs);
	}
found:
	allocp = p + nw;
	ASSERT(allocp <= alloct);
	if (q > allocp) {
		allocx = allocp->ptr;
		allocp->ptr = p->ptr;
	}
	p->ptr = setbusy(allocp);
	return((char *)(p+1));
}

/*
 * Freeing strategy tuned for LIFO allocation.
 */
void free(ap)
	register void *ap;
{
	register union store *p = (union store *)ap;

	if (p == NULL)
	        return;
	ASSERT(p > clearbusy(allocs[1].ptr) && p <= alloct);
	ASSERT(allock());
	allocp = --p;
	ASSERT(testbusy(p->ptr));
	p->ptr = clearbusy(p->ptr);
	ASSERT(p->ptr > allocp && p->ptr <= alloct);
}

/*
 * realloc(p, nbytes) resizes a block obtained from malloc() and returns
 * its new location, or NULL. The block stays allocated, at its address
 * and with its contents, until a replacement holds a copy: a NULL return
 * for a positive size leaves p valid and owned by the caller, and errno
 * says why. The historical version freed p before searching, so a failed
 * resize handed the caller a pointer whose block the next malloc() could
 * reuse. tests/libc_contracts/malloc_test.c allocates after a forced
 * failure and checks the old block is not what it gets.
 *
 * A zero size frees p and returns NULL, which is what it did before, as
 * malloc(0) is NULL. A block the caller has already freed is resized the
 * way the historical contract allowed, into a fresh block, and allocx
 * restores the header word that a new block overlapping the old one has
 * overwritten; a busy block never overlaps its replacement, so that path
 * is reached only from a freed block.
 *
 * The block grows in place when the free blocks after it hold the
 * difference, and shrinks in place by splitting a free block off its end,
 * so a resize inside a 144 KB window costs the difference and not the sum.
 * Both leave allocp at this block's header, because the free blocks the
 * growth absorbs may include the one the search pointer stands on.
 */
void *
realloc(vp, nbytes)
	register void *vp;
	size_t nbytes;
{
	register union store *p = vp;
	register union store *q;
	union store *s, *t;
	register size_t nw;
	size_t onw;

	if (p == NULL)
	        return malloc(nbytes);
	if (nbytes == 0) {
		if (testbusy(p[-1].ptr))
			free((char *)p);
		return(NULL);
	}
	if (nbytes > MAXBYTES) {
		errno = ENOMEM;
		return(NULL);
	}
	nw = (nbytes+WORD-1)/WORD;
	onw = (size_t)(clearbusy(p[-1].ptr) - p);
	if (testbusy(p[-1].ptr)) {
		if (nw <= onw) {
			if (nw < onw) {
				q = p + nw;
				q->ptr = clearbusy(p[-1].ptr);
				p[-1].ptr = setbusy(q);
				allocp = p - 1;
			}
			return((char *)p);
		}
		q = clearbusy(p[-1].ptr);
		while (!testbusy(q->ptr))
			q = q->ptr;
		if ((size_t)(q - p) >= nw) {
			if (q > p + nw)
				(p + nw)->ptr = q;
			p[-1].ptr = setbusy(p + nw);
			allocp = p - 1;
			return((char *)p);
		}
		q = (union store *)malloc(nbytes);
		if (q == NULL)
			return(NULL);
		s = p;
		t = q;
		while (onw-- != 0)
			*t++ = *s++;
		free((char *)p);
		return((char *)q);
	}
	q = (union store *)malloc(nbytes);
	if (q == NULL || q == p)
		return((char *)q);
	s = p;
	t = q;
	if (nw < onw)
		onw = nw;
	while (onw-- != 0)
		*t++ = *s++;
	if (q < p && q+nw >= p)
		(q+(q+nw-p))->ptr = allocx;
	return((char *)q);
}

#ifdef	debug
static allock()
{
#ifdef longdebug
	register union store *p;
	int x;
	x = 0;
	for (p= &allocs[0]; clearbusy(p->ptr) > p; p=clearbusy(p->ptr)) {
		if (p == allocp)
			x++;
	}
	ASSERT(p == alloct);
	return((x == 1) | (p == allocp));
#else
	return(1);
#endif
}
#endif /* debug */
