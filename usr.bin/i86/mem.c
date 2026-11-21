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
 * The pager behind mem.h.
 *
 * Paged memory runs from guest address 0 to ram_top. A fixed number of
 * frames holds the resident pages; the store file holds the others at
 * their guest offset. The clock hand gives every frame a second chance by
 * removing its page from both maps: a page touched again before the hand
 * returns takes a soft fault and is mapped again, and a frame the hand
 * finds still unmapped is the victim. The first write to a mapped page
 * takes a soft fault too, which marks the frame modified.
 *
 * The store file has no name after its creation; mem.h's mem_fini() only
 * closes it.
 *
 * A victim reaches the store only when modified and nonzero. A page with
 * no copy in the store is supplied as zeros, so memory the guest never
 * wrote costs no I/O in either direction.
 *
 * Frames and the store hold logical pages. Without expanded memory the
 * logical page of guest page pg is pg itself. With it (mem_ems()), each
 * 16 KB physical page shows sixteen consecutive logical pages from
 * pbase[]: conventional memory shows itself until expanded memory is
 * mapped over it, and the pages of expanded memory are numbered from
 * MEM_NPAGE. A logical page may show at several guest pages at once, so
 * dropping it from the maps visits every physical page (unmap()).
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mem.h"

#define NOPAGE		0xffffu
#define FR_DIRTY	1

struct frame {
	uint16_t page;
	uint8_t flags;
};

uintptr_t mem_rmap[MEM_NPAGE];
uintptr_t mem_wmap[MEM_NPAGE];
struct mem_stats mem_stats;

static uint8_t *frames;
static struct frame *ftab;
static unsigned nframes, hand, lent;
static uint32_t ram_top;
static unsigned ram_pages;
static uint8_t stored0[(MEM_NPAGE + 7) / 8];
static uint8_t *stored = stored0;	/* a bit for each page in the store */
static uint16_t *pbase;			/* null without expanded memory */
static const char *store_path;
static int store_fd = -1;

void (*mem_failed)(void);

#define NROM		3
#define WATCH_PAGES	16

static struct {
	const uint8_t *p;
	uint32_t addr, len;
} roms[NROM];
static unsigned nrom;

/* The watched window: where each page's bytes are, less its guest
 * address, or zero for a page nothing answers. */
static uintptr_t watch_map[WATCH_PAGES];
static uint32_t watch_base;
unsigned mem_watched;

void
mem_fixed(uint32_t addr, uint8_t *p, uint32_t len)
{
	uint32_t a;

	for (a = addr; a < addr + len; a += MEM_PGSIZE)
		mem_rmap[a >> MEM_PGSHIFT] = mem_wmap[a >> MEM_PGSHIFT] =
		    (uintptr_t)p - addr;
}

void
mem_rom(uint32_t addr, const uint8_t *p, uint32_t len)
{
	if (nrom == NROM)
		return;
	roms[nrom].p = p;
	roms[nrom].addr = addr;
	roms[nrom].len = len;
	nrom++;
}

void
mem_watch(uint32_t base, unsigned first, uint8_t *p, unsigned n)
{
	unsigned i, pg;

	watch_base = base;
	for (i = first; i < first + n && i < WATCH_PAGES; i++) {
		pg = (base >> MEM_PGSHIFT) + i;
		watch_map[i] = p == NULL ? 0 : (uintptr_t)p -
		    ((uintptr_t)(first + (base >> MEM_PGSHIFT)) << MEM_PGSHIFT);
		mem_rmap[pg] = watch_map[i];
		mem_wmap[pg] = 0;
	}
	mem_watched &= ~(((1u << n) - 1) << first);
}

unsigned
mem_rearm(void)
{
	unsigned i, m = mem_watched;

	for (i = 0; i < WATCH_PAGES; i++)
		if (m >> i & 1)
			mem_wmap[(watch_base >> MEM_PGSHIFT) + i] = 0;
	mem_watched = 0;
	return (m);
}

/* The first write to a watched page since mem_rearm(): note it and let
 * the writes that follow go straight to the page. */
static int
watch_fault(uint32_t a)
{
	unsigned i = (a - watch_base) >> MEM_PGSHIFT;

	if (a < watch_base || i >= WATCH_PAGES || !watch_map[i])
		return (0);
	mem_watched |= 1u << i;
	mem_wmap[a >> MEM_PGSHIFT] = watch_map[i];
	return (1);
}

int
mem_init(uint32_t ram, unsigned n, const char *store)
{
	unsigned i;

	ram_top = (ram + MEM_PGMASK) & ~MEM_PGMASK;
	if (ram_top > MEM_SIZE)
		ram_top = MEM_SIZE;
	ram_pages = ram_top >> MEM_PGSHIFT;
	if (n > (ram_top >> MEM_PGSHIFT))
		n = ram_top >> MEM_PGSHIFT;
	if (n < 4)
		return (-1);
	frames = malloc((size_t)n << MEM_PGSHIFT);
	ftab = malloc(n * sizeof(*ftab));
	if (frames == NULL || ftab == NULL)
		return (-1);
	nframes = n;
	for (i = 0; i < n; i++) {
		ftab[i].page = NOPAGE;
		ftab[i].flags = 0;
	}
	store_path = store;
	return (0);
}

void
mem_fini(void)
{
	if (store_fd >= 0) {
		close(store_fd);
		store_fd = -1;
	}
}

#ifndef SSTRUN_NEWLIB
/* The directory of the store's path, allocated, and the name in it. */
static char *
store_dir(const char **name)
{
	const char *base = strrchr(store_path, '/');
	size_t n = base == NULL ? 0 : base == store_path ? 1 :
	    (size_t)(base - store_path);
	char *dir;

	if ((dir = malloc(n + 2)) == NULL)
		return (NULL);
	if (n == 0)
		dir[n++] = '.';
	else
		memcpy(dir, store_path, n);
	dir[n] = '\0';
	*name = base == NULL ? store_path : base + 1;
	return (dir);
}
#endif

/* Take the store's name away while the file stays open. The name is
 * removed only if it still names the file this program created: another
 * process may have renamed that file and put its own at the path, and its
 * file is not this program's to remove. The system does the check and the
 * removal as one step (unlink(2), unlinkat_beneath), from the store's own
 * directory since the call crosses no mount point. The host build has no
 * such call: it compares and then removes, which leaves a window between
 * the two that the target does not have. It is kept that way because it
 * is the test tool for this sequence, create under the name and remove
 * the name, and a host that created the file without a name would test a
 * different program. Returns -1 when the name stays. */
static int
store_unname(void)
{
#if defined(SSTRUN_NEWLIB)
	return (unlink(store_path));
#else
	const char *name;
	char *dir = store_dir(&name);
	int dfd, r;

	if (dir == NULL)
		return (-1);
	dfd = open(dir, O_RDONLY);
	free(dir);
	if (dfd < 0)
		return (-1);
#ifdef HOSTBUILD
	{
		struct stat named, held;

		r = -1;
		if (fstat(store_fd, &held) == 0 &&
		    fstatat(dfd, name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
		    named.st_dev == held.st_dev && named.st_ino == held.st_ino)
			r = unlinkat(dfd, name, 0);
	}
#else
	r = unlinkat_beneath(dfd, name, store_fd);
#endif
	close(dfd);
	return (r);
#endif
}

/* The store is this run's own file: an existing path is refused, and the
 * name is removed while the file is open, so the pages go with the process
 * however it ends and nothing is removed by name afterwards. A name that
 * cannot be removed is a store that cannot be used. */
static int
store_open(void)
{
	store_fd = open(store_path, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (store_fd < 0)
		return (-1);
	return (store_unname());
}

static void
store_io(unsigned page, uint8_t *p, int wr)
{
	ssize_t n;

	if (store_fd < 0 && store_open() != 0)
		goto fail;
	if (lseek(store_fd, (off_t)page << MEM_PGSHIFT, SEEK_SET) < 0)
		goto fail;
	n = wr ? write(store_fd, p, MEM_PGSIZE) : read(store_fd, p, MEM_PGSIZE);
	if (n == (ssize_t)MEM_PGSIZE)
		return;
fail:
	if (mem_failed != NULL)
		mem_failed();
	_exit(3);
}

static int
all_zero(const uint8_t *p)
{
	const uint32_t *w = (const uint32_t *)(const void *)p;
	unsigned i;

	for (i = 0; i < MEM_PGSIZE / 4; i++)
		if (w[i])
			return (0);
	return (1);
}

/* The logical page guest page pg shows, or NOPAGE where no paged memory
 * answers. */
static unsigned
logical(unsigned pg)
{
	unsigned b;

	if (pbase == NULL)
		return (pg < ram_pages ? pg : NOPAGE);
	if ((b = pbase[pg / MEM_EMSPG]) == NOPAGE)
		return (NOPAGE);
	b += pg % MEM_EMSPG;
	return (b < MEM_NPAGE && b >= ram_pages ? NOPAGE : b);
}

/* Take every guest page that shows logical page lp out of both maps.
 * Returns nonzero when one of them was mapped, that is, referenced since
 * the clock hand last passed. */
static int
unmap(unsigned lp)
{
	unsigned p, g;
	int ref;

	if (pbase == NULL) {
		ref = mem_rmap[lp] != 0;
		mem_rmap[lp] = mem_wmap[lp] = 0;
		return (ref);
	}
	ref = 0;
	for (p = 0; p < MEM_NPAGE / MEM_EMSPG; p++)
		if (pbase[p] != NOPAGE && lp - pbase[p] < MEM_EMSPG) {
			g = p * MEM_EMSPG + (lp - pbase[p]);
			ref |= mem_rmap[g] != 0;
			mem_rmap[g] = mem_wmap[g] = 0;
		}
	return (ref);
}

/* Empty frame i, whose page is in neither map. */
static void
drop(unsigned i)
{
	struct frame *f = &ftab[i];
	uint8_t *p = frames + ((size_t)i << MEM_PGSHIFT);
	unsigned pg = f->page;

	if (!(f->flags & FR_DIRTY)) {
		mem_stats.clean++;
	} else if (all_zero(p)) {
		stored[pg >> 3] &= (uint8_t)~(1u << (pg & 7));
		mem_stats.elided++;
	} else {
		store_io(pg, p, 1);
		stored[pg >> 3] |= (uint8_t)(1u << (pg & 7));
		mem_stats.out++;
	}
	f->page = NOPAGE;
}

static unsigned
evict(void)
{
	unsigned i, pg;

	for (;;) {
		i = hand;
		hand = hand + 1 == nframes ? 0 : hand + 1;
		pg = ftab[i].page;
		if (pg == NOPAGE)
			return (i);
		if (unmap(pg))
			continue;
		drop(i);
		return (i);
	}
}

uint8_t *
mem_lend(unsigned n)
{
	unsigned i, pg;

	if (lent || nframes < n + 4)
		return (NULL);
	for (i = nframes - n; i < nframes; i++) {
		pg = ftab[i].page;
		if (pg == NOPAGE)
			continue;
		(void)unmap(pg);
		drop(i);
	}
	nframes -= n;
	lent = n;
	if (hand >= nframes)
		hand = 0;
	return (frames + ((size_t)nframes << MEM_PGSHIFT));
}

void
mem_return(void)
{
	nframes += lent;
	lent = 0;
}

/* Make logical page lp resident; returns its frame. */
static unsigned
resident(unsigned lp)
{
	uint8_t *p;
	unsigned i;

	for (i = 0; i < nframes; i++)
		if (ftab[i].page == lp)
			break;
	if (i < nframes) {
		mem_stats.soft++;
	} else {
		i = evict();
		p = frames + ((size_t)i << MEM_PGSHIFT);
		if (stored[lp >> 3] & (1u << (lp & 7))) {
			store_io(lp, p, 0);
			mem_stats.in++;
		} else {
			memset(p, 0, MEM_PGSIZE);
			mem_stats.zero++;
		}
		ftab[i].page = (uint16_t)lp;
		ftab[i].flags = 0;
	}
	return (i);
}

/* Make guest page pg resident and mapped for reading, and for writing
 * when wr. */
static void
fault(unsigned pg, int wr)
{
	unsigned i = resident(logical(pg));
	uint8_t *p = frames + ((size_t)i << MEM_PGSHIFT);

	mem_rmap[pg] = (uintptr_t)p - ((uintptr_t)pg << MEM_PGSHIFT);
	if (wr) {
		ftab[i].flags |= FR_DIRTY;
		mem_wmap[pg] = mem_rmap[pg];
	}
}

unsigned
mem_rslow(uint32_t a)
{
	unsigned i;

	if (logical(a >> MEM_PGSHIFT) != NOPAGE) {
		fault(a >> MEM_PGSHIFT, 0);
		return (*(const uint8_t *)(mem_rmap[a >> MEM_PGSHIFT] + a));
	}
	for (i = 0; i < nrom; i++)
		if (a - roms[i].addr < roms[i].len)
			return (roms[i].p[a - roms[i].addr]);
	return (0xff);
}

void
mem_wslow(uint32_t a, unsigned v)
{
	if (logical(a >> MEM_PGSHIFT) != NOPAGE) {
		fault(a >> MEM_PGSHIFT, 1);
		*(uint8_t *)(mem_wmap[a >> MEM_PGSHIFT] + a) = (uint8_t)v;
	} else if (watch_fault(a))
		*(uint8_t *)(mem_wmap[a >> MEM_PGSHIFT] + a) = (uint8_t)v;
}

uint8_t *
mem_span(uint32_t a, int wr, uint32_t *len)
{
	uintptr_t *map = wr ? mem_wmap : mem_rmap;
	unsigned pg = a >> MEM_PGSHIFT;

	*len = MEM_PGSIZE - (a & MEM_PGMASK);
	if (!map[pg]) {
		if (logical(pg) != NOPAGE)
			fault(pg, wr);
		else if (!wr || !watch_fault(a))
			return (NULL);
	}
	return ((uint8_t *)(map[pg] + a));
}

int
mem_ems(unsigned n)
{
	unsigned total = MEM_NPAGE + n * MEM_EMSPG, p;
	uint8_t *bits;

	if (pbase != NULL || total >= NOPAGE)
		return (-1);
	bits = calloc((total + 7) / 8, 1);
	pbase = malloc(MEM_NPAGE / MEM_EMSPG * sizeof(*pbase));
	if (bits == NULL || pbase == NULL) {
		free(bits);
		free(pbase);
		pbase = NULL;
		return (-1);
	}
	memcpy(bits, stored0, sizeof(stored0));
	stored = bits;
	for (p = 0; p < MEM_NPAGE / MEM_EMSPG; p++)
		pbase[p] = p * MEM_EMSPG < ram_pages ? p * MEM_EMSPG : NOPAGE;
	return (0);
}

void
mem_map16(unsigned phys, unsigned base)
{
	unsigned g;

	for (g = phys * MEM_EMSPG; g < (phys + 1) * MEM_EMSPG; g++)
		mem_rmap[g] = mem_wmap[g] = 0;
	pbase[phys] = (uint16_t)base;
}

void
mem_drop16(unsigned base)
{
	unsigned lp, i;

	for (lp = base; lp < base + MEM_EMSPG; lp++) {
		for (i = 0; i < nframes; i++)
			if (ftab[i].page == lp) {
				(void)unmap(lp);
				ftab[i].page = NOPAGE;
				ftab[i].flags = 0;
			}
		stored[lp >> 3] &= (uint8_t)~(1u << (lp & 7));
	}
}

uint8_t *
mem_lpage(unsigned lp, int wr)
{
	unsigned i = resident(lp);

	if (wr)
		ftab[i].flags |= FR_DIRTY;
	return (frames + ((size_t)i << MEM_PGSHIFT));
}
