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
static int active;			/* A session is open; events are recorded. */

/* Leave no keystroke in memory: the queue is cleared, not just emptied. */
static void
session_end(void)
{
	unsigned i;

	for (i = 0; i < LCD_KEYEV_QUEUE; i++) {
		queue[i].state = 0;
		queue[i].key = 0;
	}
	active = 0;
	owner = 0;
	owner_proc = 0;
	owner_pid = 0;
	owner_pgrp = 0;
	head = tail = 0;
	lost = 0;
}

/* The caller is the process that started the session. */
static int
is_owner(const void *proc, int pid)
{
	return owner_proc == proc && owner_pid == pid;
}

int
picokbd_evq_start(const void *file, const void *proc, int pid, int pgrp,
    int tty_pgrp, int readable)
{
	if (!readable)
		return EBADF;
	if (pgrp == 0 || pgrp != tty_pgrp)
		return EIO;
	picokbd_evq_foreground(tty_pgrp);
	if (active && (owner != file || !is_owner(proc, pid)))
		return EBUSY;
	session_end();
	active = 1;
	owner = file;
	owner_proc = proc;
	owner_pid = pid;
	owner_pgrp = pgrp;
	return 0;
}

int
picokbd_evq_stop(const void *file, const void *proc, int pid)
{
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
	if (active && is_owner(proc, pid))
		session_end();
}

void
picokbd_evq_foreground(int tty_pgrp)
{
	if (active && owner_pgrp != tty_pgrp)
		session_end();
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
}

int
picokbd_evq_drain(struct lcd_keyevs *out, const void *proc, int pid, int pgrp,
    int tty_pgrp, int readable)
{
	unsigned n = 0;

	if (!readable)
		return EBADF;
	if (pgrp == 0 || pgrp != tty_pgrp)
		return EIO;
	picokbd_evq_foreground(tty_pgrp);
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
