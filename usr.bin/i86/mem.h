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
 * Guest memory: one megabyte of address space in pages of MEM_PGSIZE.
 *
 * The page is one kilobyte: booting FreeDOS 1.1 to its prompt and listing
 * a directory inside 96 KB of resident pages read 837 KB back from the
 * page store with this size, 3.8 MB with 2 KB pages and 6.7 MB with 4 KB
 * pages (sys/arch/rp2040/doc/I86.md).
 *
 * mem_rmap[] and mem_wmap[] hold, for each page, the address of its
 * resident copy minus the page's guest address, so a guest address indexes
 * the copy directly; zero sends the access to mem.c. A page that is
 * resident but absent from a map takes that path once and comes back
 * mapped: this is how mem.c learns that a page was referenced (mem_rmap)
 * or modified (mem_wmap) without a store on every access.
 */
#ifndef I86_MEM_H
#define I86_MEM_H

#include <stdint.h>

#ifndef MEM_PGSHIFT
#define MEM_PGSHIFT	10
#endif
#define MEM_PGSIZE	(1u << MEM_PGSHIFT)
#define MEM_PGMASK	(MEM_PGSIZE - 1)
#define MEM_SIZE	0x100000u
#define MEM_NPAGE	(MEM_SIZE >> MEM_PGSHIFT)

struct mem_stats {
	unsigned long soft;	/* resident page mapped again */
	unsigned long zero;	/* page supplied as zeros */
	unsigned long in;	/* page read from the store */
	unsigned long out;	/* page written to the store */
	unsigned long elided;	/* modified page found zero, not written */
	unsigned long clean;	/* unmodified page dropped */
};

extern uintptr_t mem_rmap[MEM_NPAGE];
extern uintptr_t mem_wmap[MEM_NPAGE];
extern struct mem_stats mem_stats;

/* ram is the size of paged memory from address 0, frames the number of
 * resident pages, store the path at which the file that holds the others
 * is created on the first eviction; the path must not exist, and the name
 * is removed as soon as the file is open. Returns 0, or -1 when the frames cannot be allocated. */
int mem_init(uint32_t ram, unsigned frames, const char *store);
/* Called when the store cannot hold or return a page. Guest memory is
 * lost at that point, so the function must end the program without
 * touching guest memory; when it is null or returns, mem.c exits with
 * status 3. */
extern void (*mem_failed)(void);
void mem_fini(void);
/* A resident region outside paged memory (video memory). */
void mem_fixed(uint32_t addr, uint8_t *p, uint32_t len);
/* Read-only bytes outside paged memory (the firmware). */
void mem_rom(uint32_t addr, const uint8_t *p, uint32_t len);

unsigned mem_rslow(uint32_t a);
void mem_wslow(uint32_t a, unsigned v);
/* A pointer to guest address a, good for *len bytes and until the next
 * access to guest memory; null where no memory answers. */
uint8_t *mem_span(uint32_t a, int wr, uint32_t *len);

static inline unsigned
mem_rb(uint32_t a)
{
	uintptr_t b = mem_rmap[a >> MEM_PGSHIFT];

	return (b ? *(const uint8_t *)(b + a) : mem_rslow(a));
}

static inline void
mem_wb(uint32_t a, unsigned v)
{
	uintptr_t b = mem_wmap[a >> MEM_PGSHIFT];

	if (b)
		*(uint8_t *)(b + a) = (uint8_t)v;
	else
		mem_wslow(a, v);
}

/* The two bytes at a and a + 1; the caller keeps a + 1 inside the map. */
static inline unsigned
mem_rw(uint32_t a)
{
	uintptr_t b = mem_rmap[a >> MEM_PGSHIFT];

	if (b && (a & MEM_PGMASK) != MEM_PGMASK) {
		const uint8_t *p = (const uint8_t *)(b + a);

		return (p[0] | (unsigned)p[1] << 8);
	}
	return (mem_rb(a) | mem_rb((a + 1) & (MEM_SIZE - 1)) << 8);
}

static inline void
mem_ww(uint32_t a, unsigned v)
{
	uintptr_t b = mem_wmap[a >> MEM_PGSHIFT];

	if (b && (a & MEM_PGMASK) != MEM_PGMASK) {
		uint8_t *p = (uint8_t *)(b + a);

		p[0] = (uint8_t)v;
		p[1] = (uint8_t)(v >> 8);
		return;
	}
	mem_wb(a, v);
	mem_wb((a + 1) & (MEM_SIZE - 1), v >> 8);
}

#endif
