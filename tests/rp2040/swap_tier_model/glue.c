/*
 * Host harness for the swap tier model. It supplies the kernel symbols that
 * the shipped swapram.c and subr_rmap.c reference and exports panic-guarded
 * entry points that Python calls through cffi. The code under test is
 * compiled from the tree unmodified; this file only stands in for the rest
 * of the kernel.
 *
 * A kernel panic becomes a longjmp back to the guarded entry point, which
 * returns GLUE_PANIC and leaves the message for glue_panic_message().
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <machine/swapram.h>
#include "heatshrink_encoder.h"
#undef malloc			/* the shim renamed the map allocator */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLUE_PANIC	(-1000)

/*
 * The kernel's SMAPSIZ (machine/machparam.h) slots, the last one the
 * terminator that m_limit names, as machdep.c declares the swap map.
 */
#define GLUE_MAPSLOTS	29

struct proc proc[NPROC];
struct user u, u0;
size_t swapnext;

static struct mapent glue_swapent[GLUE_MAPSLOTS];
struct map swapmap[1] = {
	{ glue_swapent, &glue_swapent[GLUE_MAPSLOTS - 1], "swapmap", 0, 0, 0 },
};

static jmp_buf glue_jump;
static int glue_armed;
static char glue_message[160];

void
panic(const char *message)
{
	snprintf(glue_message, sizeof glue_message, "%s", message);
	if (glue_armed) {
		glue_armed = 0;
		longjmp(glue_jump, 1);
	}
	fprintf(stderr, "unguarded panic: %s\n", message);
	abort();
}

const char *
glue_panic_message(void)
{
	return glue_message;
}

void
swap_cursor_publish(size_t next)
{
	(void)next;
}

/* The model never evacuates through swapram.c; these refuse loudly. */
struct buf *
geteblk(void)
{
	panic("glue: geteblk");
	return NULL;
}

void
brelse(struct buf *bp)
{
	(void)bp;
	panic("glue: brelse");
}

void
swap_with_buf(struct buf *bp, size_t blkno, size_t coreaddr, int count,
    int flags)
{
	(void)bp;
	(void)blkno;
	(void)coreaddr;
	(void)count;
	(void)flags;
	panic("glue: swap_with_buf");
}

/* Layout facts the Python side checks against its cdef at load. */
unsigned int
glue_layout(int what)
{
	switch (what) {
	case 0: return sizeof(struct swapram_pool);
	case 1: return sizeof(struct swapram_seg);
	case 2: return sizeof(struct map);
	case 3: return sizeof(struct mapent);
	case 4: return SWAPRAM_GRAIN;
	case 5: return SWAPRAM_KB;
	case 6: return NPROC;
	case 7: return USIZE;
	case 8: return SWAP_IMAGE_ALIGN;
	case 9: return GLUE_MAPSLOTS;
	case 10: return DEV_BSIZE;
	default: return 0;
	}
}

/*
 * Swap map: an empty ring over blocks [first, end), first being one erase
 * sector as init_main.c leaves it, with the given guard and cursor.
 */
int
glue_map_reset(size_t end, size_t guard, size_t cursor)
{
	memset(glue_swapent, 0, sizeof glue_swapent);
	swapmap->m_ring_first = 0;
	swapmap->m_ring_end = 0;
	swapmap->m_guard = 0;
	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	mfree(swapmap, end - SWAP_IMAGE_ALIGN, SWAP_IMAGE_ALIGN);
	swapmap->m_ring_first = SWAP_IMAGE_ALIGN;
	swapmap->m_ring_end = end;
	swapmap->m_guard = guard;
	swapnext = cursor;
	glue_armed = 0;
	return 0;
}

/* malloc3_contiguous_next on the swap map: the span, 0 on refusal. */
long
glue_map_alloc(size_t d, size_t s, size_t ublocks, size_t out[3])
{
	size_t got;

	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	got = malloc3_contiguous_next(swapmap, d, s, ublocks, SWAP_IMAGE_ALIGN,
	    &swapnext, out);
	glue_armed = 0;
	return (long)got;
}

int
glue_map_free(size_t size, size_t addr)
{
	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	mfree(swapmap, size, addr);
	glue_armed = 0;
	return 0;
}

/* Copy the free runs out; returns how many. */
int
glue_map_runs(size_t *addr, size_t *size, int max)
{
	int n;

	for (n = 0; n < max && glue_swapent[n].m_size != 0; n++) {
		addr[n] = glue_swapent[n].m_addr;
		size[n] = glue_swapent[n].m_size;
	}
	return n;
}

/*
 * swap_exchange_free_prefix(), swap_exchange_take_prefix() and
 * swap_exchange_tail_reserve() of kern/vm_swap.c, which are static there:
 * the same statements over the same map, so the model's exchange admits
 * exactly the tails the kernel admits.
 */
static struct mapent *
glue_free_prefix(size_t addr, size_t size)
{
	struct mapent *entry;

	for (entry = swapmap->m_map;
	    entry->m_size != 0 && entry->m_addr < addr; entry++)
		continue;
	if (entry->m_size < size || entry->m_addr != addr)
		return NULL;
	return entry;
}

static void
glue_take_prefix(struct mapent *entry, size_t size)
{
	entry->m_addr += size;
	entry->m_size -= size;
	if (entry->m_size == 0)
		do {
			*entry = entry[1];
		} while ((entry++)->m_size != 0);
}

int
glue_map_take_tail(size_t base, size_t incoming_span, size_t outgoing_span)
{
	struct mapent *upper, *lower;
	size_t end, tail, above;

	if (outgoing_span <= incoming_span)
		return 1;
	end = base + incoming_span;
	tail = outgoing_span - incoming_span;
	above = tail;
	if (swapmap->m_ring_end != 0) {
		if (end >= swapmap->m_ring_end)
			end -= swapmap->m_ring_end - swapmap->m_ring_first;
		if (above > swapmap->m_ring_end - end)
			above = swapmap->m_ring_end - end;
	}
	upper = glue_free_prefix(end, above);
	if (upper == NULL)
		return 0;
	if (above != tail) {
		lower = glue_free_prefix(swapmap->m_ring_first, tail - above);
		if (lower == NULL || lower == upper)
			return 0;
		glue_take_prefix(upper, above);
		glue_take_prefix(lower, tail - above);
		return 1;
	}
	glue_take_prefix(upper, above);
	return 1;
}

size_t
glue_map_cursor(void)
{
	return swapnext;
}

/*
 * swapram.c driven the way swap_image_copy_out() drives it: reserve, put
 * each nonempty segment, commit. Returns 1 when the pool took the image,
 * 0 when the caller must use flash, GLUE_PANIC on a kernel panic.
 */
int
glue_swapram_admit(int slot, unsigned char *d, size_t dlen,
    unsigned char *s, size_t slen, unsigned char *ub)
{
	struct proc *p = &proc[slot];

	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	p->p_pid = 100 + slot;
	if (!swapram_out(p, (caddr_t)d, dlen, (caddr_t)s, slen, USIZE)) {
		glue_armed = 0;
		return 0;
	}
	if (dlen)
		swapram_put(p, SWAPRAM_DATA, (caddr_t)d, dlen);
	if (slen)
		swapram_put(p, SWAPRAM_STACK, (caddr_t)s, slen);
	swapram_put(p, SWAPRAM_U, (caddr_t)ub, USIZE);
	swapram_commit(p);
	glue_armed = 0;
	return 1;
}

int
glue_swapram_restore(int slot, unsigned char *d, unsigned char *s,
    unsigned char *ub)
{
	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	swapram_in(&proc[slot], (caddr_t)d, (caddr_t)s, (caddr_t)ub);
	glue_armed = 0;
	return 0;
}

/*
 * The largest run swapram.c's own pool can still hand out, found through
 * the exec spool entry points, which share its allocator: the largest size
 * that swapram_spool_alloc accepts, released again at once.
 */
long
glue_swapram_largest(void)
{
	volatile unsigned int low = 0, high = SWAPRAM_KB * 1024 / SWAPRAM_GRAIN;
	unsigned int mid, off;

	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	while (low < high) {
		mid = (low + high + 1) / 2;
		if (swapram_spool_alloc(mid * SWAPRAM_GRAIN, &off) == 0) {
			swapram_spool_free(off, mid * SWAPRAM_GRAIN);
			low = mid;
		} else {
			high = mid - 1;
		}
	}
	glue_armed = 0;
	return (long)(low * SWAPRAM_GRAIN);
}

/* Hold or release one spool, to shape swapram.c's pool before an admit. */
long
glue_swapram_spool(unsigned int size)
{
	unsigned int off;

	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	if (swapram_spool_alloc(size, &off) != 0)
		off = (unsigned int)-1;
	glue_armed = 0;
	return off == (unsigned int)-1 ? -1 : (long)off;
}

int
glue_swapram_spool_free(unsigned int off, unsigned int size)
{
	if (setjmp(glue_jump) != 0)
		return GLUE_PANIC;
	glue_armed = 1;
	swapram_spool_free(off, size);
	glue_armed = 0;
	return 0;
}

/*
 * The length heatshrink produces for len bytes at the configuration the
 * kernel builds with. The stream length does not depend on the poll buffer
 * size, so this equals the counting pass of swapram.c's sr_encode().
 */
static heatshrink_encoder glue_encoder;

long
glue_hs_count(const unsigned char *src, size_t len)
{
	unsigned char scratch[64];
	size_t in = 0, n;
	long out = 0;
	HSE_poll_res poll;
	HSE_finish_res finish;

	heatshrink_encoder_reset(&glue_encoder);
	for (;;) {
		if (in < len) {
			if (heatshrink_encoder_sink(&glue_encoder,
			    (uint8_t *)(uintptr_t)(src + in), len - in, &n) < 0)
				return -1;
			in += n;
		} else {
			finish = heatshrink_encoder_finish(&glue_encoder);
			if (finish < 0)
				return -1;
			if (finish == HSER_FINISH_DONE)
				break;
		}
		do {
			poll = heatshrink_encoder_poll(&glue_encoder, scratch,
			    sizeof scratch, &n);
			if (poll < 0)
				return -1;
			out += (long)n;
		} while (poll == HSER_POLL_MORE);
	}
	return out;
}
