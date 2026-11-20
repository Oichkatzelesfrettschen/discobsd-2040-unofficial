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
 */
#include <sys/types.h>
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
static unsigned nframes, hand;
static uint32_t ram_top;
static uint8_t stored[(MEM_NPAGE + 7) / 8];
static const char *store_path;
static int store_fd = -1;

void (*mem_failed)(void);

static const uint8_t *rom_p;
static uint32_t rom_addr, rom_len;

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
	rom_p = p;
	rom_addr = addr;
	rom_len = len;
}

int
mem_init(uint32_t ram, unsigned n, const char *store)
{
	unsigned i;

	ram_top = (ram + MEM_PGMASK) & ~MEM_PGMASK;
	if (ram_top > MEM_SIZE)
		ram_top = MEM_SIZE;
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

static void
store_io(unsigned page, uint8_t *p, int wr)
{
	ssize_t n;

	if (store_fd < 0) {
		/* The store is this run's own file: an existing path is
		 * refused, and the new name is removed while the file is
		 * open. Nothing is ever removed by name afterwards, so a
		 * file put at that path later is not this program's to
		 * touch, and the pages go with the process however it
		 * ends. */
		store_fd = open(store_path, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (store_fd < 0)
			goto fail;
		(void)unlink(store_path);
	}
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

static unsigned
evict(void)
{
	struct frame *f;
	uint8_t *p;
	unsigned i, pg;

	for (;;) {
		i = hand;
		hand = hand + 1 == nframes ? 0 : hand + 1;
		f = &ftab[i];
		pg = f->page;
		if (pg == NOPAGE)
			return (i);
		if (mem_rmap[pg]) {
			mem_rmap[pg] = mem_wmap[pg] = 0;
			continue;
		}
		p = frames + ((size_t)i << MEM_PGSHIFT);
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
		return (i);
	}
}

/* Make page pg resident and mapped for reading, and for writing when wr. */
static void
fault(unsigned pg, int wr)
{
	uint8_t *p;
	unsigned i;

	for (i = 0; i < nframes; i++)
		if (ftab[i].page == pg)
			break;
	if (i < nframes) {
		mem_stats.soft++;
	} else {
		i = evict();
		p = frames + ((size_t)i << MEM_PGSHIFT);
		if (stored[pg >> 3] & (1u << (pg & 7))) {
			store_io(pg, p, 0);
			mem_stats.in++;
		} else {
			memset(p, 0, MEM_PGSIZE);
			mem_stats.zero++;
		}
		ftab[i].page = (uint16_t)pg;
		ftab[i].flags = 0;
	}
	p = frames + ((size_t)i << MEM_PGSHIFT);
	mem_rmap[pg] = (uintptr_t)p - ((uintptr_t)pg << MEM_PGSHIFT);
	if (wr) {
		ftab[i].flags |= FR_DIRTY;
		mem_wmap[pg] = mem_rmap[pg];
	}
}

unsigned
mem_rslow(uint32_t a)
{
	if (a < ram_top) {
		fault(a >> MEM_PGSHIFT, 0);
		return (*(const uint8_t *)(mem_rmap[a >> MEM_PGSHIFT] + a));
	}
	if (a - rom_addr < rom_len)
		return (rom_p[a - rom_addr]);
	return (0xff);
}

void
mem_wslow(uint32_t a, unsigned v)
{
	if (a < ram_top) {
		fault(a >> MEM_PGSHIFT, 1);
		*(uint8_t *)(mem_wmap[a >> MEM_PGSHIFT] + a) = (uint8_t)v;
	}
}

uint8_t *
mem_span(uint32_t a, int wr, uint32_t *len)
{
	uintptr_t *map = wr ? mem_wmap : mem_rmap;
	unsigned pg = a >> MEM_PGSHIFT;

	*len = MEM_PGSIZE - (a & MEM_PGMASK);
	if (!map[pg]) {
		if (a >= ram_top)
			return (NULL);
		fault(pg, wr);
	}
	return ((uint8_t *)(map[pg] + a));
}
