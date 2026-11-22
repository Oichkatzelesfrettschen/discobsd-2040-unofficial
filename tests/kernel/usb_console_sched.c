/*
 * Process contexts for the USB console gate.
 *
 * ttwrite() and ttywait() sleep in the middle of a loop and resume with their
 * locals intact, so a modeled process needs its own stack. Each one runs on a
 * ucontext the harness switches to; the kernel's sleep() switches back. The
 * file includes host headers only and avoids every kernel type, the boundary
 * hostkern.h describes: the kernel side passes a channel as an opaque pointer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#include "usb_console_sched.h"

#define	UCS_STACK	(256 * 1024)

struct ucs_proc {
	ucontext_t	context;
	void		(*body)(void);
	const void	*channel;	/* Sleep channel, or NULL when runnable. */
	int		state;
	char		*stack;
};

static struct ucs_proc procs[UCS_NPROC];
static ucontext_t harness;
static int current = -1;

static void
ucs_trampoline(void)
{
	struct ucs_proc *p = &procs[current];

	p->body();
	p->state = UCS_DONE;
	p->channel = NULL;
	current = -1;
	setcontext(&harness);
}

void
ucs_reset(void)
{
	int i;

	for (i = 0; i < UCS_NPROC; i++) {
		procs[i].state = UCS_IDLE;
		procs[i].channel = NULL;
	}
	current = -1;
}

void
ucs_start(int id, void (*body)(void))
{
	struct ucs_proc *p = &procs[id];

	if (p->stack == NULL && (p->stack = malloc(UCS_STACK)) == NULL) {
		fprintf(stderr, "usb console gate: no stack\n");
		exit(2);
	}
	if (getcontext(&p->context) != 0) {
		fprintf(stderr, "usb console gate: getcontext failed\n");
		exit(2);
	}
	p->context.uc_stack.ss_sp = p->stack;
	p->context.uc_stack.ss_size = UCS_STACK;
	p->context.uc_link = NULL;
	makecontext(&p->context, ucs_trampoline, 0);
	p->body = body;
	p->state = UCS_RUNNABLE;
	p->channel = NULL;
}

int
ucs_state(int id)
{
	return procs[id].state;
}

const void *
ucs_channel(int id)
{
	return procs[id].channel;
}

int
ucs_current(void)
{
	return current;
}

/* Called by the kernel's sleep(): park the running process on channel. */
void
ucs_sleep(const void *channel)
{
	struct ucs_proc *p;

	if (current < 0) {
		fprintf(stderr, "usb console gate: sleep outside a process\n");
		exit(2);
	}
	p = &procs[current];
	p->state = UCS_ASLEEP;
	p->channel = channel;
	current = -1;
	swapcontext(&p->context, &harness);
}

/* Called by the kernel's wakeup(): make every sleeper on channel runnable. */
void
ucs_wakeup(const void *channel)
{
	int i;

	for (i = 0; i < UCS_NPROC; i++)
		if (procs[i].state == UCS_ASLEEP && procs[i].channel == channel) {
			procs[i].state = UCS_RUNNABLE;
			procs[i].channel = NULL;
		}
}

/*
 * Run runnable processes in index order until none is. Returns the number of
 * context switches, so a caller can bound a livelock.
 */
int
ucs_run(void)
{
	int i, switches = 0, progress = 1;

	while (progress && switches < 10000) {
		progress = 0;
		for (i = 0; i < UCS_NPROC; i++) {
			if (procs[i].state != UCS_RUNNABLE)
				continue;
			current = i;
			procs[i].state = UCS_RUNNING;
			switches++;
			swapcontext(&harness, &procs[i].context);
			progress = 1;
		}
	}
	return switches;
}
