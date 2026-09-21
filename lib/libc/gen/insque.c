/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Insert an entry into a doubly linked queue.
 *
 * NOTE: this implementation is non-atomic!!
 */

struct queue_entry {
	struct queue_entry	*q_next;
	struct queue_entry	*q_prev;
};

insque(e, prev)
	register struct queue_entry *e, *prev;
{
	e->q_prev = prev;
	e->q_next = prev->q_next;
	prev->q_next->q_prev = e;
	prev->q_next = e;
}
