/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Remove an entry from a doubly linked queue.
 *
 * NOTE: this implementation is non-atomic!!
 */

struct queue_entry {
	struct queue_entry	*q_next;
	struct queue_entry	*q_prev;
};

remque(e)
	register struct queue_entry *e;
{
	e->q_prev->q_next = e->q_next;
	e->q_next->q_prev = e->q_prev;
}
