/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

/*
 * storage allocator
 * (circular first fit strategy)
 */

#define BUSY 01
/* RP2040 and the ILP32 host gate align block links to four bytes. */
#define PARSE_ALLOC 02
#define BLOCK_FLAGS (BUSY | PARSE_ALLOC)
#define PARSE_INACTIVE 0
#define PARSE_ACTIVE 1
#define PARSE_ALLOCATION_PRESENT 2
#define busy(x) (Rcheat((x)->word) & BUSY)
#define next_block(x) ((struct blk *)(Rcheat((x)->word) & ~BLOCK_FLAGS))

unsigned int    brkincr = BRKINCR;
struct blk *blokp;                      /*current search pointer*/
struct blk *bloktop;            /* top of arena (last blok) */

char            *brkbegin;
static int      parse_allocation_state;

char *
alloc(unsigned int nbytes)
{
	register unsigned int rbytes = round(nbytes+BYTESPERWORD, BYTESPERWORD);

	for (;;)
	{
		int     c = 0;
		register struct blk *p = blokp;
		register struct blk *q;

		do
		{
			if (!busy(p))
			{
				while (!busy(q = p->word))
					p->word = q->word;
				if ((unsigned)((char *)q - (char *)p) >= rbytes)
				{
					blokp = (struct blk *)((char *)p + rbytes);
					if (q > blokp)
						blokp->word = p->word;
					p->word = (struct blk *)(Rcheat(blokp) | BUSY |
					    (parse_allocation_state ? PARSE_ALLOC : 0));
					if (parse_allocation_state)
						parse_allocation_state = PARSE_ALLOCATION_PRESENT;
					return((char *)(p + 1));
				}
			}
			q = p;
			p = next_block(p);
		} while (p > q || (c++) == 0);
		addblok(rbytes);
	}
}

void
addblok(unsigned int reqd)
{
	if (stakbot == NIL)
	{
		brkbegin = setbrk(BRKINCR * 5);
		bloktop = (struct blk *)brkbegin;
	}

	if (stakbas != staktop)
	{
		register char *rndstak;
		register struct blk *blokstak;

		pushstak(0);
		rndstak = (char *)round(staktop, BYTESPERWORD);
		blokstak = (struct blk *)(stakbas) - 1;
		blokstak->word = stakbsy;
		stakbsy = blokstak;
		bloktop->word = (struct blk *)(Rcheat(rndstak) | BUSY);
		bloktop = (struct blk *)(rndstak);
	}
	reqd += brkincr;
	reqd &= ~(brkincr - 1);
	blokp = bloktop;
	bloktop = bloktop->word = (struct blk *)(Rcheat(bloktop) + reqd);
	bloktop->word = (struct blk *)(brkbegin + 1);
	{
		register char *stakadr = (char *)(bloktop + 2);

		if (stakbot != staktop)
			staktop = movstr(stakbot, stakadr);
		else
			staktop = stakadr;

		stakbas = stakbot = stakadr;
	}
}

void
free(void *ap)
{
	register struct blk *p;

	if ((p = ap) && p < bloktop)
	{
#ifdef DEBUG
		chkbptr(p);
#endif
		--p;
		p->word = (struct blk *)(Rcheat(p->word) & ~BLOCK_FLAGS);
	}
}

void
parse_alloc_begin(void)
{
	parse_allocation_state = PARSE_ACTIVE;
}

static void
parse_alloc_finish(int rollback)
{
	register struct blk *p = (struct blk *)brkbegin;
	register struct blk *q;

	if (parse_allocation_state != PARSE_ALLOCATION_PRESENT)
	{
		parse_allocation_state = PARSE_INACTIVE;
		return;
	}
	for (;;)
	{
		q = next_block(p);
		if (Rcheat(p->word) & PARSE_ALLOC)
		{
			if (rollback)
				p->word = q;
			else
				p->word = (struct blk *)(Rcheat(q) | BUSY);
		}
		if (p == bloktop)
			break;
		p = q;
	}
	parse_allocation_state = PARSE_INACTIVE;
}

void
parse_alloc_commit(void)
{
	parse_alloc_finish(0);
}

void
parse_alloc_rollback(void)
{
	parse_alloc_finish(1);
}


#ifdef DEBUG

void
chkbptr(ptr)
	struct blk *ptr;
{
	int	exf = 0;
	register struct blk *p = (struct blk *)brkbegin;
	register struct blk *q;
	int	us = 0, un = 0;

	for (;;)
	{
		q = next_block(p);

		if (p+1 == ptr)
			exf++;

		if (q < (struct blk *)brkbegin || q > bloktop)
			abort(3);

		if (p == bloktop)
			break;

		if (busy(p))
			us += q - p;
		else
			un += q - p;

		if (p >= q)
			abort(4);

		p = q;
	}
	if (exf == 0)
		abort(1);
}

void
chkmem()
{
	register struct blk *p = (struct blk *)brkbegin;
	register struct blk *q;
	int	us = 0, un = 0;

	for (;;)
	{
		q = next_block(p);

		if (q < (struct blk *)brkbegin || q > bloktop)
			abort(3);

		if (p == bloktop)
			break;

		if (busy(p))
			us += q - p;
		else
			un += q - p;

		if (p >= q)
			abort(4);

		p = q;
	}

	prs("un/used/avail ");
	prn(un);
	blank();
	prn(us);
	blank();
	prn((char *)bloktop - brkbegin - (un + us));
	newline();

}
#endif
