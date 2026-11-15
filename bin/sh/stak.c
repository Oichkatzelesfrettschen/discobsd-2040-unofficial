/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

/* ========	storage allocation	======== */

/*
 * Grow the break until it covers every byte below top. fault() grows the
 * break only when a store past it raises SIGSEGV, but the RP2040 MPU grants
 * the whole process window (sys/arch/rp2040/doc/MPU.md), so such a store
 * succeeds there. brk(2) then zeroes each range it grants
 * (sys/kern/kern_mman.c), and swap carries only the bytes below the break
 * (sys/kern/vm_swap.c), so a block header or stack item stored above the
 * break is lost at the next growth or swap. A zeroed block header links its
 * block to address 0, and alloc() follows that link into the boot ROM.
 */
void
brkroom(char *top)
{
	while (brkend < top)
	{
		if (setbrk(brkincr) == (char *)-1)
			error(nospace);
	}
}

char *
getstak(int asize)
{
	register char	*oldstak;
	register int	size;

	size = round(asize, BYTESPERWORD);
	brkroom(stakbot + size);
	oldstak = stakbot;
	staktop = stakbot += size;
	return(oldstak);
}

/*
 * set up stack for local use
 * should be followed by `endstak'
 */
char *
locstak(void)
{
	if (brkend - stakbot < BRKINCR)
	{
		if (setbrk(brkincr) == (char *)-1)
			error(nostack);
		if (brkincr < BRKMAX)
			brkincr += 256;
	}
	return(stakbot);
}

char *
savstak(void)
{
	assert(staktop == stakbot);
	return(stakbot);
}

char *
endstak(char *argp)
{
	register char	*oldstak;

	stakput(argp, 0);
	oldstak = stakbot;
	stakbot = staktop = (char *)round(argp, BYTESPERWORD);
	return(oldstak);
}

void
tdystak(char *x)
{
	while ((char *)(stakbsy) > (char *)(x))
	{
		free(stakbsy);
		stakbsy = stakbsy->word;
	}
	staktop = stakbot = max((char *)(x), (char *)(stakbas));
	rmtemp((struct ionod *)x);
}

void
stakchk(void)
{
	if ((brkend - stakbas) > BRKINCR + BRKINCR)
		setbrk(-BRKINCR);
}

char *
cpystak(char *x)
{
	register char	*argp = locstak();

	brkroom(argp + length(x));
	return(endstak(movstr(x, argp)));
}
