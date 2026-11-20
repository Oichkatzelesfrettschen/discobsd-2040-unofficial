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

/*
 * Undo records. Every command writes at most three, so one Undo restores
 * the whole command whatever its size:
 *
 *   U_INS   typed text: [pos, pos + len) from the add stream at ref, with
 *           the property indexes chp and pap;
 *   U_PAP   the paragraph rejoin after a deleted line break: [pos, pos +
 *           len) changed from paragraph index pap to npap;
 *   U_DELM  a deletion of [pos, pos + len): its text at ref and its property
 *           runs as a run map at map;
 *   U_INSM  an insertion of runs (a paste): text at ref, run map at map;
 *   U_FMTM  a formatting change over [pos, pos + len): the run map at map
 *           holds each run's indexes before and after;
 *   U_SNAP  a Replace: ref names a 24-byte block in the add stream holding
 *           the text, run map and length of the document before and after.
 *
 * A run map is a sequence of 8-byte entries in the add stream: the run
 * length (4 bytes, little-endian), then its chp, pap, new chp and new pap.
 */
enum { U_INS = 1, U_PAP, U_DELM, U_INSM, U_FMTM, U_SNAP };

struct urec {
	woff_t pos;
	woff_t len;
	woff_t ref;		/* add stream offset of the text */
	woff_t map;		/* add stream offset of the run map */
	uint32_t seq;		/* names this record's text state; never reused */
	uint16_t group;
	uint8_t kind, chp, pap, npap;
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

static void	note_growth(void);

static struct blk cache[NBLK];
static uint32_t cache_clock;

static struct urec ur[NUNDO];
static unsigned ucount, upos;
static uint16_t ugroup;
static uint32_t useq;		/* last record sequence number */
static uint32_t ulost;		/* records dropped from the oldest end */
static woff_t lowmark;		/* lowest offset edited since buf_lowmark() */
static uint32_t nedits;		/* counts every change to the document */

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

/*
 * Failure injection for tests/buf_test.c: with BUF_TEST, the allocation
 * point numbered buf_fault_at (counting from 0 at each buf_test_fault()
 * call) fails with ENOSPC. The allocation points are the add stream
 * appends and the slot reservation.
 */
#ifdef BUF_TEST
static int fault_at = -1;
static unsigned peak;		/* most slots an atomic edit took past its start */

void
buf_test_fault(int at)
{
	fault_at = at;
}

unsigned
buf_test_peak(void)
{
	unsigned p = peak;

	peak = 0;
	return p;
}

unsigned
buf_test_reserve(void);

void
buf_test_undo(unsigned *count, unsigned *pos)
{
	*count = ucount;
	*pos = upos;
}

static int
fault(void)
{
	if (fault_at < 0)
		return 0;
	return fault_at-- == 0;
}
#else
#define fault()	0
#endif

/* Append bytes to the add stream; *ref receives their stream offset. */
static int
add_append(const char *s, size_t n, woff_t *ref)
{
	if (fault())
		return ENOSPC;
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
	nedits++;
	if (pos < lowmark)
		lowmark = pos;
}

unsigned
buf_piece_cap(void)
{
	return NPIECE;
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
	note_growth();
	hint_reset();
	return i + 1;
}

/*
 * Rewrite the document when the slots run out: neighbouring pieces with
 * equal properties become one add stream piece. The whole text is copied
 * first, in one run of the add stream, and the table is rewritten only
 * after the copy succeeds, so a failed compaction leaves every piece as it
 * was. The text and properties are unchanged, so every undo record stays
 * valid.
 */
static int
compact(void)
{
	unsigned i, j, out = 0;
	woff_t s = 0, len, ref;
	int e;

	if (doclen > 0 && (e = add_copy(0, doclen, &ref)) != 0)
		return e;
	for (i = 0; i < npiece; i = j) {
		len = 0;
		for (j = i; j < npiece && pt[j].chp == pt[i].chp &&
		    pt[j].pap == pt[i].pap; j++)
			len += pt[j].len;
		pt[out].off = (ref + s) | ADDBIT;
		pt[out].len = len;
		pt[out].chp = pt[i].chp;
		pt[out].pap = pt[i].pap;
		s += len;
		out++;
	}
	npiece = out;
	hint_reset();
	return npiece + 2 > NPIECE ? ENOSPC : 0;
}

/*
 * An atomic edit stages its text, then reserves BUF_RESERVE free slots and
 * runs with reserving set, when no raw operation compacts or fails for
 * want of a slot. The count is the smallest that tests/buf_test.c accepts:
 * every delete and replace of its documents succeeds from a table filled
 * to BUF_RESERVE free slots, and fails with one fewer (each raw operation
 * asks for two slots before it splits).
 */
#define BUF_RESERVE	3u

#ifdef BUF_TEST
unsigned
buf_test_reserve(void)
{
	return BUF_RESERVE;
}

/* A copy of the whole store, so tests/buf_test.c rebuilds a document fast. */
static struct {
	struct piece pt[NPIECE];
	unsigned npiece, nchp, npap, ucount, upos;
	woff_t doclen, spilled;
	uint32_t addram_len;
	char addram[ADD_RAM];
	struct urec ur[NUNDO];
	struct word_chp chps[WORD_NCHP];
	struct word_pap paps[WORD_NPAP];
} saved;

void
buf_test_save(void)
{
	memcpy(saved.pt, pt, sizeof(pt));
	memcpy(saved.addram, addram, sizeof(addram));
	memcpy(saved.ur, ur, sizeof(ur));
	memcpy(saved.chps, chps, sizeof(chps));
	memcpy(saved.paps, paps, sizeof(paps));
	saved.npiece = npiece;
	saved.nchp = nchp;
	saved.npap = npap;
	saved.ucount = ucount;
	saved.upos = upos;
	saved.doclen = doclen;
	saved.spilled = spilled;
	saved.addram_len = addram_len;
}

void
buf_test_restore(void)
{
	unsigned i;

	memcpy(pt, saved.pt, sizeof(pt));
	memcpy(addram, saved.addram, sizeof(addram));
	memcpy(ur, saved.ur, sizeof(ur));
	memcpy(chps, saved.chps, sizeof(chps));
	memcpy(paps, saved.paps, sizeof(paps));
	npiece = saved.npiece;
	nchp = saved.nchp;
	npap = saved.npap;
	ucount = saved.ucount;
	upos = saved.upos;
	doclen = saved.doclen;
	spilled = saved.spilled;
	addram_len = saved.addram_len;
	for (i = 0; i < NBLK; i++)
		cache[i].valid = 0;
	hint_reset();
	touched(0);
}
#endif

static int reserving;
static unsigned rbase;		/* npiece when the atomic edit began */

static void
note_growth(void)
{
#ifdef BUF_TEST
	if (reserving && npiece > rbase && npiece - rbase > peak)
		peak = npiece - rbase;
#endif
}

/* Make need free slots, compacting if the table is short of them. */
static int
reserve_n(unsigned need)
{
	int e;

	if (fault())
		return ENOSPC;
	if (need > NPIECE)
		return ENOSPC;
	if (npiece + need > NPIECE) {
		if ((e = compact()) != 0)
			return e;
		if (npiece + need > NPIECE)
			return ENOSPC;
	}
	return 0;
}

static int
reserve(void)
{
	return reserve_n(BUF_RESERVE);
}

/*
 * Slots for a raw operation: compaction when they run out, except inside
 * an atomic edit, whose reservation must already hold them (EFAULT, which
 * tests/buf_test.c treats as a failure, says it did not).
 */
static int
slots(void)
{
	if (reserving)
		return npiece + 2 > NPIECE ? EFAULT : 0;
	return npiece + 2 > NPIECE ? compact() : 0;
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
	if ((e = slots()) != 0)
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
	note_growth();
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
	if ((e = slots()) != 0)
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
	if ((e = slots()) != 0)
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

static int snapping;		/* a Replace in progress: records suppressed */
static struct urec scratch;

static struct urec *
urec_push(int kind, woff_t pos, woff_t len, woff_t ref, unsigned chp,
    unsigned pap)
{
	struct urec *u;

	if (snapping) {
		memset(&scratch, 0, sizeof(scratch));
		return &scratch;
	}
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
		/*
		 * The history is full: drop the oldest group whole, so no Undo
		 * restores part of a command. Only a run of typing keys, one
		 * record per key, can be the whole history; then its oldest key
		 * goes, and Undo still steps back key by key.
		 */
		unsigned k = 1;

		if (ur[0].group != ugroup)
			while (k < ucount && ur[k].group == ur[0].group)
				k++;
		memmove(&ur[0], &ur[k], (NUNDO - k) * sizeof(ur[0]));
		ucount -= k;
		ulost += k;
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

static int
has_break(woff_t pos, woff_t n)
{
	woff_t p;

	for (p = pos; p < pos + n; p++)
		if (buf_at(p) == '\n')
			return 1;
	return 0;
}

/* ---- run maps ---- */

struct mrun {
	woff_t len;
	uint8_t chp, pap, nchp, npap;
};

/* Append one run map entry; *first gets the offset of the first. */
static int
map_put(const struct mrun *m, woff_t *first, int *have)
{
	char b[8];
	woff_t r;
	int e;

	b[0] = (char)(m->len & 0xff);
	b[1] = (char)(m->len >> 8 & 0xff);
	b[2] = (char)(m->len >> 16 & 0xff);
	b[3] = (char)(m->len >> 24 & 0xff);
	b[4] = (char)m->chp;
	b[5] = (char)m->pap;
	b[6] = (char)m->nchp;
	b[7] = (char)m->npap;
	e = add_append(b, sizeof(b), &r);
	if (e == 0 && !*have) {
		*first = r;
		*have = 1;
	}
	return e;
}

static void
map_get(woff_t at, struct mrun *m)
{
	m->len = (woff_t)buf_ref_at(at) | (woff_t)buf_ref_at(at + 1) << 8 |
	    (woff_t)buf_ref_at(at + 2) << 16 | (woff_t)buf_ref_at(at + 3) << 24;
	m->chp = (uint8_t)buf_ref_at(at + 4);
	m->pap = (uint8_t)buf_ref_at(at + 5);
	m->nchp = (uint8_t)buf_ref_at(at + 6);
	m->npap = (uint8_t)buf_ref_at(at + 7);
}

/* The number of entries of the map at map covering len bytes. */
static unsigned
map_runs(woff_t map, woff_t len)
{
	struct mrun m;
	unsigned n = 0;

	while (len > 0) {
		map_get(map + 8u * n, &m);
		if (m.len == 0 || m.len > len)
			break;
		len -= m.len;
		n++;
	}
	return n;
}

/* Stage the property runs of [pos, pos + n) as a run map. */
static int
stage_runs(woff_t pos, woff_t n, woff_t *map)
{
	struct mrun m;
	woff_t end = pos + n, s, b;
	unsigned i;
	int e, have = 0;

	*map = spilled + addram_len;
	m.len = 0;
	while (pos < end) {
		i = piece_find(pos, &s);
		b = s + pt[i].len < end ? s + pt[i].len : end;
		if (m.len > 0 && (pt[i].chp != m.chp || pt[i].pap != m.pap)) {
			if ((e = map_put(&m, map, &have)) != 0)
				return e;
			m.len = 0;
		}
		if (m.len == 0) {
			m.chp = m.nchp = pt[i].chp;
			m.pap = m.npap = pt[i].pap;
		}
		m.len += b - pos;
		pos = b;
	}
	return m.len > 0 ? map_put(&m, map, &have) : 0;
}

/* Insert the runs of a map at pos; the caller reserved runs + 2 slots. */
static int
map_insert(woff_t pos, woff_t ref, woff_t map, woff_t len)
{
	struct mrun m;
	woff_t off = 0;
	unsigned k;
	int e;

	for (k = 0; off < len; k++) {
		map_get(map + 8u * k, &m);
		if ((e = raw_insert(pos + off, ref + off, m.len, m.chp, m.pap)) != 0)
			return e;
		off += m.len;
	}
	return 0;
}

/* Set each run's indexes, before (undo) or after; runs + 3 slots reserved. */
static int
map_props(woff_t pos, woff_t map, woff_t len, int undo)
{
	struct mrun m;
	woff_t off = 0;
	unsigned k;
	int e;

	for (k = 0; off < len; k++) {
		map_get(map + 8u * k, &m);
		e = raw_props(pos + off, m.len, 3, undo ? m.chp : m.nchp,
		    undo ? m.pap : m.npap);
		if (e != 0)
			return e;
		off += m.len;
	}
	return 0;
}

/*
 * After a deletion that removed a line break at pos, the text before pos
 * joins the paragraph after it and takes that paragraph's properties.
 * Every byte of a paragraph carries the paragraph's index, so the change
 * is one index for the whole range and needs no map; it runs inside the
 * deletion's reservation.
 */
static int
rejoin(woff_t pos)
{
	woff_t bol = pos;
	unsigned from, to;
	int e;

	if (pos == 0 || pos >= doclen)
		return 0;
	while (bol > 0 && buf_at(bol - 1) != '\n')
		bol--;
	to = (unsigned)buf_props(pos) >> 8;
	from = (unsigned)buf_props(pos - 1) >> 8;
	if (bol == pos || from == to)
		return 0;
	if ((e = raw_props(bol, pos - bol, 2, 0, to)) != 0)
		return e;
	urec_push(U_PAP, bol, pos - bol, 0, 0, from)->npap = (uint8_t)to;
	return 0;
}

/*
 * Replace n bytes at pos by s[0..len); fchp >= 0 names the character
 * properties of the new text (the typing format), otherwise it takes those
 * of the first byte it replaces. The new text, the deleted text and its
 * run map are staged in the add stream and the piece slots reserved before
 * the document changes, so a failure leaves the document and the undo
 * records as they were.
 */
int
buf_replace_fmt(woff_t pos, woff_t n, const char *s, size_t len, int fchp)
{
	woff_t dref = 0, iref = 0, map = 0;
	unsigned chp, pap;
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
	if (n > 0 && ((e = add_copy(pos, n, &dref)) != 0 ||
	    (e = stage_runs(pos, n, &map)) != 0))
		return e;
	if ((e = reserve()) != 0)
		return e;
	reserving = 1;
	rbase = npiece;
	if (n > 0 && (e = raw_delete(pos, n)) == 0)
		urec_push(U_DELM, pos, n, dref, 0, 0)->map = map;
	if (e == 0 && (e = raw_insert(pos, iref, (woff_t)len, chp, pap)) == 0 &&
	    len > 0)
		urec_push(U_INS, pos, (woff_t)len, iref, chp, pap);
	if (e == 0 && nl)
		e = rejoin(pos + (woff_t)len);
	reserving = 0;
	return e;
}

int
buf_replace(woff_t pos, woff_t n, const char *s, size_t len)
{
	return buf_replace_fmt(pos, n, s, len, -1);
}

/*
 * Insert len bytes of the add stream at ref as runs (a paste): runs[i]
 * covers runs[i].len bytes, and the last run takes whatever remains. The
 * run map is staged and the slots reserved first, so the paste is whole or
 * not at all.
 */
int
buf_insert_runs(woff_t pos, woff_t ref, woff_t len, const struct word_run *runs,
    unsigned nrun)
{
	struct mrun m;
	woff_t off = 0, map = 0;
	unsigned i, k = 0;
	int e, have = 0;

	if (pos > doclen)
		return EINVAL;
	if (len == 0)
		return 0;
	for (i = 0; off < len; i++) {
		m.len = i + 1 >= nrun || runs[i].len > len - off ? len - off :
		    runs[i].len;
		m.chp = m.nchp = i < nrun ? runs[i].chp : 0;
		m.pap = m.npap = i < nrun ? runs[i].pap : 0;
		if (m.len == 0)
			continue;
		if ((e = map_put(&m, &map, &have)) != 0)
			return e;
		off += m.len;
		k++;
	}
	if ((e = reserve_n(k + 2)) != 0)
		return e;
	reserving = 1;
	rbase = npiece;
	e = map_insert(pos, ref, map, len);
	reserving = 0;
	if (e == 0)
		urec_push(U_INSM, pos, len, ref, 0, 0)->map = map;
	return e;
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

/* Copy n add stream bytes at ref to the end of the add stream. */
int
buf_ref_copy(woff_t ref, woff_t n, woff_t *out)
{
	char chunk[64];
	woff_t done = 0, r;
	int e;

	*out = spilled + addram_len;
	while (done < n) {
		size_t k = 0;

		while (k < sizeof(chunk) && done + k < n) {
			int c = buf_ref_at(ref + done + (woff_t)k);

			if (c < 0)
				return EIO;
			chunk[k++] = (char)c;
		}
		if ((e = add_append(chunk, k, &r)) != 0)
			return e;
		done += (woff_t)k;
	}
	return 0;
}

int
buf_copy(woff_t pos, woff_t n, woff_t *ref)
{
	if (pos > doclen || n > doclen - pos)
		return EINVAL;
	return add_copy(pos, n, ref);
}

/*
 * A deletion copies its text and its run map into the add stream,
 * reserves its slots, then removes the text and rejoins the paragraphs it
 * merged. Every failure point comes before the first change, and the
 * deletion is one record however many runs it spans.
 */
int
buf_delete(woff_t pos, woff_t n, woff_t *ref)
{
	woff_t r, map;
	int e, nl;

	if (n == 0)
		return 0;
	if (pos > doclen || n > doclen - pos)
		return EINVAL;
	if ((e = buf_copy(pos, n, &r)) != 0 || (e = stage_runs(pos, n, &map)) != 0)
		return e;
	nl = has_break(pos, n);
	if ((e = reserve()) != 0)
		return e;
	reserving = 1;
	rbase = npiece;
	if ((e = raw_delete(pos, n)) == 0)
		urec_push(U_DELM, pos, n, r, 0, 0)->map = map;
	if (e == 0 && nl)
		e = rejoin(pos);
	reserving = 0;
	if (e == 0 && ref != NULL)
		*ref = r;
	return e;
}

/*
 * Change the properties of [lo, hi) run by run: fn maps each run's old
 * index to its new one and may add a record to the property table. Every
 * mapping is made, and the run map staged, before the document changes; a
 * full table or a failed staging leaves the document, the undo records and
 * the table as they were. The change is one record however many runs it
 * spans.
 */
int
buf_restyle(woff_t lo, woff_t hi, int which,
    int (*fn)(unsigned old, int which, void *ctx), void *ctx)
{
	struct mrun m;
	woff_t s, b, map = 0;
	unsigned i, snc = nchp, snp = npap;
	int ni, e = 0, have = 0, changed = 0;

	if (hi > doclen)
		hi = doclen;
	for (b = lo; b < hi && e == 0; ) {
		i = piece_find(b, &s);
		m.chp = m.nchp = pt[i].chp;
		m.pap = m.npap = pt[i].pap;
		ni = fn(which == 1 ? m.chp : m.pap, which, ctx);
		if (ni < 0) {
			e = ENOSPC;
			break;
		}
		if (which == 1)
			m.nchp = (uint8_t)ni;
		else
			m.npap = (uint8_t)ni;
		changed |= m.nchp != m.chp || m.npap != m.pap;
		m.len = (s + pt[i].len < hi ? s + pt[i].len : hi) - b;
		e = map_put(&m, &map, &have);
		b += m.len;
	}
	/* The runs are pieces already; only the two ends can split. */
	if (e == 0 && changed)
		e = reserve_n(4);
	if (e != 0 || !changed) {
		nchp = snc;
		npap = snp;
		return e;
	}
	reserving = 1;
	rbase = npiece;
	e = map_props(lo, map, hi - lo, 0);
	reserving = 0;
	if (e == 0)
		urec_push(U_FMTM, lo, hi - lo, 0, 0, 0)->map = map;
	return e;
}

/* ---- Replace: one record for the whole command ---- */

static woff_t snap_map, snap_len, snap_ref;
static uint32_t snap_block;

/* Stage the whole document, text and run map, in the add stream. */
static int
stage_doc(woff_t *ref, woff_t *map, woff_t *len)
{
	int e;

	*len = doclen;
	*ref = *map = spilled + addram_len;
	if (doclen == 0)
		return 0;
	if ((e = add_copy(0, doclen, ref)) != 0)
		return e;
	return stage_runs(0, doclen, map);
}

/* Make the document the staged one; this needs no allocation. */
static void
restore_doc(woff_t ref, woff_t map, woff_t len)
{
	struct mrun m;
	woff_t off = 0;
	unsigned k;

	npiece = 0;
	for (k = 0; off < len && npiece < NPIECE; k++) {
		map_get(map + 8u * k, &m);
		pt[npiece].off = (ref + off) | ADDBIT;
		pt[npiece].len = m.len;
		pt[npiece].chp = m.chp;
		pt[npiece].pap = m.pap;
		npiece++;
		off += m.len;
	}
	doclen = off;
	hint_reset();
	touched(0);
}

int
buf_snap_begin(void)
{
	int e = stage_doc(&snap_ref, &snap_map, &snap_len);

	if (e != 0)
		return e;
	snapping = 1;
	snap_block = nedits;
	return 0;
}

/*
 * End a Replace. With ok, the document as it stands becomes one U_SNAP
 * record; without it, or when the after state cannot be staged, the
 * document returns to its state at buf_snap_begin().
 */
int
buf_snap_end(int ok)
{
	char blk[24];
	woff_t aref, amap, alen, b;
	int e = 0, i;

	if (!snapping)
		return 0;
	snapping = 0;
	if (nedits == snap_block)
		return 0;
	if (ok)
		e = stage_doc(&aref, &amap, &alen);
	if (!ok || e != 0) {
		restore_doc(snap_ref, snap_map, snap_len);
		return e;
	}
	for (i = 0; i < 4; i++) {
		blk[i] = (char)(snap_ref >> 8 * i);
		blk[4 + i] = (char)(snap_map >> 8 * i);
		blk[8 + i] = (char)(snap_len >> 8 * i);
		blk[12 + i] = (char)(aref >> 8 * i);
		blk[16 + i] = (char)(amap >> 8 * i);
		blk[20 + i] = (char)(alen >> 8 * i);
	}
	if ((e = add_append(blk, sizeof(blk), &b)) != 0) {
		restore_doc(snap_ref, snap_map, snap_len);
		return e;
	}
	urec_push(U_SNAP, 0, 0, b, 0, 0);
	return 0;
}

static woff_t
get32ref(woff_t at)
{
	return (woff_t)buf_ref_at(at) | (woff_t)buf_ref_at(at + 1) << 8 |
	    (woff_t)buf_ref_at(at + 2) << 16 | (woff_t)buf_ref_at(at + 3) << 24;
}

/* ---- undo and redo ---- */

/*
 * The pieces a format map needs split to restore its runs: one for each
 * run boundary, the two ends included, that is not a piece boundary now.
 */
static unsigned
map_splits(woff_t pos, woff_t map, woff_t len)
{
	struct mrun m;
	woff_t off = 0, s;
	unsigned k, n = 0;

	for (k = 0; ; k++) {
		if (pos + off > 0 && pos + off < doclen) {
			piece_find(pos + off, &s);
			n += s != pos + off;
		}
		if (off >= len)
			break;
		map_get(map + 8u * k, &m);
		if (m.len == 0)
			break;
		off += m.len;
	}
	return n;
}

/* The free slots a record needs to be undone or redone. */
static unsigned
rec_need(const struct urec *u)
{
	switch (u->kind) {
	case U_DELM:
	case U_INSM:
		return map_runs(u->map, u->len) + 2;
	case U_FMTM:
		return map_splits(u->pos, u->map, u->len) + 2;
	case U_SNAP:
		return 0;
	default:
		return 2;
	}
}

static int
apply(const struct urec *u, int undo, woff_t *pos)
{
	woff_t b;

	*pos = u->pos;
	switch (u->kind) {
	case U_PAP:
		return raw_props(u->pos, u->len, 2, 0, undo ? u->pap : u->npap);
	case U_FMTM:
		return map_props(u->pos, u->map, u->len, undo);
	case U_SNAP:
		b = u->ref + (undo ? 0 : 12);
		restore_doc(get32ref(b), get32ref(b + 4), get32ref(b + 8));
		*pos = 0;
		return 0;
	case U_INS:
		if (undo)
			return raw_delete(u->pos, u->len);
		*pos = u->pos + u->len;
		return raw_insert(u->pos, u->ref, u->len, u->chp, u->pap);
	default:
		break;
	}
	/* U_DELM and U_INSM: one inserts the runs, the other removes them. */
	if ((u->kind == U_INSM) == (undo != 0))
		return raw_delete(u->pos, u->len);
	*pos = u->pos + u->len;
	return map_insert(u->pos, u->ref, u->map, u->len);
}

/*
 * Undo or redo one group. The slots every record of the group needs are
 * reserved first, so the group is restored whole or not at all.
 */
static int
apply_group(int undo, woff_t *pos)
{
	unsigned i, need = 0, first = upos, last = upos;
	uint16_t g = undo ? ur[upos - 1].group : ur[upos].group;
	int e;

	/* The group is ur[first .. last). */
	if (undo)
		while (first > 0 && ur[first - 1].group == g)
			first--;
	else
		while (last < ucount && ur[last].group == g)
			last++;
	for (i = first; i < last; i++)
		need += rec_need(&ur[i]);
	if ((e = reserve_n(need + 2)) != 0)
		return e;
	reserving = 1;
	rbase = npiece;
	if (undo)
		for (i = last; i > first && e == 0; i--)
			e = apply(&ur[i - 1], 1, pos);
	else
		for (i = first; i < last && e == 0; i++)
			e = apply(&ur[i], 0, pos);
	reserving = 0;
	if (e != 0)
		return e;
	upos = undo ? first : last;
	ugroup++;
	return 0;
}

int
buf_undo(woff_t *pos)
{
	if (upos == 0)
		return 0;
	return apply_group(1, pos) == 0 ? 1 : -1;
}

int
buf_redo(woff_t *pos)
{
	if (upos == ucount)
		return 0;
	return apply_group(0, pos) == 0 ? 1 : -1;
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

/*
 * A save writes a temporary beside the target and renames it over the
 * target. The temporary is created exclusively under a name that carries
 * the process id and a retry counter: O_EXCL refuses an existing file and
 * a symbolic link alike, so a pre-made name is skipped and never written
 * through, and two saves never share a temporary. save_begin() and
 * save_end() serve the text save here and the Word save of lexeme_doc.c.
 */
int
save_begin(const char *path, struct save *sv)
{
	struct stat st;
	unsigned tries;

	if (strlen(path) >= WORD_PATH_MAX)
		return ENAMETOOLONG;
	sv->path = path;
	sv->have = stat(path, &st) == 0;
	sv->mode = sv->have ? (unsigned)(st.st_mode & 07777) : 0;
	sv->fd = -1;
	for (tries = 0; tries < 32 && sv->fd < 0; tries++) {
		snprintf(sv->tmp, sizeof(sv->tmp), "%s.wtmp.%ld.%u", path,
		    (long)getpid(), tries);
		sv->fd = open(sv->tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (sv->fd < 0 && errno != EEXIST)
			return errno;
	}
	return sv->fd < 0 ? EEXIST : 0;
}

/*
 * Finish a save begun by save_begin(): with e zero, keep the target's mode,
 * make the backup when asked and rename the temporary over the target;
 * otherwise, or when any of that fails, remove the temporary. Returns the
 * first error.
 */
int
save_end(struct save *sv, int e, int backup)
{
	char bak[WORD_PATH_MAX + 2];

	if (sv->have && e == 0 && fchmod(sv->fd, (mode_t)sv->mode) != 0)
		e = errno;
	if (close(sv->fd) != 0 && e == 0)
		e = errno;
	if (e == 0 && sv->have && backup) {
		snprintf(bak, sizeof(bak), "%s~", sv->path);
		unlink(bak);
		if (link(sv->path, bak) != 0)
			e = errno;
	}
	if (e == 0 && rename(sv->tmp, sv->path) != 0)
		e = errno;
	if (e != 0)
		unlink(sv->tmp);
	return e;
}

int
buf_save(const char *path, int backup)
{
	char chunk[BLK];
	struct save sv;
	int e;
	woff_t off = 0;

	if ((e = save_begin(path, &sv)) != 0)
		return e;
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
			e = write_all(sv.fd, chunk, k);
	}
	return save_end(&sv, e, backup);
}
