/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The raw key event queue and its session; picokbd_evq.h states the
 * contract.
 *
 * head and tail count events taken in and removed since the session began
 * and wrap modulo 2^32, so head - tail is the fill and the slot of event k
 * is k % LCD_KEYEV_QUEUE.
 *
 * A session started with the wake request (picokbd_evq_wakeon()) also makes
 * the console's select(2) report the owner readable while the queue holds
 * an event, picokbd_evq_ready(), and calls picokbd_evq_wake() after every
 * event it queues. A drain empties the queue, which clears the readiness.
 */

#include <sys/errno.h>

#include "picokbd_evq.h"

_Static_assert((LCD_KEYEV_QUEUE & (LCD_KEYEV_QUEUE - 1)) == 0,
    "the queue index is a mask");
_Static_assert(LCD_KEY_PRESSED == PICOKBD_PRESSED &&
    LCD_KEY_HOLD == PICOKBD_HOLD && LCD_KEY_RELEASED == PICOKBD_RELEASED,
    "the user states are the controller's states");

static struct lcd_keyev queue[LCD_KEYEV_QUEUE];
static unsigned head, tail;
static unsigned lost;			/* Drops since the last drain, at most 255. */
static const void *owner;		/* Open file that owns the session. */
static const void *owner_proc;		/* Process that started it, with its pid. */
static int owner_pid;
static int owner_pgrp;			/* Its process group; the foreground group is required to equal it. */
static unsigned owner_mode;		/* The tty's mode word when the session started. */
static int active;			/* A session is open; events are recorded. */
static int owner_wake;			/* Its select(2) readiness is on. */
static const void *lost_proc;		/* Former owner of a waking session ended from outside, */
static int lost_pid;			/* until its next request; it is read-ready meanwhile. */

/* Erase every keystroke from memory: the queue is cleared, not just emptied. */
static void
session_end(void)
{
	unsigned i;

	for (i = 0; i < LCD_KEYEV_QUEUE; i++) {
		queue[i].state = 0;
		queue[i].key = 0;
	}
	active = 0;
	owner_wake = 0;
	owner = 0;
	owner_proc = 0;
	owner_pid = 0;
	owner_pgrp = 0;
	owner_mode = 0;
	head = tail = 0;
	lost = 0;
}

/*
 * The session ends by an act that is not its owner's: a change of the
 * foreground group or of the tty's mode word. An owner that asked for
 * select(2) readiness may be asleep in select() with no event to come, so
 * it becomes read-ready and its selector is woken; its next request finds
 * no session and reports that.
 */
static void
session_lost(void)
{
	const void *proc = owner_proc;
	int pid = owner_pid, wake = owner_wake;

	session_end();
	if (wake) {
		lost_proc = proc;
		lost_pid = pid;
		picokbd_evq_wake();
	}
}

/* A request by the former owner of a lost session ends its readiness. */
static void
lost_seen(const void *proc, int pid)
{
	if (lost_proc == proc && lost_pid == pid) {
		lost_proc = 0;
		lost_pid = 0;
	}
}

/* The caller is the process that started the session. */
static int
is_owner(const void *proc, int pid)
{
	return owner_proc == proc && owner_pid == pid;
}

/* The part of the tty's flag word that the session compares. */
static unsigned
mode_of(unsigned tty_flags)
{
	return tty_flags & ~(unsigned)PICOKBD_EVQ_VOLATILE;
}

int
picokbd_evq_start(const void *file, const void *proc, int pid, int pgrp,
    int tty_pgrp, unsigned tty_flags, int readable)
{
	lost_seen(proc, pid);
	if (!readable)
		return EBADF;
	if (pgrp == 0 || pgrp != tty_pgrp)
		return EIO;
	picokbd_evq_check(tty_pgrp, tty_flags);
	if ((tty_flags & PICOKBD_EVQ_RAW_OR_CBREAK) == 0)
		return EINVAL;
	if (active && (owner != file || !is_owner(proc, pid)))
		return EBUSY;
	session_end();
	active = 1;
	owner_mode = mode_of(tty_flags);
	owner = file;
	owner_proc = proc;
	owner_pid = pid;
	owner_pgrp = pgrp;
	return 0;
}

int
picokbd_evq_stop(const void *file, const void *proc, int pid)
{
	lost_seen(proc, pid);
	if (!active)
		return 0;
	if (owner != file || !is_owner(proc, pid))
		return EPERM;
	session_end();
	return 0;
}

void
picokbd_evq_fileclose(const void *file)
{
	if (active && owner == file)
		session_end();
}

void
picokbd_evq_procexit(const void *proc, int pid)
{
	lost_seen(proc, pid);
	if (active && is_owner(proc, pid))
		session_end();
}

void
picokbd_evq_check(int tty_pgrp, unsigned tty_flags)
{
	if (active && (owner_pgrp != tty_pgrp || owner_mode != mode_of(tty_flags)))
		session_lost();
}

void
picokbd_evq_modeset(const void *proc, int pid)
{
	if (active && !is_owner(proc, pid))
		session_lost();
}

void
picokbd_evq_wakeon(void)
{
	if (active)
		owner_wake = 1;
}

/*
 * owner_wake is set only while a session is active and cleared when it ends.
 * The former owner of a lost session is ready whatever its group, since the
 * foreground group may be what changed.
 */
int
picokbd_evq_ready(const void *proc, int pid, int pgrp, int tty_pgrp,
    unsigned tty_flags)
{
	picokbd_evq_check(tty_pgrp, tty_flags);
	if (lost_proc != 0 && lost_proc == proc && lost_pid == pid)
		return 1;
	return owner_wake && head != tail && is_owner(proc, pid) &&
	    pgrp != 0 && pgrp == tty_pgrp;
}

int
picokbd_evq_select(const struct picokbd_sel *o, const void *proc, int pid,
    int pgrp, int tty_pgrp, unsigned tty_flags)
{
	int x, r;

	x = o->raise();
	r = picokbd_evq_ready(proc, pid, pgrp, tty_pgrp, tty_flags);
	if (!r)
		r = o->tty();
	o->restore(x);
	return r;
}

void
picokbd_evq_put(int state, int key)
{
	struct lcd_keyev *ev;

	if (!active)
		return;
	if (state != PICOKBD_PRESSED && state != PICOKBD_HOLD &&
	    state != PICOKBD_RELEASED)
		return;
	if (head - tail >= LCD_KEYEV_QUEUE) {
		tail++;
		if (lost < 255)
			lost++;
	}
	ev = &queue[head++ & (LCD_KEYEV_QUEUE - 1)];
	ev->state = (unsigned char)state;
	ev->key = (unsigned char)key;
	if (owner_wake)
		picokbd_evq_wake();
}

int
picokbd_evq_drain(struct lcd_keyevs *out, const void *proc, int pid, int pgrp,
    int tty_pgrp, unsigned tty_flags, int readable)
{
	unsigned n = 0;

	lost_seen(proc, pid);
	if (!readable)
		return EBADF;
	if (pgrp == 0 || pgrp != tty_pgrp)
		return EIO;
	picokbd_evq_check(tty_pgrp, tty_flags);
	if (!active)
		return EINVAL;
	if (!is_owner(proc, pid))
		return EPERM;
	while (n < LCD_KEYEV_N && tail != head)
		out->ev[n++] = queue[tail++ & (LCD_KEYEV_QUEUE - 1)];
	out->n = (unsigned char)n;
	out->lost = (unsigned char)lost;
	lost = 0;
	return 0;
}

void
picokbd_evq_reset(void)
{
	session_end();
	lost_proc = 0;
	lost_pid = 0;
}

int
picokbd_event(int state, int key, int appcursor, int *ctrl,
    char out[PICOKBD_MAXSEQ])
{
	picokbd_evq_put(state, key);
	return picokbd_translate(state, key, appcursor, ctrl, out);
}

#ifdef PICOKBD_EVQ_TEST
/* Host test inspection: the events held and the bytes of them left in memory. */
unsigned
picokbd_evq_fill(void)
{
	return head - tail;
}

unsigned
picokbd_evq_residue(void)
{
	unsigned i, n = 0;

	for (i = 0; i < LCD_KEYEV_QUEUE; i++)
		n += (queue[i].state != 0) + (queue[i].key != 0);
	return n;
}
#endif
