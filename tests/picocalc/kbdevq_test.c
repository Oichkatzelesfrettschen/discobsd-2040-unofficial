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
 * Host test of the raw key event queue behind LCDIOC_GETKEY
 * (sys/arch/rp2040/dev/picokbd_evq.c) together with the key translation it
 * sits beside (picokbd_map.c).
 *
 * The oracle for the queue is a plain array the test appends to, so a
 * dropped, reordered or miscounted event shows against an independent
 * record. The oracle for the translated path is picokbd_translate() itself:
 * picokbd_event(), which the poller of picokbd.c calls, must return the same
 * bytes and leave the same Ctrl state as a translate-only run over the same
 * events, for every state, every key and a long pseudo-random sequence.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/lcdcons.h>

#include "picokbd_evq.h"

static int failures;

static void
expect(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

/*
 * The identities the kernel glue passes: two open files, the owner's process
 * group, which is the tty's foreground group, and another group.
 */
static int file_a, file_b;

/* Test hooks of picokbd_evq.c (PICOKBD_EVQ_TEST): events held, key bytes left in memory. */
unsigned picokbd_evq_fill(void);
unsigned picokbd_evq_residue(void);
#define	PG		100
#define	OTHER		200


/*
 * The calling process, as the kernel glue passes it: a proc entry and a pid.
 * Two entries and two pids let a test swap either one (a slot reused by a new
 * pid, a pid reused in another slot).
 */
static int proc_a, proc_b;
#define	PID_A		10
#define	PID_B		11
static const void *cur_proc = &proc_a;
static int cur_pid = PID_A;

static void
as_process(const void *proc, int pid)
{
	cur_proc = proc;
	cur_pid = pid;
}

static int
drain_as(struct lcd_keyevs *out, int pgrp, int tty_pgrp, int readable)
{
	return picokbd_evq_drain(out, cur_proc, cur_pid, pgrp, tty_pgrp, readable);
}

static int
start_f(const void *file, int pgrp, int tty_pgrp, int readable)
{
	return picokbd_evq_start(file, cur_proc, cur_pid, pgrp, tty_pgrp, readable);
}

static int
stop_f(const void *file)
{
	return picokbd_evq_stop(file, cur_proc, cur_pid);
}

/* A fresh session owned by process A through file A in the foreground group. */
static void
begin(void)
{
	picokbd_evq_reset();
	as_process(&proc_a, PID_A);
	expect("session start", start_f(&file_a, PG, PG, 1), 0);
}

static unsigned rng_state = 1;

static unsigned
rnd(unsigned n)
{
	rng_state = rng_state * 1103515245U + 12345U;
	return (rng_state >> 16) % n;
}

/* The event numbered i of a long run: a state and key that identify i. */
static int
ev_state(unsigned i)
{
	return 1 + (int)(i % 3);
}

static int
ev_key(unsigned i)
{
	return (int)(i & 0xff);
}

static void
put_run(unsigned first, unsigned count)
{
	unsigned i;

	for (i = first; i < first + count; i++)
		picokbd_evq_put(ev_state(i), ev_key(i));
}

/* Drain and require events first .. first + n - 1 in order; returns the drop count. */
static int
drain_run(const char *what, unsigned first, unsigned n_want)
{
	struct lcd_keyevs out;
	unsigned k;

	memset(&out, 0xee, sizeof(out));
	expect("drain succeeds", drain_as(&out, PG, PG, 1), 0);
	expect(what, out.n, n_want);
	for (k = 0; k < out.n && k < LCD_KEYEV_N; k++) {
		expect("event state", out.ev[k].state, ev_state(first + k));
		expect("event key", out.ev[k].key, ev_key(first + k));
	}
	for (; k < LCD_KEYEV_N; k++) {
		expect("slots past n are untouched (state)", out.ev[k].state, 0xee);
		expect("slots past n are untouched (key)", out.ev[k].key, 0xee);
	}
	return out.lost;
}

/* Drain everything, event first onward, checking order; returns the events taken. */
static unsigned
drain_all(unsigned first)
{
	struct lcd_keyevs out;
	unsigned total = 0, k;

	for (;;) {
		memset(&out, 0, sizeof(out));
		expect("drain succeeds", drain_as(&out, PG, PG, 1), 0);
		for (k = 0; k < out.n; k++) {
			expect("order: state", out.ev[k].state,
			    ev_state(first + total));
			expect("order: key", out.ev[k].key, ev_key(first + total));
			total++;
		}
		if (out.n < LCD_KEYEV_N)
			return total;
	}
}

static void
test_constants(void)
{
	char out[PICOKBD_MAXSEQ];
	int ctrl = 0;

	expect("pressed", LCD_KEY_PRESSED, PICOKBD_PRESSED);
	expect("hold", LCD_KEY_HOLD, PICOKBD_HOLD);
	expect("released", LCD_KEY_RELEASED, PICOKBD_RELEASED);
	expect("event record is 14 bytes", sizeof(struct lcd_keyevs), 14);
	expect("one call fits the ioctl storage", sizeof(struct lcd_keyevs) <= 16, 1);
	expect("the queue is a power of two",
	    (LCD_KEYEV_QUEUE & (LCD_KEYEV_QUEUE - 1)), 0);

	/* The key codes a game uses are the codes the translation maps. */
	expect("up", picokbd_translate(1, LCD_KEY_UP, 0, &ctrl, out), 3);
	expect("up final", out[2], 'A');
	expect("down", picokbd_translate(1, LCD_KEY_DOWN, 0, &ctrl, out), 3);
	expect("down final", out[2], 'B');
	expect("right", picokbd_translate(1, LCD_KEY_RIGHT, 0, &ctrl, out), 3);
	expect("right final", out[2], 'C');
	expect("left", picokbd_translate(1, LCD_KEY_LEFT, 0, &ctrl, out), 3);
	expect("left final", out[2], 'D');
	expect("enter", picokbd_translate(1, LCD_KEY_ENTER, 0, &ctrl, out), 1);
	expect("enter byte", out[0], '\r');
	expect("tab", picokbd_translate(1, LCD_KEY_TAB, 0, &ctrl, out), 1);
	expect("tab byte", out[0], '\t');
	expect("backspace", picokbd_translate(1, LCD_KEY_BACKSPACE, 0, &ctrl, out), 1);
	expect("backspace byte", out[0], 0x7f);
	expect("escape", picokbd_translate(1, LCD_KEY_ESC, 0, &ctrl, out), 1);
	expect("escape byte", out[0], '\033');
	expect("ctrl types nothing", picokbd_translate(1, LCD_KEY_CTRL, 0, &ctrl, out), 0);
	expect("ctrl is applied", ctrl, 1);
	expect("ctrl release", picokbd_translate(3, LCD_KEY_CTRL, 0, &ctrl, out), 0);
	expect("ctrl is released", ctrl, 0);
}

/* A typed sequence: every event reaches the queue, the tty bytes are unchanged. */
static void
test_script(void)
{
	static const struct {
		int state, key;
		int nbytes;
		char byte0;
	} script[] = {
		{ PICOKBD_PRESSED, 'a', 1, 'a' },
		{ PICOKBD_HOLD, 'a', 0, 0 },
		{ PICOKBD_HOLD, 'a', 0, 0 },
		{ PICOKBD_RELEASED, 'a', 0, 0 },
		{ PICOKBD_PRESSED, LCD_KEY_CTRL, 0, 0 },
		{ PICOKBD_PRESSED, 'c', 1, 3 },
		{ PICOKBD_RELEASED, 'c', 0, 0 },
		{ PICOKBD_RELEASED, LCD_KEY_CTRL, 0, 0 },
		{ PICOKBD_PRESSED, LCD_KEY_LEFT, 3, '\033' },
		{ PICOKBD_RELEASED, LCD_KEY_LEFT, 0, 0 },
		{ PICOKBD_PRESSED, 0x81, 3, '\033' },
	};
	struct lcd_keyevs out;
	char seq[PICOKBD_MAXSEQ];
	unsigned i, n = sizeof(script) / sizeof(script[0]);
	int ctrl = 0, got;

	begin();
	memset(&out, 0, sizeof(out));
	for (i = 0; i < n; i++) {
		got = picokbd_event(script[i].state, script[i].key, 0, &ctrl, seq);
		expect("script: translated length", got, script[i].nbytes);
		if (got > 0)
			expect("script: translated byte", seq[0], script[i].byte0);
	}
	expect("script: ctrl ends released", ctrl, 0);
	expect("script: drain", drain_as(&out, PG, PG, 1), 0);
	expect("script: first call returns a full record", out.n, LCD_KEYEV_N);
	expect("script: nothing lost", out.lost, 0);
	for (i = 0; i < LCD_KEYEV_N; i++) {
		expect("script: state", out.ev[i].state, script[i].state);
		expect("script: key", out.ev[i].key, script[i].key);
	}
	expect("script: drain 2", drain_as(&out, PG, PG, 1), 0);
	expect("script: second call returns the rest", out.n, (long)(n - LCD_KEYEV_N));
	for (i = 0; i < out.n; i++) {
		expect("script: state (2)", out.ev[i].state,
		    script[LCD_KEYEV_N + i].state);
		expect("script: key (2)", out.ev[i].key, script[LCD_KEYEV_N + i].key);
	}
	expect("script: drain 3", drain_as(&out, PG, PG, 1), 0);
	expect("script: then empty", out.n, 0);

	/* The released state, which the tty path drops, is in the queue. */
	begin();
	picokbd_event(PICOKBD_RELEASED, 'x', 0, &ctrl, seq);
	memset(&out, 0, sizeof(out));
	expect("released: drain", drain_as(&out, PG, PG, 1), 0);
	expect("released is queued", out.n, 1);
	expect("released state", out.ev[0].state, LCD_KEY_RELEASED);
	expect("released key", out.ev[0].key, 'x');

	/* The hold state is queued too. */
	begin();
	picokbd_event(PICOKBD_HOLD, 'x', 0, &ctrl, seq);
	memset(&out, 0, sizeof(out));
	expect("hold: drain", drain_as(&out, PG, PG, 1), 0);
	expect("hold is queued", out.n, 1);
	expect("hold state", out.ev[0].state, LCD_KEY_HOLD);

	/* No event, and states that are no event, do not enter the queue. */
	begin();
	expect("idle types nothing", picokbd_event(PICOKBD_IDLE, 0, 0, &ctrl, seq), 0);
	picokbd_event(4, 'a', 0, &ctrl, seq);
	picokbd_event(-1, 'a', 0, &ctrl, seq);
	picokbd_event(255, 'a', 0, &ctrl, seq);
	memset(&out, 0, sizeof(out));
	expect("idle: drain", drain_as(&out, PG, PG, 1), 0);
	expect("idle and bad states are not queued", out.n, 0);
}

/* The translated bytes are the same with and without the queue. */
static void
test_translation_unchanged(int session)
{
	char a[PICOKBD_MAXSEQ], b[PICOKBD_MAXSEQ];
	int ctrl_a = 0, ctrl_b = 0, state, key, app, na, nb;
	unsigned i, bad = 0;

	if (session)
		begin();
	else
		picokbd_evq_reset();
	/* Every state against every key, with and without application cursor mode. */
	for (app = 0; app < 2; app++) {
		for (state = 0; state < 4; state++) {
			for (key = 0; key < 256; key++) {
				memset(a, 0x55, sizeof(a));
				memset(b, 0x55, sizeof(b));
				na = picokbd_event(state, key, app, &ctrl_a, a);
				nb = picokbd_translate(state, key, app, &ctrl_b, b);
				if (na != nb || ctrl_a != ctrl_b ||
				    memcmp(a, b, sizeof(a)) != 0)
					bad++;
				if ((key & 0x1f) == 0) {
					struct lcd_keyevs out;

					(void)drain_as(&out, PG, PG, 1);
				}
			}
		}
	}
	expect("every state and key translates as without the queue", bad, 0);

	/* A long random run, with Ctrl in it, never drained. */
	ctrl_a = ctrl_b = 0;
	for (i = 0; i < 20000; i++) {
		state = (int)rnd(4);
		key = rnd(8) == 0 ? 0xa5 : (int)rnd(256);
		app = (int)rnd(2);
		na = picokbd_event(state, key, app, &ctrl_a, a);
		nb = picokbd_translate(state, key, app, &ctrl_b, b);
		if (na != nb || ctrl_a != ctrl_b || (na > 0 && memcmp(a, b, (size_t)na) != 0))
			bad++;
	}
	expect("a long random run translates as without the queue", bad, 0);
	picokbd_evq_reset();
}

/* Raw events are recorded only while a session is active. */
static void
test_no_recording_outside_session(void)
{
	struct lcd_keyevs out;
	char seq[PICOKBD_MAXSEQ];
	int ctrl = 0;

	picokbd_evq_reset();
	put_run(0, 10);
	picokbd_event(PICOKBD_PRESSED, 'a', 0, &ctrl, seq);
	expect("before any session", drain_as(&out, PG, PG, 1), EINVAL);
	expect("nothing is held without a session", picokbd_evq_fill(), 0);
	expect("no key byte is kept without a session", picokbd_evq_residue(), 0);

	/* Events from before a session are not in it. */
	expect("start", start_f(&file_a, PG, PG, 1), 0);
	memset(&out, 0xee, sizeof(out));
	expect("drain", drain_as(&out, PG, PG, 1), 0);
	expect("a new session starts empty", out.n, 0);
	expect("a new session starts with no drops", out.lost, 0);

	/* Events after the end are not recorded for the next one. */
	put_run(0, 5);
	expect("a session holds its events", picokbd_evq_fill(), 5);
	expect("and their bytes", picokbd_evq_residue() > 0, 1);
	expect("stop", stop_f(&file_a), 0);
	expect("the end empties the queue", picokbd_evq_fill(), 0);
	expect("the end leaves no key byte in memory", picokbd_evq_residue(), 0);
	put_run(100, 5);
	expect("an ended session records nothing", picokbd_evq_fill(), 0);
	expect("an ended session keeps no byte", picokbd_evq_residue(), 0);
	picokbd_event(PICOKBD_PRESSED, 'b', 0, &ctrl, seq);
	expect("after the end", drain_as(&out, PG, PG, 1), EINVAL);
	expect("start again", start_f(&file_a, PG, PG, 1), 0);
	expect("drain again", drain_as(&out, PG, PG, 1), 0);
	expect("the ended session left nothing", out.n, 0);

	/* An overflowed session leaves no drop count behind either. */
	put_run(0, 100);
	expect("stop after overflow", stop_f(&file_a), 0);
	expect("start after overflow", start_f(&file_b, PG, PG, 1), 0);
	memset(&out, 0xee, sizeof(out));
	expect("drain after overflow", drain_as(&out, PG, PG, 1), 0);
	expect("no events carried over", out.n, 0);
	expect("no drops carried over", out.lost, 0);

	/* Restarting by the owner empties the queue. */
	put_run(0, 7);
	expect("restart", start_f(&file_b, PG, PG, 1), 0);
	expect("drain after restart", drain_as(&out, PG, PG, 1), 0);
	expect("a restart empties the queue", out.n, 0);

	/* Stopping with no session is not an error. */
	picokbd_evq_reset();
	expect("stop without session", stop_f(&file_a), 0);
}

/* Starting needs a readable descriptor in the foreground group. */
static void
test_start_checks(void)
{
	struct lcd_keyevs out;

	picokbd_evq_reset();
	expect("write-only start", start_f(&file_a, PG, PG, 0), EBADF);
	put_run(0, 3);
	expect("a refused start opens no session", drain_as(&out, PG, PG, 1), EINVAL);
	expect("background start", start_f(&file_a, OTHER, PG, 1), EIO);
	expect("no foreground group", start_f(&file_a, 0, 0, 1), EIO);
	expect("no process group", start_f(&file_a, 0, PG, 1), EIO);
	put_run(0, 3);
	expect("refused starts open no session", drain_as(&out, PG, PG, 1), EINVAL);

	/* A refused start leaves a running session and its queue alone. */
	begin();
	put_run(0, 3);
	expect("write-only start over a session",
	    start_f(&file_a, PG, PG, 0), EBADF);
	expect("background start over a session",
	    start_f(&file_a, OTHER, PG, 1), EIO);
	expect("the queue is intact", drain_run("intact", 0, 3), 0);

	/* A second open file cannot take the session, or end it. */
	put_run(10, 2);
	expect("second owner", start_f(&file_b, PG, PG, 1), EBUSY);
	expect("second owner's stop", stop_f(&file_b), EPERM);
	expect("the owner's queue is intact", drain_run("owner", 10, 2), 0);
	put_run(20, 1);
	expect("still recording", drain_run("recording", 20, 1), 0);
	expect("the owner stops", stop_f(&file_a), 0);
	expect("the second file starts", start_f(&file_b, PG, PG, 1), 0);
}

/* A drain needs the readable descriptor and the foreground group, and leaves the queue alone when refused. */
static void
test_drain_checks(void)
{
	struct lcd_keyevs out;

	begin();
	put_run(0, 8);
	memset(&out, 0xee, sizeof(out));
	expect("write-only drain", drain_as(&out, PG, PG, 0), EBADF);
	expect("write-only drain leaves out alone", out.n, 0xee);
	expect("background drain", drain_as(&out, OTHER, PG, 1), EIO);
	expect("background drain leaves out alone", out.n, 0xee);
	expect("no process group", drain_as(&out, 0, PG, 1), EIO);
	expect("no foreground group", drain_as(&out, 0, 0, 1), EIO);
	expect("write-only and background", drain_as(&out, OTHER, PG, 0), EBADF);

	/* The refusals neither consumed events nor reset the drop count. */
	put_run(8, 40);
	expect("write-only drain over an overflow", drain_as(&out, PG, PG, 0), EBADF);
	expect("background drain over an overflow", drain_as(&out, OTHER, PG, 1), EIO);
	expect("events intact and drops kept", drain_run("kept", 16, 6), 16);

	/* A foreground reader of any open file may drain. */
	picokbd_evq_reset();
	expect("start", start_f(&file_a, PG, PG, 1), 0);
	put_run(0, 2);
	expect("another process of the group drains", drain_run("peer", 0, 2), 0);
}

/* The session ends when the owner closes the file. */
static void
test_close(void)
{
	struct lcd_keyevs out;

	begin();
	put_run(0, 4);
	picokbd_evq_fileclose(&file_b);
	expect("another file's close keeps the session", drain_run("kept", 0, 4), 0);
	put_run(4, 2);
	picokbd_evq_fileclose(&file_a);
	expect("the close leaves no key byte in memory", picokbd_evq_residue(), 0);
	put_run(10, 3);
	expect("after the close", drain_as(&out, PG, PG, 1), EINVAL);
	expect("nothing is recorded after the close", picokbd_evq_fill(), 0);
	expect("restart", start_f(&file_b, PG, PG, 1), 0);
	expect("restart drain", drain_as(&out, PG, PG, 1), 0);
	expect("the closed session left nothing", out.n, 0);

	/* Closing with no session is harmless. */
	picokbd_evq_reset();
	picokbd_evq_fileclose(&file_a);
	expect("no session after a stray close", drain_as(&out, PG, PG, 1), EINVAL);
}

/*
 * The session belongs to the process that started it. A parent shell holds
 * the same open file, in the same foreground group, so neither the file nor
 * the group identifies the owner.
 */
static void
test_owner_process(void)
{
	struct lcd_keyevs out;

	/* Another process sharing the file and the group is not the owner. */
	begin();
	put_run(0, 4);
	as_process(&proc_b, PID_B);
	memset(&out, 0xee, sizeof(out));
	expect("peer drain", drain_as(&out, PG, PG, 1), EPERM);
	expect("peer drain leaves out alone", out.n, 0xee);
	expect("peer stop on the shared file", stop_f(&file_a), EPERM);
	expect("peer start on the shared file", start_f(&file_a, PG, PG, 1), EBUSY);
	picokbd_evq_fileclose(&file_b);
	as_process(&proc_a, PID_A);
	expect("the owner's queue is intact", drain_run("owner", 0, 4), 0);

	/* The owner exits; its parent still holds the file and cannot read on. */
	put_run(4, 3);
	as_process(&proc_b, PID_B);
	picokbd_evq_procexit(&proc_b, PID_B);
	expect("a peer's exit does not end it", picokbd_evq_fill(), 3);
	picokbd_evq_procexit(&proc_a, PID_A);
	expect("the owner's exit empties the queue", picokbd_evq_fill(), 0);
	expect("and clears its bytes", picokbd_evq_residue(), 0);
	put_run(20, 3);
	expect("the exited owner's session records nothing", picokbd_evq_fill(), 0);
	expect("the parent finds no session", drain_as(&out, PG, PG, 1), EINVAL);
	expect("the parent may start its own on the same file",
	    start_f(&file_a, PG, PG, 1), 0);
	expect("which starts empty", drain_run("parent", 0, 0), 0);

	/* A slot reused by a new pid, or a pid reused in another slot, is not the owner. */
	begin();
	put_run(0, 3);
	as_process(&proc_a, PID_B);
	expect("same slot, new pid: drain", drain_as(&out, PG, PG, 1), EPERM);
	expect("same slot, new pid: stop", stop_f(&file_a), EPERM);
	picokbd_evq_procexit(&proc_a, PID_B);
	as_process(&proc_b, PID_A);
	expect("same pid, other slot: drain", drain_as(&out, PG, PG, 1), EPERM);
	expect("same pid, other slot: stop", stop_f(&file_a), EPERM);
	picokbd_evq_procexit(&proc_b, PID_A);
	expect("the wrong identities ended nothing", picokbd_evq_fill(), 3);
	as_process(&proc_a, PID_A);
	expect("the owner still reads", drain_run("still", 0, 3), 0);

	/* An ended session does not revive for a process that reuses the pid. */
	picokbd_evq_procexit(&proc_a, PID_A);
	as_process(&proc_a, PID_A);
	put_run(0, 2);
	expect("no revival of the owner's identity", drain_as(&out, PG, PG, 1), EINVAL);
	expect("nothing recorded", picokbd_evq_fill(), 0);

	/* The owner stops with its own request from its own process. */
	begin();
	put_run(0, 2);
	expect("owner stop", stop_f(&file_a), 0);
	expect("owner stop empties", picokbd_evq_fill(), 0);
}

/* The session ends when the tty's foreground group is no longer the owner's. */
static void
test_foreground(void)
{
	struct lcd_keyevs out;
	char seq[PICOKBD_MAXSEQ];
	int ctrl = 0;

	begin();
	put_run(0, 4);
	picokbd_evq_foreground(PG);
	expect("the same group keeps the session", drain_run("same", 0, 4), 0);
	put_run(4, 3);
	picokbd_evq_foreground(OTHER);
	expect("a change of group leaves no key byte in memory", picokbd_evq_residue(), 0);
	put_run(10, 3);
	picokbd_event(PICOKBD_PRESSED, 'x', 0, &ctrl, seq);
	expect("nothing is recorded after the change", picokbd_evq_fill(), 0);
	expect("after a change of group the old owner reads nothing",
	    drain_as(&out, PG, OTHER, 1), EIO);
	expect("the new foreground group finds no session",
	    drain_as(&out, OTHER, OTHER, 1), EINVAL);
	expect("and may start its own", start_f(&file_b, OTHER, OTHER, 1), 0);
	memset(&out, 0xee, sizeof(out));
	expect("its drain", drain_as(&out, OTHER, OTHER, 1), 0);
	expect("starts empty", out.n, 0);

	/* A request from the new foreground group ends a stale session by itself. */
	begin();
	put_run(0, 5);
	expect("drain by another group", drain_as(&out, OTHER, OTHER, 1), EINVAL);
	expect("the owner's group is background now",
	    drain_as(&out, PG, OTHER, 1), EIO);
	put_run(0, 2);
	expect("the stale session recorded nothing more",
	    drain_as(&out, OTHER, OTHER, 1), EINVAL);

	begin();
	put_run(0, 5);
	expect("a start from the new group replaces a stale session",
	    start_f(&file_b, OTHER, OTHER, 1), 0);
	memset(&out, 0xee, sizeof(out));
	expect("new owner drains", drain_as(&out, OTHER, OTHER, 1), 0);
	expect("replaced session is empty", out.n, 0);
	expect("the old owner cannot read", drain_as(&out, PG, OTHER, 1), EIO);

	/* The group coming back does not revive an ended session. */
	begin();
	put_run(0, 3);
	picokbd_evq_foreground(OTHER);
	picokbd_evq_foreground(PG);
	expect("no revival", drain_as(&out, PG, PG, 1), EINVAL);
}

static void
test_drain_limit(void)
{
	begin();
	expect("empty drain", drain_run("empty", 0, 0), 0);

	put_run(0, 14);
	expect("first call returns six", drain_run("first", 0, 6), 0);
	expect("second call returns six", drain_run("second", 6, 6), 0);
	expect("third call returns two", drain_run("third", 12, 2), 0);
	expect("fourth call returns none", drain_run("fourth", 14, 0), 0);

	/* Exactly one call's worth. */
	put_run(100, 6);
	expect("exactly six", drain_run("six", 100, 6), 0);
	expect("then none", drain_run("none", 106, 0), 0);

	/* Indexes wrap past the queue size many times, in odd batches. */
	{
		unsigned next = 200, taken = 0, batch;

		for (batch = 1; batch < 40; batch++) {
			put_run(next, batch % 11 + 1);
			next += batch % 11 + 1;
			taken += drain_all(200 + taken);
		}
		expect("every event of the wrap run came back", taken, next - 200);
	}
}

static void
test_overflow(void)
{
	unsigned taken;

	/* Exactly full: nothing lost. */
	begin();
	put_run(0, LCD_KEYEV_QUEUE);
	expect("a full queue loses nothing", drain_run("full", 0, 6), 0);
	taken = 6 + drain_all(6);
	expect("a full queue returns all of it", taken, LCD_KEYEV_QUEUE);

	/* One over: the oldest goes, the drop is counted once. */
	begin();
	put_run(0, LCD_KEYEV_QUEUE + 1);
	expect("one over drops one", drain_run("one over", 1, 6), 1);
	expect("the drop is reported once", drain_run("after", 7, 6), 0);
	taken = 12 + drain_all(13);
	expect("the newest events survive", taken, LCD_KEYEV_QUEUE);

	/* Forty events: the last 32 remain, oldest first, 8 dropped. */
	begin();
	put_run(0, 40);
	expect("forty events drop eight", drain_run("forty", 8, 6), 8);
	taken = 6 + drain_all(14);
	expect("the last 32 events return", taken, LCD_KEYEV_QUEUE);

	/* The drop count survives an empty drain only until it is reported. */
	begin();
	put_run(0, 50);
	expect("fifty events drop eighteen", drain_run("fifty", 18, 6), 18);
	expect("reported once", drain_run("fifty again", 24, 6), 0);

	/* The count saturates at 255 and the newest 32 remain. */
	begin();
	put_run(0, 1000);
	expect("a thousand events saturate the count", drain_run("thousand", 968, 6), 255);
	taken = 6 + drain_all(974);
	expect("the last 32 of a thousand return", taken, LCD_KEYEV_QUEUE);

	/* A reset ends the session and forgets events and drops. */
	put_run(0, 100);
	picokbd_evq_reset();
	expect("reset ends the session", drain_as(&(struct lcd_keyevs){0}, PG, PG, 1), EINVAL);
	begin();
	expect("reset emptied the queue and the count", drain_run("reset", 0, 0), 0);
}

int
main(void)
{
	test_constants();
	test_script();
	test_translation_unchanged(1);
	test_translation_unchanged(0);
	test_no_recording_outside_session();
	test_start_checks();
	test_drain_checks();
	test_close();
	test_owner_process();
	test_foreground();
	test_drain_limit();
	test_overflow();
	if (failures != 0) {
		fprintf(stderr, "kbdevq_test: %d failures\n", failures);
		return 1;
	}
	printf("kbdevq_test: ok\n");
	return 0;
}
