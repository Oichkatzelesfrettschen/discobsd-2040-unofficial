/*
 * Host gate for the effective uid the signal path reads, linked from
 * sys/kern/kern_sig.c, kern_prot2.c and kern_proc.c.
 *
 * The effective uid is kept twice: u_uid in the u area and p_uid in the
 * proc entry, and cansignal() decides kill(2) from p_uid because the
 * target's u area may be swapped out. The gate therefore judges seteuid()
 * by what kill() does afterwards rather than by comparing the two fields:
 * a process that drops root with seteuid() must be refused a signal to an
 * unrelated process, and one that takes root back through its saved id
 * must be allowed it. The signal is SIGUSR1 and the target is unrelated
 * and owned by a third uid, so neither the real-uid matches nor the
 * SIGCONT-to-descendant exception can pass the check on their own.
 *
 * kern_sig.c compiles at the target's width alone, since issignal() and
 * core() cast pointers to int, so this gate builds at -m32 beside prf_test.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/namei.h>
#define SIGPROP		/* sigprop[] is defined by the unit that names it, as kern_sig2.c does */
#include <sys/signalvar.h>
#include <sys/uio.h>

_Static_assert(sizeof(long) == 4, "long is the target's four bytes");

struct user u;
struct proc proc[NPROC];
char curpri;
int runrun;
char __user_data_start[1];
char __user_data_end[1];

#define SELF_PID	17
#define CHILD_PID	18
#define OTHER_PID	19
#define ROOT_UID	0
#define REAL_UID	1000
#define OTHER_UID	2000
/* Above SHRT_MAX: a uid that a 16-bit p_uid stores as a negative value. */
#define WIDE_UID	40000



/* The tail of psignal(), which reaches the scheduler; none of it decides
 * authorization, so each entry records that it ran and returns, except
 * exit(), which is __dead and leaves through hk_escape(). */
static unsigned setrun_calls;

void setrun(struct proc *p) { (void)p; setrun_calls++; }
void setrq(struct proc *p) { (void)p; }
void unsleep(struct proc *p) { (void)p; }
void wakeup(caddr_t chan) { (void)chan; }
int setpri(struct proc *p) { (void)p; return 0; }
void swtch(void) { }
int procxmt(void) { return 0; }
void addupc(caddr_t pc, struct uprof *up, int ticks) { (void)pc; (void)up; (void)ticks; }
void sendsig(sig_t p, int sig, long mask) { (void)p; (void)sig; (void)mask; }
/* kern_sig.c and this file are built with -Dexit=sigauth_exit, so proc.h's
 * declaration types this stub and the harness's own exit() stays libc's:
 * hk_escape() and panic() leave through it. */
__dead void exit(int rv) { hk_note("exit(%d) reached", rv); hk_escape(); }
int ffs(u_long i) { int n = 0; if (i == 0) return 0; while (!(i & 1)) { i >>= 1; n++; } return n + 1; }

/* core() names the filesystem; kill() never reaches it. */
struct inode *namei(struct nameidata *ndp) { (void)ndp; return NULL; }
struct inode *maknode(int mode, struct nameidata *ndp) { (void)mode; (void)ndp; return NULL; }
void iput(struct inode *ip) { (void)ip; }
void itrunc(struct inode *ip, off_t length, int flags) { (void)ip; (void)length; (void)flags; }
int access(struct inode *ip, int mode) { (void)ip; (void)mode; return 0; }
int rdwri(enum uio_rw rw, struct inode *ip, caddr_t base, int len, off_t offset, int ioflg, int *aresid)
{ (void)rw; (void)ip; (void)base; (void)len; (void)offset; (void)ioflg; (void)aresid; return 0; }

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	bcopy(from, to, nbytes);
	return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	bcopy(from, to, nbytes);
	return 0;
}

struct uid_arg {
	uid_t uid;
};

struct kill_arg {
	int pid;
	int signo;
};

/*
 * The caller with real uid 1000, and the effective and saved ids given;
 * a child of it; and an unrelated process owned by uid 2000 whose parent
 * chain ends, so inferior() reaches its end without a match.
 */
static void
reset(uid_t effective, uid_t saved)
{
	unsigned i;

	for (i = 0; i < PIDHSZ; i++)
		pidhash[i] = 0;
	for (i = 0; i < NPROC; i++)
		bzero(&proc[i], sizeof proc[i]);
	bzero(&u, sizeof u);

	proc[0].p_pid = SELF_PID;
	proc[0].p_ppid = 1;
	proc[0].p_pgrp = SELF_PID;
	proc[0].p_uid = effective;
	proc[0].p_stat = SRUN;
	proc[0].p_ruid = REAL_UID;

	proc[1].p_pid = CHILD_PID;
	proc[1].p_ppid = SELF_PID;
	proc[1].p_pgrp = SELF_PID;
	proc[1].p_uid = effective;
	proc[1].p_pptr = &proc[0];
	proc[1].p_stat = SRUN;
	proc[1].p_ruid = REAL_UID;

	proc[2].p_pid = OTHER_PID;
	proc[2].p_ppid = 0;
	proc[2].p_pgrp = OTHER_PID;
	proc[2].p_uid = OTHER_UID;
	proc[2].p_pptr = NULL;
	proc[2].p_stat = SRUN;
	proc[2].p_ruid = OTHER_UID;

	for (i = 0; i < 3; i++) {
		proc[i].p_hash = pidhash[PIDHASH(proc[i].p_pid)];
		pidhash[PIDHASH(proc[i].p_pid)] = pid_slot(&proc[i]);
		proc[i].p_nxt = i + 1 < 3 ? &proc[i + 1] : NULL;
	}
	allproc = &proc[0];

	u.u_procp = &proc[0];
	u.u_uid = effective;
	u.u_ruid = REAL_UID;
	u.u_svuid = saved;
	u.u_error = 0;
	setrun_calls = 0;
	hk_reset_output();
}

static void
call_seteuid(uid_t uid)
{
	struct uid_arg *a = (struct uid_arg *)u.u_arg;

	a->uid = uid;
	u.u_error = 0;
	seteuid();
}

static void
call_kill(int pid, int signo)
{
	struct kill_arg *a = (struct kill_arg *)u.u_arg;

	a->pid = pid;
	a->signo = signo;
	u.u_error = 0;
	kill();
}

/* Both copies of the effective uid, and the real and saved ids untouched. */
static void
check_identity(uid_t effective, uid_t saved)
{
	HK_CHECK(u.u_uid == effective);
	HK_CHECK(proc[0].p_uid == effective);
	HK_CHECK(u.u_ruid == REAL_UID);
	HK_CHECK(u.u_svuid == saved);
}

/*
 * A set-user program: real 1000, effective root, saved root. It drops to
 * its real uid, is refused a signal to a stranger, is refused a uid it
 * never held, takes root back through the saved id, and is allowed the
 * signal.
 */
static void
drop_and_regain(void)
{
	reset(ROOT_UID, ROOT_UID);
	check_identity(ROOT_UID, ROOT_UID);

	call_seteuid(REAL_UID);
	HK_CHECK(u.u_error == 0);
	check_identity(REAL_UID, ROOT_UID);

	/* Unprivileged now: the stranger is out of reach. */
	call_kill(OTHER_PID, SIGUSR1);
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(proc[2].p_sig == 0);
	HK_CHECK(setrun_calls == 0);

	/* A uid that is neither real nor saved: refused, nothing moves. */
	call_seteuid(OTHER_UID);
	HK_CHECK(u.u_error == EPERM);
	check_identity(REAL_UID, ROOT_UID);

	/* Back to root through the saved id, without privilege. */
	call_seteuid(ROOT_UID);
	HK_CHECK(u.u_error == 0);
	check_identity(ROOT_UID, ROOT_UID);

	call_kill(OTHER_PID, SIGUSR1);
	HK_CHECK(u.u_error == 0);
	HK_CHECK((proc[2].p_sig & sigmask(SIGUSR1)) != 0);
}

/*
 * The same transitions seen by killpg1(): a broadcast from the dropped
 * process reaches the child, which shares its uid, and not the stranger.
 */
static void
broadcast_after_drop(void)
{
	reset(ROOT_UID, ROOT_UID);
	call_seteuid(REAL_UID);
	HK_CHECK(u.u_error == 0);

	call_kill(-1, SIGUSR1);
	HK_CHECK(u.u_error == 0);
	HK_CHECK((proc[1].p_sig & sigmask(SIGUSR1)) != 0);
	HK_CHECK(proc[2].p_sig == 0);
}

/*
 * The other direction: a process that starts unprivileged with a saved
 * root id takes it, and the signal path sees root at once.
 */
static void
regain_from_unprivileged(void)
{
	reset(REAL_UID, ROOT_UID);
	call_kill(OTHER_PID, SIGUSR1);
	HK_CHECK(u.u_error == EPERM);

	call_seteuid(ROOT_UID);
	HK_CHECK(u.u_error == 0);
	check_identity(ROOT_UID, ROOT_UID);
	call_kill(OTHER_PID, SIGUSR1);
	HK_CHECK(u.u_error == 0);
}

/*
 * uid_t is u_int, and passwd(5) admits any value it holds, so a uid above
 * SHRT_MAX is legitimate; cansignal() compares p_uid with the real uid it
 * reads from the process table, and a p_uid narrower than uid_t makes the
 * comparison fail for such a uid in both directions. The caller's
 * effective uid equals the stranger's real uid, then the caller's real
 * uid equals the stranger's effective uid; each alone authorizes kill(2).
 */
static void
wide_uid(void)
{
	reset(WIDE_UID, WIDE_UID);
	check_identity(WIDE_UID, WIDE_UID);
	proc[2].p_ruid = WIDE_UID;
	call_kill(OTHER_PID, SIGUSR1);
	HK_CHECK(u.u_error == 0);
	HK_CHECK((proc[2].p_sig & sigmask(SIGUSR1)) != 0);

	reset(REAL_UID, REAL_UID);
	u.u_ruid = WIDE_UID;
	proc[2].p_uid = WIDE_UID;
	call_kill(OTHER_PID, SIGUSR1);
	HK_CHECK(u.u_error == 0);
	HK_CHECK((proc[2].p_sig & sigmask(SIGUSR1)) != 0);

	/* seteuid() to the wide uid lands in p_uid whole. */
	reset(REAL_UID, WIDE_UID);
	call_seteuid(WIDE_UID);
	HK_CHECK(u.u_error == 0);
	check_identity(WIDE_UID, WIDE_UID);
}

/* The SIGCONT exception is the descendant's alone, whatever the uid. */
static void
sigcont_exception(void)
{
	reset(REAL_UID, REAL_UID);
	call_kill(OTHER_PID, SIGCONT);
	HK_CHECK(u.u_error == EPERM);
	call_kill(CHILD_PID, SIGCONT);
	HK_CHECK(u.u_error == 0);
}

/* The kernel's exit() reaches the stub, which leaves through hk_escape(). */
static void
call_kernel_exit(void)
{
	exit(SIGKILL);
}

static void
exit_stub_escapes(void)
{
	HK_CHECK(hk_catch(call_kernel_exit) == 1);
}

int
main(void)
{
	exit_stub_escapes();
	drop_and_regain();
	broadcast_after_drop();
	regain_from_unprivileged();
	sigcont_exception();
	wide_uid();
	return hk_verdict("sigauth");
}
