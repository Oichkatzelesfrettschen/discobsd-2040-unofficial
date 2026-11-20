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
 * lexeme_buf.c: the document as a piece table.
 *
 * A piece names a run of bytes in one of two sources: the loaded file,
 * which stays open read-only and is read on demand, or the add stream,
 * which only grows. The add stream keeps its newest ADD_RAM bytes in
 * memory and writes each full chunk to an unlinked temporary file, so
 * neither the file nor the edits need to fit the process window. Both
 * sources are immutable once written, which lets a small block cache
 * serve every read and lets an undo record name deleted text by its add
 * stream offset: a deletion copies the bytes it removes into the add
 * stream first.
 *
 * A save writes a temporary file beside the target and renames it over
 * the target; the descriptor of the loaded file keeps the old bytes
 * readable, so the pieces stay valid after the rename.
 */

#include "lexeme.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NPIECE	768u		/* piece slots; compaction frees them */
#define ADD_RAM	8192u		/* in-memory tail of the add stream */
#define BLK	512u		/* cache block of a source */
#define NBLK	6u		/* cache blocks */
#define NUNDO	192u		/* undo records */
#define ADDBIT	0x80000000u	/* piece offset in the add stream */

/*
 * Every byte carries two property indexes: its character properties and
 * the paragraph properties of the paragraph it belongs to (the properties
 * of a paragraph are those its closing line break carries). Both index
 * small tables of distinct property records; index 0 is the default.
 */
struct piece {
	uint32_t off;		/* source offset, ADDBIT for the add stream */
	woff_t len;
	uint8_t chp, pap;
};

struct blk {
	uint32_t no;		/* block number, ADDBIT for the spill file */
	uint32_t use;
	uint16_t len;
	uint8_t valid;
	char data[BLK];
};

enum { U_INS = 1, U_DEL = 2, U_FMT = 3 };

/*
 * An insert or delete names its text by add stream offset and its
 * properties by index; a format record names the range and the indexes
 * before (chp, pap) and after (nchp, npap).
 */
struct urec {
	woff_t pos;
	woff_t len;
	woff_t ref;		/* add stream offset of the text */
	uint32_t seq;		/* names this record's text state; never reused */
	uint16_t group;
	uint8_t kind, chp, pap, nchp, npap;
};

static struct piece pt[NPIECE];
static unsigned npiece;
static woff_t doclen;
static int srcfd = -1;

static struct word_chp chps[WORD_NCHP];
static struct word_pap paps[WORD_NPAP];
static unsigned nchp = 1, npap = 1;

static char addram[ADD_RAM];
static uint32_t addram_len;
static woff_t spilled;		/* add stream bytes in the spill file */
static int spillfd = -1;

static struct blk cache[NBLK];
static uint32_t cache_clock;

static struct urec ur[NUNDO];
static unsigned ucount, upos;
static uint16_t ugroup;
static uint32_t useq;		/* last record sequence number */
static uint32_t ulost;		/* records dropped from the oldest end */
static woff_t lowmark;		/* lowest offset edited since buf_lowmark() */

/* Piece lookup hint: the index and document offset of a piece. */
static unsigned hint_i;
static woff_t hint_start;

static int
blk_byte(uint32_t key, woff_t at)
{
	uint32_t no = (key & ADDBIT) | ((at & ~ADDBIT) / BLK);
	unsigned i, victim = 0;
	int fd = (key & ADDBIT) ? spillfd : srcfd;
	ssize_t n;
	struct blk *b;

	for (i = 0; i < NBLK; i++) {
		b = &cache[i];
		if (b->valid && b->no == no) {
			b->use = ++cache_clock;
			goto hit;
		}
		if (cache[i].use < cache[victim].use)
			victim = i;
	}
	b = &cache[victim];
	b->valid = 0;
	if (fd < 0 || lseek(fd, (off_t)((no & ~ADDBIT) * BLK), SEEK_SET) < 0)
		return -1;
	n = read(fd, b->data, BLK);
	if (n <= 0)
		return -1;
	b->len = (uint16_t)n;
	b->no = no;
	b->valid = 1;
	b->use = ++cache_clock;
hit:
	if ((at % BLK) >= b->len)
		return -1;
	return (unsigned char)b->data[at % BLK];
}

static void
cache_drop_file(void)
{
	unsigned i;

	for (i = 0; i < NBLK; i++)
		if (!(cache[i].no & ADDBIT))
			cache[i].valid = 0;
}

int
buf_ref_at(woff_t ref)
{
	if (ref >= spilled + addram_len)
		return -1;
	if (ref >= spilled)
		return (unsigned char)addram[ref - spilled];
	return blk_byte(ADDBIT, ref);
}

static int
src_at(uint32_t off)
{
	if (off & ADDBIT)
		return buf_ref_at(off & ~ADDBIT);
	return blk_byte(0, off);
}

static int
spill_open(void)
{
	char path[64];
	const char *dir = getenv("TMPDIR");

	if (dir == NULL || strlen(dir) > sizeof(path) - 16)
		dir = "/tmp";
	snprintf(path, sizeof(path), "%s/wordXXXXXX", dir);
	spillfd = mkstemp(path);
	if (spillfd < 0)
		return -1;
	unlink(path);
	return 0;
}

/* Append bytes to the add stream; *ref receives their stream offset. */
static int
add_append(const char *s, size_t n, woff_t *ref)
{
	if (spillfd < 0 && addram_len + n > ADD_RAM && spill_open() != 0)
		return ENOSPC;
	*ref = spilled + addram_len;
	while (n > 0) {
		size_t k = ADD_RAM - addram_len;

		if (k > n)
			k = n;
		memcpy(addram + addram_len, s, k);
		addram_len += (uint32_t)k;
		s += k;
		n -= k;
		if (addram_len == ADD_RAM && n > 0) {
			/*
			 * blk_byte() leaves the shared descriptor at its read
			 * position; the append goes to the end of the stream.
			 */
			if (lseek(spillfd, (off_t)spilled, SEEK_SET) < 0)
				return errno;
			if (write(spillfd, addram, ADD_RAM) != (ssize_t)ADD_RAM)
				return errno != 0 ? errno : EIO;
			spilled += ADD_RAM;
			addram_len = 0;
		}
	}
	return 0;
}

/* Copy n document bytes at pos into the add stream. */
static int
add_copy(woff_t pos, woff_t n, woff_t *ref)
{
	char chunk[64];
	woff_t done = 0, r;
	int e;

	*ref = spilled + addram_len;
	while (done < n) {
		size_t k = 0;

		while (k < sizeof(chunk) && done + k < n) {
			int c = buf_at(pos + done + (woff_t)k);

			if (c < 0)
				return EIO;
			chunk[k++] = (char)c;
		}
		e = add_append(chunk, k, &r);
		if (e != 0)
			return e;
		done += (woff_t)k;
	}
	return 0;
}

woff_t
buf_len(void)
{
	return doclen;
}

/*
 * The document state as the undo stack names it: the sequence number of the
 * newest applied record, or a value that moves with each dropped record when
 * none is applied. Undo back to a state returns its value; an edit after an
 * undo takes a new record number, so it never returns to a saved one.
 */
uint32_t
buf_gen(void)
{
	return upos > 0 ? ur[upos - 1].seq : 0xffffffffu - ulost;
}

woff_t
buf_lowmark(void)
{
	woff_t m = lowmark;

	lowmark = WOFF_END;
	return m;
}

static void
touched(woff_t pos)
{
	if (pos < lowmark)
		lowmark = pos;
}

unsigned
buf_pieces(void)
{
	return npiece;
}

woff_t
buf_spilled(void)
{
	return spilled;
}

/* Index of the piece holding off, and that piece's start offset. */
static unsigned
piece_find(woff_t off, woff_t *start)
{
	unsigned i = hint_i;
	woff_t s = hint_start;

	if (i > npiece || s > doclen || off < s) {
		i = 0;
		s = 0;
	}
	while (i < npiece && off >= s + pt[i].len) {
		s += pt[i].len;
		i++;
	}
	hint_i = i;
	hint_start = s;
	*start = s;
	return i;
}

int
buf_at(woff_t off)
{
	woff_t s;
	unsigned i;

	if (off >= doclen)
		return -1;
	i = piece_find(off, &s);
	return src_at(pt[i].off + (off - s));
}

int
buf_props(woff_t off)
{
	woff_t s;
	unsigned i;

	if (off >= doclen)
		return -1;
	i = piece_find(off, &s);
	return pt[i].pap << 8 | pt[i].chp;
}

const struct word_chp *
buf_chp(unsigned i)
{
	return &chps[i < nchp ? i : 0];
}

const struct word_pap *
buf_pap(unsigned i)
{
	return &paps[i < npap ? i : 0];
}

int
buf_chp_index(const struct word_chp *c)
{
	unsigned i;

	for (i = 0; i < nchp; i++)
		if (memcmp(&chps[i], c, sizeof(*c)) == 0)
			return (int)i;
	if (nchp == WORD_NCHP)
		return -1;
	chps[nchp] = *c;
	return (int)nchp++;
}

int
buf_pap_index(const struct word_pap *p)
{
	unsigned i;

	for (i = 0; i < npap; i++)
		if (memcmp(&paps[i], p, sizeof(*p)) == 0)
			return (int)i;
	if (npap == WORD_NPAP)
		return -1;
	paps[npap] = *p;
	return (int)npap++;
}

static void
hint_reset(void)
{
	hint_i = 0;
	hint_start = 0;
}

/* Make a piece boundary at off; returns the index of the piece there. */
static unsigned
split_at(woff_t off)
{
	woff_t s;
	unsigned i = piece_find(off, &s);
	woff_t within = off - s;

	if (i >= npiece || within == 0)
		return i;
	memmove(&pt[i + 2], &pt[i + 1], (npiece - i - 1) * sizeof(pt[0]));
	pt[i + 1] = pt[i];
	pt[i + 1].off = pt[i].off + within;
	pt[i + 1].len = pt[i].len - within;
	pt[i].len = within;
	npiece++;
	hint_reset();
	return i + 1;
}

/*
 * Rewrite the document when the slots run out: neighbouring pieces with
 * equal properties become one add stream piece. The text and properties
 * are unchanged, so every undo record stays valid.
 */
static int
compact(void)
{
	unsigned i, j, out = 0;
	woff_t s = 0, len, ref;
	int e;

	for (i = 0; i < npiece; i = j) {
		len = 0;
		for (j = i; j < npiece && pt[j].chp == pt[i].chp &&
		    pt[j].pap == pt[i].pap; j++)
			len += pt[j].len;
		e = add_copy(s, len, &ref);
		if (e != 0)
			return e;
		s += len;
		pt[out].off = ref | ADDBIT;
		pt[out].len = len;
		pt[out].chp = pt[i].chp;
		pt[out].pap = pt[i].pap;
		out++;
		hint_reset();
	}
	npiece = out;
	hint_reset();
	return npiece + 2 > NPIECE ? ENOSPC : 0;
}

static int
raw_insert(woff_t pos, woff_t ref, woff_t n, unsigned chp, unsigned pap)
{
	unsigned i;
	woff_t s;
	int e;

	if (n == 0)
		return 0;
	if (pos > doclen)
		return EINVAL;
	if (npiece + 2 > NPIECE && (e = compact()) != 0)
		return e;
	if (pos > 0) {
		i = piece_find(pos - 1, &s);
		if (s + pt[i].len == pos && (pt[i].off & ADDBIT) &&
		    pt[i].off + pt[i].len == (ref | ADDBIT) &&
		    pt[i].chp == chp && pt[i].pap == pap) {
			pt[i].len += n;
			doclen += n;
			touched(pos);
			return 0;
		}
	}
	i = split_at(pos);
	memmove(&pt[i + 1], &pt[i], (npiece - i) * sizeof(pt[0]));
	pt[i].off = ref | ADDBIT;
	pt[i].len = n;
	pt[i].chp = (uint8_t)chp;
	pt[i].pap = (uint8_t)pap;
	npiece++;
	doclen += n;
	touched(pos);
	hint_reset();
	return 0;
}

static int
raw_delete(woff_t pos, woff_t n)
{
	unsigned a, b;
	int e;

	if (n == 0)
		return 0;
	if (pos > doclen || n > doclen - pos)
		return EINVAL;
	if (npiece + 2 > NPIECE && (e = compact()) != 0)
		return e;
	a = split_at(pos);
	b = split_at(pos + n);
	memmove(&pt[a], &pt[b], (npiece - b) * sizeof(pt[0]));
	npiece -= b - a;
	doclen -= n;
	touched(pos);
	hint_reset();
	return 0;
}

/* Set the property indexes of [pos, pos + n); which: 1 chp, 2 pap. */
static int
raw_props(woff_t pos, woff_t n, int which, unsigned chp, unsigned pap)
{
	unsigned a, b, i;
	int e;

	if (n == 0)
		return 0;
	if (npiece + 2 > NPIECE && (e = compact()) != 0)
		return e;
	a = split_at(pos);
	b = split_at(pos + n);
	for (i = a; i < b; i++) {
		if (which & 1)
			pt[i].chp = (uint8_t)chp;
		if (which & 2)
			pt[i].pap = (uint8_t)pap;
	}
	touched(pos);
	return 0;
}

static struct urec *
urec_push(int kind, woff_t pos, woff_t len, woff_t ref, unsigned chp,
    unsigned pap)
{
	struct urec *u;

	ucount = upos;
	if (ucount > 0) {
		u = &ur[ucount - 1];
		if (kind == U_INS && u->kind == U_INS && u->group == ugroup &&
		    u->pos + u->len == pos && u->ref + u->len == ref &&
		    u->chp == chp && u->pap == pap) {
			u->len += len;
			u->seq = ++useq;
			return NULL;
		}
	}
	if (ucount == NUNDO) {
		memmove(&ur[0], &ur[1], (NUNDO - 1) * sizeof(ur[0]));
		ucount--;
		ulost++;
	}
	u = &ur[ucount++];
	memset(u, 0, sizeof(*u));
	u->kind = (uint8_t)kind;
	u->pos = pos;
	u->len = len;
	u->ref = ref;
	u->chp = (uint8_t)chp;
	u->pap = (uint8_t)pap;
	u->group = ugroup;
	u->seq = ++useq;
	upos = ucount;
	return u;
}

void
buf_mark(void)
{
	ugroup++;
}

int
buf_insert_props(woff_t pos, woff_t ref, woff_t n, unsigned chp, unsigned pap)
{
	int e = raw_insert(pos, ref, n, chp, pap);

	if (e == 0 && n > 0)
		urec_push(U_INS, pos, n, ref, chp, pap);
	return e;
}

/*
 * Text inserted at pos takes the character properties of the byte before
 * it (or after it, at the start) and the paragraph properties of the byte
 * at pos (or before it, at the end). Every byte of a paragraph carries the
 * paragraph's properties, so that is the paragraph the text enters.
 */
void
buf_props_for(woff_t pos, unsigned *chp, unsigned *pap)
{
	int v;

	v = buf_props(pos > 0 ? pos - 1 : pos);
	*chp = v < 0 ? 0 : (unsigned)(v & 0xff);
	v = buf_props(pos < doclen ? pos : (doclen > 0 ? doclen - 1 : 0));
	*pap = v < 0 ? 0 : (unsigned)v >> 8;
}

int
buf_insert_ref(woff_t pos, woff_t ref, woff_t n)
{
	unsigned chp, pap;

	buf_props_for(pos, &chp, &pap);
	return buf_insert_props(pos, ref, n, chp, pap);
}

int
buf_insert(woff_t pos, const char *s, size_t n)
{
	woff_t ref;
	int e;

	if (n == 0)
		return 0;
	e = add_append(s, n, &ref);
	if (e != 0)
		return e;
	return buf_insert_ref(pos, ref, (woff_t)n);
}

static unsigned join_pap;

static int
join_map(unsigned old, int which, void *ctx)
{
	(void)old;
	(void)which;
	(void)ctx;
	return (int)join_pap;
}

/*
 * After a deletion that removed a line break at pos, the text before pos
 * joins the paragraph after it and takes that paragraph's properties.
 */
static int
rejoin(woff_t pos)
{
	woff_t bol = pos;
	int v;

	if (pos == 0 || pos >= doclen)
		return 0;
	while (bol > 0 && buf_at(bol - 1) != '\n')
		bol--;
	v = buf_props(pos);
	join_pap = (unsigned)v >> 8;
	if (bol == pos || (unsigned)buf_props(pos - 1) >> 8 == join_pap)
		return 0;
	return buf_restyle(bol, pos, 2, join_map, NULL);
}

static int
has_break(woff_t pos, woff_t n)
{
	woff_t p;

	for (p = pos; p < pos + n; p++)
		if (buf_at(p) == '\n')
			return 1;
	return 0;
}

/*
 * Replace n bytes at pos by s[0..len); fchp >= 0 names the character
 * properties of the new text (the typing format), otherwise it takes those
 * of the first byte it replaces. Both texts are staged in the add
 * stream and the piece slots are secured before the document changes, so a
 * failure leaves the document and the undo records as they were.
 */
int
buf_replace_fmt(woff_t pos, woff_t n, const char *s, size_t len, int fchp)
{
	woff_t dref = 0, iref = 0, end, s0, run;
	unsigned chp, pap, i, c, p;
	int e, v, nl;

	if (pos > doclen || n > doclen - pos)
		return EINVAL;
	nl = has_break(pos, n);
	buf_props_for(pos, &chp, &pap);
	if (n > 0 && (v = buf_props(pos)) >= 0)
		chp = (unsigned)(v & 0xff);
	if (fchp >= 0)
		chp = (unsigned)fchp;
	if (n == 0 && len == 0)
		return 0;
	if (len > 0 && (e = add_append(s, len, &iref)) != 0)
		return e;
	if (n > 0 && (e = add_copy(pos, n, &dref)) != 0)
		return e;
	/*
	 * Reserve every slot the call can need before the document changes:
	 * two splits for the deleted range, two for the inserted piece and
	 * two for the paragraph rejoin. A shortage after compaction fails
	 * here, with the text and the undo records as they were.
	 */
	if (npiece + 8 > NPIECE) {
		if ((e = compact()) != 0)
			return e;
		if (npiece + 8 > NPIECE)
			return ENOSPC;
	}
	/* One undo record per property run, as buf_delete() makes them. */
	for (end = pos + n; end > pos; end -= run) {
		i = piece_find(end - 1, &s0);
		run = end - (s0 > pos ? s0 : pos);
		c = pt[i].chp;
		p = pt[i].pap;
		if ((e = raw_delete(end - run, run)) != 0)
			return e;
		urec_push(U_DEL, end - run, run, dref + (end - run - pos), c, p);
	}
	if ((e = raw_insert(pos, iref, (woff_t)len, chp, pap)) != 0)
		return e;
	if (len > 0)
		urec_push(U_INS, pos, (woff_t)len, iref, chp, pap);
	return nl ? rejoin(pos + (woff_t)len) : 0;
}

int
buf_replace(woff_t pos, woff_t n, const char *s, size_t len)
{
	return buf_replace_fmt(pos, n, s, len, -1);
}

int
buf_append_props(const char *s, size_t n, unsigned chp, unsigned pap)
{
	woff_t ref;
	int e;

	if (n == 0)
		return 0;
	e = add_append(s, n, &ref);
	if (e != 0)
		return e;
	return raw_insert(doclen, ref, (woff_t)n, chp, pap);
}

void
buf_loaded(void)
{
	ucount = upos = 0;
	touched(0);
}

int
buf_insert_fmt(woff_t pos, const char *s, size_t n, int chp)
{
	unsigned c, p;
	woff_t ref;
	int e;

	if (n == 0)
		return 0;
	e = add_append(s, n, &ref);
	if (e != 0)
		return e;
	buf_props_for(pos, &c, &p);
	return buf_insert_props(pos, ref, (woff_t)n, chp >= 0 ? (unsigned)chp : c, p);
}

int
buf_copy(woff_t pos, woff_t n, woff_t *ref)
{
	if (pos > doclen || n > doclen - pos)
		return EINVAL;
	return add_copy(pos, n, ref);
}

/*
 * A deletion copies its text into the add stream, then removes it one
 * property run at a time from the end, so undo restores each run with
 * its own properties.
 */
int
buf_delete(woff_t pos, woff_t n, woff_t *ref)
{
	woff_t r, end, s, run;
	unsigned i, c, p;
	int e, nl;

	if (n == 0)
		return 0;
	e = buf_copy(pos, n, &r);
	if (e != 0)
		return e;
	nl = has_break(pos, n);
	end = pos + n;
	while (end > pos) {
		i = piece_find(end - 1, &s);
		run = end - (s > pos ? s : pos);
		c = pt[i].chp;
		p = pt[i].pap;
		e = raw_delete(end - run, run);
		if (e != 0)
			return e;
		urec_push(U_DEL, end - run, run, r + (end - run - pos), c, p);
		end -= run;
	}
	if (ref != NULL)
		*ref = r;
	return nl ? rejoin(pos) : 0;
}

int
buf_restyle(woff_t lo, woff_t hi, int which,
    int (*fn)(unsigned old, int which, void *ctx), void *ctx)
{
	woff_t s, a, b;
	unsigned i;
	int ni, e;
	struct urec *u;

	if (hi > doclen)
		hi = doclen;
	while (lo < hi) {
		i = piece_find(lo, &s);
		a = lo;
		b = s + pt[i].len < hi ? s + pt[i].len : hi;
		ni = fn(which == 1 ? pt[i].chp : pt[i].pap, which, ctx);
		if (ni < 0)
			return ENOSPC;
		if ((which == 1 && pt[i].chp != ni) || (which == 2 && pt[i].pap != ni)) {
			uint8_t oc = pt[i].chp, op = pt[i].pap;

			e = raw_props(a, b - a, which, (unsigned)ni, (unsigned)ni);
			if (e != 0)
				return e;
			u = urec_push(U_FMT, a, b - a, 0, oc, op);
			u->nchp = which == 1 ? (uint8_t)ni : oc;
			u->npap = which == 2 ? (uint8_t)ni : op;
		}
		lo = b;
	}
	return 0;
}

static int
apply(const struct urec *u, int undo, woff_t *pos)
{
	if (u->kind == U_FMT) {
		*pos = u->pos;
		return raw_props(u->pos, u->len, 3, undo ? u->chp : u->nchp,
		    undo ? u->pap : u->npap);
	}
	if ((u->kind == U_INS) == (undo != 0)) {
		*pos = u->pos;
		return raw_delete(u->pos, u->len);
	}
	*pos = u->pos + u->len;
	return raw_insert(u->pos, u->ref, u->len, u->chp, u->pap);
}

int
buf_undo(woff_t *pos)
{
	uint16_t g;

	if (upos == 0)
		return 0;
	g = ur[upos - 1].group;
	while (upos > 0 && ur[upos - 1].group == g) {
		if (apply(&ur[upos - 1], 1, pos) != 0)
			return -1;
		upos--;
	}
	ugroup++;
	return 1;
}

int
buf_redo(woff_t *pos)
{
	uint16_t g;

	if (upos == ucount)
		return 0;
	g = ur[upos].group;
	while (upos < ucount && ur[upos].group == g) {
		if (apply(&ur[upos], 0, pos) != 0)
			return -1;
		upos++;
	}
	ugroup++;
	return 1;
}
int
buf_init(void)
{
	buf_clear();
	return 0;
}

void
buf_clear(void)
{
	if (srcfd >= 0)
		close(srcfd);
	srcfd = -1;
	cache_drop_file();
	npiece = 0;
	doclen = 0;
	ucount = upos = 0;
	nchp = npap = 1;
	ulost = 0;
	touched(0);
	hint_reset();
}

int
buf_load(const char *path)
{
	struct stat st;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return errno;
	if (fstat(fd, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) {
		close(fd);
		return EINVAL;
	}
	if ((uintmax_t)st.st_size >= ADDBIT) {
		close(fd);
		return EFBIG;
	}
	buf_clear();
	srcfd = fd;
	if (st.st_size > 0) {
		pt[0].off = 0;
		pt[0].len = (woff_t)st.st_size;
		pt[0].chp = pt[0].pap = 0;
		npiece = 1;
		doclen = (woff_t)st.st_size;
	}
	touched(0);
	return 0;
}

int
buf_merge(woff_t pos, const char *path, woff_t *n)
{
	char chunk[256];
	woff_t ref = 0, r, total = 0;
	ssize_t k;
	int e, fd = open(path, O_RDONLY);

	if (fd < 0)
		return errno;
	while ((k = read(fd, chunk, sizeof(chunk))) > 0) {
		e = add_append(chunk, (size_t)k, &r);
		if (e != 0) {
			close(fd);
			return e;
		}
		if (total == 0)
			ref = r;
		total += (woff_t)k;
	}
	e = k < 0 ? errno : 0;
	close(fd);
	if (e != 0)
		return e;
	*n = total;
	return buf_insert_ref(pos, ref, total);
}

static int
write_all(int fd, const char *p, size_t n)
{
	while (n > 0) {
		ssize_t k = write(fd, p, n);

		if (k < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		p += k;
		n -= (size_t)k;
	}
	return 0;
}

int
buf_save(const char *path, int backup)
{
	char tmp[WORD_PATH_MAX + 32], bak[WORD_PATH_MAX + 2], chunk[BLK];
	struct stat st;
	int fd = -1, e = 0, have = stat(path, &st) == 0;
	unsigned tries;
	woff_t off = 0;

	if (strlen(path) >= WORD_PATH_MAX)
		return ENAMETOOLONG;
	/*
	 * The temporary is created exclusively under a name that carries the
	 * process id and a retry counter. O_EXCL refuses an existing file and
	 * a symbolic link alike, so a pre-made name is skipped and never
	 * written through.
	 */
	for (tries = 0; tries < 32 && fd < 0; tries++) {
		snprintf(tmp, sizeof(tmp), "%s.wtmp.%ld.%u", path,
		    (long)getpid(), tries);
		fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd < 0 && errno != EEXIST)
			return errno;
	}
	if (fd < 0)
		return EEXIST;
	while (e == 0 && off < doclen) {
		size_t k = 0;

		while (k < sizeof(chunk) && off < doclen) {
			int c = buf_at(off++);

			if (c < 0) {
				e = EIO;
				break;
			}
			chunk[k++] = (char)c;
		}
		if (e == 0)
			e = write_all(fd, chunk, k);
	}
	if (have && e == 0 && fchmod(fd, st.st_mode & 07777) != 0)
		e = errno;
	if (close(fd) != 0 && e == 0)
		e = errno;
	if (e == 0 && have && backup) {
		snprintf(bak, sizeof(bak), "%s~", path);
		unlink(bak);
		if (link(path, bak) != 0)
			e = errno;
	}
	if (e == 0 && rename(tmp, path) != 0)
		e = errno;
	if (e != 0)
		unlink(tmp);
	return e;
}
