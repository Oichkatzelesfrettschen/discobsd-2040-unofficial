/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * buf_test.c: the atomic edits of lexeme_buf.c (buf_delete(), buf_replace_fmt())
 * on the host, built with BUF_TEST. Three checks over every delete and
 * replace inside small documents of one- to four-byte pieces with mixed
 * character and paragraph properties:
 *
 *   undo  each edit undone restores the text, properties and undo count;
 *   slots in a document filled to BUF_RESERVE free slots, with neighbours
 *         that compaction cannot merge, every edit succeeds: the
 *         reservation holds every slot an edit takes;
 *   fault an allocation failure injected at each allocation point in
 *         turn leaves the text, properties and undo records as they were,
 *         in a small document and in tables one to four slots short of
 *         full, where the edit or its paragraph rejoin must compact.
 */

#include "lexeme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NPIECE_TEST	768	/* lexeme_buf.c NPIECE */
#define SNAP_MAX	2048

struct snap {
	woff_t len;
	char text[SNAP_MAX];
	int props[SNAP_MAX];
	unsigned ucount, upos;
};

static int chp_idx[3], pap_idx[3];
static long nops, failures;

static void
fail(const char *what, int op, woff_t pos, woff_t n, int k)
{
	if (failures++ < 10)
		printf("FAIL %s: op %d pos %lu n %lu fault %d\n", what, op,
		    (unsigned long)pos, (unsigned long)n, k);
}

static void
take(struct snap *s)
{
	woff_t i;

	s->len = buf_len();
	for (i = 0; i < s->len && i < SNAP_MAX; i++) {
		s->text[i] = (char)buf_at(i);
		s->props[i] = buf_props(i);
	}
	buf_test_undo(&s->ucount, &s->upos);
}

static int
same(const struct snap *a, const struct snap *b)
{
	return a->len == b->len && a->ucount == b->ucount &&
	    a->upos == b->upos && memcmp(a->text, b->text, a->len) == 0 &&
	    memcmp(a->props, b->props, a->len * sizeof(a->props[0])) == 0;
}

/*
 * The test document "abcde\nfghij\nklm" in pieces of one to four bytes,
 * character properties cycling through three records, each paragraph's bytes carrying its own
 * paragraph record. With fill > 0, pieces of alternating properties
 * follow until the table holds fill pieces; with merge, the filler
 * pieces share one record so a compaction can free them.
 */
static void
build_slow(int fill, int merge)
{
	/* Pieces of one to four bytes; a paragraph's pieces share its record. */
	static const char *const piece[] = {
		"abcd", "e", "\n", "fg", "hij", "\n", "k", "lm"
	};
	size_t i;
	int para = 0;

	buf_clear();
	for (i = 0; i < sizeof(piece) / sizeof(piece[0]); i++) {
		buf_append_props(piece[i], strlen(piece[i]),
		    (unsigned)chp_idx[i % 3], (unsigned)pap_idx[para]);
		if (piece[i][0] == '\n')
			para++;
	}
	while (fill > 0 && buf_pieces() < (unsigned)fill) {
		unsigned c = merge ? 0 : (unsigned)chp_idx[buf_pieces() % 2];
		woff_t gap;

		/* A copied byte between appends keeps equal pieces apart. */
		if (merge)
			buf_copy(0, 1, &gap);
		buf_append_props(buf_pieces() % 7 == 0 ? "\n" : "z", 1, c,
		    (unsigned)pap_idx[2]);
	}
	buf_loaded();
}

/* A document, built once for each fill and restored from a copy. */
static void
build(int fill, int merge)
{
	static int made = -1;
	int which = fill * 2 + merge;

	if (made != which) {
		build_slow(fill, merge);
		made = which;
		buf_test_save();
		return;
	}
	buf_test_restore();
}

/* Run edit op on [pos, pos + n); ops 0-1 delete, 2-7 replace. */
static int
edit(int op, woff_t pos, woff_t n)
{
	static const char *const with[] = { "", "X", "XY" };

	if (op < 2)
		return n == 0 ? 0 : buf_delete(pos, n, NULL);
	op -= 2;
	return buf_replace_fmt(pos, n, with[op % 3], strlen(with[op % 3]),
	    op >= 3 ? chp_idx[1] : -1);
}

int
main(void)
{
	struct word_chp c;
	struct word_pap p;
	struct snap before, after;
	woff_t pos, n, len;
	unsigned peak = 0, pk;
	int i, op, k, e, phase;

	buf_init();
	for (i = 0; i < 3; i++) {
		memset(&c, 0, sizeof(c));
		c.flags = (uint8_t)(1u << i);
		chp_idx[i] = buf_chp_index(&c);
		memset(&p, 0, sizeof(p));
		p.align = (uint8_t)i;
		p.left = (int16_t)(720 * i);
		pap_idx[i] = buf_pap_index(&p);
	}
	build(0, 0);
	len = buf_len();
	/* undo: each edit and its undo. */
	for (op = 0; op < 8; op++)
		for (pos = 0; pos <= len; pos++)
			for (n = 0; pos + n <= len; n++) {
				build(0, 0);
				take(&before);
				buf_test_peak();
				buf_mark();
				e = edit(op, pos, n);
				pk = buf_test_peak();
				if (pk > peak)
					peak = pk;
				if (e != 0)
					fail("edit failed", op, pos, n, -1);
				nops++;
				if (buf_undo(&(woff_t){ 0 }) < 0)
					fail("undo failed", op, pos, n, -1);
				take(&after);
				after.ucount = before.ucount;
				after.upos = before.upos;
				if (!same(&before, &after))
					fail("undo did not restore", op, pos, n, -1);
			}
	/* slots: a table with BUF_RESERVE free slots and nothing to compact. */
	for (op = 0; op < 8; op++)
		for (pos = 0; pos <= len; pos++)
			for (n = 0; pos + n <= len; n++) {
				build(NPIECE_TEST - (int)buf_test_reserve(), 0);
				if (edit(op, pos, n) != 0)
					fail("reserved edit failed", op, pos, n, -1);
			}
	/*
	 * fault: every allocation point in turn, in the small document and in
	 * tables one to four slots short of full, where the edit itself or the
	 * paragraph rejoin after it needs a compaction.
	 */
	for (phase = 0; phase < 5; phase++)
		for (op = 0; op < 8; op++)
			for (pos = 0; pos <= len; pos++)
				for (n = 0; pos + n <= len; n++)
					for (k = 0; ; k++) {
						build(phase ? NPIECE_TEST - phase : 0, 1);
						take(&before);
						buf_test_fault(k);
						e = edit(op, pos, n);
						buf_test_fault(-1);
						if (e == 0)
							break;
						take(&after);
						if (!same(&before, &after))
							fail("failed edit changed the document",
							    op, pos, n, k);
						if (k == 16) {
							/* Past every allocation point it must work. */
							fail("edit never succeeded", op, pos, n, k);
							break;
						}
					}
	printf("buf_test: %ld edits, most slots taken %u, reserve %u\n", nops,
	    peak, buf_test_reserve());
	if (peak > buf_test_reserve())
		fail("an edit took more slots than the reservation", -1, 0, 0, -1);
	if (failures != 0) {
		printf("buf_test: %ld failures\n", failures);
		return 1;
	}
	printf("buf_test: PASS\n");
	return 0;
}
