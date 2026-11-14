#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <signal.h>

typedef char signal_set_target_width[sizeof(sigset_t) == 4 ? 1 : -1];

int (*signal_empty_provider)(sigset_t *) = (sigemptyset);
int (*signal_full_provider)(sigset_t *) = (sigfillset);
int (*signal_add_provider)(sigset_t *, int) = (sigaddset);
int (*signal_delete_provider)(sigset_t *, int) = (sigdelset);
int (*signal_member_provider)(const sigset_t *, int) = (sigismember);

int errno;
static sigset_t sets[2];
static unsigned pointer_calls, number_calls;

static sigset_t *
next_set(void)
{
    pointer_calls++;
    return &sets[0];
}

static int
next_number(void)
{
    number_calls++;
    return 31;
}

int
signal_set_header_calls(sigset_t *set, const sigset_t *constant, int number)
{
    return sigemptyset(set) + sigfillset(set) + sigaddset(set, number) +
        sigdelset(set, number) + sigismember(constant, number);
}

int
main(void)
{
    const sigset_t *constant = &sets[0];

    sets[1] = 0x12345678UL;
    if (sigemptyset(next_set()) || pointer_calls != 1 || sets[0] != 0)
        return 1;
    if (sigfillset(next_set()) || pointer_calls != 2 ||
        sets[0] != ~(sigset_t)0)
        return 2;
    if (signal_empty_provider(&sets[0]) || sets[0] != 0)
        return 3;
    if (sigaddset(next_set(), next_number()) || pointer_calls != 3 ||
        number_calls != 1 || sets[0] != (1UL << 30))
        return 4;
    if (sigismember(next_set(), next_number()) != 1 || pointer_calls != 4 ||
        number_calls != 2 || signal_member_provider(constant, 31) != 1)
        return 5;
    if (sigdelset(next_set(), next_number()) || pointer_calls != 5 ||
        number_calls != 3 || sets[0] != 0)
        return 6;
    if (signal_full_provider(&sets[0]) || sets[0] != ~(sigset_t)0)
        return 7;
    errno = 0;
    if (sigaddset(&sets[0], 0) != -1 || errno != EINVAL ||
        sets[0] != ~(sigset_t)0)
        return 8;
    errno = 0;
    if (sigdelset(&sets[0], 32) != -1 || errno != EINVAL ||
        sets[0] != ~(sigset_t)0)
        return 9;
    errno = 0;
    if (sigismember(constant, -1) != -1 || errno != EINVAL)
        return 10;
    return sets[1] != 0x12345678UL;
}
