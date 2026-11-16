#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include "../../lib/libc/gen/sigsetops.c"

static unsigned checks, failures;

static void
check(int condition, const char *name)
{
    checks++;
    if (!condition) {
        if (failures < 8)
            fprintf(stderr, "signal sets: %s\n", name);
        failures++;
    }
}

int
main(void)
{
    sigset_t set;
    unsigned bits;
    int signo;
    const int invalid[] = { INT_MIN, -1, 0, NSIG, NSIG + 1, INT_MAX };

    for (bits = 0; bits < 1024; bits++) {
        set = bits;
        for (signo = 1; signo < NSIG; signo++)
            check(sigismember(&set, signo) ==
                ((bits & (1U << (signo - 1))) != 0), "membership");
    }
    set = ~(sigset_t)0;
    check(sigemptyset(&set) == 0 && set == 0, "empty");
    check(sigfillset(&set) == 0 && set == ~(sigset_t)0, "full");
    for (signo = 1; signo < NSIG; signo++) {
        sigset_t bit = (sigset_t)1 << (signo - 1);

        set = 0;
        errno = EDOM;
        check(sigaddset(&set, signo) == 0 && set == bit, "add singleton");
        check(sigismember(&set, signo) == 1, "singleton membership");
        check(sigdelset(&set, signo) == 0 && set == 0, "delete singleton");
        check(errno == EDOM, "valid calls preserve errno");
        set = ~(sigset_t)0;
        check(sigdelset(&set, signo) == 0 && set == ~bit, "delete full");
        check(sigismember(&set, signo) == 0, "deleted membership");
        check(sigaddset(&set, signo) == 0 && set == ~(sigset_t)0, "restore full");
    }
    for (bits = 0; bits < sizeof(invalid) / sizeof(invalid[0]); bits++) {
        set = 0x55555555U;
        errno = 0;
        check(sigaddset(&set, invalid[bits]) == -1 && errno == EINVAL &&
            set == 0x55555555U, "invalid add");
        errno = 0;
        check(sigdelset(&set, invalid[bits]) == -1 && errno == EINVAL &&
            set == 0x55555555U, "invalid delete");
        errno = 0;
        check(sigismember(&set, invalid[bits]) == -1 && errno == EINVAL &&
            set == 0x55555555U, "invalid membership");
    }
    printf("signal sets: %u/%u pass\n", checks - failures, checks);
    return failures != 0;
}
