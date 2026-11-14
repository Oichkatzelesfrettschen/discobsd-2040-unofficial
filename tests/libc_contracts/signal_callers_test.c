#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

void db_abort(void);
int db_system(char *);

static int scenario, failures, event_count;
static int events[16];
static const sigset_t saved_mask = 0x125001UL;

static void
record(int event)
{
    if (event_count < 16)
        events[event_count++] = event;
    else
        failures++;
}

static void old_int(int signo) { (void)signo; }
static void old_quit(int signo) { (void)signo; }

static int
sequence(const int *expected, int count)
{
    int i;
    if (event_count != count)
        return 0;
    for (i = 0; i < count; i++)
        if (events[i] != expected[i])
            return 0;
    return 1;
}

int
probe_sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    if (scenario == 0) {
        record(1);
        if (how != SIG_SETMASK || old ||
            *set != (~(sigset_t)0 & ~((sigset_t)1 << (SIGABRT - 1))))
            failures++;
    } else if (event_count == 0) {
        record(1);
        if (how != SIG_BLOCK || *set != ((sigset_t)1 << (SIGCHLD - 1)) || !old)
            failures++;
        if (old)
            *old = saved_mask;
    } else {
        record(6);
        if (how != SIG_SETMASK || old || *set != saved_mask)
            failures++;
    }
    return 0;
}

pid_t probe_getpid(void) { return 77; }

int
probe_kill(pid_t pid, int signo)
{
    record(2);
    if (pid != 77 || signo != SIGABRT)
        failures++;
    return 0;
}

sig_t
probe_signal(int signo, sig_t action)
{
    if (scenario == 0) {
        record(3);
        if (signo != SIGABRT || action != SIG_DFL)
            failures++;
        return SIG_DFL;
    }
    if (signo == SIGINT) {
        record(action == SIG_IGN ? 3 : 7);
        if (action != SIG_IGN && action != old_int)
            failures++;
        return old_int;
    }
    if (signo == SIGQUIT) {
        record(action == SIG_IGN ? 4 : 8);
        if (action != SIG_IGN && action != old_quit)
            failures++;
        return old_quit;
    }
    failures++;
    return SIG_DFL;
}

pid_t
probe_vfork(void)
{
    record(2);
    return scenario == 2 ? -1 : scenario == 3 ? 0 : 42;
}

pid_t
probe_waitpid(pid_t pid, int *status, int options)
{
    record(5);
    if (pid != 42 || options || !status)
        failures++;
    if (status)
        *status = 0x2500;
    return scenario == 4 ? -1 : 42;
}

int
probe_execl(const char *path, const char *arg, ...)
{
    va_list ap;
    const char *option, *command, *end;
    record(9);
    va_start(ap, arg);
    option = va_arg(ap, const char *);
    command = va_arg(ap, const char *);
    end = va_arg(ap, const char *);
    va_end(ap);
    if (strcmp(path, "/bin/sh") || strcmp(arg, "sh") ||
        strcmp(option, "-c") || strcmp(command, "probe-command") || end)
        failures++;
    return -1;
}

void
probe_exit(int status)
{
    const int expected[] = { 1, 2, 3, 1, 2, 4 };
    record(4);
    _exit(scenario || status != 1 || failures || !sequence(expected, 6));
}

void
probe_child_exit(int status)
{
    const int expected[] = { 1, 2, 6, 9, 10 };
    record(10);
    _exit(scenario != 3 || status != 127 || failures || !sequence(expected, 5));
}

int
main(int argc, char **argv)
{
    const int parent[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const int fork_failure[] = { 1, 2, 6 };
    int result;
    if (argc != 2 || argv[1][0] < '0' || argv[1][0] > '5' || argv[1][1])
        return 2;
    scenario = argv[1][0] - '0';
    if (scenario == 0) {
        db_abort();
        return 3;
    }
    result = db_system(scenario == 5 ? (char *)0 : "probe-command");
    if (scenario == 3)
        return 4;
    if (scenario == 5)
        return result != 1 || event_count != 0;
    if (scenario == 2)
        return failures || result != (127 << 8) || !sequence(fork_failure, 3);
    return failures || result != (scenario == 4 ? -1 : 0x2500) || !sequence(parent, 8);
}
