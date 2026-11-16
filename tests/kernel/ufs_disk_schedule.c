/* Deterministic host scheduling leaves each production kernel stack intact. */
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <ucontext.h>

#include "ufs_disk_schedule.h"

static ucontext_t scheduler_context, contexts[2];
static unsigned char stacks[2][65536];
static void (*jobs[2])(void);
static void (*state_switch)(int, int);
static int finished[2], active, budget_exhausted;
static unsigned switches;

static _Noreturn void
return_on_budget(void)
{
    budget_exhausted = 1;
    state_switch(active, -1);
    active = -1;
    if (setcontext(&scheduler_context) != 0)
        abort();
    abort();
}

static void
switch_to(int destination)
{
    int previous = active;

    if (++switches > 64)
        return_on_budget();
    state_switch(previous, destination);
    active = destination;
    if (swapcontext(previous < 0 ? &scheduler_context : &contexts[previous],
        destination < 0 ? &scheduler_context : &contexts[destination]) != 0)
        abort();
}

static void
enter_job(void)
{
    int current = active;

    jobs[current]();
    finished[current] = 1;
    switch_to(finished[1 - current] ? -1 : 1 - current);
    abort();
}

int
disk_schedule_run(void (*first)(void), void (*second)(void),
    void (*switch_state)(int, int))
{
    int index;

    jobs[0] = first;
    jobs[1] = second;
    state_switch = switch_state;
    active = -1;
    switches = 0;
    budget_exhausted = 0;
    for (index = 0; index < 2; index++) {
        finished[index] = 0;
        if (getcontext(&contexts[index]) != 0)
            abort();
        contexts[index].uc_stack.ss_sp = stacks[index];
        contexts[index].uc_stack.ss_size = sizeof(stacks[index]);
        contexts[index].uc_link = NULL;
        makecontext(&contexts[index], enter_job, 0);
    }
    switch_to(0);
    return budget_exhausted;
}

void
disk_schedule_yield(void)
{
    if (!finished[1 - active])
        switch_to(1 - active);
    else if (++switches > 64)
        return_on_budget();
}

int
disk_schedule_current(void)
{
    return active;
}
