/*
 * Shared helpers for proctab.c and filetab.c; see captest.h.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "captest.h"

static const char *const kind_name[CAPACITY_KIND_COUNT] = {
    "proc", "inode", "file", "clist", "buffer"
};

static pid_t kid[CAP_MAXKIDS];
static int nkid;
static int failures;
static sigset_t empty_mask;

static void
emit(const char *prefix, const char *format, va_list arguments)
{
    char line[144];
    int length;
    int body;

    length = snprintf(line, sizeof(line), "%s", prefix);
    if (length < 0 || length >= (int)sizeof(line))
        return;
    body = vsnprintf(line + length, sizeof(line) - (size_t)length, format,
        arguments);
    if (body < 0)
        return;
    length += body;
    if (length > (int)sizeof(line) - 2)
        length = (int)sizeof(line) - 2;
    line[length++] = '\n';
    (void)write(STDOUT_FILENO, line, (size_t)length);
}

void
cap_say(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    emit("", format, arguments);
    va_end(arguments);
}

void
cap_check(int condition, const char *format, ...)
{
    va_list arguments;

    if (!condition)
        failures++;
    va_start(arguments, format);
    emit(condition ? "PASS " : "FAIL ", format, arguments);
    va_end(arguments);
}

int
cap_failures(void)
{
    return failures;
}

int
cap_sysctl_int(int first, int second, int *value)
{
    int name[2];
    size_t length = sizeof(*value);

    name[0] = first;
    name[1] = second;
    if (sysctl(name, 2, value, &length, NULL, 0) < 0)
        return -1;
    return length == sizeof(*value) ? 0 : -1;
}

int
cap_snapshot(struct kernel_capacity_stats *stats)
{
    int name[2] = { CTL_KERN, KERN_CAPACITY };
    size_t length = sizeof(*stats);

    memset(stats, 0, sizeof(*stats));
    if (sysctl(name, 2, stats, &length, NULL, 0) < 0)
        return -1;
    if (length != sizeof(*stats) || stats->version != KERNEL_CAPACITY_VERSION ||
        stats->size != sizeof(*stats)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void
cap_print(const char *label, const struct kernel_capacity_stats *stats)
{
    int kind;

    cap_say("snapshot %s", label);
    for (kind = 0; kind < CAPACITY_KIND_COUNT; kind++)
        cap_say("  %-6s limit %u current %u peak %u min-free %u failures %lu",
            kind_name[kind], stats->metric[kind].limit,
            stats->metric[kind].current, stats->metric[kind].peak,
            stats->metric[kind].minimum_free,
            (unsigned long)stats->metric[kind].failures);
}

unsigned long
cap_failure_delta(const struct kernel_capacity_stats *before,
    const struct kernel_capacity_stats *after, enum capacity_kind kind)
{
    return (unsigned long)(after->metric[kind].failures -
        before->metric[kind].failures);
}

unsigned long
cap_other_failure_delta(const struct kernel_capacity_stats *before,
    const struct kernel_capacity_stats *after, enum capacity_kind kind)
{
    unsigned long total = 0;
    int other;

    for (other = 0; other < CAPACITY_KIND_COUNT; other++)
        if (other != (int)kind)
            total += cap_failure_delta(before, after,
                (enum capacity_kind)other);
    return total;
}

static void
release_handler(int signo)
{
    (void)signo;
}

int
cap_hold_setup(void)
{
    struct sigaction action;
    sigset_t block;

    memset(&action, 0, sizeof(action));
    action.sa_handler = release_handler;
    action.sa_mask = 0;
    if (sigaction(SIGUSR1, &action, NULL) < 0)
        return -1;
    block = (sigset_t)sigmask(SIGUSR1);
    empty_mask = 0;
    return sigprocmask(SIG_BLOCK, &block, NULL);
}

void
cap_hold_park(void)
{
    alarm(CAP_PARK_SECONDS);
    (void)sigsuspend(&empty_mask);
    _exit(0);
}

int
cap_kid_add(pid_t pid)
{
    int index;

    for (index = 0; index < CAP_MAXKIDS; index++)
        if (kid[index] == 0) {
            kid[index] = pid;
            nkid++;
            return 0;
        }
    return -1;
}

int
cap_kid_count(void)
{
    return nkid;
}

void
cap_release(void)
{
    int index;

    for (index = 0; index < CAP_MAXKIDS; index++)
        if (kid[index] != 0)
            (void)kill(kid[index], SIGUSR1);
}

/*
 * One pass reaps every child that has exited; further passes, one second
 * apart, run until none is left or the budget is spent.
 */
struct cap_reaped
cap_reap(unsigned seconds)
{
    struct cap_reaped result;
    unsigned pass;
    int index;
    int status;
    pid_t pid;

    memset(&result, 0, sizeof(result));
    for (pass = 0;; pass++) {
        for (index = 0; index < CAP_MAXKIDS; index++) {
            if (kid[index] == 0)
                continue;
            pid = waitpid(kid[index], &status, WNOHANG);
            if (pid == 0)
                continue;
            if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                result.clean++;
            } else {
                if (result.other == 0)
                    result.status = pid > 0 ? status : -1;
                result.other++;
            }
            kid[index] = 0;
            nkid--;
        }
        if (nkid == 0 || pass >= seconds)
            break;
        sleep(1);
    }
    result.live = nkid;
    return result;
}

/* Every exit path ends here: no child outlives the program. */
void
cap_abort_all(void)
{
    int index;

    for (index = 0; index < CAP_MAXKIDS; index++)
        if (kid[index] != 0)
            (void)kill(kid[index], SIGKILL);
    (void)cap_reap(CAP_SETTLE_SECONDS);
}
