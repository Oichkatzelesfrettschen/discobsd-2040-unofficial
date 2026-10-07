/*
 * Production tsleep() and vmmeter() paths: the timeout bound and the
 * per-second event counters.
 *
 * tsleep() takes its timeout as u_int and hands it to timeout(), whose
 * callout delta is an int, so a value above INT_MAX must be refused before
 * the process is queued. A pending caught signal with PCATCH set makes
 * tsleep() leave through its resume path without calling swtch(), which is
 * what lets the accepted boundary run on a host.
 *
 * vmmeter() folds the struct vmrate counters into the long totals in sum
 * once a second and clears them, so a counter must hold every event one
 * second can bring.
 */
#include "hostkern.h"
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vmmeter.h>

struct user u;
struct timeval time;
struct proc proc[NPROC];
struct proc *qs, *allproc;
short avenrun[3];
int noproc;
char *panicstr;

static unsigned timeouts, untimeouts;
static int timeout_ticks;

void
timeout(void (*fun)(caddr_t), caddr_t arg, int t)
{
	(void)fun;
	(void)arg;
	timeouts++;
	timeout_ticks = t;
}

void
untimeout(void (*fun)(caddr_t), caddr_t arg)
{
	(void)fun;
	(void)arg;
	untimeouts++;
}

int
issignal(struct proc *p)
{
	(void)p;
	return SIGINT;
}

/* swtch() is unreachable from the paths below; reaching it is a failure. */
void
idle(void)
{
	HK_CHECK(0);
}

static char channel;

static void
reset(void)
{
	bzero(&u, sizeof u);
	bzero(proc, sizeof proc);
	timeouts = untimeouts = 0;
	timeout_ticks = 0;
	u.u_procp = &proc[0];
	proc[0].p_stat = SRUN;
	proc[0].p_sig = sigmask(SIGINT);
	u.u_sigintr = sigmask(SIGINT);
}

static void
sleep_bound(void)
{
	static const u_int refused[] = { (u_int)INT_MAX + 1, 0xffffffffU };
	unsigned i;

	/* INT_MAX is the largest timeout the callout can carry. */
	reset();
	HK_CHECK(tsleep(&channel, PCATCH, INT_MAX) == EINTR);
	HK_CHECK(timeouts == 1 && timeout_ticks == INT_MAX);
	HK_CHECK(untimeouts == 1);
	HK_CHECK(proc[0].p_wchan == 0);

	/* Larger ones are refused before the process is queued. */
	for (i = 0; i < sizeof refused / sizeof refused[0]; i++) {
		reset();
		HK_CHECK(tsleep(&channel, PCATCH, refused[i]) == EINVAL);
		HK_CHECK(timeouts == 0 && untimeouts == 0);
		HK_CHECK(proc[0].p_wchan == 0);
	}
}

static void
meter_width(void)
{
	long before = sum.v_syscall;
	unsigned i;

	time.tv_sec = 1;
	bzero(&cnt, sizeof cnt);
	for (i = 0; i < 70000; i++)
		cnt.v_syscall++;
	vmmeter();
	HK_CHECK(sum.v_syscall - before == 70000);
	HK_CHECK(cnt.v_syscall == 0);
	HK_CHECK(rate.v_syscall == 70000 / 5);
}

int
main(void)
{
	sleep_bound();
	meter_width();
	return hk_verdict("tsleep/vmmeter");
}
