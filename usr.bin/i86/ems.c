/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Expanded memory under LIM EMS 4.0 (ems.h). Status codes and the
 * functions that return them follow the specification's Appendix A.
 *
 * Expanded memory is npool pages of 16 KB, the pager's logical pages from
 * MEM_NPAGE on. A physical page is a 16 KB block of the first megabyte
 * that a logical page can show: the page frame at E000h, whose pages are
 * physical pages 0 to 3 as version 3.2 programs expect, and A000h to
 * B7FFh and C000h to DFFFh, which this machine leaves empty: 288 KB above
 * 640 KB, the space Windows 3.0 in real mode banks applications into. No
 * conventional memory is mappable, and handle 0 owns no pages, because
 * conventional pages would lie over memory where EDR-DOS keeps its own
 * data. Windows 3.0 reports this arrangement as "Large Frame EMS"
 * (docs/research/i86-dos-emulator/windows30-host.txt).
 *
 * A mapping is the logical page a physical page shows (cur[]); a save
 * area holds a word of physical page count and then one word for each
 * physical page. Functions 55h and 56h (alter the map and jump or call)
 * are not implemented and answer 84h. There are no alternate map register
 * sets, so 5Bh simulates them as the specification's Appendix D has an
 * operating environment do: set 0 is a save area in guest memory that
 * 5B01h makes current, and the current area stands for the registers.
 * Switching to another area stores the mapping in the one left and loads
 * the new one; 5B00h stores the mapping in the current area before it
 * returns the area's pointer as 5B01h received it. There are no DMA
 * register sets: 5B05h gives none and 5B06h and 5B07h answer 9Eh.
 *
 * 57h reads conventional memory as the bus does, the firmware ROM
 * included, refuses a region that runs past 1 MB (A2h) and treats two
 * physical pages that show one logical page as one memory: an exchange
 * whose regions share bytes through them answers 97h, and a move that no
 * single copy order serves goes through staging pages past the pool.
 *
 * 5Dh keeps the operating system's access key: the first 5D00h or 5D01h
 * hands it out in BX:CX, later calls must present it (A4h otherwise), and
 * 5D02h takes it back; 5D02h with no key out answers A4h. While 5D01h has the functions off, 5900h and 5Bh
 * answer A4h. The key is a hash of the count of calls so far, the same
 * from run to run under -T.
 */
#include <stdlib.h>
#include <string.h>

#include "ems.h"
#include "mem.h"

#define NHANDLE		64
#define NPHYS		48
#define NOBASE		MEM_NOMAP
#define NSTAGE		64		/* 16 KB pages past the pool: 1 MB */
#define PAGE		16384u
#define OK		0x00
#define E_HANDLE	0x83		/* no such handle */
#define E_FUNC		0x84		/* function not defined */
#define E_NOHANDLE	0x85
#define E_TOTAL		0x87		/* more pages than there are */
#define E_FREE		0x88		/* more pages than are free */
#define E_ZERO		0x89
#define E_LPAGE		0x8a		/* logical page out of range */
#define E_PPAGE		0x8b		/* physical page out of range */
#define E_SAVED		0x8d
#define E_NOSAVE	0x8e
#define E_SUB		0x8f		/* subfunction not defined */

struct handle {
	uint16_t pages;
	uint8_t used, saved;
	char name[8];
	uint16_t save[4];	/* the page frame's mapping (47h) */
};

int ems_on;

static struct handle *hd;
static uint8_t *owner;		/* each pool page's handle + 1, or 0 */
static uint16_t *lnum;		/* its page number in that handle */
static unsigned npool, nphys;
static uint8_t blk[NPHYS];	/* each physical page's 16 KB block */
static uint16_t cur[NPHYS];	/* the logical page it shows */
/* A guest array a function takes (a save area, a partial map, a map
 * list), read once by take() before any entry is checked or applied.
 * Applying an entry can remap the guest page the array lies in, and an
 * output buffer may overlap it, so a second read of guest memory could see
 * words that were never checked. */
static uint16_t arg[1 + 2 * NPHYS];
static uint32_t alt_area;	/* set 0's save area (5B01h), linear */
static uint16_t alt_seg, alt_off; /* its pointer as given; 0:0 is none */
static uint8_t os_off;		/* 5D01h: 5900h and 5Bh refused */
static uint8_t key_out;		/* 5D00h or 5D01h has given out key */
static uint32_t key, calls;

static uint32_t
lin(unsigned seg, unsigned off)
{
	return (((uint32_t)seg << 4) + off) & (MEM_SIZE - 1);
}

/*
 * Guest memory at a linear address that wraps at 1 MB, as the 20 address
 * lines do. Every buffer address an EMS function derives from a guest
 * pointer (ES:DI + 4n and the like) passes through these, so a buffer that
 * crosses the top of memory continues at 0:0 instead of past the maps.
 */
static unsigned
grb(uint32_t a)
{
	return (mem_rb(a & (MEM_SIZE - 1)));
}

static unsigned
grw(uint32_t a)
{
	return (mem_rw(a & (MEM_SIZE - 1)));
}

static void
gwb(uint32_t a, unsigned v)
{
	mem_wb(a & (MEM_SIZE - 1), v);
}

static void
gww(uint32_t a, unsigned v)
{
	mem_ww(a & (MEM_SIZE - 1), v);
}

static void
add(unsigned b)
{
	blk[nphys] = (uint8_t)b;
	cur[nphys] = NOBASE;
	nphys++;
}

int
ems_init(unsigned kbytes)
{
	unsigned b;

	npool = kbytes / 16;
	hd = calloc(NHANDLE, sizeof(*hd));
	owner = calloc(npool ? npool : 1, 1);
	lnum = calloc(npool ? npool : 1, sizeof(*lnum));
	if (hd == NULL || owner == NULL || lnum == NULL ||
	    mem_ems(npool + NSTAGE) != 0)
		return (-1);
	for (b = 56; b < 60; b++)
		add(b);
	for (b = 40; b < 46; b++)
		add(b);
	for (b = 48; b < 56; b++)
		add(b);
	hd[0].used = 1;
	ems_on = 1;
	return (0);
}

static int
valid(unsigned h)
{
	return (h < NHANDLE && hd[h].used);
}

/* The first logical page of handle h's page n, or NOBASE. */
static unsigned
page_base(unsigned h, unsigned n)
{
	unsigned k;

	for (k = 0; k < npool; k++)
		if (owner[k] == h + 1 && lnum[k] == n)
			return (MEM_NPAGE + k * MEM_EMSPG);
	return (NOBASE);
}

static unsigned
nfree(void)
{
	unsigned k, n = 0;

	for (k = 0; k < npool; k++)
		n += owner[k] == 0;
	return (n);
}

static int base_ok(unsigned);

/* Every caller has checked p and base; the test keeps a missed check
 * from reaching the pager's tables with an index they do not hold. */
static void
show(unsigned p, unsigned base)
{
	if (p >= nphys || !base_ok(base))
		return;
	cur[p] = (uint16_t)base;
	mem_map16(blk[p], base);
}

/* Copy n words of guest memory from a into arg[]. */
static void
take(uint32_t a, unsigned n)
{
	unsigned k;

	for (k = 0; k < n && k < sizeof(arg) / sizeof(arg[0]); k++)
		arg[k] = (uint16_t)grw(a + 2 * k);
}

/* Map handle h's page n, or nothing for FFFFh, at physical page p. */
static unsigned
map(unsigned p, unsigned h, unsigned n)
{
	unsigned base = NOBASE;

	if (p >= nphys)
		return (E_PPAGE);
	if (n != 0xffff && (base = page_base(h, n)) == NOBASE)
		return (E_LPAGE);
	show(p, base);
	return (OK);
}

/* The physical page at segment seg, or nphys. */
static unsigned
phys_at(unsigned seg)
{
	unsigned p;

	for (p = 0; p < nphys && (unsigned)blk[p] << 10 != seg; p++)
		;
	return (p);
}

/* A logical page a save area may hold: nothing or the start of a pool
 * page. */
static int
base_ok(unsigned b)
{
	return (b == NOBASE || (b >= MEM_NPAGE &&
	    (b - MEM_NPAGE) % MEM_EMSPG == 0 &&
	    (b - MEM_NPAGE) / MEM_EMSPG < npool));
}

static void
put_map(uint32_t a)
{
	unsigned p;

	gww(a, nphys);
	for (p = 0; p < nphys; p++)
		gww(a + 2 + 2 * p, cur[p]);
}

/* Take the save area at a into arg[]: A3h when it holds no mapping. */
static unsigned
load_map(uint32_t a)
{
	unsigned p;

	take(a, 1 + nphys);
	if (arg[0] != nphys)
		return (0xa3);
	for (p = 0; p < nphys; p++)
		if (!base_ok(arg[1 + p]))
			return (0xa3);
	return (OK);
}

/* Show the mapping load_map() took. */
static void
apply_map(void)
{
	unsigned p;

	for (p = 0; p < nphys; p++)
		show(p, arg[1 + p]);
}

/* Give handle h pages from n on until it has want. */
static void
grow(unsigned h, unsigned n, unsigned want)
{
	unsigned k;

	for (k = 0; k < npool && n < want; k++)
		if (owner[k] == 0) {
			owner[k] = (uint8_t)(h + 1);
			lnum[k] = (uint16_t)n++;
		}
}

/* Take handle h's pages from n on; their bytes are forgotten. */
static void
shrink(unsigned h, unsigned n)
{
	unsigned k;

	for (k = 0; k < npool; k++)
		if (owner[k] == h + 1 && lnum[k] >= n) {
			owner[k] = 0;
			mem_drop16(MEM_NPAGE + k * MEM_EMSPG);
		}
}

static unsigned
alloc(unsigned n, int zero_ok, unsigned *handle)
{
	unsigned h;

	for (h = 1; h < NHANDLE && hd[h].used; h++)
		;
	if (h == NHANDLE)
		return (E_NOHANDLE);
	if (n > npool)
		return (E_TOTAL);
	if (n > nfree())
		return (E_FREE);
	if (n == 0 && !zero_ok)
		return (E_ZERO);
	memset(&hd[h], 0, sizeof(hd[h]));
	hd[h].used = 1;
	hd[h].pages = (uint16_t)n;
	grow(h, 0, n);
	*handle = h;
	return (OK);
}

/* One end of a move or exchange (57h): conventional memory from a linear
 * address, or a handle's pages from page and offset. */
struct end {
	unsigned type, h, page;
	uint32_t at;
};

/* Up to *n bytes at byte i of the region, contiguous in guest memory or
 * in one logical page; *n is cut to what the pointer covers. */
static uint8_t *
region(const struct end *e, uint32_t i, int wr, uint32_t *n)
{
	uint32_t len, off;
	uint8_t *p;

	if (e->type == 0) {
		p = mem_span(e->at + i, wr, &len);
	} else {
		off = e->at + i;
		p = mem_lpage(page_base(e->h, e->page + off / PAGE) +
		    off % PAGE / MEM_PGSIZE, wr) + off % MEM_PGSIZE;
		len = MEM_PGSIZE - off % MEM_PGSIZE;
	}
	if (len < *n)
		*n = len;
	return (p);
}

static void
copy(uint8_t *buf, const struct end *e, uint32_t i, uint32_t n, int wr)
{
	uint32_t k;
	uint8_t *p;

	while (n) {
		k = n;
		p = region(e, i, wr, &k);
		if (p == NULL) {
			/* Conventional memory outside the pages: the firmware
			 * ROM, or nothing (FFh), through the slow read; a
			 * write there goes nowhere, as it would on the bus. */
			k = 1;
			if (!wr)
				*buf = e->type == 0 ?
				    (uint8_t)mem_rb((e->at + i) & (MEM_SIZE - 1)) :
				    0xff;
		} else if (wr)
			memcpy(p, buf, k);
		else
			memcpy(buf, p, k);
		buf += k;
		i += k;
		n -= k;
	}
}

/*
 * Whether conventional region c and expanded region x of a move share
 * bytes: c covers part of a physical page that shows one of x's handle
 * pages at the offsets x occupies in it (status 94h).
 */
static int
alias(const struct end *c, const struct end *x, uint32_t len)
{
	uint32_t xs = (uint32_t)x->page * PAGE + x->at, b, lo, hi, m;
	unsigned p, k;

	for (p = 0; p < nphys; p++) {
		b = (uint32_t)blk[p] * PAGE;
		if (cur[p] == NOBASE || c->at >= b + PAGE || b >= c->at + len)
			continue;
		k = (cur[p] - MEM_NPAGE) / MEM_EMSPG;
		if (owner[k] != x->h + 1)
			continue;
		m = (uint32_t)lnum[k] * PAGE;
		lo = c->at > b ? c->at - b : 0;
		hi = c->at + len < b + PAGE ? c->at + len - b : PAGE;
		if (m + lo < xs + len && xs < m + hi)
			return (1);
	}
	return (0);
}

/*
 * Whether conventional regions a (read) and b (written) of a move share
 * bytes through two physical pages that show one logical page. A shared
 * byte at offset o of that page is read as byte o + bp - a->at of the
 * move and written as byte o + bq - b->at; a forward copy writes it
 * before reading it when the second is smaller, and *back asks for the
 * copy from the end then. Returns 0 when no byte is shared, 1 when one
 * order serves, 2 when pairs ask for both orders (regions of more than a
 * page through windows on two logical pages in crossed order): no single
 * pass copies those, so move() stages them (staged()).
 */
static int
alias0(const struct end *a, const struct end *b, uint32_t len, int *back)
{
	uint32_t bp, bq, lo, hi;
	unsigned p, q;
	int shared = 0, fwd = 0, rev = 0;

	for (p = 0; p < nphys; p++) {
		bp = (uint32_t)blk[p] * PAGE;
		if (cur[p] == NOBASE || a->at >= bp + PAGE ||
		    bp >= a->at + len)
			continue;
		for (q = 0; q < nphys; q++) {
			bq = (uint32_t)blk[q] * PAGE;
			if (q == p || cur[q] != cur[p] ||
			    b->at >= bq + PAGE || bq >= b->at + len)
				continue;
			lo = a->at > bp ? a->at - bp : 0;
			if (b->at > bq && b->at - bq > lo)
				lo = b->at - bq;
			hi = a->at + len < bp + PAGE ? a->at + len - bp : PAGE;
			if (b->at + len < bq + PAGE && b->at + len - bq < hi)
				hi = b->at + len - bq;
			if (lo >= hi)
				continue;
			shared = 1;
			if (bq + a->at < bp + b->at)
				rev = 1;
			else
				fwd = 1;
		}
	}
	*back = rev && !fwd;
	return (!shared ? 0 : rev && fwd ? 2 : 1);
}

/*
 * A move no single copy order serves: the source whole into the NSTAGE
 * staging pages past the pool, then the destination from them. The pages
 * live in the page file like any logical page and are forgotten after.
 */
static void
staged(const struct end *e, uint32_t len, uint8_t *buf)
{
	uint32_t i, n, lp;
	unsigned pass;

	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < len; i += n) {
			n = len - i < 256 ? len - i : 256;
			lp = MEM_NPAGE + npool * MEM_EMSPG + i / MEM_PGSIZE;
			if (pass == 0) {
				copy(buf, &e[0], i, n, 0);
				memcpy(mem_lpage(lp, 1) + i % MEM_PGSIZE, buf, n);
			} else {
				memcpy(buf, mem_lpage(lp, 0) + i % MEM_PGSIZE, n);
				copy(buf, &e[1], i, n, 1);
			}
		}
	for (i = 0; i < NSTAGE; i++)
		mem_drop16(MEM_NPAGE + (npool + i) * MEM_EMSPG);
}

static unsigned
move(uint32_t a, int xchg)
{
	uint8_t one[256], two[256];
	struct end e[2];
	uint32_t len = grw(a) | (uint32_t)grw(a + 2) << 16, i, n;
	uint32_t s0, s1;
	unsigned k, status = OK;
	int back = 0, shared = 0;

	if (len > MEM_SIZE)
		return (0x96);
	for (k = 0; k < 2; k++) {
		uint32_t d = a + 4 + 7 * k;

		e[k].type = grb(d);
		e[k].h = grw(d + 1);
		e[k].page = grw(d + 5);
		if (e[k].type == 0) {
			/* Unwrapped: a region past 1 MB is refused. */
			e[k].at = ((uint32_t)grw(d + 5) << 4) + grw(d + 3);
			if (e[k].at + len > MEM_SIZE)
				return (0xa2);
		} else if (e[k].type == 1) {
			e[k].at = grw(d + 3);
			if (!valid(e[k].h))
				return (E_HANDLE);
			if (e[k].page >= hd[e[k].h].pages)
				return (E_LPAGE);
			if (e[k].at >= PAGE)
				return (0x95);
			if (len && e[k].page + (e[k].at + len - 1) / PAGE >=
			    hd[e[k].h].pages)
				return (0x93);
		} else
			return (0x98);
	}
	if (e[0].type == e[1].type && (e[0].type == 0 || e[0].h == e[1].h)) {
		s0 = e[0].type ? e[0].page * PAGE + e[0].at : e[0].at;
		s1 = e[1].type ? e[1].page * PAGE + e[1].at : e[1].at;
		if (s0 < s1 + len && s1 < s0 + len && len) {
			if (xchg)
				return (0x97);
			back = s1 > s0;
			if (e[0].type)
				status = 0x92;
		} else if (e[0].type == 0 && len &&
		    (shared = alias0(&e[0], &e[1], len, &back)) != 0 && xchg)
			return (0x97);
	} else if (e[0].type != e[1].type && len &&
	    alias(&e[e[0].type != 0], &e[e[0].type == 0], len))
		return (0x94);
	if (shared == 2) {
		staged(e, len, one);
		return (status);
	}
	for (i = 0; i < len; i += n) {
		n = len - i < sizeof(one) ? len - i : sizeof(one);
		k = back ? (unsigned)(len - i - n) : (unsigned)i;
		copy(one, &e[0], k, n, 0);
		if (xchg) {
			copy(two, &e[1], k, n, 0);
			copy(two, &e[0], k, n, 1);
		}
		copy(one, &e[1], k, n, 1);
	}
	return (status);
}

void
ems_int(struct i86 *c)
{
	unsigned ah = c->r[R_AX] >> 8, al = c->r[R_AX] & 0xff;
	unsigned dx = c->r[R_DX], bx = c->r[R_BX], cx = c->r[R_CX];
	uint32_t esdi = lin(c->s[S_ES], c->r[R_DI]);
	uint32_t dssi = lin(c->s[S_DS], c->r[R_SI]);
	unsigned st = OK, h, k, n, p;

	calls++;
	if (os_off && ((ah == 0x59 && al == 0) || ah == 0x5b))
		st = 0xa4;
	else switch (ah) {
	case 0x40:
		break;
	case 0x41:
		c->r[R_BX] = 0xe000;
		break;
	case 0x42:
		c->r[R_BX] = (uint16_t)nfree();
		c->r[R_DX] = (uint16_t)npool;
		break;
	case 0x43:
		if ((st = alloc(bx, 0, &h)) == OK)
			c->r[R_DX] = (uint16_t)h;
		break;
	case 0x44:
		st = !valid(dx) ? E_HANDLE : map(al, dx, bx);
		break;
	case 0x45:
		if (!valid(dx))
			st = E_HANDLE;
		else if (hd[dx].saved)
			st = 0x86;
		else if (dx != 0) {
			shrink(dx, 0);
			memset(&hd[dx], 0, sizeof(hd[dx]));
		}
		break;
	case 0x46:
		c->r[R_AX] = 0x40;
		break;
	case 0x47:
		if (!valid(dx))
			st = E_HANDLE;
		else if (hd[dx].saved)
			st = E_SAVED;
		else {
			for (p = 0; p < 4; p++)
				hd[dx].save[p] = cur[p];
			hd[dx].saved = 1;
		}
		break;
	case 0x48:
		if (!valid(dx))
			st = E_HANDLE;
		else if (!hd[dx].saved)
			st = E_NOSAVE;
		else {
			for (p = 0; p < 4; p++)
				show(p, hd[dx].save[p]);
			hd[dx].saved = 0;
		}
		break;
	case 0x4b:
		for (h = n = 0; h < NHANDLE; h++)
			n += hd[h].used;
		c->r[R_BX] = (uint16_t)n;
		break;
	case 0x4c:
		if (!valid(dx))
			st = E_HANDLE;
		else
			c->r[R_BX] = hd[dx].pages;
		break;
	case 0x4d:
		for (h = n = 0; h < NHANDLE; h++)
			if (hd[h].used) {
				gww(esdi + 4 * n, h);
				gww(esdi + 4 * n + 2, hd[h].pages);
				n++;
			}
		c->r[R_BX] = (uint16_t)n;
		break;
	case 0x4e:
		/* The new map is taken before the old one is stored, which
		 * may overwrite it. */
		if (al > 3) {
			st = E_SUB;
			break;
		}
		if (al == 1 || al == 2)
			st = load_map(dssi);
		if (al == 0 || al == 2)
			put_map(esdi);
		if ((al == 1 || al == 2) && st == OK)
			apply_map();
		if (al == 3)
			c->r[R_AX] = (uint16_t)(2 + 2 * nphys);
		break;
	case 0x4f:
		/* A partial map names at most every physical page once. */
		if (al < 2 && grw(dssi) > nphys) {
			st = 0xa3;
			break;
		}
		if (al == 0) {
			/* The segments are taken before any output word is
			 * written over them. */
			n = grw(dssi);
			take(dssi + 2, n);
			for (k = 0; k < n; k++)
				if (phys_at(arg[k]) == nphys) {
					st = E_PPAGE;
					break;
				}
			gww(esdi, n);
			for (k = 0; k < n && st == OK; k++) {
				gww(esdi + 2 + 4 * k, arg[k]);
				gww(esdi + 4 + 4 * k, cur[phys_at(arg[k])]);
			}
		} else if (al == 1) {
			/* Every entry is taken and checked before any is
			 * applied. */
			n = grw(dssi);
			take(dssi + 2, 2 * n);
			for (k = 0; k < n && st == OK; k++)
				if (phys_at(arg[2 * k]) == nphys ||
				    !base_ok(arg[2 * k + 1]))
					st = 0xa3;
			for (k = 0; k < n && st == OK; k++)
				show(phys_at(arg[2 * k]), arg[2 * k + 1]);
		} else if (al == 2) {
			if (bx > nphys)
				st = E_PPAGE;
			else
				c->r[R_AX] = (uint16_t)(2 + 4 * bx);
		} else
			st = E_SUB;
		break;
	case 0x50:
		/* An array longer than the mappable pages maps nothing (8Bh);
		 * a bad entry stops the call with the entries before it
		 * mapped, as the specification's 8Ah and 8Bh texts say. */
		if (al > 1)
			st = E_SUB;
		else if (!valid(dx))
			st = E_HANDLE;
		else if (cx > nphys)
			st = E_PPAGE;
		else
			take(dssi, 2 * cx);
		for (k = 0; k < cx && st == OK; k++) {
			n = arg[2 * k];
			p = arg[2 * k + 1];
			st = map(al ? phys_at(p) : p, dx, n);
		}
		break;
	case 0x51:
		/* Handle 0 owns no pages and keeps none; asking for none
		 * succeeds, as Windows 3.0 asks to tell 4.0 from 3.2. */
		if (!valid(dx))
			st = E_HANDLE;
		else if (dx == 0)
			st = bx == 0 ? OK : E_FREE;
		else if (bx > npool)
			st = E_TOTAL;
		else if (bx > hd[dx].pages + nfree())
			st = E_FREE;
		else {
			if (bx < hd[dx].pages)
				shrink(dx, bx);
			else
				grow(dx, hd[dx].pages, bx);
			hd[dx].pages = (uint16_t)bx;
		}
		c->r[R_BX] = valid(dx) ? hd[dx].pages : 0;
		break;
	case 0x52:
		if (al > 2)
			st = E_SUB;
		else if (al < 2 && !valid(dx))
			st = E_HANDLE;
		else if (al == 1)
			st = (bx & 0xff) == 0 ? OK : (bx & 0xff) == 1 ? 0x91 :
			    0x90;
		else
			c->r[R_AX] = 0;
		break;
	case 0x53:
		if (al > 1)
			st = E_SUB;
		else if (!valid(dx))
			st = E_HANDLE;
		else if (al == 0) {
			for (k = 0; k < 8; k++)
				gwb(esdi + k, (uint8_t)hd[dx].name[k]);
		} else {
			char name[8];

			for (k = 0; k < 8; k++)
				name[k] = (char)grb(dssi + k);
			for (h = 0; h < NHANDLE; h++)
				if (h != dx && hd[h].used &&
				    memcmp(hd[h].name, name, 8) == 0 &&
				    memcmp(name, "\0\0\0\0\0\0\0\0", 8) != 0)
					st = 0xa1;
			if (st == OK)
				memcpy(hd[dx].name, name, 8);
		}
		break;
	case 0x54:
		if (al == 0) {
			for (h = n = 0; h < NHANDLE; h++)
				if (hd[h].used) {
					gww(esdi + 10 * n, h);
					for (k = 0; k < 8; k++)
						gwb(esdi + 10 * n + 2 + k,
						    (uint8_t)hd[h].name[k]);
					n++;
				}
			c->r[R_AX] = (uint16_t)n;
		} else if (al == 1) {
			char name[8];

			for (k = 0; k < 8; k++)
				name[k] = (char)grb(dssi + k);
			/* A name of nulls finds a handle with no name (A1h),
			 * or none (A0h) once every open handle has one. */
			st = 0xa0;
			if (memcmp(name, "\0\0\0\0\0\0\0\0", 8) == 0) {
				for (h = 0; h < NHANDLE; h++)
					if (hd[h].used && memcmp(hd[h].name,
					    "\0\0\0\0\0\0\0\0", 8) == 0) {
						st = 0xa1;
						break;
					}
			} else
				for (h = 0; h < NHANDLE; h++)
					if (hd[h].used &&
					    memcmp(hd[h].name, name, 8) == 0) {
						c->r[R_DX] = (uint16_t)h;
						st = OK;
						break;
					}
		} else if (al == 2)
			c->r[R_BX] = NHANDLE;
		else
			st = E_SUB;
		break;
	case 0x57:
		st = al > 1 ? E_SUB : move(dssi, al);
		break;
	case 0x58:
		if (al > 1) {
			st = E_SUB;
			break;
		}
		if (al == 0)	/* by segment: the conventional pages first */
			for (k = n = 0; k < 64; k++)
				for (p = 0; p < nphys; p++)
					if (blk[p] == k) {
						gww(esdi + 4 * n, k << 10);
						gww(esdi + 4 * n + 2, p);
						n++;
					}
		c->r[R_CX] = (uint16_t)nphys;
		break;
	case 0x59:
		if (al == 0) {
			gww(esdi, PAGE >> 4);
			gww(esdi + 2, 0);
			gww(esdi + 4, 2 + 2 * nphys);
			gww(esdi + 6, 0);
			gww(esdi + 8, 0);
		} else if (al == 1) {
			c->r[R_BX] = (uint16_t)nfree();
			c->r[R_DX] = (uint16_t)npool;
		} else
			st = E_SUB;
		break;
	case 0x5a:
		if (al > 1)
			st = E_SUB;
		else if ((st = alloc(bx, 1, &h)) == OK)
			c->r[R_DX] = (uint16_t)h;
		break;
	case 0x5b:
		switch (al) {
		case 0:
			if (alt_seg | alt_off)
				put_map(alt_area);
			c->r[R_BX] &= 0xff00;
			c->s[S_ES] = alt_seg;
			c->r[R_DI] = alt_off;
			break;
		case 1:
			if (bx & 0xff) {
				st = 0x9c;
				break;
			}
			/* The new set is taken before the old one is
			 * stored, which may overwrite it. The pointer is
			 * kept as given for 5B00h; 0000:0000 is none. */
			if ((c->s[S_ES] | c->r[R_DI]) &&
			    (st = load_map(esdi)) != OK)
				break;
			if (alt_seg | alt_off)
				put_map(alt_area);
			alt_seg = c->s[S_ES];
			alt_off = c->r[R_DI];
			alt_area = esdi;
			if (alt_seg | alt_off)
				apply_map();
			break;
		case 2:
			c->r[R_DX] = (uint16_t)(2 + 2 * nphys);
			break;
		case 3:
		case 5:
			c->r[R_BX] &= 0xff00;
			break;
		case 4:
		case 8:
			if (bx & 0xff)
				st = 0x9c;
			break;
		case 6:
		case 7:
			st = bx & 0xff ? 0x9c : 0x9e;
			break;
		default:
			st = E_SUB;
		}
		break;
	case 0x5c:
		break;
	case 0x5d:
		if (al > 2)
			st = E_SUB;
		else if (key_out ? ((uint32_t)bx << 16 | cx) != key : al == 2)
			st = 0xa4;	/* a wrong key, or 5D02h with none out */
		else if (al == 2)
			key_out = os_off = 0;
		else {
			if (!key_out) {
				key = calls * 2654435761u;
				key_out = 1;
				c->r[R_BX] = (uint16_t)(key >> 16);
				c->r[R_CX] = (uint16_t)key;
			}
			os_off = (uint8_t)al;
		}
		break;
	default:
		st = E_FUNC;
		break;
	}
	c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xff) | st << 8);
}
