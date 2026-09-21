/*
 * Fixed-table capacity and kernel-stack high-water accounting.
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

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/capacity.h>
#include <sys/clist.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>

#include <machine/uarea_stats.h>

static struct kernel_capacity_stats capacity_state;

_Static_assert(USIZE == UAREA_BYTES,
    "assembly and C must use the same u-area size");
_Static_assert(sizeof(struct user) <= UAREA_STACK_OFFSET,
    "struct user overlaps the measured kernel-stack region");

static uint16_t
capacity_live(enum capacity_kind kind)
{
    uint16_t live = 0;
    int index;

    switch (kind) {
    case CAPACITY_PROC:
        /*
         * kern_proc.c puts each free slot on freeproc with p_stat zero.
         * newproc() assigns SIDL after removing a slot, and wait4() clears
         * p_stat before returning a reaped zombie, so every nonzero state
         * consumes one process-table slot.
         */
        for (index = 0; index < NPROC; index++)
            live += proc[index].p_stat != 0;
        break;
    case CAPACITY_INODE:
        for (index = 0; index < NINODE; index++)
            live += inode[index].i_count != 0;
        break;
    case CAPACITY_FILE:
        for (index = 0; index < NFILE; index++)
            live += file[index].f_count != 0;
        break;
    case CAPACITY_CLIST:
        /* cfreecount measures payload bytes in exact CBSIZE blocks. */
        live = NCLIST - cfreecount / CBSIZE;
        break;
    case CAPACITY_BUFFER:
        for (index = 0; index < NBUF; index++)
            live += (buf[index].b_flags & B_BUSY) != 0;
        break;
    case CAPACITY_KIND_COUNT:
        break;
    }
    return live;
}

static void
capacity_refresh(enum capacity_kind kind)
{
    struct capacity_metric *metric = &capacity_state.metric[kind];
    uint16_t free_slots;

    metric->current = capacity_live(kind);
    if (metric->current > metric->peak)
        metric->peak = metric->current;
    free_slots = metric->limit - metric->current;
    if (free_slots < metric->minimum_free)
        metric->minimum_free = free_slots;
}

void
capacity_stack_sample(void)
{
    const uint32_t *const bottom =
        (const uint32_t *)((const char *)&u + UAREA_STACK_OFFSET);
    const uint32_t *cursor = bottom;
    const uint32_t *const top =
        (const uint32_t *)((const char *)&u + UAREA_BYTES);
    uint16_t remaining;
    uint16_t touched;

    while (cursor < top && *cursor == UAREA_STACK_SENTINEL)
        cursor++;
    remaining = (uint16_t)((const char *)cursor - (const char *)bottom);
    touched = capacity_state.stack_bytes - remaining;
    if (touched > capacity_state.stack_peak)
        capacity_state.stack_peak = touched;
    if (remaining < capacity_state.stack_minimum_free)
        capacity_state.stack_minimum_free = remaining;
    if (remaining == 0)
        capacity_state.stack_saturated = 1;
}

void
capacity_init(void)
{
    struct capacity_metric *metric;
    int kind;

    capacity_state.size = sizeof(capacity_state);
    capacity_state.metric[CAPACITY_PROC].limit = NPROC;
    capacity_state.metric[CAPACITY_INODE].limit = NINODE;
    capacity_state.metric[CAPACITY_FILE].limit = NFILE;
    capacity_state.metric[CAPACITY_CLIST].limit = NCLIST;
    capacity_state.metric[CAPACITY_BUFFER].limit = NBUF;
    capacity_state.metric[CAPACITY_PROC].slot_bytes = sizeof(struct proc);
#ifdef SWAPRAM
    capacity_state.metric[CAPACITY_PROC].slot_bytes += 32;
#endif
    capacity_state.metric[CAPACITY_INODE].slot_bytes = sizeof(struct inode);
    capacity_state.metric[CAPACITY_FILE].slot_bytes = sizeof(struct file);
    capacity_state.metric[CAPACITY_CLIST].slot_bytes = sizeof(struct cblock);
    capacity_state.metric[CAPACITY_BUFFER].slot_bytes =
        sizeof(struct buf) + MAXBSIZE;
    capacity_state.uarea_bytes = USIZE;
    capacity_state.user_bytes = sizeof(struct user);
    capacity_state.stack_bytes = USIZE - UAREA_STACK_OFFSET;
    capacity_state.stack_minimum_free = capacity_state.stack_bytes;
    for (kind = 0; kind < CAPACITY_KIND_COUNT; kind++) {
        metric = &capacity_state.metric[kind];
        metric->minimum_free = metric->limit;
        capacity_refresh(kind);
    }
    capacity_stack_sample();
    capacity_state.version = KERNEL_CAPACITY_VERSION;
}

void
capacity_note(enum capacity_kind kind, int failed)
{
    if (capacity_state.version != KERNEL_CAPACITY_VERSION)
        return;
    if (failed)
        capacity_state.metric[kind].failures++;
    capacity_refresh(kind);
}

const struct kernel_capacity_stats *
capacity_snapshot(void)
{
    int kind;

    for (kind = 0; kind < CAPACITY_KIND_COUNT; kind++)
        capacity_refresh(kind);
    capacity_stack_sample();
    return &capacity_state;
}
