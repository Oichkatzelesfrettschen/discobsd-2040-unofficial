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
    /*
     * Admission probe. Each swapram_reserve() call ends in an admission or
     * in exactly one of CLOSED, CODEC, ENCODE, NOSPACE and FRAGMENT. NOSPACE
     * means the pool's total free bytes are fewer than the reservation;
     * FRAGMENT means they suffice but no single run does. The WANT, FREE and
     * LARGEST sums cover the NOSPACE and FRAGMENT refusals; DATA_FITS counts
     * those whose compressed data and stack alone fit the largest run. SHORT
     * bins count the same refusals by reservation minus largest run, in
     * KiB steps (SHORT_3K: more than 2 and at most 3 KiB). ADMITTED bytes
     * are compressed sizes of admitted images. FLASH and EXCHANGE count the
     * swap-outs the pool did not take; SCHED_REFUSALS counts attempts where
     * neither tier had room and nothing was written. SCAN and ZERO bytes
     * cover the data and stack segments of each reservation call, split by
     * its outcome; DATA_CHUNKS and ZERO_CHUNKS count whole 4 KiB pieces of
     * the data segment of refused calls and the pieces that were all zero.
     * SPAN histograms count completed swap-outs by flash image length in
     * 4 KiB sectors; SPAN_12 holds twelve and more.
     */
    STORAGE_SWAPRAM_REFUSED_CLOSED,
    STORAGE_SWAPRAM_REFUSED_CODEC,
    STORAGE_SWAPRAM_REFUSED_ENCODE,
    STORAGE_SWAPRAM_REFUSED_NOSPACE,
    STORAGE_SWAPRAM_REFUSED_FRAGMENT,
    STORAGE_SWAPRAM_REFUSED_WANT_BYTES,
    STORAGE_SWAPRAM_REFUSED_FREE_BYTES,
    STORAGE_SWAPRAM_REFUSED_LARGEST_BYTES,
    STORAGE_SWAPRAM_REFUSED_DATA_FITS,
    STORAGE_SWAPRAM_SHORT_1K,
    STORAGE_SWAPRAM_SHORT_2K,
    STORAGE_SWAPRAM_SHORT_3K,
    STORAGE_SWAPRAM_SHORT_4K,
    STORAGE_SWAPRAM_SHORT_5K,
    STORAGE_SWAPRAM_SHORT_6K,
    STORAGE_SWAPRAM_SHORT_7K,
    STORAGE_SWAPRAM_SHORT_MORE,
    STORAGE_SWAPRAM_ADMITTED_DATA_BYTES,
    STORAGE_SWAPRAM_ADMITTED_U_BYTES,
    STORAGE_SWAPRAM_FLASH_SWAPOUTS,
    STORAGE_SWAPRAM_EXCHANGE_SWAPOUTS,
    STORAGE_SWAPRAM_SCHED_REFUSALS,
    STORAGE_SWAPRAM_ADMIT_SCAN_BYTES,
    STORAGE_SWAPRAM_ADMIT_ZERO_BYTES,
    STORAGE_SWAPRAM_REFUSE_SCAN_BYTES,
    STORAGE_SWAPRAM_REFUSE_ZERO_BYTES,
    STORAGE_SWAPRAM_REFUSE_DATA_CHUNKS,
    STORAGE_SWAPRAM_REFUSE_ZERO_CHUNKS,
    STORAGE_SWAPRAM_RAM_SPAN_01,
    STORAGE_SWAPRAM_RAM_SPAN_02,
    STORAGE_SWAPRAM_RAM_SPAN_03,
    STORAGE_SWAPRAM_RAM_SPAN_04,
    STORAGE_SWAPRAM_RAM_SPAN_05,
    STORAGE_SWAPRAM_RAM_SPAN_06,
    STORAGE_SWAPRAM_RAM_SPAN_07,
    STORAGE_SWAPRAM_RAM_SPAN_08,
    STORAGE_SWAPRAM_RAM_SPAN_09,
    STORAGE_SWAPRAM_RAM_SPAN_10,
    STORAGE_SWAPRAM_RAM_SPAN_11,
    STORAGE_SWAPRAM_RAM_SPAN_12,
    STORAGE_SWAPRAM_FLASH_SPAN_01,
    STORAGE_SWAPRAM_FLASH_SPAN_02,
    STORAGE_SWAPRAM_FLASH_SPAN_03,
    STORAGE_SWAPRAM_FLASH_SPAN_04,
    STORAGE_SWAPRAM_FLASH_SPAN_05,
    STORAGE_SWAPRAM_FLASH_SPAN_06,
    STORAGE_SWAPRAM_FLASH_SPAN_07,
    STORAGE_SWAPRAM_FLASH_SPAN_08,
    STORAGE_SWAPRAM_FLASH_SPAN_09,
    STORAGE_SWAPRAM_FLASH_SPAN_10,
    STORAGE_SWAPRAM_FLASH_SPAN_11,
    STORAGE_SWAPRAM_FLASH_SPAN_12,
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
