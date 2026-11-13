/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_sysctl.c	8.4.12 (2.11BSD) 2000/5/17
 */

/*
 * sysctl system call.
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/capacity.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/dk.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/vm.h>
#include <sys/map.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#ifdef SWAPRAM
#include <machine/swapram.h>
#endif
#include <sys/conf.h>

sysctlfn kern_sysctl;
sysctlfn hw_sysctl;
struct sysctl_lock memlock;
#ifdef DEBUG
sysctlfn debug_sysctl;
#endif
sysctlfn vm_sysctl;
sysctlfn fs_sysctl;
#ifdef INET
sysctlfn net_sysctl;
#endif
sysctlfn cpu_sysctl;

#ifdef SYSTRACE
int systrace;
int systrace_pid;
#endif

struct sysctl_args {
	int	*name;
	u_int	 namelen;
	void	*old;
	size_t	*oldlenp;
	void	*new;
	size_t	 newlen;
};

void
__sysctl()
{
	struct sysctl_args *uap = (struct sysctl_args *)u.u_arg;
	int error;
	size_t savelen, oldlen = 0;
	sysctlfn *fn;
	int name[CTL_MAXNAME];

	if (uap->new != NULL && !suser())
		return;
	/*
	 * All top-level sysctl names are non-terminal.
	 */
	if (uap->namelen > CTL_MAXNAME || uap->namelen < 2) {
		u.u_error = EINVAL;
		return;
	}
	error = copyin((caddr_t)uap->name, (caddr_t)&name,
	    uap->namelen * sizeof(int));
	if (error) {
		u.u_error = error;
		return;
	}

	switch (name[0]) {
	case CTL_KERN:
		fn = kern_sysctl;
		break;
	case CTL_HW:
		fn = hw_sysctl;
		break;
	case CTL_VM:
		fn = vm_sysctl;
		break;
#ifdef INET
	case CTL_NET:
		fn = net_sysctl;
		break;
#endif
#ifdef notyet
	case CTL_FS:
		fn = fs_sysctl;
		break;
#endif
	case CTL_MACHDEP:
		fn = cpu_sysctl;
		break;
#ifdef DEBUG
	case CTL_DEBUG:
		fn = debug_sysctl;
		break;
#endif
	default:
		u.u_error = EOPNOTSUPP;
		return;
	}

	if (uap->oldlenp && (error = copyin((caddr_t)uap->oldlenp,
	    (caddr_t)&oldlen, sizeof(oldlen)))) {
		u.u_error = error;
		return;
	}
	/*
	 * The buffer the caller offered, kept because the node overwrites
	 * oldlen with the length its value needs.
	 */
	savelen = oldlen;
	if (uap->old != NULL) {
		while (memlock.sl_lock) {
			memlock.sl_want = 1;
			sleep((caddr_t)&memlock, PRIBIO + 1);
			memlock.sl_locked++;
		}
		memlock.sl_lock = 1;
	}
	error = (*fn)(name + 1, uap->namelen - 1, uap->old, &oldlen,
	    uap->new, uap->newlen);
	if (uap->old != NULL) {
		memlock.sl_lock = 0;
		if (memlock.sl_want) {
			memlock.sl_want = 0;
			wakeup((caddr_t)&memlock);
		}
	}
	/*
	 * The length reaches the caller whatever the node answered, because
	 * it is what sizes the next call: a node that reported more than the
	 * buffer holds has truncated its value, and a caller reading the
	 * length after ENOMEM learns how much to offer. A failure to deliver
	 * it stands only where the node itself did not already fail.
	 */
	if (uap->oldlenp) {
		int lenerror = copyout((caddr_t)&oldlen,
		    (caddr_t)uap->oldlenp, sizeof(oldlen));

		if (error == 0)
			error = lenerror;
	}
	/*
	 * ENOMEM is the last verdict, so it never displaces the node's own.
	 * The helpers in this file copy the prefix that fits and report the
	 * length the value needs (sysctl(3)), which makes the comparison
	 * here the one place truncation is detected.
	 */
	if (error == 0 && uap->old != NULL && savelen < oldlen)
		error = ENOMEM;
	/*
	 * Success returns 0, as sysctl(3) documents: the length reaches the
	 * caller through *oldlenp alone, and syscall() has already zeroed
	 * u_rval.
	 */
	if (error)
		u.u_error = error;
}

/*
 * Kernel related system variables.
 */
static int sysctl_tty(void *, size_t *, void *);

int
kern_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int error, level;
	u_long longhostid;

	/* All sysctl names at this level are terminal. */
	if (namelen != 1 && !(name[0] == KERN_PROC || name[0] == KERN_FILE ||
	    name[0] == KERN_PROF))
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case KERN_OSTYPE:
		return (sysctl_rdstring(oldp, oldlenp, newp, ostype));
	case KERN_OSRELEASE:
		return (sysctl_rdstring(oldp, oldlenp, newp, osrelease));
	case KERN_OSREV:
		return (sysctl_rdlong(oldp, oldlenp, newp, (long)DiscoBSD));
	case KERN_OSVERSION:
		return (sysctl_rdstring(oldp, oldlenp, newp, osversion));
	case KERN_VERSION:
		return (sysctl_rdstring(oldp, oldlenp, newp, version));
	case KERN_MAXINODES:
		return (sysctl_rdint(oldp, oldlenp, newp, NINODE));
	case KERN_MAXPROC:
		return (sysctl_rdint(oldp, oldlenp, newp, NPROC));
	case KERN_MAXFILES:
		return (sysctl_rdint(oldp, oldlenp, newp, NFILE));
	case KERN_ARGMAX:
		return (sysctl_rdint(oldp, oldlenp, newp, NCARGS));
#ifdef CAPACITY_STATS
	case KERN_CAPACITY:
		return (sysctl_rdstruct(oldp, oldlenp, newp,
		    (void *)capacity_snapshot(),
		    sizeof(struct kernel_capacity_stats)));
#endif
#ifdef SYSTRACE
	case KERN_SYSTRACE:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &systrace));
	case KERN_SYSTRACEPID:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &systrace_pid));
#endif
	case KERN_SECURELVL:
		level = securelevel;
		if ((error = sysctl_int(oldp, oldlenp, newp, newlen, &level)) ||
		    newp == NULL)
			return (error);
		if (level < securelevel && u.u_procp->p_pid != 1)
			return (EPERM);
		securelevel = level;
		return (0);
	case KERN_HOSTNAME:
		error = sysctl_string(oldp, oldlenp, newp, newlen,
		    hostname, sizeof(hostname));
		if (newp && !error)
			hostnamelen = newlen;
		return (error);
	case KERN_HOSTID:
		longhostid = hostid;
		error = sysctl_long(oldp, oldlenp, newp, newlen,
		    (long *)&longhostid);
		hostid = longhostid;
		return (error);
	case KERN_CLOCKRATE:
		return (sysctl_clockrate(oldp, oldlenp));
	case KERN_BOOTTIME:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &boottime,
		    sizeof(struct timeval)));
	case KERN_INODE:
		if (newp != NULL)
			return (EPERM);
		return (sysctl_inode(oldp, oldlenp));
	case KERN_TTY:
		return (sysctl_tty(oldp, oldlenp, newp));
	case KERN_PROC:
		if (newp != NULL)
			return (EPERM);
		return (sysctl_doproc(name + 1, namelen - 1, oldp, oldlenp));
	case KERN_FILE:
		if (newp != NULL)
			return (EPERM);
		return (sysctl_dofile(name + 1, namelen - 1, oldp, oldlenp));
#ifdef GPROF
	case KERN_PROF:
		return (sysctl_doprof(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
	case KERN_NGROUPS:
		return (sysctl_rdint(oldp, oldlenp, newp, NGROUPS));
	case KERN_JOB_CONTROL:
		return (sysctl_rdint(oldp, oldlenp, newp, 1));
	case KERN_POSIX1:
	case KERN_SAVED_IDS:
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * Hardware related system variables.
 */
#ifdef UCB_METER
_Static_assert(CPUSTATES == KI_CPUSTATES, "kinfo_vmstat holds every cpu state");
_Static_assert(sizeof(struct kinfo_vmstat) == 152,
    "kinfo_vmstat packs its fields without padding");
_Static_assert(sizeof(struct kinfo_disk) == 16,
    "kinfo_disk packs its fields without padding");

/* Copy the ten vm events of a struct vmrate or struct vmsum. */
#define	KINFO_VMEVENTS(dst, src) do {					\
	(dst).ve_swtch = (src).v_swtch;					\
	(dst).ve_trap = (src).v_trap;					\
	(dst).ve_syscall = (src).v_syscall;				\
	(dst).ve_intr = (src).v_intr;					\
	(dst).ve_soft = (src).v_soft;					\
	(dst).ve_fpsim = (src).v_fpsim;					\
	(dst).ve_kbin = (src).v_kbin;					\
	(dst).ve_kbout = (src).v_kbout;					\
	(dst).ve_swpin = (src).v_swpin;					\
	(dst).ve_swpout = (src).v_swpout;				\
} while (0)

/*
 * VM_STATS: the event counters as one record (sys/sysctl.h). Every value
 * is a copy, so the record names no kernel address.
 */
static int
sysctl_vmstats(void *oldp, size_t *oldlenp, void *newp)
{
	struct kinfo_vmstat vs;
	int i;

	bzero(&vs, sizeof(vs));
	vs.vs_size = sizeof(vs);
	for (i = 0; i < CPUSTATES; i++)
		vs.vs_cp_time[i] = cp_time[i];
	KINFO_VMEVENTS(vs.vs_rate, rate);
	KINFO_VMEVENTS(vs.vs_sum, sum);
	vs.vs_cntfork = forkstat.cntfork;
	vs.vs_cntvfork = forkstat.cntvfork;
	vs.vs_sizfork = forkstat.sizfork;
	vs.vs_sizvfork = forkstat.sizvfork;
	vs.vs_nch_goodhits = nchstats.ncs_goodhits;
	vs.vs_nch_badhits = nchstats.ncs_badhits;
	vs.vs_nch_falsehits = nchstats.ncs_falsehits;
	vs.vs_nch_miss = nchstats.ncs_miss;
	vs.vs_nch_long = nchstats.ncs_long;
	vs.vs_nch_pass2 = nchstats.ncs_pass2;
	vs.vs_nch_2passes = nchstats.ncs_2passes;
	vs.vs_tk_nin = tk_nin;
	vs.vs_tk_nout = tk_nout;
	return (sysctl_rdstruct(oldp, oldlenp, newp, &vs, sizeof(vs)));
}

/*
 * HW_DISKSTATS: one record per claimed disk-statistics slot, in slot
 * order. dk_alloc() fills the slots from the first, so the claimed slots
 * are the ones before the first unnamed one; the name is copied, never
 * its address.
 */
static int
sysctl_diskstats(void *oldp, size_t *oldlenp, void *newp)
{
	struct kinfo_disk kd[DK_NDRIVE];
	int n, i;

	bzero(kd, sizeof(kd));
	for (n = 0; n < DK_NDRIVE && dk_name[n] != NULL; n++) {
		for (i = 0; i < KI_DISKNAMESZ - 1 && dk_name[n][i] != '\0'; i++)
			kd[n].kd_name[i] = dk_name[n][i];
		kd[n].kd_xfer = dk_xfer[n];
		kd[n].kd_bytes = dk_bytes[n];
	}
	return (sysctl_rdstruct(oldp, oldlenp, newp, kd, n * sizeof(kd[0])));
}
#endif

int
hw_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen __unused)
{
	extern char machine[], machine_arch[], cpu_model[];

	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case HW_MACHINE:
		return (sysctl_rdstring(oldp, oldlenp, newp, machine));
	case HW_MODEL:
		return (sysctl_rdstring(oldp, oldlenp, newp, cpu_model));
	case HW_NCPU:
		return (sysctl_rdint(oldp, oldlenp, newp, 1));	/* XXX */
	case HW_BYTEORDER:
		return (sysctl_rdint(oldp, oldlenp, newp, ENDIAN));
	case HW_PHYSMEM:
		return (sysctl_rdlong(oldp, oldlenp, newp, physmem));
#ifdef UCB_METER
	case HW_USERMEM:
		return (sysctl_rdlong(oldp, oldlenp, newp, freemem));
	case HW_DISKSTATS:
		return (sysctl_diskstats(oldp, oldlenp, newp));
#endif
	case HW_PAGESIZE:
		return (sysctl_rdint(oldp, oldlenp, newp, DEV_BSIZE));
	case HW_MACHINE_ARCH:
		return (sysctl_rdstring(oldp, oldlenp, newp, machine_arch));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

#ifdef DEBUG
/*
 * Debugging related system variables.
 */
struct ctldebug debug0, debug1, debug2, debug3, debug4;
struct ctldebug debug5, debug6, debug7, debug8, debug9;
struct ctldebug debug10, debug11, debug12, debug13, debug14;
struct ctldebug debug15, debug16, debug17, debug18, debug19;
static struct ctldebug *debugvars[CTL_DEBUG_MAXID] = {
	&debug0, &debug1, &debug2, &debug3, &debug4,
	&debug5, &debug6, &debug7, &debug8, &debug9,
	&debug10, &debug11, &debug12, &debug13, &debug14,
	&debug15, &debug16, &debug17, &debug18, &debug19,
};

int
debug_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	struct ctldebug *cdp;

	/* All sysctl names at this level are name and field. */
	if (namelen != 2)
		return (ENOTDIR);		/* overloaded */
	cdp = debugvars[name[0]];
	if (cdp->debugname == 0)
		return (EOPNOTSUPP);
	switch (name[1]) {
	case CTL_DEBUG_NAME:
		return (sysctl_rdstring(oldp, oldlenp, newp, cdp->debugname));
	case CTL_DEBUG_VALUE:
		return (sysctl_int(oldp, oldlenp, newp, newlen, cdp->debugvar));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}
#endif /* DEBUG */

/*
 * Bit of a hack.  2.11 currently uses 'short avenrun[3]' and a fixed scale
 * of 256.  In order not to break all the applications which nlist() for
 * 'avenrun' we build a local 'averunnable' structure here to return to the
 * user.  Eventually (after all applications which look up the load average
 * the old way) have been converted we can change things.
 *
 * We do not call vmtotal(), that could get rather expensive, rather we rely
 * on the 5 second update.
 *
 * The swapmap case is a 2.11BSD extension.
 */
int
vm_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen __unused)
{
	struct loadavg averunnable;		/* loadavg in resource.h */
	int len;

	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case VM_LOADAVG:
		averunnable.fscale = 256;
		averunnable.ldavg[0] = avenrun[0];
		averunnable.ldavg[1] = avenrun[1];
		averunnable.ldavg[2] = avenrun[2];
		return (sysctl_rdstruct(oldp, oldlenp, newp, &averunnable,
		    sizeof(averunnable)));
	case VM_METER:
#ifdef notsure
		vmtotal();	/* Could be expensive to do this every time. */
#endif
		return (sysctl_rdstruct(oldp, oldlenp, newp, &total,
		    sizeof(total)));
	case VM_SWAPMAP:
		/*
		 * The payload is the mapent array, which swapmap[0].m_map
		 * addresses, rather than the struct map that holds the
		 * address: a copy beginning at swapmap hands out m_map,
		 * m_limit and m_name -- three kernel addresses -- and then
		 * reads beyond the descriptor for the rest of the length.
		 * m_limit addresses the map's last usable slot
		 * (sys/kern/subr_rmap.c), so the difference is the extent of
		 * the entries a caller may see and the final slot stays the
		 * allocator's terminator. sysctl_rdstruct() reports that same
		 * length whether or not a buffer is offered, so the size query
		 * and the copy answer with one number by construction.
		 * tests/kernel/sysctl_test.c is the oracle.
		 */
		len = (char *)swapmap[0].m_limit - (char *)swapmap[0].m_map;
		return (sysctl_rdstruct(oldp, oldlenp, newp, swapmap[0].m_map,
		    len));
	case VM_NSWAP:
		return (sysctl_rdint(oldp, oldlenp, newp, (int)nswap));
#ifdef UCB_METER
	case VM_STATS:
		return (sysctl_vmstats(oldp, oldlenp, newp));
#endif
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * The eight helpers below share one contract, which sysctl(3) states: a
 * caller whose buffer is shorter than the value receives as much of it as
 * fits, *oldlenp reports the length the value needs rather than the length
 * copied, and __sysctl() turns the difference between the two into ENOMEM.
 * Each therefore copies MIN(value, *oldlenp) bytes and assigns *oldlenp
 * last, on every path that reaches a copy, so that a size query (oldp NULL)
 * and a short read answer with the same number. Refusals that stand ahead
 * of the value -- EPERM for a write to a read-only node, EINVAL for a new
 * value of the wrong length -- return before any of it.
 *
 * tests/kernel/sysctl_test.c is the oracle.
 */

/*
 * Validate parameters and get old / set new parameters
 * for an integer-valued sysctl function.
 */
int
sysctl_int(void *oldp, size_t *oldlenp, void *newp, size_t newlen, int *valp)
{
	int error = 0;

	if (newp && newlen != sizeof(int))
		return (EINVAL);
	if (oldp)
		error = copyout((caddr_t)valp, (caddr_t)oldp,
		    MIN(sizeof(int), *oldlenp));
	if (error == 0 && newp)
		error = copyin((caddr_t)newp, (caddr_t)valp, sizeof(int));
	*oldlenp = sizeof(int);
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdint(void *oldp, size_t *oldlenp, void *newp, int val)
{
	int error = 0;

	if (newp)
		return (EPERM);
	if (oldp)
		error = copyout((caddr_t)&val, oldp, MIN(sizeof(int), *oldlenp));
	*oldlenp = sizeof(int);
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a long-valued sysctl function.
 */
int
sysctl_long(void *oldp, size_t *oldlenp, void *newp, size_t newlen, long *valp)
{
	int error = 0;

	if (newp && newlen != sizeof(long))
		return (EINVAL);
	if (oldp)
		error = copyout((caddr_t)valp, (caddr_t)oldp,
		    MIN(sizeof(long), *oldlenp));
	if (error == 0 && newp)
		error = copyin((caddr_t)newp, (caddr_t)valp, sizeof(long));
	*oldlenp = sizeof(long);
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdlong(void *oldp, size_t *oldlenp, void *newp, long val)
{
	int error = 0;

	if (newp)
		return (EPERM);
	if (oldp)
		error = copyout((caddr_t)&val, oldp, MIN(sizeof(long), *oldlenp));
	*oldlenp = sizeof(long);
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a string-valued sysctl function.
 */
int
sysctl_string(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    char *str, int maxlen)
{
	size_t len;
	int error = 0;

	len = strlen(str) + 1;
	if (newp && newlen >= (size_t)maxlen)
		return (EINVAL);
	if (oldp)
		error = copyout(str, oldp, MIN(len, *oldlenp));
	if (error == 0 && newp) {
		error = copyin(newp, str, newlen);
		str[newlen] = 0;
	}
	*oldlenp = len;
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdstring(void *oldp, size_t *oldlenp, void *newp, const char *str)
{
	size_t len;
	int error = 0;

	len = strlen(str) + 1;
	if (newp)
		return (EPERM);
	if (oldp)
		error = copyout((caddr_t)str, oldp, MIN(len, *oldlenp));
	*oldlenp = len;
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a structure-oriented sysctl function.
 */
int
sysctl_struct(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    void *sp, int len)
{
	int error = 0;

	if (newp && newlen != (size_t)len)
		return (EINVAL);
	if (oldp)
		error = copyout(sp, oldp, MIN((size_t)len, *oldlenp));
	if (error == 0 && newp)
		error = copyin(newp, sp, len);
	*oldlenp = len;
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdstruct(void *oldp, size_t *oldlenp, void *newp, void *sp, int len)
{
	int error = 0;

	if (newp)
		return (EPERM);
	if (oldp)
		error = copyout(sp, oldp, MIN((size_t)len, *oldlenp));
	*oldlenp = len;
	return (error);
}

/*
 * The CPU_NLIST node: name and namelen are the components after CPU_NLIST,
 * which spell a symbol name as sys/sysctl.h defines at CTL_NLIST_BYTES.
 * __sysctl() copied them into the kernel, so the name is read from kernel
 * memory alone. table ends with a null ss_name. A malformed name answers
 * EINVAL and a name the table lacks EOPNOTSUPP.
 */
int
sysctl_nlist(const struct sysctl_symbol *table, int *name, u_int namelen,
    void *oldp, size_t *oldlenp, void *newp)
{
	char symbol[(CTL_MAXNAME - 2) * CTL_NLIST_BYTES];
	size_t length, i;

	if (newp)
		return (EPERM);
	if (namelen == 0 || namelen > CTL_MAXNAME - 2)
		return (EINVAL);
	for (i = 0; i < namelen * CTL_NLIST_BYTES; i++)
		symbol[i] = (char)(((u_int)name[i / CTL_NLIST_BYTES] >>
		    (8 * (i % CTL_NLIST_BYTES))) & 0xff);
	length = 0;
	while (length < i && symbol[length] != '\0')
		length++;
	/* The terminator falls in the last component; the rest is zero. */
	if (length == i || length / CTL_NLIST_BYTES != namelen - 1)
		return (EINVAL);
	while (++length < i)
		if (symbol[length] != '\0')
			return (EINVAL);
	for (; table->ss_name; table++)
		if (strncmp(symbol, table->ss_name, sizeof symbol) == 0)
			return (sysctl_rdint(oldp, oldlenp, NULL, table->ss_addr));
	return (EOPNOTSUPP);
}

/*
 * This one is in kern_clock.c in 4.4 but placed here for the reasons
 * given earlier (back around line 367).
 */
int
sysctl_clockrate(char *where, size_t *sizep)
{
	struct clockinfo clkinfo;

	/*
	 * Construct clockinfo structure.
	 */
	bzero(&clkinfo, sizeof clkinfo);
	clkinfo.tick = usechz;
	clkinfo.hz = hz;
	clkinfo.profhz = 0;
	clkinfo.stathz = hz;
	return (sysctl_rdstruct(where, sizep, NULL, &clkinfo, sizeof clkinfo));
}

_Static_assert(sizeof(struct kinfo_tty) == 28,
    "kinfo_tty packs its fields without padding");

/*
 * KERN_TTY: the state of the terminal the console redirector forwards to
 * (cntty(), sys/dev/cons.c), to root alone. A console whose driver keeps
 * no terminal returns EOPNOTSUPP.
 */
static int
sysctl_tty(void *oldp, size_t *oldlenp, void *newp)
{
	struct kinfo_tty kt;
	const struct tty *tp = cntty();

	if (u.u_procp->p_uid != 0)
		return (EPERM);
	if (tp == NULL)
		return (EOPNOTSUPP);
	bzero(&kt, sizeof(kt));
	kt.kt_size = sizeof(kt);
	kt.kt_rawcc = tp->t_rawq.c_cc;
	kt.kt_cancc = tp->t_canq.c_cc;
	kt.kt_outcc = tp->t_outq.c_cc;
	kt.kt_dev = tp->t_dev;
	kt.kt_flags = tp->t_flags;
	kt.kt_state = tp->t_state;
	kt.kt_pgrp = tp->t_pgrp;
	kt.kt_delct = tp->t_delct;
	kt.kt_col = tp->t_col;
	return (sysctl_rdstruct(oldp, oldlenp, newp, &kt, sizeof(kt)));
}

_Static_assert(sizeof(struct kinfo_inode) == 32,
    "kinfo_inode packs its fields without padding");

/*
 * KERN_INODE: a struct kinfo_inode for each in-core inode with a reference
 * (sys/sysctl.h), to root alone. Nothing here sleeps, so the table is read
 * in one pass.
 */
int
sysctl_inode(char *where, size_t *sizep)
{
	struct kinfo_inode ki;
	struct inode *ip;
	size_t done = 0;
	int error;

	if (u.u_procp->p_uid != 0)
		return (EPERM);
	if (where == NULL) {
		*sizep = NINODE * sizeof(ki);
		return (0);
	}
	for (ip = inode; ip < inode + NINODE; ip++) {
		if (ip->i_count == 0)
			continue;
		if (*sizep - done < sizeof(ki)) {
			*sizep = NINODE * sizeof(ki);
			return (0);
		}
		bzero(&ki, sizeof(ki));
		ki.ki_size = sizeof(ki);
		ki.ki_flag = ip->i_flag;
		ki.ki_count = ip->i_count;
		ki.ki_nlink = ip->i_nlink;
		ki.ki_dev = INODE_DEVICE(ip);
		ki.ki_number = ip->i_number;
		ki.ki_mode = ip->i_mode;
		if (!(ip->i_flag & IPIPE)) {
			ki.ki_shlockc = ip->i_shlockc;
			ki.ki_exlockc = ip->i_exlockc;
		}
		ki.ki_uid = ip->i_uid;
		if ((ip->i_mode & IFMT) == IFBLK || (ip->i_mode & IFMT) == IFCHR)
			ki.ki_isize = ip->i_rdev;
		else
			ki.ki_isize = ip->i_size;
		if ((error = copyout((caddr_t)&ki, where + done, sizeof(ki))) != 0)
			return (error);
		done += sizeof(ki);
	}
	*sizep = done;
	return (0);
}

/* True when slot p no longer holds live process pid. */
static int
proc_changed(const struct proc *p, int pid)
{
	return (p->p_stat == 0 || p->p_stat == SZOMB || p->p_pid != pid);
}

/*
 * Find the user area of live process p.  A resident user area is the kernel
 * memory p_addr names.  A nonresident one is read into a buffer, from the
 * SwapRAM pool through swapram_uarea_prefix() or from flash through
 * swap_with_buf_result(), which folds a ring address at or above m_ring_end
 * (sys/map.h) and rejects an error or a short transfer.  geteblk() and the
 * flash read sleep, and meanwhile p can exit, come back into core, or lose
 * its blocks to another image.  A read block is therefore accepted only when
 * its u_procp names p and p still holds pid, nonresident; a process that came
 * back into core is read in place.
 *
 * XXX - We rely on the fact that u_ttyp, u_ttyd, and u_ruid are all within
 * XXX - the first 1kb of the u area.  If this ever changes the logic below
 * XXX - will break (and badly).  At the present time (97/9/2) the u area
 * XXX - is 856 bytes long.
 *
 * The first DEV_BSIZE bytes hold the whole struct user (subr_capacity.c).
 * On success *upp addresses them.  *bpp is the buffer for
 * proc_uarea_release(), or NULL.  ESRCH reports a process that exited or
 * changed during the read, EIO a failed read.
 */
static int
proc_uarea(struct proc *p, const struct user **upp, struct buf **bpp)
{
	const struct user *up;
	struct buf *bp;
	int pid = p->p_pid;
	int error;

	*bpp = NULL;
	if (proc_changed(p, pid))
		return (ESRCH);
	if (p->p_flag & SLOAD)
		goto resident;
	bp = geteblk();
	*bpp = bp;
	if (proc_changed(p, pid))
		return (ESRCH);
	if (p->p_flag & SLOAD)
		goto resident;
#ifdef SWAPRAM
	if (swapram_present(p))
		error = swapram_uarea_prefix(p, bp->b_addr, DEV_BSIZE);
	else
#endif
		error = swap_with_buf_result(bp, p->p_addr, (size_t)bp->b_addr,
		    DEV_BSIZE, B_READ);
	if (proc_changed(p, pid))
		return (ESRCH);
	if (p->p_flag & SLOAD)
		goto resident;
	if (error)
		return (EIO);
	up = (const struct user *)bp->b_addr;
	if (up->u_procp != p)
		return (ESRCH);
	*upp = up;
	return (0);
resident:
	*upp = (const struct user *)p->p_addr;
	return (0);
}

static void
proc_uarea_release(struct buf *bp)
{
	if (bp == NULL)
		return;
	bp->b_flags |= B_AGE;
	brelse(bp);
}

/*
 * The real uid, controlling terminal device and terminal of process p live
 * in its user area, which proc_uarea() finds.  A zombie, or a process whose
 * user area cannot be read, reports uid -2 and no terminal.
 */
void
fill_from_u(struct proc *p, uid_t *rup, struct tty **ttp, dev_t *tdp)
{
	const struct user *up;
	struct buf *bp;
	dev_t ttyd = NODEV;
	uid_t ruid = (uid_t)-2;
	struct tty *ttyp = NULL;

	if (proc_uarea(p, &up, &bp) == 0) {
		ruid = up->u_ruid;
		ttyd = up->u_ttyd;
		ttyp = up->u_ttyp;
	}
	proc_uarea_release(bp);
	if (rup)
		*rup = ruid;
	if (ttp)
		*ttp = ttyp;
	if (tdp)
		*tdp = ttyd;
}

_Static_assert(sizeof(struct kinfo_proc) == 184,
    "struct kinfo_proc is 183 bytes and one of tail padding");
_Static_assert(offsetof(struct kinfo_proc, kp_comm) == 67,
    "kinfo_proc packs its fixed fields without interior padding");
_Static_assert(sizeof(((struct user *)0)->u_comm) == KI_COMMSZ,
    "kp_comm carries the whole command name");
_Static_assert(sizeof(((struct user *)0)->u_psargs) == KI_ARGSZ,
    "kp_args carries the whole exec argument prefix");

/*
 * Fill *kp for live or zombie process p. The process-table fields need no
 * sleep and come first. The user-area groups follow through proc_uarea(),
 * which may sleep and checks the process again after it; when it refuses,
 * the record keeps the process-table fields and names the reason.
 * Arguments go to root and to a caller whose effective uid is the
 * process's real or effective uid.
 */
static void
kinfo_fill(struct proc *p, struct kinfo_proc *kp)
{
	const struct user *up;
	struct buf *bp;
	uid_t caller = u.u_procp->p_uid;
	int error;

	bzero(kp, sizeof(*kp));
	kp->kp_size = sizeof(*kp);
	kp->kp_uid = p->p_uid;
	kp->kp_tdev = NODEV;
	kp->kp_pid = p->p_pid;
	kp->kp_ppid = p->p_ppid;
	kp->kp_flag = p->p_flag;
	kp->kp_stat = p->p_stat;
	if (p->p_stat == SZOMB) {
		kp->kp_utime = p->p_ru.ru_utime;
		kp->kp_stime = p->p_ru.ru_stime;
		kp->kp_valid = KP_VALID_TIMES;
		return;
	}
	kp->kp_pgid = p->p_pgrp;
	if (p->p_tip != NULL)
		kp->kp_tsize = p->p_tsize;
	kp->kp_dsize = p->p_dsize;
	kp->kp_ssize = p->p_ssize;
	kp->kp_pri = p->p_pri;
	kp->kp_cpu = p->p_cpu;
	kp->kp_nice = p->p_nice;
	kp->kp_valid = KP_VALID_LIVE;

	error = proc_uarea(p, &up, &bp);
	if (error != 0) {
		kp->kp_error = error;
		proc_uarea_release(bp);
		return;
	}
	kp->kp_ruid = up->u_ruid;
	kp->kp_gid = up->u_groups[0];
	kp->kp_rgid = up->u_rgid;
	kp->kp_start = up->u_start;
	kp->kp_utime = up->u_ru.ru_utime;
	kp->kp_stime = up->u_ru.ru_stime;
	kp->kp_ctime = up->u_cru.ru_utime + up->u_cru.ru_stime;
	if (up->u_ttyp != NULL) {
		kp->kp_tdev = up->u_ttyd;
		kp->kp_tpgid = up->u_ttyp->t_pgrp;
	}
	bcopy(up->u_comm, kp->kp_comm, sizeof(kp->kp_comm) - 1);
	kp->kp_valid |= KP_VALID_TIMES | KP_VALID_CRED | KP_VALID_START |
	    KP_VALID_TTY | KP_VALID_COMM;
	if (caller == 0 || caller == kp->kp_uid || caller == kp->kp_ruid) {
		bcopy(up->u_psargs, kp->kp_args, sizeof(kp->kp_args) - 1);
		kp->kp_valid |= KP_VALID_ARGS;
	}
	proc_uarea_release(bp);
}

/*
 * KERN_PROC: {selector, argument, record size} after the KERN_PROC
 * component (sys/sysctl.h). KERN_PROC_ALL returns every process and
 * KERN_PROC_PID the one whose id is the argument; the pid test runs before
 * kinfo_fill(), so a single-process query reads one user area. Every other
 * projection is the caller's, over the full table. The walk runs over the
 * slots of proc[], so it holds no list cursor across the sleeps in
 * kinfo_fill(); a process keeps its slot for its life, so one alive for the
 * whole call appears exactly once. A buffer too short for every record
 * reports the length of a full table, which __sysctl() turns into ENOMEM.
 */
int
sysctl_doproc(int *name, u_int namelen, char *where, size_t *sizep)
{
	struct kinfo_proc kp;
	size_t stride, copy, done = 0;
	int selector, slot, error;

	if (namelen != 3)
		return (EINVAL);
	selector = name[0];
	stride = (u_int)name[2];
	if ((u_int)selector > KERN_PROC_PID || stride < KI_PROC_MINSIZE ||
	    stride > DEV_BSIZE)
		return (EINVAL);
	if (where == NULL) {
		*sizep = NPROC * stride;
		return (0);
	}
	copy = stride < sizeof(kp) ? stride : sizeof(kp);
	for (slot = 0; slot < NPROC; slot++) {
		struct proc *p = &proc[slot];

		if (p->p_stat == 0 || p->p_stat == SIDL)
			continue;
		if (selector == KERN_PROC_PID && p->p_pid != name[1])
			continue;
		if (*sizep - done < stride) {
			*sizep = NPROC * stride;
			return (0);
		}
		kinfo_fill(p, &kp);
		if ((error = copyout((caddr_t)&kp, where + done, copy)) != 0)
			return (error);
		done += stride;
	}
	*sizep = done;
	return (0);
}

_Static_assert(sizeof(struct kinfo_file) == 36,
    "kinfo_file packs its fields without padding");

/* The inode fields of a KERN_FILE record. */
static void
kinfo_file_inode(const struct inode *ip, struct kinfo_file *kf)
{
	kf->kf_dev = INODE_DEVICE(ip);
	kf->kf_inum = ip->i_number;
	kf->kf_mode = ip->i_mode;
	kf->kf_isize = ip->i_size;
	kf->kf_valid |= KF_VALID_INODE;
}

/* The fields of a KERN_FILE record that come from file entry fp. */
static void
kinfo_file_entry(const struct file *fp, struct kinfo_file *kf)
{
	kf->kf_type = fp->f_type;
	kf->kf_flag = fp->f_flag;
	kf->kf_offset = fp->f_offset;
	kf->kf_count = fp->f_count;
	kf->kf_msgcount = fp->f_msgcount;
	kf->kf_valid = KF_VALID_FILE;
	if ((fp->f_type == DTYPE_INODE || fp->f_type == DTYPE_PIPE) &&
	    fp->f_data != NULL)
		kinfo_file_inode((const struct inode *)fp->f_data, kf);
}

/*
 * KERN_FILE_BYFILE: a record for each open entry of the system file table,
 * to root alone, with kf_fd the entry's index. Nothing here sleeps.
 */
static int
sysctl_filetable(size_t stride, char *where, size_t *sizep)
{
	struct kinfo_file kf;
	size_t copy, need = NFILE * stride, done = 0;
	int slot, error;

	if (u.u_procp->p_uid != 0)
		return (EPERM);
	if (where == NULL) {
		*sizep = need;
		return (0);
	}
	copy = stride < sizeof(kf) ? stride : sizeof(kf);
	for (slot = 0; slot < NFILE; slot++) {
		if (file[slot].f_count == 0)
			continue;
		if (*sizep - done < stride) {
			*sizep = need;
			return (0);
		}
		bzero(&kf, sizeof(kf));
		kf.kf_size = sizeof(kf);
		kf.kf_pid = -1;
		kf.kf_fd = slot;
		kinfo_file_entry(&file[slot], &kf);
		if ((error = copyout((caddr_t)&kf, where + done, copy)) != 0)
			return (error);
		done += stride;
	}
	*sizep = done;
	return (0);
}

/*
 * KERN_FILE: {selector, argument, record size} after the KERN_FILE
 * component (sys/sysctl.h). The descriptor table lives in the user area,
 * which proc_uarea() reads and checks after its sleeps. From its return to
 * proc_uarea_release() nothing sleeps -- copyout() is a bounds check and a
 * copy -- so the process cannot close a descriptor meanwhile, and each
 * u_ofile entry holds a reference that keeps its file and inode in place.
 * A buffer too short for every record reports the size-query length, which
 * __sysctl() turns into ENOMEM.
 */
int
sysctl_dofile(int *name, u_int namelen, char *where, size_t *sizep)
{
	struct kinfo_file kf;
	const struct user *up;
	struct buf *bp;
	struct proc *p = NULL;
	uid_t caller = u.u_procp->p_uid;
	size_t stride, copy, need, done = 0;
	int slot, fd, error;

	if (namelen != 3)
		return (EINVAL);
	stride = (u_int)name[2];
	if ((name[0] != KERN_FILE_BYPID && name[0] != KERN_FILE_BYFILE) ||
	    stride < KI_FILE_MINSIZE || stride > DEV_BSIZE)
		return (EINVAL);
	if (name[0] == KERN_FILE_BYFILE)
		return (sysctl_filetable(stride, where, sizep));
	need = (NOFILE + 1) * stride;
	if (where == NULL) {
		*sizep = need;
		return (0);
	}
	for (slot = 0; slot < NPROC; slot++) {
		if (proc[slot].p_stat != 0 && proc[slot].p_stat != SIDL &&
		    proc[slot].p_pid == name[1]) {
			p = &proc[slot];
			break;
		}
	}
	if (p == NULL)
		return (ESRCH);
	if (p->p_stat == SZOMB) {
		*sizep = 0;
		return (0);
	}
	error = proc_uarea(p, &up, &bp);
	if (error != 0)
		goto release;
	if (caller != 0 && caller != p->p_uid && caller != up->u_ruid) {
		error = EPERM;
		goto release;
	}
	copy = stride < sizeof(kf) ? stride : sizeof(kf);
	for (fd = KF_FD_CWD; fd < NOFILE; fd++) {
		const struct file *fp = NULL;

		if (fd == KF_FD_CWD ? up->u_cdir == NULL :
		    (fp = up->u_ofile[fd]) == NULL)
			continue;
		if (*sizep - done < stride) {
			*sizep = need;
			goto release;
		}
		bzero(&kf, sizeof(kf));
		kf.kf_size = sizeof(kf);
		kf.kf_pid = p->p_pid;
		kf.kf_fd = fd;
		if (fp == NULL) {
			kf.kf_type = DTYPE_INODE;
			kinfo_file_inode(up->u_cdir, &kf);
		} else
			kinfo_file_entry(fp, &kf);
		if ((error = copyout((caddr_t)&kf, where + done, copy)) != 0)
			goto release;
		done += stride;
	}
	*sizep = done;
release:
	proc_uarea_release(bp);
	return (error);
}
