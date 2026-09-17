/*
 * Host gate for sys/kern/tty_subr.c, the character lists every tty queues
 * input and output through.
 *
 * The gate links the kernel source itself. A clist is a chain of CBLOCK-sized
 * blocks drawn from one global free list, and the code finds a block's base by
 * masking a character pointer, so what has to hold is that the chain, the
 * character order and the free list all stay consistent across the boundary
 * cases: a block that fills exactly, a block that empties exactly, a free list
 * that runs out mid-transfer, and a queue drained from either end.
 *
 * Every entry point here raises priority and lowers it again. The harness
 * counts that in place of PRIMASK, so each scenario also states that the
 * routine left the level where it found it; one that did not would wedge the
 * board's console rather than fail a comparison.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/clist.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>

/*
 * The block pool. sys/arch/${MACHINE}/${MACHINE}/machdep.c defines cfree for
 * the kernel; a host gate defines it here, aligned, so every block in it is
 * usable. cinit() in init_main.c rounds the kernel's up instead and spends at
 * most one block doing it.
 */
_Alignas(CBLOCK) struct cblock cfree[NCLIST];

static struct clist queue;
static struct clist other;

/* Bytes the pool holds when nothing is queued. */
#define POOL_BYTES	(NCLIST * (int)CBSIZE)

/* The kernel's sleep partner for a clist waiting on a block. */
void
wakeup(caddr_t channel)
{
	(void)channel;
}

/*
 * Empty both queues and rebuild the free list, the way cinit() does at boot.
 */
static void
setup(void)
{
	int i;

	for (i = 0; i < NCLIST; i++) {
		cfree[i].c_next = NULL;
		for (int j = 0; j < (int)CBSIZE; j++)
			cfree[i].c_info[j] = 0;
	}
	cfreelist = NULL;
	cfreecount = 0;
	for (i = NCLIST; i-- > 0; ) {
		cfree[i].c_next = cfreelist;
		cfreelist = &cfree[i];
		cfreecount += (int)CBSIZE;
	}
	queue.c_cc = 0;
	queue.c_cf = queue.c_cl = NULL;
	other.c_cc = 0;
	other.c_cf = other.c_cl = NULL;
	hk_ipl = 0;
	hk_ipl_raises = 0;
	hk_reset_output();
}

/* Priority came back down, and the routine under test did raise it. */
static void
check_balanced(void)
{
	HK_CHECK(hk_ipl == 0);
	HK_CHECK(hk_ipl_raises > 0);
}

/* Put n bytes of a counting pattern in, one putc at a time. */
static int
fill(struct clist *p, int n, int seed)
{
	int i;

	for (i = 0; i < n; i++)
		if (putc((seed + i) & 0x7f, p) != 0)
			return i;
	return n;
}

/*
 * One character in and the same character out, with the block it needed taken
 * from the free list and handed straight back.
 */
static void
one_character(void)
{
	setup();
	HK_CHECK(getc(&queue) == -1);
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(queue.c_cf == NULL && queue.c_cl == NULL);
	HK_CHECK(cfreecount == POOL_BYTES);

	HK_CHECK(putc('a', &queue) == 0);
	HK_CHECK(queue.c_cc == 1);
	HK_CHECK(cfreecount == POOL_BYTES - (int)CBSIZE);

	HK_CHECK(getc(&queue) == 'a');
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(queue.c_cf == NULL && queue.c_cl == NULL);
	HK_CHECK(cfreecount == POOL_BYTES);
	check_balanced();
}

/*
 * A queue longer than one block chains a second, and the characters come back
 * in order across the join. Draining it returns both blocks.
 */
static void
chain_across_blocks(void)
{
	int i, n;

	setup();
	n = (int)CBSIZE + 5;
	HK_CHECK(fill(&queue, n, 1) == n);
	HK_CHECK(queue.c_cc == n);
	HK_CHECK(cfreecount == POOL_BYTES - 2 * (int)CBSIZE);

	for (i = 0; i < n; i++)
		HK_CHECK(getc(&queue) == ((1 + i) & 0x7f));
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(getc(&queue) == -1);
	HK_CHECK(cfreecount == POOL_BYTES);
	check_balanced();
}

/*
 * A block that fills exactly is the boundary putc tests for, and the pool has
 * a fixed size: the queue takes every byte the pool holds and refuses the next
 * without disturbing what it already has.
 */
static void
pool_runs_out(void)
{
	setup();
	HK_CHECK(fill(&queue, POOL_BYTES, 0) == POOL_BYTES);
	HK_CHECK(queue.c_cc == POOL_BYTES);
	HK_CHECK(cfreecount == 0);
	HK_CHECK(cfreelist == NULL);

	HK_CHECK(putc('x', &queue) == -1);
	HK_CHECK(queue.c_cc == POOL_BYTES);

	HK_CHECK(getc(&queue) == 0);
	HK_CHECK(queue.c_cc == POOL_BYTES - 1);
	check_balanced();
}

/*
 * The bulk paths move the same bytes as the per-character ones. b_to_q answers
 * with what it could not place, q_to_b with what it took.
 */
static void
bulk_transfer(void)
{
	char out[4 * CBSIZE];
	char in[4 * CBSIZE];
	int i, n;

	setup();
	n = 3 * (int)CBSIZE + 7;
	for (i = 0; i < n; i++)
		out[i] = (char)((i * 3) & 0x7f);

	HK_CHECK(b_to_q(out, n, &queue) == 0);
	HK_CHECK(queue.c_cc == n);

	/* A short destination takes a prefix and leaves the rest queued. */
	HK_CHECK(q_to_b(&queue, in, 5) == 5);
	for (i = 0; i < 5; i++)
		HK_CHECK(in[i] == out[i]);
	HK_CHECK(queue.c_cc == n - 5);

	HK_CHECK(q_to_b(&queue, in, (int)sizeof in) == n - 5);
	for (i = 0; i < n - 5; i++)
		HK_CHECK(in[i] == out[i + 5]);
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(cfreecount == POOL_BYTES);

	/* An empty queue yields nothing, and a zero count asks for nothing. */
	HK_CHECK(q_to_b(&queue, in, (int)sizeof in) == 0);
	HK_CHECK(q_to_b(&queue, in, 0) == 0);
	HK_CHECK(b_to_q(out, 0, &queue) == 0);
	check_balanced();
}

/*
 * More bytes than the pool holds: b_to_q places what it can and reports the
 * remainder, so a driver knows how much of its buffer to keep.
 */
static void
bulk_transfer_runs_out(void)
{
	static char out[POOL_BYTES + 2 * CBSIZE];
	int left;

	setup();
	for (int i = 0; i < (int)sizeof out; i++)
		out[i] = (char)(i & 0x7f);

	left = b_to_q(out, (int)sizeof out, &queue);
	HK_CHECK(left == (int)sizeof out - POOL_BYTES);
	HK_CHECK(queue.c_cc == POOL_BYTES);
	HK_CHECK(cfreecount == 0);
	check_balanced();
}

/*
 * ndqb counts the run a caller can hand to a device without following the
 * chain: from the first character to the end of its block, or to the first
 * character carrying the flag.
 */
static void
contiguous_run(void)
{
	char out[3 * CBSIZE];
	int i, first;

	setup();
	for (i = 0; i < (int)sizeof out; i++)
		out[i] = (char)(i & 0x7f);
	HK_CHECK(b_to_q(out, 2 * (int)CBSIZE, &queue) == 0);

	/* The first block holds CBSIZE characters, so that is the run. */
	HK_CHECK(ndqb(&queue, 0) == (int)CBSIZE);

	/* With a flag the count stops at the first character carrying it. */
	first = ndqb(&queue, 0x40);
	for (i = 0; i < (int)CBSIZE; i++)
		if (out[i] & 0x40)
			break;
	HK_CHECK(first == i);

	/* A queue shorter than its block reports its own length. */
	setup();
	HK_CHECK(b_to_q(out, 3, &queue) == 0);
	HK_CHECK(ndqb(&queue, 0) == 3);
	check_balanced();
}

/*
 * ndflush drops characters from the front. Crossing a block boundary returns
 * the block it emptied.
 */
static void
flush_from_front(void)
{
	char out[3 * CBSIZE];
	int i;

	setup();
	for (i = 0; i < (int)sizeof out; i++)
		out[i] = (char)((i + 1) & 0x7f);
	HK_CHECK(b_to_q(out, 2 * (int)CBSIZE, &queue) == 0);
	HK_CHECK(cfreecount == POOL_BYTES - 2 * (int)CBSIZE);

	ndflush(&queue, 3);
	HK_CHECK(queue.c_cc == 2 * (int)CBSIZE - 3);
	HK_CHECK(getc(&queue) == out[3]);

	/* Past the end of the first block, that block goes back. */
	ndflush(&queue, (int)CBSIZE);
	HK_CHECK(queue.c_cc == (int)CBSIZE - 4);
	HK_CHECK(cfreecount == POOL_BYTES - (int)CBSIZE);
	HK_CHECK(getc(&queue) == out[(int)CBSIZE + 4]);

	/* Flushing the remainder empties the queue and returns the last block. */
	ndflush(&queue, queue.c_cc);
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(cfreecount == POOL_BYTES);
	check_balanced();
}

/*
 * nextc walks the queue without consuming it, which is what the tty code does
 * when it looks ahead for a delimiter. The walk crosses a block join and ends
 * at the last character.
 */
static void
walk_without_consuming(void)
{
	char out[2 * CBSIZE];
	char *cp;
	int i, seen;

	setup();
	for (i = 0; i < (int)sizeof out; i++)
		out[i] = (char)((i + 9) & 0x7f);
	HK_CHECK(b_to_q(out, (int)CBSIZE + 6, &queue) == 0);

	seen = 0;
	cp = queue.c_cf;
	HK_CHECK(*cp == out[0]);
	seen++;
	while ((cp = nextc(&queue, cp)) != NULL) {
		HK_CHECK(*cp == out[seen]);
		seen++;
	}
	HK_CHECK(seen == (int)CBSIZE + 6);

	/* Nothing was consumed and no block moved. */
	HK_CHECK(queue.c_cc == (int)CBSIZE + 6);
	HK_CHECK(cfreecount == POOL_BYTES - 2 * (int)CBSIZE);
	HK_CHECK(getc(&queue) == out[0]);
}

/*
 * unputc takes the last character back, which is how the line editor erases.
 * Emptying a block from the end frees it, and that path has to find the
 * block's predecessor by walking the chain.
 */
static void
take_from_the_end(void)
{
	int i, n;

	setup();
	HK_CHECK(unputc(&queue) == -1);

	HK_CHECK(fill(&queue, 3, 20) == 3);
	HK_CHECK(unputc(&queue) == 22);
	HK_CHECK(queue.c_cc == 2);
	HK_CHECK(unputc(&queue) == 21);
	HK_CHECK(unputc(&queue) == 20);
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(cfreecount == POOL_BYTES);
	HK_CHECK(unputc(&queue) == -1);

	/* One character into a second block, then take it back again. */
	setup();
	n = (int)CBSIZE + 1;
	HK_CHECK(fill(&queue, n, 1) == n);
	HK_CHECK(cfreecount == POOL_BYTES - 2 * (int)CBSIZE);
	HK_CHECK(unputc(&queue) == ((1 + (int)CBSIZE) & 0x7f));
	HK_CHECK(queue.c_cc == (int)CBSIZE);
	HK_CHECK(cfreecount == POOL_BYTES - (int)CBSIZE);

	/* What is left still reads back in order. */
	for (i = 0; i < (int)CBSIZE; i++)
		HK_CHECK(getc(&queue) == ((1 + i) & 0x7f));
	HK_CHECK(cfreecount == POOL_BYTES);
	check_balanced();
}

/*
 * catq appends one queue to another. An empty destination takes the source's
 * chain outright, which costs no copy; a destination with characters in it
 * copies through a buffer.
 */
static void
concatenate(void)
{
	int i, n;

	setup();
	n = (int)CBSIZE + 4;
	HK_CHECK(fill(&other, n, 30) == n);
	HK_CHECK(queue.c_cc == 0);

	catq(&other, &queue);
	HK_CHECK(queue.c_cc == n);
	HK_CHECK(other.c_cc == 0);
	HK_CHECK(other.c_cf == NULL && other.c_cl == NULL);
	/* The blocks moved rather than being copied and freed. */
	HK_CHECK(cfreecount == POOL_BYTES - 2 * (int)CBSIZE);
	for (i = 0; i < n; i++)
		HK_CHECK(getc(&queue) == ((30 + i) & 0x7f));

	/* A destination that already holds characters appends after them. */
	setup();
	HK_CHECK(fill(&queue, 3, 60) == 3);
	HK_CHECK(fill(&other, (int)CBSIZE + 2, 70) == (int)CBSIZE + 2);
	catq(&other, &queue);
	HK_CHECK(queue.c_cc == 3 + (int)CBSIZE + 2);
	HK_CHECK(other.c_cc == 0);
	for (i = 0; i < 3; i++)
		HK_CHECK(getc(&queue) == ((60 + i) & 0x7f));
	for (i = 0; i < (int)CBSIZE + 2; i++)
		HK_CHECK(getc(&queue) == ((70 + i) & 0x7f));
	HK_CHECK(queue.c_cc == 0);
	HK_CHECK(cfreecount == POOL_BYTES);
	check_balanced();
}

int
main(void)
{
	one_character();
	chain_across_blocks();
	pool_runs_out();
	bulk_transfer();
	bulk_transfer_runs_out();
	contiguous_run();
	flush_from_front();
	walk_without_consuming();
	take_from_the_end();
	concatenate();
	return hk_verdict("tty_subr");
}
