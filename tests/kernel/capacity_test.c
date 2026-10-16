/*
 * Host gate for sys/kern/subr_capacity.c.
 *
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

#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/capacity.h>
#include <sys/clist.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>

#include <machine/uarea_stats.h>

struct proc proc[NPROC];
struct inode inode[NINODE];
struct file file[NFILE];
struct buf buf[NBUF];
int cfreecount;

static uint32_t *
stack_word(int index)
{
	return (uint32_t *)(void *)((char *)&u + UAREA_STACK_OFFSET) + index;
}

static void
paint_stack(void)
{
	int index;

	for (index = 0; index < (USIZE - UAREA_STACK_OFFSET) /
	    (int)sizeof(uint32_t); index++)
		*stack_word(index) = UAREA_STACK_SENTINEL;
}

static void
check_initial(const struct kernel_capacity_stats *stats)
{
	int kind;

	HK_CHECK(stats->version == KERNEL_CAPACITY_VERSION);
	HK_CHECK(stats->size == sizeof(*stats));
	HK_CHECK(stats->metric[CAPACITY_PROC].limit == NPROC);
	HK_CHECK(stats->metric[CAPACITY_INODE].limit == NINODE);
	HK_CHECK(stats->metric[CAPACITY_FILE].limit == NFILE);
	HK_CHECK(stats->metric[CAPACITY_CLIST].limit == NCLIST);
	HK_CHECK(stats->metric[CAPACITY_BUFFER].limit == NBUF);
	for (kind = 0; kind < CAPACITY_KIND_COUNT; kind++) {
		HK_CHECK(stats->metric[kind].current == 0);
		HK_CHECK(stats->metric[kind].peak == 0);
		HK_CHECK(stats->metric[kind].minimum_free ==
		    stats->metric[kind].limit);
		HK_CHECK(stats->metric[kind].failures == 0);
		HK_CHECK(stats->metric[kind].slot_bytes != 0);
	}
	HK_CHECK(stats->metric[CAPACITY_PROC].slot_bytes ==
	    sizeof(struct proc) + 32);
	HK_CHECK(stats->metric[CAPACITY_INODE].slot_bytes == sizeof(struct inode));
	HK_CHECK(stats->metric[CAPACITY_FILE].slot_bytes == sizeof(struct file));
	HK_CHECK(stats->metric[CAPACITY_CLIST].slot_bytes == sizeof(struct cblock));
	HK_CHECK(stats->metric[CAPACITY_BUFFER].slot_bytes ==
	    sizeof(struct buf) + MAXBSIZE);
	HK_CHECK(stats->uarea_bytes == USIZE);
	HK_CHECK(stats->user_bytes == sizeof(struct user));
	HK_CHECK(stats->stack_bytes == USIZE - UAREA_STACK_OFFSET);
	HK_CHECK(stats->stack_peak == 0);
	HK_CHECK(stats->stack_minimum_free == stats->stack_bytes);
	HK_CHECK(stats->stack_saturated == 0);
}

static void
exercise_tables(void)
{
	const struct kernel_capacity_stats *stats;

	proc[0].p_stat = SIDL;
	proc[1].p_stat = SRUN;
	proc[2].p_stat = SZOMB;
	inode[0].i_count = 1;
	inode[1].i_count = 7;
	file[0].f_count = 1;
	file[1].f_count = 4;
	cfreecount -= 2 * CBSIZE;
	buf[0].b_flags = B_BUSY;
	buf[1].b_flags = B_DELWRI;

	capacity_note(CAPACITY_PROC, 0);
	capacity_note(CAPACITY_INODE, 1);
	capacity_note(CAPACITY_FILE, 0);
	capacity_note(CAPACITY_CLIST, 1);
	capacity_note(CAPACITY_BUFFER, 1);
	stats = capacity_snapshot();
	HK_CHECK(stats->metric[CAPACITY_PROC].current == 3);
	HK_CHECK(stats->metric[CAPACITY_INODE].current == 2);
	HK_CHECK(stats->metric[CAPACITY_FILE].current == 2);
	HK_CHECK(stats->metric[CAPACITY_CLIST].current == 2);
	HK_CHECK(stats->metric[CAPACITY_BUFFER].current == 1);
	HK_CHECK(stats->metric[CAPACITY_PROC].minimum_free == NPROC - 3);
	HK_CHECK(stats->metric[CAPACITY_INODE].minimum_free == NINODE - 2);
	HK_CHECK(stats->metric[CAPACITY_FILE].minimum_free == NFILE - 2);
	HK_CHECK(stats->metric[CAPACITY_CLIST].minimum_free == NCLIST - 2);
	HK_CHECK(stats->metric[CAPACITY_BUFFER].minimum_free == NBUF - 1);
	HK_CHECK(stats->metric[CAPACITY_INODE].failures == 1);
	HK_CHECK(stats->metric[CAPACITY_CLIST].failures == 1);
	HK_CHECK(stats->metric[CAPACITY_BUFFER].failures == 1);

	proc[0].p_stat = proc[1].p_stat = proc[2].p_stat = 0;
	inode[0].i_count = inode[1].i_count = 0;
	file[0].f_count = file[1].f_count = 0;
	cfreecount = NCLIST * CBSIZE;
	buf[0].b_flags = buf[1].b_flags = 0;
	stats = capacity_snapshot();
	HK_CHECK(stats->metric[CAPACITY_PROC].current == 0);
	HK_CHECK(stats->metric[CAPACITY_INODE].current == 0);
	HK_CHECK(stats->metric[CAPACITY_FILE].current == 0);
	HK_CHECK(stats->metric[CAPACITY_CLIST].current == 0);
	HK_CHECK(stats->metric[CAPACITY_BUFFER].current == 0);
	HK_CHECK(stats->metric[CAPACITY_PROC].peak == 3);
	HK_CHECK(stats->metric[CAPACITY_INODE].peak == 2);
	HK_CHECK(stats->metric[CAPACITY_FILE].peak == 2);
	HK_CHECK(stats->metric[CAPACITY_CLIST].peak == 2);
	HK_CHECK(stats->metric[CAPACITY_BUFFER].peak == 1);
}

static void
exercise_stack(void)
{
	const struct kernel_capacity_stats *stats;
	int words = (USIZE - UAREA_STACK_OFFSET) / (int)sizeof(uint32_t);

	*stack_word(words - 1) = 0;
	capacity_stack_sample();
	stats = capacity_snapshot();
	HK_CHECK(stats->stack_peak == sizeof(uint32_t));
	HK_CHECK(stats->stack_minimum_free == stats->stack_bytes -
	    sizeof(uint32_t));

	*stack_word(words / 4) = 0;
	capacity_stack_sample();
	stats = capacity_snapshot();
	HK_CHECK(stats->stack_peak == stats->stack_bytes -
	    (words / 4) * sizeof(uint32_t));
	HK_CHECK(stats->stack_saturated == 0);

	*stack_word(0) = 0;
	capacity_stack_sample();
	stats = capacity_snapshot();
	HK_CHECK(stats->stack_peak == stats->stack_bytes);
	HK_CHECK(stats->stack_minimum_free == 0);
	HK_CHECK(stats->stack_saturated == 1);
	paint_stack();
	stats = capacity_snapshot();
	HK_CHECK(stats->stack_peak == stats->stack_bytes);
	HK_CHECK(stats->stack_minimum_free == 0);
	HK_CHECK(stats->stack_saturated == 1);
}

int
main(void)
{
	const struct kernel_capacity_stats *stats;

	cfreecount = NCLIST * CBSIZE;
	paint_stack();
	capacity_init();
	stats = capacity_snapshot();
	check_initial(stats);
	exercise_tables();
	exercise_stack();
	hk_note("  limits: proc=%d inode=%d file=%d clist=%d buffer=%d usize=%d",
	    NPROC, NINODE, NFILE, NCLIST, NBUF, USIZE);
	return hk_verdict("kernel capacity");
}
