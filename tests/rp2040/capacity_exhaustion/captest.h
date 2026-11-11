/*
 * Shared helpers for the fixed-table exhaustion programs proctab.c and
 * filetab.c: console reporting that leaves the image small, the
 * kern.capacity snapshot, and a set of parked child processes that every
 * exit path kills and reaps.
 */
#ifndef CAPTEST_H
#define CAPTEST_H

#include <sys/types.h>
#include <sys/capacity.h>

/* Children of one run; larger than any NPROC this port configures. */
#define CAP_MAXKIDS 64

/* A parked child ends itself through alarm(2) after this many seconds. */
#define CAP_PARK_SECONDS 180

/* Bound on any wait for a child or a counter to settle. */
#define CAP_SETTLE_SECONDS 120

struct cap_reaped {
    int clean;          /* exited with status 0 */
    int other;          /* exited nonzero or died on a signal */
    int live;           /* still unreaped */
    int status;         /* wait status of the first nonclean child */
};

/*
 * Output goes through write(2) from a stack buffer. A stdio buffer would
 * enlarge the data segment, and fork copies that segment to swap once per
 * child, so each byte of image costs swap in every child.
 */
void cap_say(const char *format, ...);
void cap_check(int condition, const char *format, ...);
int cap_failures(void);

int cap_snapshot(struct kernel_capacity_stats *stats);
void cap_print(const char *label, const struct kernel_capacity_stats *stats);
unsigned long cap_failure_delta(const struct kernel_capacity_stats *before,
    const struct kernel_capacity_stats *after, enum capacity_kind kind);
unsigned long cap_other_failure_delta(
    const struct kernel_capacity_stats *before,
    const struct kernel_capacity_stats *after, enum capacity_kind kind);
int cap_sysctl_int(int first, int second, int *value);

/*
 * Parked children wait for SIGUSR1 in sigsuspend(2). cap_hold_setup()
 * blocks SIGUSR1 before any fork, so a release that arrives before a child
 * first runs stays pending until the child's sigsuspend unblocks it.
 */
int cap_hold_setup(void);
void cap_hold_park(void) __attribute__((noreturn));
int cap_kid_add(pid_t pid);
int cap_kid_count(void);
void cap_release(void);
struct cap_reaped cap_reap(unsigned seconds);
void cap_abort_all(void);

#endif /* CAPTEST_H */
