/*
 * Host gate for sys/kern/kern_resource.c, linked from the kernel source
 * together with sys/kern/kern_proc.c, whose pfind() the priority syscalls
 * answer with.
 *
 * Three things here are worth stating rather than trusting. Scheduling
 * priority is asymmetric: raising a process's nice number is ordinary, and
 * lowering it is root's, so a gate that only checked the happy path would
 * miss the direction that matters. Resource limits are kept in one unit and
 * presented in another, so a limit set in seconds has to read back in
 * seconds. And a limit is checked against the maximum already stored, which
 * makes the order of two calls matter.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/kernel.h>
#include <sys/resource.h>

/* The kernel's own globals, which machdep.c and kern_clock.c define. */
struct user u;
struct proc proc[NPROC];
int hz = 1000;
int usechz = 1000;

#define SELF_PID	21
#define PEER_PID	22
#define OTHER_PID	23
#define SELF_PGRP	21
#define OTHER_PGRP	23
#define REAL_UID	1000
#define OTHER_UID	1001

static int copy_fails;

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	if (copy_fails)
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	if (copy_fails)
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

/* suser() as sys/kern/ufs_fio.c defines it, in six lines. */
int
suser(void)
{
	if (u.u_uid == 0)
		return 1;
	u.u_error = EPERM;
	return 0;
}

struct which_who_arg {
	int which;
	int who;
};

struct priority_arg {
	int which;
	int who;
	int prio;
};

struct rlimit_arg {
	u_int which;
	struct rlimit *lim;
};

struct rusage_arg {
	int who;
	struct rusage *rusage;
};

/*
 * Three processes: the caller, a peer in its group owned by the same user,
 * and one in another group owned by someone else. allproc threads them, and
 * the pid hash answers pfind().
 */
static void
reset(uid_t effective, uid_t real)
{
	unsigned i;

	for (i = 0; i < PIDHSZ; i++)
		pidhash[i] = NULL;
	for (i = 0; i < NPROC; i++)
		bzero((caddr_t)&proc[i], sizeof proc[i]);
	bzero((caddr_t)&u, sizeof u);

	proc[0].p_pid = SELF_PID;
	proc[0].p_pgrp = SELF_PGRP;
	proc[0].p_uid = (short)real;
	proc[0].p_nice = 0;

	proc[1].p_pid = PEER_PID;
	proc[1].p_pgrp = SELF_PGRP;
	proc[1].p_uid = (short)real;
	proc[1].p_nice = 5;

	proc[2].p_pid = OTHER_PID;
	proc[2].p_pgrp = OTHER_PGRP;
	proc[2].p_uid = (short)OTHER_UID;
	proc[2].p_nice = 3;

	proc[0].p_nxt = &proc[1];
	proc[1].p_nxt = &proc[2];
	proc[2].p_nxt = NULL;
	allproc = &proc[0];

	for (i = 0; i < 3; i++) {
		proc[i].p_hash = pidhash[PIDHASH(proc[i].p_pid)];
		pidhash[PIDHASH(proc[i].p_pid)] = &proc[i];
	}

	u.u_procp = &proc[0];
	u.u_uid = effective;
	u.u_ruid = real;
	u.u_error = 0;
	u.u_rval = 0;
	copy_fails = 0;
	hk_reset_output();
}

/*
 * Reading a priority. A process answers with its own nice number; a group or
 * a user answers with the lowest among its members, which is the one that
 * will run first.
 */
static void
read_priority(void)
{
	struct which_who_arg *a;

	reset(REAL_UID, REAL_UID);
	a = (struct which_who_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = 0;
	getpriority();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rval == 0);

	reset(REAL_UID, REAL_UID);
	a = (struct which_who_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = PEER_PID;
	getpriority();
	HK_CHECK(u.u_rval == 5);

	/* The group answers with the lowest of its two members. */
	reset(REAL_UID, REAL_UID);
	a = (struct which_who_arg *)u.u_arg;
	proc[0].p_nice = 7;
	a->which = PRIO_PGRP;
	a->who = 0;
	getpriority();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rval == 5);
	HK_CHECK(a->who == SELF_PGRP);

	/* So does the user, across group boundaries. */
	reset(REAL_UID, REAL_UID);
	a = (struct which_who_arg *)u.u_arg;
	proc[1].p_nice = -3;
	a->which = PRIO_USER;
	a->who = 0;
	getpriority();
	HK_CHECK(u.u_rval == -3);

	/* Nothing matched, and a request that names no category. */
	reset(REAL_UID, REAL_UID);
	a = (struct which_who_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = 4242;
	getpriority();
	HK_CHECK(u.u_error == ESRCH);

	reset(REAL_UID, REAL_UID);
	a = (struct which_who_arg *)u.u_arg;
	a->which = 99;
	a->who = 0;
	getpriority();
	HK_CHECK(u.u_error == EINVAL);
}

/*
 * Setting a priority is asymmetric. Raising the nice number, which yields
 * the processor, is ordinary; lowering it, which takes the processor, is
 * root's. A caller also has to own the process, by either identity.
 */
static void
write_priority(void)
{
	struct priority_arg *a;

	/* Yielding is allowed. */
	reset(REAL_UID, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = 0;
	a->prio = 6;
	setpriority();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(proc[0].p_nice == 6);

	/* Taking it back is not, once given away. */
	reset(REAL_UID, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	proc[0].p_nice = 6;
	a->which = PRIO_PROCESS;
	a->who = 0;
	a->prio = 1;
	setpriority();
	HK_CHECK(u.u_error == EACCES);
	HK_CHECK(proc[0].p_nice == 6);

	/* Root may take it back. */
	reset(0, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	proc[0].p_nice = 6;
	a->which = PRIO_PROCESS;
	a->who = 0;
	a->prio = 1;
	setpriority();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(proc[0].p_nice == 1);

	/* A process owned by someone else is refused, and left alone. */
	reset(REAL_UID, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = OTHER_PID;
	a->prio = 9;
	setpriority();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(proc[2].p_nice == 3);

	/* The value is clamped rather than refused at either end. */
	reset(0, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = 0;
	a->prio = 500;
	setpriority();
	HK_CHECK(proc[0].p_nice == PRIO_MAX);

	reset(0, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	a->which = PRIO_PROCESS;
	a->who = 0;
	a->prio = -500;
	setpriority();
	HK_CHECK(proc[0].p_nice == PRIO_MIN);

	/* A group moves every member it holds. */
	reset(0, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	a->which = PRIO_PGRP;
	a->who = SELF_PGRP;
	a->prio = 8;
	setpriority();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(proc[0].p_nice == 8);
	HK_CHECK(proc[1].p_nice == 8);
	HK_CHECK(proc[2].p_nice == 3);

	/* A group with no members is the ESRCH case. */
	reset(0, REAL_UID);
	a = (struct priority_arg *)u.u_arg;
	a->which = PRIO_PGRP;
	a->who = 4242;
	a->prio = 4;
	setpriority();
	HK_CHECK(u.u_error == ESRCH);
}

/*
 * Processor time is kept in ticks and presented in seconds, so the pair of
 * calls has to round-trip. Everything else is stored in the unit it is given
 * in, and the gate states both.
 */
static void
cpu_limit_round_trip(void)
{
	struct rlimit_arg *a;
	struct rlimit given, read_back;

	reset(0, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	u.u_rlimit[RLIMIT_CPU].rlim_cur = RLIM_INFINITY;
	u.u_rlimit[RLIMIT_CPU].rlim_max = RLIM_INFINITY;
	given.rlim_cur = 100;
	given.rlim_max = 200;
	a->which = RLIMIT_CPU;
	a->lim = &given;
	setrlimit();
	HK_CHECK(u.u_error == 0);

	/* Stored as ticks. */
	HK_CHECK(u.u_rlimit[RLIMIT_CPU].rlim_cur == 100 * hz);
	HK_CHECK(u.u_rlimit[RLIMIT_CPU].rlim_max == 200 * hz);

	/* Read back in the unit it was given in. */
	a = (struct rlimit_arg *)u.u_arg;
	a->which = RLIMIT_CPU;
	a->lim = &read_back;
	getrlimit();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(read_back.rlim_cur == 100);
	HK_CHECK(read_back.rlim_max == 200);

	/* A limit too large to hold in ticks saturates rather than wrapping. */
	reset(0, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	u.u_rlimit[RLIMIT_CPU].rlim_max = RLIM_INFINITY;
	given.rlim_cur = RLIM_INFINITY / hz;
	given.rlim_max = RLIM_INFINITY;
	a->which = RLIMIT_CPU;
	a->lim = &given;
	setrlimit();
	HK_CHECK(u.u_rlimit[RLIMIT_CPU].rlim_cur == RLIM_INFINITY);
	HK_CHECK(u.u_rlimit[RLIMIT_CPU].rlim_max == RLIM_INFINITY);

	/* And infinity survives the way back out rather than dividing. */
	a = (struct rlimit_arg *)u.u_arg;
	a->which = RLIMIT_CPU;
	a->lim = &read_back;
	getrlimit();
	HK_CHECK(read_back.rlim_cur == RLIM_INFINITY);
	HK_CHECK(read_back.rlim_max == RLIM_INFINITY);
}

/*
 * Every other limit passes through unconverted, a request past the end of
 * the table is refused, and raising a limit past the maximum already stored
 * needs privilege. That last check reads the stored maximum, not the one
 * being set, so lowering the maximum and raising it again in one call still
 * needs root.
 */
static void
other_limits(void)
{
	struct rlimit_arg *a;
	struct rlimit given, read_back;

	reset(REAL_UID, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	u.u_rlimit[RLIMIT_DATA].rlim_cur = 4096;
	u.u_rlimit[RLIMIT_DATA].rlim_max = 8192;
	given.rlim_cur = 2048;
	given.rlim_max = 8192;
	a->which = RLIMIT_DATA;
	a->lim = &given;
	setrlimit();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rlimit[RLIMIT_DATA].rlim_cur == 2048);

	a = (struct rlimit_arg *)u.u_arg;
	a->which = RLIMIT_DATA;
	a->lim = &read_back;
	getrlimit();
	HK_CHECK(read_back.rlim_cur == 2048);
	HK_CHECK(read_back.rlim_max == 8192);

	/* Past the stored maximum, an ordinary caller is refused. */
	reset(REAL_UID, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	u.u_rlimit[RLIMIT_DATA].rlim_cur = 4096;
	u.u_rlimit[RLIMIT_DATA].rlim_max = 8192;
	given.rlim_cur = 16384;
	given.rlim_max = 16384;
	a->which = RLIMIT_DATA;
	a->lim = &given;
	setrlimit();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(u.u_rlimit[RLIMIT_DATA].rlim_cur == 4096);

	/* Root is not. */
	reset(0, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	u.u_rlimit[RLIMIT_DATA].rlim_cur = 4096;
	u.u_rlimit[RLIMIT_DATA].rlim_max = 8192;
	given.rlim_cur = 16384;
	given.rlim_max = 16384;
	a->which = RLIMIT_DATA;
	a->lim = &given;
	setrlimit();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rlimit[RLIMIT_DATA].rlim_max == 16384);

	/* A resource the table does not hold. */
	reset(0, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	a->which = RLIM_NLIMITS;
	a->lim = &given;
	setrlimit();
	HK_CHECK(u.u_error == EINVAL);

	reset(0, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	a->which = RLIM_NLIMITS;
	a->lim = &read_back;
	getrlimit();
	HK_CHECK(u.u_error == EINVAL);

	/* A copy that faults leaves the stored limit alone. */
	reset(0, REAL_UID);
	a = (struct rlimit_arg *)u.u_arg;
	u.u_rlimit[RLIMIT_DATA].rlim_cur = 4096;
	u.u_rlimit[RLIMIT_DATA].rlim_max = 8192;
	given.rlim_cur = 1024;
	given.rlim_max = 8192;
	a->which = RLIMIT_DATA;
	a->lim = &given;
	copy_fails = 1;
	setrlimit();
	HK_CHECK(u.u_error == EFAULT);
	HK_CHECK(u.u_rlimit[RLIMIT_DATA].rlim_cur == 4096);
}

/*
 * Usage accounting keeps ticks and reports a timeval, and folds a child's
 * totals into its parent's as one loop over the shared prefix of the two
 * structures.
 */
static void
usage_accounting(void)
{
	struct rusage_arg *a;
	struct rusage reported;
	struct k_rusage child;

	reset(REAL_UID, REAL_UID);
	a = (struct rusage_arg *)u.u_arg;
	u.u_ru.ru_utime = 2 * hz + 250;
	u.u_ru.ru_stime = hz / 2;
	u.u_ru.ru_inblock = 17;
	u.u_ru.ru_nvcsw = 9;
	a->who = RUSAGE_SELF;
	a->rusage = &reported;
	getrusage();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(reported.ru_utime.tv_sec == 2);
	HK_CHECK(reported.ru_utime.tv_usec == 250 * usechz);
	HK_CHECK(reported.ru_stime.tv_sec == 0);
	HK_CHECK(reported.ru_stime.tv_usec == (hz / 2) * usechz);
	HK_CHECK(reported.ru_inblock == 17);
	HK_CHECK(reported.ru_nvcsw == 9);
	/* Fields the kernel does not keep come back zeroed, not stale. */
	HK_CHECK(reported.ru_maxrss == 0);
	HK_CHECK(reported.ru_majflt == 0);

	/* Children are accounted separately. */
	reset(REAL_UID, REAL_UID);
	a = (struct rusage_arg *)u.u_arg;
	u.u_ru.ru_utime = 5 * hz;
	u.u_cru.ru_utime = 3 * hz;
	a->who = RUSAGE_CHILDREN;
	a->rusage = &reported;
	getrusage();
	HK_CHECK(reported.ru_utime.tv_sec == 3);

	reset(REAL_UID, REAL_UID);
	a = (struct rusage_arg *)u.u_arg;
	a->who = 42;
	a->rusage = &reported;
	getrusage();
	HK_CHECK(u.u_error == EINVAL);

	/* ruadd folds one set of totals into another, every field at once. */
	reset(REAL_UID, REAL_UID);
	bzero((caddr_t)&child, sizeof child);
	u.u_cru.ru_utime = 10;
	u.u_cru.ru_inblock = 1;
	u.u_cru.ru_nivcsw = 4;
	child.ru_utime = 5;
	child.ru_stime = 7;
	child.ru_inblock = 2;
	child.ru_nivcsw = 6;
	ruadd(&u.u_cru, &child);
	HK_CHECK(u.u_cru.ru_utime == 15);
	HK_CHECK(u.u_cru.ru_stime == 7);
	HK_CHECK(u.u_cru.ru_inblock == 3);
	HK_CHECK(u.u_cru.ru_nivcsw == 10);
}

int
main(void)
{
	read_priority();
	write_priority();
	cpu_limit_round_trip();
	other_limits();
	usage_accounting();
	return hk_verdict("kern_resource");
}
