#ifndef _SYS_CAPACITY_H_
#define _SYS_CAPACITY_H_

#include <sys/types.h>
#include <sys/stdint.h>

#define KERNEL_CAPACITY_VERSION 1

enum capacity_kind {
    CAPACITY_PROC,
    CAPACITY_INODE,
    CAPACITY_FILE,
    CAPACITY_CLIST,
    CAPACITY_BUFFER,
    CAPACITY_KIND_COUNT
};

struct capacity_metric {
    uint16_t limit;
    uint16_t current;
    uint16_t peak;
    uint16_t minimum_free;
    uint16_t slot_bytes;
    uint16_t reserved;
    uint32_t failures;
};

struct kernel_capacity_stats {
    uint16_t version;
    uint16_t size;
    struct capacity_metric metric[CAPACITY_KIND_COUNT];
    uint16_t uarea_bytes;
    uint16_t user_bytes;
    uint16_t stack_bytes;
    uint16_t stack_peak;
    uint16_t stack_minimum_free;
    uint8_t stack_saturated;
    uint8_t reserved[3];
};

#ifdef KERNEL
#ifdef CAPACITY_STATS
void capacity_init(void);
void capacity_note(enum capacity_kind kind, int failed);
void capacity_stack_sample(void);
const struct kernel_capacity_stats *capacity_snapshot(void);
#else
#define capacity_init()                 ((void)0)
#define capacity_note(kind, failed)     ((void)0)
#define capacity_stack_sample()         ((void)0)
#endif
#endif

#endif /* _SYS_CAPACITY_H_ */
