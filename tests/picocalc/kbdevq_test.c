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
	picokbd_evq_drain(&out);
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
		picokbd_evq_drain(&out);
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

	picokbd_evq_reset();
	memset(&out, 0, sizeof(out));
	for (i = 0; i < n; i++) {
		got = picokbd_event(script[i].state, script[i].key, 0, &ctrl, seq);
		expect("script: translated length", got, script[i].nbytes);
		if (got > 0)
			expect("script: translated byte", seq[0], script[i].byte0);
	}
	expect("script: ctrl ends released", ctrl, 0);
	picokbd_evq_drain(&out);
	expect("script: first call returns a full record", out.n, LCD_KEYEV_N);
	expect("script: nothing lost", out.lost, 0);
	for (i = 0; i < LCD_KEYEV_N; i++) {
		expect("script: state", out.ev[i].state, script[i].state);
		expect("script: key", out.ev[i].key, script[i].key);
	}
	picokbd_evq_drain(&out);
	expect("script: second call returns the rest", out.n, (long)(n - LCD_KEYEV_N));
	for (i = 0; i < out.n; i++) {
		expect("script: state (2)", out.ev[i].state,
		    script[LCD_KEYEV_N + i].state);
		expect("script: key (2)", out.ev[i].key, script[LCD_KEYEV_N + i].key);
	}
	picokbd_evq_drain(&out);
	expect("script: then empty", out.n, 0);

	/* The released state, which the tty path drops, is in the queue. */
	picokbd_evq_reset();
	picokbd_event(PICOKBD_RELEASED, 'x', 0, &ctrl, seq);
	memset(&out, 0, sizeof(out));
	picokbd_evq_drain(&out);
	expect("released is queued", out.n, 1);
	expect("released state", out.ev[0].state, LCD_KEY_RELEASED);
	expect("released key", out.ev[0].key, 'x');

	/* The hold state is queued too. */
	picokbd_evq_reset();
	picokbd_event(PICOKBD_HOLD, 'x', 0, &ctrl, seq);
	memset(&out, 0, sizeof(out));
	picokbd_evq_drain(&out);
	expect("hold is queued", out.n, 1);
	expect("hold state", out.ev[0].state, LCD_KEY_HOLD);

	/* No event, and states that are no event, do not enter the queue. */
	picokbd_evq_reset();
	expect("idle types nothing", picokbd_event(PICOKBD_IDLE, 0, 0, &ctrl, seq), 0);
	picokbd_event(4, 'a', 0, &ctrl, seq);
	picokbd_event(-1, 'a', 0, &ctrl, seq);
	picokbd_event(255, 'a', 0, &ctrl, seq);
	memset(&out, 0, sizeof(out));
	picokbd_evq_drain(&out);
	expect("idle and bad states are not queued", out.n, 0);
}

/* The translated bytes are the same with and without the queue. */
static void
test_translation_unchanged(void)
{
	char a[PICOKBD_MAXSEQ], b[PICOKBD_MAXSEQ];
	int ctrl_a = 0, ctrl_b = 0, state, key, app, na, nb;
	unsigned i, bad = 0;

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

					picokbd_evq_drain(&out);
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

static void
test_drain_limit(void)
{
	picokbd_evq_reset();
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
	picokbd_evq_reset();
	put_run(0, LCD_KEYEV_QUEUE);
	expect("a full queue loses nothing", drain_run("full", 0, 6), 0);
	taken = 6 + drain_all(6);
	expect("a full queue returns all of it", taken, LCD_KEYEV_QUEUE);

	/* One over: the oldest goes, the drop is counted once. */
	picokbd_evq_reset();
	put_run(0, LCD_KEYEV_QUEUE + 1);
	expect("one over drops one", drain_run("one over", 1, 6), 1);
	expect("the drop is reported once", drain_run("after", 7, 6), 0);
	taken = 12 + drain_all(13);
	expect("the newest events survive", taken, LCD_KEYEV_QUEUE);

	/* Forty events: the last 32 remain, oldest first, 8 dropped. */
	picokbd_evq_reset();
	put_run(0, 40);
	expect("forty events drop eight", drain_run("forty", 8, 6), 8);
	taken = 6 + drain_all(14);
	expect("the last 32 events return", taken, LCD_KEYEV_QUEUE);

	/* The drop count survives an empty drain only until it is reported. */
	picokbd_evq_reset();
	put_run(0, 50);
	expect("fifty events drop eighteen", drain_run("fifty", 18, 6), 18);
	expect("reported once", drain_run("fifty again", 24, 6), 0);

	/* The count saturates at 255 and the newest 32 remain. */
	picokbd_evq_reset();
	put_run(0, 1000);
	expect("a thousand events saturate the count", drain_run("thousand", 968, 6), 255);
	taken = 6 + drain_all(974);
	expect("the last 32 of a thousand return", taken, LCD_KEYEV_QUEUE);

	/* A reset forgets events and drops. */
	put_run(0, 100);
	picokbd_evq_reset();
	expect("reset empties the queue and the count", drain_run("reset", 0, 0), 0);
}

int
main(void)
{
	test_constants();
	test_script();
	test_translation_unchanged();
	test_drain_limit();
	test_overflow();
	if (failures != 0) {
		fprintf(stderr, "kbdevq_test: %d failures\n", failures);
		return 1;
	}
	printf("kbdevq_test: ok\n");
	return 0;
}
