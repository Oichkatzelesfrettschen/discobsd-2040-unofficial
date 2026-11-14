/*
 * proctab: board check that the process table refuses a fork cleanly when
 * it is full and recovers once every child is reaped.
 *
 * Authority: newproc() and fork1() in sys/kern/kern_fork.c. fork1() refuses
 * with EAGAIN when freeproc is empty, and notes one CAPACITY_PROC failure;
 * newproc() refuses with ENOMEM, without a counter, when
 * swap_image_reserve() finds no backing for the child's image
 * (sys/kern/vm_swap.c). The program tells the two apart by errno and by
 * the failure counter of kern.capacity (sys/sys/capacity.h).
 *
 * The program runs as root: fork1() also refuses a non-root caller at its
 * MAXUPRC limit and when only the last slot is free, and both refusals note
 * the same counter, so only root attributes an EAGAIN to the table. Each
 * child parks in sigsuspend(2) until SIGUSR1, so the children cost no
 * process time and exit on request; alarm(2) ends a child that nobody
 * releases. Each child is a copy of this image, so the image stays small:
 * output goes through write(2), and no buffer is allocated before the
 * forks.
 *
 * Verdict lines: PROCTABTEST OK (exit 0), PROCTABTEST FAILED (exit 1) and
 * PROCTABTEST INCONCLUSIVE (exit 3) when swap ran out before the table did,
 * which establishes nothing about NPROC.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "captest.h"

/* Free-run slots of the swap map; SMAPSIZ is 29 on this port. */
#define MAP_ENTRIES 32

static struct kernel_capacity_stats before, full, after_release, after_recovery;
static struct kinfo_proc self;
static struct mapent runs[MAP_ENTRIES];

/*
 * The swap blocks one fork reserves for a copy of this process, by the rule
 * of swap_image_span() in sys/kern/vm_swap.c: data past the clean text,
 * stack and u-area blocks, rounded up to the image alignment.
 */
static unsigned
image_span(void)
{
    int name[5] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, 0,
        sizeof(struct kinfo_proc) };
    size_t length = sizeof(self);
    unsigned long text, blocks;

    name[3] = getpid();
    if (sysctl(name, 5, &self, &length, NULL, 0) < 0 ||
        length != sizeof(self) || (self.kp_valid & KP_VALID_LIVE) == 0)
        return 0;
    text = self.kp_tsize <= self.kp_dsize ? self.kp_tsize : 0;
    blocks = btod(self.kp_dsize - text) + btod(self.kp_ssize) + btod(USIZE);
    cap_say("image dsize %lu tsize %lu ssize %lu: %lu swap blocks before "
        "alignment", (unsigned long)self.kp_dsize,
        (unsigned long)self.kp_tsize, (unsigned long)self.kp_ssize, blocks);
    return (unsigned)((blocks + SWAP_IMAGE_ALIGN - 1) &
        ~(unsigned long)(SWAP_IMAGE_ALIGN - 1));
}

/*
 * Images of the given span the flash swap map can hold now, counting only
 * aligned starts inside each free run. The SwapRAM pool adds to this and
 * the swapper needs one spare image of headroom, so the figure bounds the
 * expectation; the fork errno decides.
 */
static unsigned
flash_images(unsigned span, unsigned *free_blocks, unsigned *nruns)
{
    int name[2] = { CTL_VM, VM_SWAPMAP };
    size_t length = sizeof(runs);
    unsigned index, start, end, images = 0;

    *free_blocks = 0;
    *nruns = 0;
    if (span == 0 || sysctl(name, 2, runs, &length, NULL, 0) < 0)
        return 0;
    for (index = 0; index < length / sizeof(runs[0]); index++) {
        if (runs[index].m_size == 0)
            break;
        *free_blocks += runs[index].m_size;
        (*nruns)++;
        start = ((unsigned)runs[index].m_addr + SWAP_IMAGE_ALIGN - 1) &
            ~(unsigned)(SWAP_IMAGE_ALIGN - 1);
        end = (unsigned)runs[index].m_addr + runs[index].m_size;
        if (end > start)
            images += (end - start) / span;
    }
    return images;
}

static int
finish(const char *verdict)
{
    cap_abort_all();
    cap_say("PROCTABTEST %s", verdict);
    return cap_failures() ? 1 : 0;
}

int
main(void)
{
    struct cap_reaped reaped;
    int limit, free_slots, children, fork_errno, refused, status;
    unsigned span, free_blocks, nruns, fits;
    pid_t pid;

    if (geteuid() != 0) {
        cap_say("FAIL run as root: MAXUPRC and the reserved last slot refuse "
            "a non-root fork before the table is empty");
        cap_say("PROCTABTEST FAILED");
        return 1;
    }
    if (cap_sysctl_int(CTL_KERN, KERN_MAXPROC, &limit) < 0 ||
        cap_snapshot(&before) < 0) {
        cap_say("FAIL kern.maxproc or kern.capacity unreadable, errno %d",
            errno);
        cap_say("PROCTABTEST FAILED");
        return 1;
    }
    if (limit < 2 || limit > CAP_MAXKIDS ||
        before.metric[CAPACITY_PROC].limit != limit) {
        cap_say("FAIL kern.maxproc %d disagrees with kern.capacity limit %u "
            "or exceeds the %d children this program tracks", limit,
            before.metric[CAPACITY_PROC].limit, CAP_MAXKIDS);
        cap_say("PROCTABTEST FAILED");
        return 1;
    }
    free_slots = limit - before.metric[CAPACITY_PROC].current;
    span = image_span();
    fits = flash_images(span, &free_blocks, &nruns);
    cap_say("kern.maxproc %d, %d slots in use, %d free", limit,
        before.metric[CAPACITY_PROC].current, free_slots);
    cap_say("swap map: %u free blocks in %u runs; a child needs %u blocks, "
        "so flash alone holds %u children", free_blocks, nruns, span, fits);
    cap_print("before", &before);
    if (cap_hold_setup() < 0) {
        cap_say("FAIL cannot arm the parked-child signal, errno %d", errno);
        cap_say("PROCTABTEST FAILED");
        return 1;
    }

    fork_errno = 0;
    for (;;) {
        pid = fork();
        if (pid == 0)
            cap_hold_park();
        if (pid < 0) {
            fork_errno = errno;
            break;
        }
        if (cap_kid_add(pid) < 0 || cap_kid_count() > limit) {
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
            cap_check(0, "forked more children than the %d table slots", limit);
            return finish("FAILED");
        }
    }
    children = cap_kid_count();
    cap_say("fork refused after %d children with errno %d (%s)", children,
        fork_errno, strerror(fork_errno));

    if (fork_errno == ENOMEM) {
        if (cap_snapshot(&full) == 0)
            cap_print("at the swap refusal", &full);
        cap_say("INFO swap held no backing for child %d; %d of %d free "
            "process slots filled, so NPROC was not reached", children + 1,
            children, free_slots);
        cap_abort_all();
        cap_say("PROCTABTEST INCONCLUSIVE");
        return 3;
    }
    cap_check(fork_errno == EAGAIN, "a full process table refuses fork with "
        "EAGAIN, not ENOMEM");
    if (fork_errno != EAGAIN)
        return finish("FAILED");
    cap_check(children == free_slots, "children created (%d) equal the slots "
        "free at the start (%d)", children, free_slots);

    refused = 1;
    pid = fork();
    if (pid == 0)
        cap_hold_park();
    if (pid > 0) {
        (void)cap_kid_add(pid);
        cap_check(0, "a second fork at a full table must refuse");
    } else {
        refused++;
        cap_check(errno == EAGAIN, "a repeated fork refuses with EAGAIN "
            "(errno %d)", errno);
    }
    if (cap_snapshot(&full) < 0) {
        cap_check(0, "kern.capacity readable at the full table");
        return finish("FAILED");
    }
    cap_print("full", &full);
    cap_check(full.metric[CAPACITY_PROC].current == limit &&
        full.metric[CAPACITY_PROC].minimum_free == 0 &&
        full.metric[CAPACITY_PROC].peak == limit,
        "process counter reports a full table (current, peak %u of %d, "
        "min-free %u)", full.metric[CAPACITY_PROC].current, limit,
        full.metric[CAPACITY_PROC].minimum_free);
    cap_check(cap_failure_delta(&before, &full, CAPACITY_PROC) ==
        (unsigned long)refused, "process failure counter rose by the %d "
        "refusals (rose by %lu)", refused,
        cap_failure_delta(&before, &full, CAPACITY_PROC));
    cap_check(cap_other_failure_delta(&before, &full, CAPACITY_PROC) == 0,
        "no other table counted a failure");

    cap_release();
    reaped = cap_reap(CAP_SETTLE_SECONDS);
    cap_check(reaped.live == 0 && reaped.other == 0 && reaped.clean == children,
        "every child exits 0 and is reaped (clean %d, other %d, live %d)",
        reaped.clean, reaped.other, reaped.live);
    if (cap_snapshot(&after_release) < 0) {
        cap_check(0, "kern.capacity readable after the reap");
        return finish("FAILED");
    }
    cap_print("after release", &after_release);
    cap_check(after_release.metric[CAPACITY_PROC].current ==
        before.metric[CAPACITY_PROC].current, "occupancy returns to the "
        "starting %u slots (now %u)", before.metric[CAPACITY_PROC].current,
        after_release.metric[CAPACITY_PROC].current);
    cap_check(after_release.metric[CAPACITY_PROC].peak == (unsigned)limit &&
        cap_failure_delta(&full, &after_release, CAPACITY_PROC) == 0,
        "the peak and the failures stay latched across the release");

    pid = fork();
    if (pid == 0)
        _exit(0);
    cap_check(pid > 0, "a fork after the reap succeeds (errno %d)",
        pid > 0 ? 0 : errno);
    if (pid > 0) {
        status = -1;
        cap_check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0, "the recovery child exits 0 and is "
            "reaped");
    }
    if (cap_snapshot(&after_recovery) == 0) {
        cap_print("after recovery", &after_recovery);
        cap_check(after_recovery.metric[CAPACITY_PROC].current ==
            before.metric[CAPACITY_PROC].current &&
            cap_failure_delta(&after_release, &after_recovery,
            CAPACITY_PROC) == 0, "recovery leaves occupancy at %u and adds "
            "no failure", before.metric[CAPACITY_PROC].current);
    } else {
        cap_check(0, "kern.capacity readable after recovery");
    }
    return finish(cap_failures() ? "FAILED" : "OK");
}
