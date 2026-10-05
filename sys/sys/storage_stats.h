#ifndef _SYS_STORAGE_STATS_H_
#define _SYS_STORAGE_STATS_H_

#include <sys/stdint.h>

#define STORAGE_STATS_VERSION 1

enum storage_metric {
    STORAGE_ROOT_READS,
    STORAGE_ROOT_READ_BYTES,
    STORAGE_ROOT_WRITES,
    STORAGE_ROOT_WRITE_BYTES,
    STORAGE_SWAP_READS,
    STORAGE_SWAP_READ_BYTES,
    STORAGE_SWAP_WRITES,
    STORAGE_SWAP_WRITE_BYTES,
    STORAGE_MAP_WRITES,
    STORAGE_CHECKPOINT_ATTEMPTS,
    STORAGE_CHECKPOINT_SUCCESSES,
    STORAGE_CHECKPOINT_ERRORS,
    STORAGE_ROOT_PROGRAM_PAGES,
    STORAGE_ROOT_PROGRAM_BYTES,
    STORAGE_ROOT_ERASE_SECTORS,
    STORAGE_ROOT_ERASE_BYTES,
    STORAGE_SWAP_PROGRAM_PAGES,
    STORAGE_SWAP_PROGRAM_BYTES,
    STORAGE_SWAP_ERASE_SECTORS,
    STORAGE_SWAP_ERASE_BYTES,
    STORAGE_DIRTY_TRANSITIONS,
    STORAGE_DIRTY_REWRITES,
    STORAGE_EVICTION_WRITES,
    STORAGE_BUFFER_WAITS,
    STORAGE_SWAPRAM_ATTEMPTS,
    STORAGE_SWAPRAM_ADMISSIONS,
    STORAGE_METRIC_COUNT
};

/* Saturation is explicit: a saturated capture cannot supply exact deltas. */
struct storage_stats {
    uint32_t version;
    uint32_t size;
    uint32_t saturated;
    uint32_t dirty_current;
    uint32_t dirty_peak;
    uint32_t value[STORAGE_METRIC_COUNT];
};

static inline void
storage_stats_add(struct storage_stats *stats, enum storage_metric metric,
    uint32_t amount)
{
    if (amount > UINT32_MAX - stats->value[metric]) {
        stats->value[metric] = UINT32_MAX;
        stats->saturated = 1;
    } else
        stats->value[metric] += amount;
}

#ifdef KERNEL
#ifdef STORAGE_STATS
void storage_note(enum storage_metric metric, uint32_t amount);
void storage_snapshot(struct storage_stats *stats);
void storage_dirty_sample(void);
#else
#define storage_note(metric, amount) ((void)0)
#define storage_dirty_sample() ((void)0)
#endif
#endif

#endif
