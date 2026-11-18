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
static int fire_timeout;

void
timeout(void (*fun)(caddr_t), caddr_t arg, int t)
{
	timeouts++;
	timeout_ticks = t;
	if (fire_timeout)
		fun(arg);
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
	fire_timeout = 0;
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

	/* A fired callout removes the queued process and is not canceled. */
	reset();
	fire_timeout = 1;
	HK_CHECK(tsleep(&channel, PCATCH, 1) == EINTR);
	HK_CHECK(timeouts == 1 && untimeouts == 0);
	HK_CHECK(proc[0].p_wchan == 0);
	HK_CHECK((proc[0].p_flag & P_TIMEOUT) == 0);

	/* Larger ones are refused before the process is queued. */
	for (i = 0; i < sizeof refused / sizeof refused[0]; i++) {
		reset();
		HK_CHECK(tsleep(&channel, PCATCH, refused[i]) == EINVAL);
		HK_CHECK(timeouts == 0 && untimeouts == 0);
		HK_CHECK(proc[0].p_wchan == 0);
	}
}

#define METER_CASES(APPLY) \
	APPLY(v_swtch, 0, 0) \
	APPLY(v_trap, 1, 0) \
	APPLY(v_syscall, 2, 0) \
	APPLY(v_intr, 3, 0) \
	APPLY(v_soft, 4, 0) \
	APPLY(v_fpsim, 5, 0) \
	APPLY(v_kbin, 6, 0) \
	APPLY(v_kbout, 7, 0) \
	APPLY(v_swpin, 8, 1) \
	APPLY(v_swpout, 9, 1)

static void
meter_tick(long second, unsigned events, int fifth)
{
	reset();
	time.tv_sec = second;
	bzero(avenrun, sizeof avenrun);
	allproc = &proc[0];

#define SEED(field, slot, swap) \
	cnt.field = events + 257U * (slot); \
	rate.field = 100U + 17U * (slot); \
	sum.field = 1000L + 23L * (slot);
	METER_CASES(SEED)
#undef SEED

	vmmeter();

#define CHECK(field, slot, swap) \
	HK_CHECK(sum.field == 1000L + 23L * (slot) + (long)(events + 257U * (slot))); \
	HK_CHECK(cnt.field == 0); \
	HK_CHECK(rate.field == ((fifth && (swap)) ? 0U : \
	    (400U + 68U * (slot) + events + 257U * (slot)) / 5U));
	METER_CASES(CHECK)
#undef CHECK

	/* One sample of one runnable process from zero: 256 * (1 - exp(-1/T)) is
	 * 20.47, 4.23 and 1.42 for T of 12, 60 and 180, rounded to nearest. */
	HK_CHECK(avenrun[0] == (fifth ? 20 : 0));
	HK_CHECK(avenrun[1] == (fifth ? 4 : 0));
	HK_CHECK(avenrun[2] == (fifth ? 1 : 0));
	allproc = 0;
}

int
main(void)
{
	sleep_bound();
	meter_tick(1, 70000, 0);
	meter_tick(5, 71000, 1);
	return hk_verdict("tsleep/vmmeter");
}
