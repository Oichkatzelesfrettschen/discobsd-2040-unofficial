/*
 * filetab: board check that the system file table refuses an open with
 * ENFILE when it is full, that this refusal is distinct from the
 * per-process EMFILE, and that closing the holders restores it.
 *
 * Authority: falloc() in sys/kern/kern_descrip.c. It first takes a
 * descriptor slot with ufalloc(), which fails with EMFILE once NOFILE
 * descriptors are open and notes nothing; it then takes a slot of file[]
 * and, with none free, notes one CAPACITY_FILE failure and fails with
 * ENFILE. dup(2) takes a descriptor and shares the file slot, so it reaches
 * EMFILE without touching the table, while each open(2) of the same path
 * takes one new file slot and shares the one in-core inode.
 *
 * The control dups one descriptor to the per-process limit and requires
 * EMFILE with no file failure. The holders then open FILES_PER_HOLDER
 * descriptors each in separate processes, far below NOFILE, until too few
 * slots remain for another holder; the parent opens the rest and the next
 * open must fail with ENFILE while the parent holds far fewer than NOFILE
 * descriptors. Only the table can have refused it.
 *
 * Holders park in sigsuspend(2) until SIGUSR1, and alarm(2) ends one that
 * nobody releases. Output goes through write(2) so each fork copies a small
 * image.
 *
 * Verdict lines: FILETABTEST OK (exit 0) and FILETABTEST FAILED (exit 1).
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "captest.h"

#define TARGET "/dev/null"

/* Descriptors one holder opens; two or more holders must be needed. */
#define FILES_PER_HOLDER 6

/* Descriptor tables larger than this are outside the fixture's bookkeeping. */
#define MAX_DESCRIPTORS 64

static struct kernel_capacity_stats before, control, full, released, recovered;
static char initially_open[MAX_DESCRIPTORS];
static int held[MAX_DESCRIPTORS];

static int
finish(const char *verdict)
{
    cap_abort_all();
    cap_say("FILETABTEST %s", verdict);
    return cap_failures() ? 1 : 0;
}

/* Holder body: open the quota, report a failure as the exit status, park. */
static void
hold_files(int quota)
{
    int count, fd;

    for (count = 0; count < quota; count++) {
        fd = open(TARGET, O_RDONLY);
        if (fd < 0)
            _exit(errno > 0 && errno < 256 ? errno : 255);
    }
    cap_hold_park();
}

int
main(void)
{
    struct cap_reaped reaped;
    int nofile, limit, open0, duped, control_errno, holders, quota, spare;
    int free_slots, index, count, first_errno, second_errno, fd, waited;
    pid_t pid;

    nofile = getdtablesize();
    if (nofile < FILES_PER_HOLDER + 8 || nofile > MAX_DESCRIPTORS ||
        cap_sysctl_int(CTL_KERN, KERN_MAXFILES, &limit) < 0 ||
        cap_snapshot(&before) < 0 ||
        before.metric[CAPACITY_FILE].limit != limit) {
        cap_say("FAIL descriptor limit %d or kern.maxfiles %d unusable "
            "(errno %d)", nofile, limit, errno);
        cap_say("FILETABTEST FAILED");
        return 1;
    }
    open0 = 0;
    for (index = 0; index < nofile; index++) {
        initially_open[index] = fcntl(index, F_GETFD, 0) >= 0;
        open0 += initially_open[index];
    }
    cap_say("NOFILE %d, kern.maxfiles %d, %d descriptors open, %u file slots "
        "in use", nofile, limit, open0, before.metric[CAPACITY_FILE].current);
    cap_print("before", &before);
    if (!initially_open[STDIN_FILENO]) {
        cap_say("FAIL descriptor 0 is closed; the control needs one open "
            "descriptor to dup");
        cap_say("FILETABTEST FAILED");
        return 1;
    }

    /* Control: the per-process limit refuses with EMFILE and no counter. */
    duped = 0;
    control_errno = 0;
    for (;;) {
        fd = dup(STDIN_FILENO);
        if (fd < 0) {
            control_errno = errno;
            break;
        }
        duped++;
    }
    for (index = 0; index < nofile; index++)
        if (!initially_open[index])
            (void)close(index);
    if (cap_snapshot(&control) < 0) {
        cap_check(0, "kern.capacity readable after the control");
        return finish("FAILED");
    }
    cap_check(control_errno == EMFILE && open0 + duped == nofile,
        "dup to the per-process limit refuses with EMFILE (errno %d) at %d "
        "descriptors of %d", control_errno, open0 + duped, nofile);
    cap_check(cap_failure_delta(&before, &control, CAPACITY_FILE) == 0 &&
        control.metric[CAPACITY_FILE].current ==
        before.metric[CAPACITY_FILE].current, "EMFILE leaves the file table "
        "and its failure counter unchanged");

    free_slots = limit - control.metric[CAPACITY_FILE].current;
    quota = FILES_PER_HOLDER;
    if (quota + open0 > nofile - 2)
        quota = nofile - 2 - open0;
    holders = free_slots / quota;
    spare = free_slots - holders * quota;
    cap_say("%d file slots free: %d holders of %d files leave %d for the "
        "parent", free_slots, holders, quota, spare);
    cap_check(holders >= 2, "at least two holder processes are needed to "
        "fill the table (%d)", holders);
    if (holders < 1 || cap_hold_setup() < 0)
        return finish("FAILED");

    for (index = 0; index < holders; index++) {
        pid = fork();
        if (pid == 0)
            hold_files(quota);
        if (pid < 0) {
            cap_check(0, "fork of holder %d succeeds (errno %d)", index, errno);
            return finish("FAILED");
        }
        if (cap_kid_add(pid) < 0) {
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, &waited, 0);
            cap_check(0, "holder bookkeeping has room");
            return finish("FAILED");
        }
    }

    /* The holders have opened their quota when the table shows it. */
    for (waited = 0; waited < CAP_SETTLE_SECONDS; waited++) {
        if (cap_snapshot(&full) < 0 || full.metric[CAPACITY_FILE].current ==
            control.metric[CAPACITY_FILE].current + holders * quota)
            break;
        reaped = cap_reap(0);
        if (reaped.other > 0)
            break;
        sleep(1);
    }
    reaped = cap_reap(0);
    cap_check(reaped.other == 0 && full.metric[CAPACITY_FILE].current ==
        control.metric[CAPACITY_FILE].current + holders * quota,
        "holders hold %d slots (table shows %u, early exits %d, first "
        "status %#x)", holders * quota,
        full.metric[CAPACITY_FILE].current -
        control.metric[CAPACITY_FILE].current, reaped.other, reaped.status);
    if (reaped.other > 0 || cap_failures() > 0)
        return finish("FAILED");

    /* The parent takes the remaining slots, then the table must refuse. */
    count = 0;
    first_errno = 0;
    for (;;) {
        if (open0 + count >= nofile - 1) {
            cap_check(0, "the table refused before the parent reached its "
                "own descriptor limit");
            return finish("FAILED");
        }
        fd = open(TARGET, O_RDONLY);
        if (fd < 0) {
            first_errno = errno;
            break;
        }
        held[count++] = fd;
    }
    fd = open(TARGET, O_RDONLY);
    second_errno = fd < 0 ? errno : 0;
    if (fd >= 0)
        held[count++] = fd;
    if (cap_snapshot(&full) < 0) {
        cap_check(0, "kern.capacity readable at the full table");
        return finish("FAILED");
    }
    cap_print("full", &full);
    cap_check(first_errno == ENFILE && second_errno == ENFILE,
        "a full file table refuses open with ENFILE twice, not EMFILE "
        "(errno %d then %d)", first_errno, second_errno);
    cap_check(count == spare, "the parent opened the %d spare slots (opened "
        "%d)", spare, count);
    cap_check(open0 + count < nofile, "the parent holds %d of %d descriptors, "
        "so the per-process limit did not refuse", open0 + count, nofile);
    cap_check(full.metric[CAPACITY_FILE].current == limit &&
        full.metric[CAPACITY_FILE].minimum_free == 0 &&
        full.metric[CAPACITY_FILE].peak == limit, "file counter reports a "
        "full table (current %u of %d, peak %u, min-free %u)",
        full.metric[CAPACITY_FILE].current, limit,
        full.metric[CAPACITY_FILE].peak, full.metric[CAPACITY_FILE].minimum_free);
    cap_check(cap_failure_delta(&control, &full, CAPACITY_FILE) == 2,
        "file failure counter rose by the two refusals (rose by %lu)",
        cap_failure_delta(&control, &full, CAPACITY_FILE));
    cap_check(cap_other_failure_delta(&control, &full, CAPACITY_FILE) == 0,
        "no other table counted a failure");

    for (index = 0; index < count; index++)
        (void)close(held[index]);
    cap_release();
    reaped = cap_reap(CAP_SETTLE_SECONDS);
    cap_check(reaped.live == 0 && reaped.other == 0 && reaped.clean == holders,
        "every holder exits 0 and is reaped (clean %d, other %d, live %d)",
        reaped.clean, reaped.other, reaped.live);
    if (cap_snapshot(&released) < 0) {
        cap_check(0, "kern.capacity readable after the release");
        return finish("FAILED");
    }
    cap_print("after release", &released);
    cap_check(released.metric[CAPACITY_FILE].current ==
        control.metric[CAPACITY_FILE].current, "occupancy returns to %u file "
        "slots (now %u)", control.metric[CAPACITY_FILE].current,
        released.metric[CAPACITY_FILE].current);
    cap_check(released.metric[CAPACITY_FILE].peak == (unsigned)limit &&
        cap_failure_delta(&full, &released, CAPACITY_FILE) == 0,
        "the peak and the failures stay latched across the release");

    fd = open(TARGET, O_RDONLY);
    cap_check(fd >= 0, "an open after the release succeeds (errno %d)",
        fd >= 0 ? 0 : errno);
    if (fd >= 0)
        (void)close(fd);
    if (cap_snapshot(&recovered) == 0) {
        cap_print("after recovery", &recovered);
        cap_check(recovered.metric[CAPACITY_FILE].current ==
            control.metric[CAPACITY_FILE].current &&
            cap_failure_delta(&released, &recovered, CAPACITY_FILE) == 0,
            "recovery leaves occupancy at %u and adds no failure",
            control.metric[CAPACITY_FILE].current);
    } else {
        cap_check(0, "kern.capacity readable after recovery");
    }
    return finish(cap_failures() ? "FAILED" : "OK");
}
