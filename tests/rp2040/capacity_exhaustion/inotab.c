/*
 * inotab: board check that the in-core inode table refuses a new identity
 * with ENFILE when every slot is referenced, that the refusal belongs to
 * the inode table and not to the system file table, and that releasing the
 * references restores it.
 *
 * Authority: iget() in sys/kern/ufs_inode.c. It returns the slot already
 * holding an inode, and for a new identity takes the head of the free
 * list, which holds exactly the slots whose i_count is zero. With that
 * list empty it logs "inode: table full", notes one CAPACITY_INODE failure
 * and fails with ENFILE. The system file table fails with the same errno
 * (falloc() in sys/kern/kern_descrip.c), so the two are told apart by the
 * counter that moved and by the file slots still free.
 *
 * A process references an inode without a file slot through its working
 * directory. Each holder changes into its own directory and opens one file
 * there: two inode references for one file slot. The parent opens distinct
 * files for the slots the holders leave. The next open of a distinct file
 * and a stat(2) of another must both fail with ENFILE; stat(2) takes no
 * file slot at all. Every path is relative to the working directory, so a
 * lookup needs no slot for an intermediate directory.
 *
 * Holders park in sigsuspend(2) until SIGUSR1, and alarm(2) ends one that
 * nobody releases. Output goes through write(2) so each fork copies a small
 * image.
 *
 * Verdict lines: INOTABTEST OK (exit 0), INOTABTEST FAILED (exit 1) and
 * INOTABTEST INCONCLUSIVE (exit 3) when the free process or file slots
 * cannot carry enough holders to fill the inode table, which establishes
 * nothing about NINODE.
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "captest.h"

#define WORK "/tmp/inotab.d"

/* Inode references one holder takes: its directory and one open file. */
#define REFS_PER_HOLDER 2

/* Tables larger than this are outside the fixture's bookkeeping. */
#define MAX_SLOTS 64

static struct kernel_capacity_stats before, control, ready, full, released,
    recovered;
/* Room for a directory name, a slash and a file name. */
#define NAME_BYTES 16

static int held[MAX_SLOTS];
static int made_dirs, made_files;

static void
dir_name(char *name, int index)
{
    (void)snprintf(name, NAME_BYTES, "d%02d", index);
}

static void
file_name(char *name, int index)
{
    (void)snprintf(name, NAME_BYTES, "p%02d", index);
}

/* Remove what setup created; the working directory moves back to the root. */
static void
cleanup(void)
{
    char name[NAME_BYTES];
    int index;

    for (index = 0; index < made_files; index++) {
        file_name(name, index);
        (void)unlink(name);
    }
    for (index = 0; index < made_dirs; index++) {
        (void)snprintf(name, sizeof(name), "d%02d/f", index);
        (void)unlink(name);
        dir_name(name, index);
        (void)rmdir(name);
    }
    (void)chdir("/");
    (void)rmdir(WORK);
}

static int
finish(const char *verdict)
{
    cap_abort_all();
    cleanup();
    cap_say("INOTABTEST %s", verdict);
    return cap_failures() ? 1 : 0;
}

static int
create_file(const char *name)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0)
        return -1;
    return close(fd);
}

/* Holder body: reference a directory and a file, report a failure, park. */
static void
hold_inodes(int index)
{
    char name[NAME_BYTES];

    dir_name(name, index);
    if (chdir(name) < 0 || open("f", O_RDONLY) < 0)
        _exit(errno > 0 && errno < 256 ? errno : 255);
    cap_hold_park();
}

int
main(void)
{
    struct cap_reaped reaped;
    struct stat status;
    char name[NAME_BYTES];
    int limit, nofile, open0, free_inodes, free_files, free_procs;
    int holders, spare, index, count, fd, waited, open_errno, stat_errno;
    pid_t pid;

    nofile = getdtablesize();
    open0 = 0;
    for (index = 0; index < nofile; index++)
        open0 += fcntl(index, F_GETFD, 0) >= 0;
    if (cap_sysctl_int(CTL_KERN, KERN_MAXINODES, &limit) < 0 ||
        cap_snapshot(&before) < 0 ||
        before.metric[CAPACITY_INODE].limit != limit || limit > MAX_SLOTS) {
        cap_say("FAIL kern.maxinodes %d unusable (errno %d)", limit, errno);
        cap_say("INOTABTEST FAILED");
        return 1;
    }
    cap_print("before", &before);
    if (mkdir(WORK, 0755) < 0 || chdir(WORK) < 0) {
        cap_say("FAIL %s cannot be created and entered (errno %d)", WORK,
            errno);
        (void)rmdir(WORK);
        cap_say("INOTABTEST FAILED");
        return 1;
    }
    if (cap_snapshot(&control) < 0) {
        cap_check(0, "kern.capacity readable in the work directory");
        return finish("FAILED");
    }

    free_inodes = limit - control.metric[CAPACITY_INODE].current;
    free_files = control.metric[CAPACITY_FILE].limit -
        control.metric[CAPACITY_FILE].current;
    free_procs = control.metric[CAPACITY_PROC].limit -
        control.metric[CAPACITY_PROC].current;
    holders = free_inodes / REFS_PER_HOLDER;
    spare = free_inodes - holders * REFS_PER_HOLDER;
    cap_say("kern.maxinodes %d, %u referenced: %d holders of %d references "
        "leave %d for the parent; %d file and %d process slots free", limit,
        control.metric[CAPACITY_INODE].current, holders, REFS_PER_HOLDER,
        spare, free_files, free_procs);
    /*
     * The refused open takes its file slot before the lookup, so the file
     * table needs one slot beyond the holders' and the parent's.
     */
    if (holders < 2 || holders >= free_procs ||
        holders + spare + 1 > free_files || open0 + spare + 1 > nofile) {
        cap_say("INFO the free process, file or descriptor slots cannot "
            "carry %d holders and %d parent files, so NINODE was not reached",
            holders, spare);
        cleanup();
        cap_say("INOTABTEST INCONCLUSIVE");
        return 3;
    }

    /* Two files beyond the parent's share: one to open, one to stat. */
    for (index = 0; index < holders; index++) {
        dir_name(name, index);
        if (mkdir(name, 0755) < 0)
            break;
        made_dirs++;
        (void)snprintf(name, sizeof(name), "d%02d/f", index);
        if (create_file(name) < 0)
            break;
    }
    for (count = 0; index == holders && count < spare + 2; count++) {
        file_name(name, count);
        if (create_file(name) < 0)
            break;
        made_files++;
    }
    cap_check(made_dirs == holders && made_files == spare + 2,
        "%d directories and %d files created (errno %d)", made_dirs,
        made_files, made_dirs == holders && made_files == spare + 2 ? 0 :
        errno);
    if (cap_failures() > 0 || cap_snapshot(&ready) < 0 || cap_hold_setup() < 0)
        return finish("FAILED");
    cap_check(ready.metric[CAPACITY_INODE].current ==
        control.metric[CAPACITY_INODE].current, "creating the files leaves "
        "%u inodes referenced (now %u)",
        control.metric[CAPACITY_INODE].current,
        ready.metric[CAPACITY_INODE].current);

    for (index = 0; index < holders; index++) {
        pid = fork();
        if (pid == 0)
            hold_inodes(index);
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

    /* The holders hold their references when the table shows them. */
    for (waited = 0; waited < CAP_SETTLE_SECONDS; waited++) {
        if (cap_snapshot(&full) < 0 || full.metric[CAPACITY_INODE].current ==
            ready.metric[CAPACITY_INODE].current + holders * REFS_PER_HOLDER)
            break;
        reaped = cap_reap(0);
        if (reaped.other > 0)
            break;
        sleep(1);
    }
    reaped = cap_reap(0);
    cap_check(reaped.other == 0 && full.metric[CAPACITY_INODE].current ==
        ready.metric[CAPACITY_INODE].current + holders * REFS_PER_HOLDER,
        "holders reference %d inodes (table shows %u more, early exits %d, "
        "first status %#x)", holders * REFS_PER_HOLDER,
        full.metric[CAPACITY_INODE].current -
        ready.metric[CAPACITY_INODE].current, reaped.other, reaped.status);
    if (reaped.other > 0 || cap_failures() > 0)
        return finish("FAILED");

    /* The parent takes the remaining slots, then the table must refuse. */
    for (count = 0; count < spare; count++) {
        file_name(name, count);
        held[count] = open(name, O_RDONLY);
        if (held[count] < 0)
            break;
    }
    cap_check(count == spare, "the parent opened the %d spare slots (opened "
        "%d, errno %d)", spare, count, count == spare ? 0 : errno);
    file_name(name, spare);
    fd = open(name, O_RDONLY);
    open_errno = fd < 0 ? errno : 0;
    if (fd >= 0)
        (void)close(fd);
    file_name(name, spare + 1);
    stat_errno = stat(name, &status) < 0 ? errno : 0;
    if (cap_snapshot(&full) < 0) {
        cap_check(0, "kern.capacity readable at the full table");
        return finish("FAILED");
    }
    cap_print("full", &full);
    cap_check(open_errno == ENFILE && stat_errno == ENFILE,
        "a full inode table refuses open and stat of new identities with "
        "ENFILE (errno %d then %d)", open_errno, stat_errno);
    cap_check(full.metric[CAPACITY_INODE].current == limit &&
        full.metric[CAPACITY_INODE].minimum_free == 0 &&
        full.metric[CAPACITY_INODE].peak == limit, "inode counter reports a "
        "full table (current %u of %d, peak %u, min-free %u)",
        full.metric[CAPACITY_INODE].current, limit,
        full.metric[CAPACITY_INODE].peak,
        full.metric[CAPACITY_INODE].minimum_free);
    cap_check(cap_failure_delta(&ready, &full, CAPACITY_INODE) == 2,
        "inode failure counter rose by the two refusals (rose by %lu)",
        cap_failure_delta(&ready, &full, CAPACITY_INODE));
    cap_check(cap_other_failure_delta(&ready, &full, CAPACITY_INODE) == 0,
        "no other table counted a failure");
    cap_check(full.metric[CAPACITY_FILE].current <
        full.metric[CAPACITY_FILE].limit, "the file table holds %u of %u "
        "slots, so it did not refuse", full.metric[CAPACITY_FILE].current,
        full.metric[CAPACITY_FILE].limit);

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
    cap_check(released.metric[CAPACITY_INODE].current ==
        ready.metric[CAPACITY_INODE].current, "occupancy returns to %u "
        "referenced inodes (now %u)", ready.metric[CAPACITY_INODE].current,
        released.metric[CAPACITY_INODE].current);
    cap_check(released.metric[CAPACITY_INODE].peak == (unsigned)limit &&
        cap_failure_delta(&full, &released, CAPACITY_INODE) == 0,
        "the peak and the failures stay latched across the release");

    stat_errno = stat(name, &status) < 0 ? errno : 0;
    cap_check(stat_errno == 0, "the refused stat succeeds after the release "
        "(errno %d)", stat_errno);
    file_name(name, spare);
    fd = open(name, O_RDONLY);
    cap_check(fd >= 0, "the refused open succeeds after the release "
        "(errno %d)", fd >= 0 ? 0 : errno);
    if (fd >= 0)
        (void)close(fd);
    if (cap_snapshot(&recovered) == 0) {
        cap_print("after recovery", &recovered);
        cap_check(recovered.metric[CAPACITY_INODE].current ==
            ready.metric[CAPACITY_INODE].current &&
            cap_failure_delta(&released, &recovered, CAPACITY_INODE) == 0,
            "recovery leaves occupancy at %u and adds no failure",
            ready.metric[CAPACITY_INODE].current);
    } else {
        cap_check(0, "kern.capacity readable after recovery");
    }
    return finish(cap_failures() ? "FAILED" : "OK");
}
