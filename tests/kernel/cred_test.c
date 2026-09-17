/*
 * Host gate for the process and protection syscalls: sys/kern/kern_prot.c,
 * sys/kern/kern_prot2.c and sys/kern/kern_proc.c, all linked from the kernel
 * source.
 *
 * These decide who may signal, reparent and become whom, so what the gate
 * pins is each refusal as much as each success: which identity a caller may
 * assume without being root, which it may not, and that a refusal leaves
 * every field as it found them. A credential syscall that half-applies is
 * worse than one that fails.
 *
 * A syscall here takes its arguments from u.u_arg and answers in u.u_rval
 * and u.u_error, so the gate sets up the same union of argument structures
 * the kernel casts u_arg to.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>

/*
 * The two structures the kernel keeps its current process and its process
 * table in. sys/arch/${MACHINE}/${MACHINE}/machdep.c defines both for a
 * kernel; a host gate defines them here.
 */
struct user u;
struct proc proc[NPROC];

/* Identities the scenarios below use. Root is uid 0 by definition. */
#define SELF_PID	17
#define CHILD_PID	18
#define OTHER_PID	19
#define REAL_UID	1000
#define OTHER_UID	1001
#define SAVED_UID	1002
#define REAL_GID	100
#define OTHER_GID	101
#define SAVED_GID	102

/* Injected failure for the two copies, since neither can fail on a host. */
static int copy_fails;

/*
 * The kernel's copyin and copyout. On the board they cross the user and
 * kernel windows; here both are one address space, so the copy is the whole
 * of it and the interesting part is the failure a caller has to handle.
 */
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

/*
 * suser() as sys/kern/ufs_fio.c defines it, in six lines: root passes, and
 * anyone else is refused with the error recorded for the caller to return.
 * It lives in a file whose other contents reach the whole filesystem, which
 * is why the gate carries these six lines rather than linking that.
 */
int
suser(void)
{
	if (u.u_uid == 0)
		return 1;
	u.u_error = EPERM;
	return 0;
}

/* The argument shapes the kernel casts u.u_arg to. */
struct pid_arg {
	int pid;
};

struct pgrp_arg {
	int pid;
	int pgrp;
};

struct gidset_arg {
	u_int gidsetsize;
	int *gidset;
};

struct uid_arg {
	uid_t uid;
};

struct gid_arg {
	gid_t gid;
};

/*
 * A process table with three entries hashed the way pfind() looks them up:
 * the caller, a child of it, and an unrelated process owned by someone else.
 */
static void
reset(uid_t effective, uid_t real)
{
	unsigned i;

	for (i = 0; i < PIDHSZ; i++)
		pidhash[i] = NULL;
	for (i = 0; i < NPROC; i++)
		bzero(&proc[i], sizeof proc[i]);
	bzero(&u, sizeof u);

	proc[0].p_pid = SELF_PID;
	proc[0].p_ppid = 1;
	proc[0].p_pgrp = SELF_PID;
	proc[0].p_uid = (short)real;
	proc[0].p_pptr = NULL;

	proc[1].p_pid = CHILD_PID;
	proc[1].p_ppid = SELF_PID;
	proc[1].p_pgrp = SELF_PID;
	proc[1].p_uid = (short)real;
	proc[1].p_pptr = &proc[0];

	/* Owned by someone else, and its parent chain ends rather than
	   reaching the caller, which is what inferior() walks for. */
	proc[2].p_pid = OTHER_PID;
	proc[2].p_ppid = 0;
	proc[2].p_pgrp = OTHER_PID;
	proc[2].p_uid = (short)OTHER_UID;
	proc[2].p_pptr = NULL;

	for (i = 0; i < 3; i++) {
		proc[i].p_hash = pidhash[PIDHASH(proc[i].p_pid)];
		pidhash[PIDHASH(proc[i].p_pid)] = &proc[i];
	}

	u.u_procp = &proc[0];
	u.u_uid = effective;
	u.u_ruid = real;
	u.u_svuid = SAVED_UID;
	u.u_rgid = REAL_GID;
	u.u_svgid = SAVED_GID;
	u.u_groups[0] = REAL_GID;
	for (i = 1; i < NGROUPS; i++)
		u.u_groups[i] = NOGROUP;
	u.u_error = 0;
	u.u_rval = 0;
	copy_fails = 0;
	hk_reset_output();
}

/*
 * The identity a process reports. getegid answers from u_groups[0] rather
 * than from a field of its own, which is how this kernel stores the
 * effective group, and a caller that expected a separate field would read
 * whatever the first supplementary group happened to be.
 */
static void
identity_getters(void)
{
	reset(0, REAL_UID);

	getpid();
	HK_CHECK(u.u_rval == SELF_PID);
	getppid();
	HK_CHECK(u.u_rval == 1);

	/* A set-user binary: real and effective differ, and both are visible. */
	getuid();
	HK_CHECK(u.u_rval == REAL_UID);
	geteuid();
	HK_CHECK(u.u_rval == 0);

	getgid();
	HK_CHECK(u.u_rval == REAL_GID);
	u.u_groups[0] = OTHER_GID;
	getegid();
	HK_CHECK(u.u_rval == OTHER_GID);
	HK_CHECK(u.u_error == 0);
}

/*
 * pfind() walks the pid hash and inferior() walks the parent chain. setpgrp
 * relies on both to decide whether a caller may move another process, so a
 * gate over the syscalls has to pin the two underneath them.
 */
static void
process_lookup(void)
{
	reset(REAL_UID, REAL_UID);

	HK_CHECK(pfind(SELF_PID) == &proc[0]);
	HK_CHECK(pfind(CHILD_PID) == &proc[1]);
	HK_CHECK(pfind(OTHER_PID) == &proc[2]);
	HK_CHECK(pfind(4242) == NULL);

	/* The caller is its own ancestor, and its child is below it. */
	HK_CHECK(inferior(&proc[0]) == 1);
	HK_CHECK(inferior(&proc[1]) == 1);
	/* A chain that ends before reaching the caller is not. */
	HK_CHECK(inferior(&proc[2]) == 0);
}

/*
 * Reading a process group. A pid of zero means the caller, and the syscall
 * writes that resolved pid back into its own argument.
 */
static void
read_process_group(void)
{
	struct pid_arg *a = (struct pid_arg *)u.u_arg;

	reset(REAL_UID, REAL_UID);
	a->pid = 0;
	getpgrp();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rval == SELF_PID);
	HK_CHECK(a->pid == SELF_PID);

	reset(REAL_UID, REAL_UID);
	a = (struct pid_arg *)u.u_arg;
	a->pid = OTHER_PID;
	getpgrp();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rval == OTHER_PID);

	reset(REAL_UID, REAL_UID);
	a = (struct pid_arg *)u.u_arg;
	a->pid = 4242;
	getpgrp();
	HK_CHECK(u.u_error == ESRCH);
}

/*
 * Moving a process into a group. A caller may move a process it owns, one
 * below it in the parent chain, or anything at all if it is root; a refusal
 * leaves the target's group alone.
 */
static void
set_process_group(void)
{
	struct pgrp_arg *a;

	/* Own process. */
	reset(REAL_UID, REAL_UID);
	a = (struct pgrp_arg *)u.u_arg;
	a->pid = 0;
	a->pgrp = 55;
	setpgrp();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(proc[0].p_pgrp == 55);

	/* A process owned by someone else, as an ordinary user. */
	reset(REAL_UID, REAL_UID);
	a = (struct pgrp_arg *)u.u_arg;
	a->pid = OTHER_PID;
	a->pgrp = 66;
	setpgrp();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(proc[2].p_pgrp == OTHER_PID);

	/* The same call as root. */
	reset(0, REAL_UID);
	a = (struct pgrp_arg *)u.u_arg;
	a->pid = OTHER_PID;
	a->pgrp = 66;
	setpgrp();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(proc[2].p_pgrp == 66);

	/* A child, which the parent chain reaches even across a uid change. */
	reset(REAL_UID, REAL_UID);
	a = (struct pgrp_arg *)u.u_arg;
	proc[1].p_uid = (short)OTHER_UID;
	a->pid = CHILD_PID;
	a->pgrp = 77;
	setpgrp();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(proc[1].p_pgrp == 77);

	/* An unknown pid. */
	reset(REAL_UID, REAL_UID);
	a = (struct pgrp_arg *)u.u_arg;
	a->pid = 4242;
	a->pgrp = 88;
	setpgrp();
	HK_CHECK(u.u_error == ESRCH);
}

/*
 * Reading the group set. The count is the distance to the last entry that is
 * not NOGROUP, a buffer shorter than that is refused before anything is
 * copied, and a copy that faults leaves the caller's answer unset.
 */
static void
read_groups(void)
{
	static int received[NGROUPS];
	struct gidset_arg *a;

	reset(REAL_UID, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	u.u_groups[1] = OTHER_GID;
	u.u_groups[2] = 7;
	a->gidsetsize = NGROUPS;
	a->gidset = received;
	getgroups();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_rval == 3);
	HK_CHECK(received[0] == REAL_GID);
	HK_CHECK(received[2] == 7);

	/* One short of the set is refused, and nothing is written. */
	reset(REAL_UID, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	u.u_groups[1] = OTHER_GID;
	u.u_groups[2] = 7;
	received[0] = -1;
	a->gidsetsize = 2;
	a->gidset = received;
	getgroups();
	HK_CHECK(u.u_error == EINVAL);
	HK_CHECK(received[0] == -1);

	/* A copy that faults reports the fault and leaves no answer. */
	reset(REAL_UID, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	a->gidsetsize = NGROUPS;
	a->gidset = received;
	copy_fails = 1;
	getgroups();
	HK_CHECK(u.u_error == EFAULT);
	HK_CHECK(u.u_rval == 0);
}

/*
 * Replacing the group set is root's alone, an oversized set is refused, and
 * a shorter set has to clear the entries it does not cover, or a later
 * groupmember() would still find a group the caller gave up.
 */
static void
write_groups(void)
{
	static int supplied[4] = { 10, 11, 12, 13 };
	struct gidset_arg *a;

	/* An ordinary user cannot. */
	reset(REAL_UID, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	a->gidsetsize = 4;
	a->gidset = supplied;
	setgroups();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(u.u_groups[0] == REAL_GID);

	/* Root can, and the tail is cleared. */
	reset(0, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	u.u_groups[1] = 99;
	u.u_groups[2] = 98;
	a->gidsetsize = 2;
	a->gidset = supplied;
	setgroups();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_groups[0] == 10);
	HK_CHECK(u.u_groups[1] == 11);
	HK_CHECK(u.u_groups[2] == NOGROUP);
	HK_CHECK(u.u_groups[NGROUPS - 1] == NOGROUP);

	/* More groups than the set holds. */
	reset(0, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	a->gidsetsize = NGROUPS + 1;
	a->gidset = supplied;
	setgroups();
	HK_CHECK(u.u_error == EINVAL);
	HK_CHECK(u.u_groups[0] == REAL_GID);

	/* A copy that faults leaves the set as it was. */
	reset(0, REAL_UID);
	a = (struct gidset_arg *)u.u_arg;
	a->gidsetsize = 2;
	a->gidset = supplied;
	copy_fails = 1;
	setgroups();
	HK_CHECK(u.u_error == EFAULT);

	/* Membership scans to the first NOGROUP and no further. */
	reset(0, REAL_UID);
	u.u_groups[1] = 44;
	u.u_groups[2] = NOGROUP;
	u.u_groups[3] = 45;
	HK_CHECK(groupmember(REAL_GID) == 1);
	HK_CHECK(groupmember(44) == 1);
	HK_CHECK(groupmember(45) == 0);
	HK_CHECK(groupmember(999) == 0);
}

/*
 * Becoming another user. setuid takes all four identities at once, which is
 * what makes it the call a set-user program uses to drop privilege for good;
 * seteuid moves only the effective one and can be undone from the saved id.
 */
static void
change_user(void)
{
	struct uid_arg *a;

	/* Dropping to the real uid needs no privilege, and drops the saved
	   id too, so the program cannot come back. */
	reset(0, REAL_UID);
	a = (struct uid_arg *)u.u_arg;
	a->uid = REAL_UID;
	setuid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_uid == REAL_UID);
	HK_CHECK(u.u_ruid == REAL_UID);
	HK_CHECK(u.u_svuid == REAL_UID);
	HK_CHECK(proc[0].p_uid == (short)REAL_UID);

	/* Any other uid needs privilege. */
	reset(REAL_UID, REAL_UID);
	a = (struct uid_arg *)u.u_arg;
	a->uid = OTHER_UID;
	setuid();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(u.u_uid == REAL_UID);
	HK_CHECK(u.u_ruid == REAL_UID);
	HK_CHECK(u.u_svuid == SAVED_UID);

	/* Root may become anyone. */
	reset(0, REAL_UID);
	a = (struct uid_arg *)u.u_arg;
	a->uid = OTHER_UID;
	setuid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_uid == OTHER_UID);
	HK_CHECK(u.u_ruid == OTHER_UID);

	/* seteuid reaches the real and the saved id without privilege. */
	reset(REAL_UID, REAL_UID);
	a = (struct uid_arg *)u.u_arg;
	a->uid = SAVED_UID;
	seteuid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_uid == SAVED_UID);
	HK_CHECK(u.u_ruid == REAL_UID);

	reset(SAVED_UID, REAL_UID);
	a = (struct uid_arg *)u.u_arg;
	a->uid = REAL_UID;
	seteuid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_uid == REAL_UID);

	/* And nothing else. */
	reset(REAL_UID, REAL_UID);
	a = (struct uid_arg *)u.u_arg;
	a->uid = OTHER_UID;
	seteuid();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(u.u_uid == REAL_UID);
}

/*
 * Becoming another group. The effective group lives in u_groups[0], so
 * setgid and setegid write there rather than to a field of their own.
 */
static void
change_group(void)
{
	struct gid_arg *a;

	reset(REAL_UID, REAL_UID);
	a = (struct gid_arg *)u.u_arg;
	a->gid = REAL_GID;
	setgid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_groups[0] == REAL_GID);
	HK_CHECK(u.u_rgid == REAL_GID);
	HK_CHECK(u.u_svgid == REAL_GID);

	reset(REAL_UID, REAL_UID);
	a = (struct gid_arg *)u.u_arg;
	a->gid = OTHER_GID;
	setgid();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(u.u_rgid == REAL_GID);
	HK_CHECK(u.u_svgid == SAVED_GID);

	reset(0, REAL_UID);
	a = (struct gid_arg *)u.u_arg;
	a->gid = OTHER_GID;
	setgid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_groups[0] == OTHER_GID);
	HK_CHECK(u.u_rgid == OTHER_GID);

	/* setegid reaches the real and the saved group only. */
	reset(REAL_UID, REAL_UID);
	a = (struct gid_arg *)u.u_arg;
	a->gid = SAVED_GID;
	setegid();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(u.u_groups[0] == SAVED_GID);
	HK_CHECK(u.u_rgid == REAL_GID);

	reset(REAL_UID, REAL_UID);
	a = (struct gid_arg *)u.u_arg;
	a->gid = OTHER_GID;
	setegid();
	HK_CHECK(u.u_error == EPERM);
	HK_CHECK(u.u_groups[0] == REAL_GID);
}

int
main(void)
{
	identity_getters();
	process_lookup();
	read_process_group();
	set_process_group();
	read_groups();
	write_groups();
	change_user();
	change_group();
	return hk_verdict("kern_prot");
}
