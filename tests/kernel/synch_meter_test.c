/*
 * Production vmmeter() path: the per-second event counters.
 *
 * vmmeter() folds the struct vmrate counters into the long totals in sum
 * once a second and clears them, so a counter must hold every event one
 * second can bring.
 */
#include "hostkern.h"
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vmmeter.h>

struct user u;
struct timeval time;
struct proc proc[NPROC];
struct proc *allproc;
short avenrun[3];

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
	meter_width();
	return hk_verdict("vmmeter");
}
