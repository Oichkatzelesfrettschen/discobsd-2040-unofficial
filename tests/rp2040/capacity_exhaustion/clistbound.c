/*
 * clistbound: drive one terminal's character lists to their worst case and
 * report whether the clist pool refused a block.
 *
 * sys/kern/tty.c statically asserts that one terminal's full output queue
 * (TTHIWAT plus one OBUFSIZ chunk) and TTYHOG bytes of input fit the pool.
 * This program stops its terminal's output (TIOCSTOP), forks a writer that
 * sends CB_LINES numbered lines and blocks at the high-water mark, and
 * sleeps CB_HOLD_SECONDS while tools/renode/check-capacity.py types into
 * the stopped terminal. Typed input is echoed onto the stopped output queue
 * and, past TTYHOG, answered with a bell there, so the run measures whether
 * the pool holds the output queue the assertion budgets plus what input
 * adds to it. It then reads kern.capacity, restarts output (TIOCSTART),
 * reaps the writer, and prints:
 *
 *   INFO clist ...           the clist row while output was stopped
 *   CLISTBOUND OK            no clist refusal across the run
 *   CLISTBOUND REFUSED n     n clist refusals across the run
 *
 * The driver checks that every numbered line arrived once and in order.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "captest.h"

#define CB_LINES		60	/* numbered lines the writer sends */
#define CB_LINE_BYTES		20	/* "CB0000 ..." plus newline */
#define CB_DRAIN_SECONDS	2	/* for the start line to leave the queue */
#define CB_HOLD_SECONDS		14	/* output stays stopped this long */

static int
write_lines(void)
{
    char line[CB_LINE_BYTES + 1];
    int n;

    for (n = 0; n < CB_LINES; n++) {
        snprintf(line, sizeof(line), "CB%04d abcdefghijkl\n", n);
        if (write(STDOUT_FILENO, line, CB_LINE_BYTES) != CB_LINE_BYTES)
            return 1;
    }
    return 0;
}

int
main(void)
{
    struct kernel_capacity_stats before, held, after;
    unsigned long refused;
    pid_t writer;
    int status = 0;

    if (cap_snapshot(&before) < 0) {
        cap_say("FAIL kern.capacity: %s", strerror(errno));
        return 1;
    }
    cap_say("CLISTBOUND STOP");
    sleep(CB_DRAIN_SECONDS);
    if (ioctl(STDOUT_FILENO, TIOCSTOP, 0) < 0) {
        cap_say("FAIL TIOCSTOP: %s", strerror(errno));
        return 1;
    }
    writer = fork();
    if (writer < 0) {
        (void)ioctl(STDOUT_FILENO, TIOCSTART, 0);
        cap_say("FAIL fork: %s", strerror(errno));
        return 1;
    }
    if (writer == 0)
        _exit(write_lines());
    sleep(CB_HOLD_SECONDS);
    if (cap_snapshot(&held) < 0)
        memset(&held, 0, sizeof(held));
    (void)ioctl(STDOUT_FILENO, TIOCSTART, 0);
    if (waitpid(writer, &status, 0) != writer)
        status = -1;
    sleep(1);
    if (cap_snapshot(&after) < 0) {
        cap_say("FAIL kern.capacity: %s", strerror(errno));
        return 1;
    }
    cap_say("INFO clist held current %u peak %u failures %lu",
        held.metric[CAPACITY_CLIST].current, held.metric[CAPACITY_CLIST].peak,
        (unsigned long)held.metric[CAPACITY_CLIST].failures);
    cap_say("INFO clist limit %u peak %u min-free %u",
        after.metric[CAPACITY_CLIST].limit, after.metric[CAPACITY_CLIST].peak,
        after.metric[CAPACITY_CLIST].minimum_free);
    cap_check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "writer status %#x", status);
    refused = cap_failure_delta(&before, &after, CAPACITY_CLIST);
    if (refused == 0)
        cap_say("CLISTBOUND OK");
    else
        cap_say("CLISTBOUND REFUSED %lu", refused);
    return cap_failures() != 0;
}
