/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)kern_pdp.c	1.4 (2.11BSD) 1998/5/12
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/dk.h>
#include <sys/vmsystm.h>
#include <sys/ptrace.h>
#include <sys/namei.h>
#include <sys/vmmeter.h>
#include <sys/map.h>
#include <sys/conf.h>
#ifdef PTY_ENABLED
#include <sys/pty.h>
#endif

#include <machine/cpu.h>
#include <machine/mpuvar.h>
#include <machine/watchdog.h>
#ifdef SWAPRAM
#include <machine/swapram.h>
#endif
#ifdef UARTUSB_ENABLED
#include <rp2040/dev/usb.h>
#endif

/*
 * Kernel symbol name list.
 */
static const struct {
	const char *name;
	int addr;
} nlist[] = {
	{ "_boottime",	(int)&boottime	},	/* vmstat */
	{ "_cnttys",	(int)&cnttys	},	/* pstat */
	{ "_cp_time",	(int)&cp_time	},	/* iostat	vmstat */
	{ "_dk_busy",	(int)&dk_busy	},	/* iostat */
	{ "_dk_name",	(int)&dk_name	},	/* iostat	vmstat */
	{ "_dk_ndrive",	(int)&dk_ndrive	},	/* iostat	vmstat */
	{ "_dk_unit",	(int)&dk_unit	},	/* iostat	vmstat */
	{ "_dk_bytes",	(int)&dk_bytes	},	/* iostat */
	{ "_dk_xfer",	(int)&dk_xfer	},	/* iostat	vmstat */
	{ "_file",	(int)&file	},	/* pstat */
	{ "_forkstat",	(int)&forkstat	},	/* vmstat */
#ifdef UCB_METER
	{ "_freemem",	(int)&freemem	},	/* vmstat */
#endif
	{ "_hz",	(int)&hz	},	/* ps */
	{ "_inode",	(int)&inode	},	/* pstat */
	{ "_ipc",	(int)&ipc	},	/* ps */
	{ "_lbolt",	(int)&lbolt	},	/* ps */
	{ "_memlock",	(int)&memlock	},	/* ps */
	{ "_nchstats",	(int)&nchstats	},	/* vmstat */
	{ "_nproc",	(int)&nproc	},	/* ps		pstat */
	{ "_nswap",	(int)&nswap	},	/* pstat */
	{ "_proc",	(int)&proc	},	/* ps		pstat */
	{ "_runin",	(int)&runin	},	/* ps */
	{ "_runout",	(int)&runout	},	/* ps */
	{ "_selwait",	(int)&selwait	},	/* ps */
	{ "_swapmap",	(int)&swapmap	},	/* pstat */
	{ "_tk_nin",	(int)&tk_nin	},	/* iostat */
	{ "_tk_nout",	(int)&tk_nout	},	/* iostat */
	{ "_total",	(int)&total	},	/* vmstat */
	{ "_u",		(int)&u		},	/* ps */
#ifdef PTY_ENABLED
	{ "_npty",	(int)&npty	},	/* pstat */
	{ "_pt_tty",	(int)&pt_tty	},	/* pstat */
#endif
#ifdef UCB_METER
	{ "_rate",	(int)&rate	},	/* vmstat */
	{ "_sum",	(int)&sum	},	/* vmstat */
#endif
	{ "_bdevsw",	(int)&bdevsw	},	/* devupdate */
	{ "_cdevsw",	(int)&cdevsw	},	/* devupdate */
	{ "_nblkdev",	(int)&nblkdev	},	/* devupdate */
	{ "_nchrdev",	(int)&nchrdev	},	/* devupdate */
	{ 0,		0		},
};

/*
 * ucall allows user level code to call various kernel functions.
 * Autoconfig uses it to call the probe and attach routines of the
 * various device drivers.
 */
void
ucall(void)
{
	struct a {
		int priority;
		int (*routine)();
		int arg1;
		int arg2;
	} *uap = (struct a *)u.u_arg;

	int s;

	if (!suser())
		return;
	switch (uap->priority) {
	case 0:
		s = spl0();
		break;
	default:
		s = splhigh();
		break;
	}
	u.u_rval = (*uap->routine)(uap->arg1, uap->arg2);
	splx(s);
}

/*
 * Fetch the word at addr from flash memory or i/o port.
 * This system call is required on PIC32 because in user mode
 * the access to flash memory region is not allowed.
 */
void
ufetch(void)
{
	/* Check root privileges */
	if (!suser())
		return;

	/* XXX Not implemented */
	u.u_error = EOPNOTSUPP;
	return;
}

/*
 * Store the word at addr of i/o port.
 */
void
ustore(void)
{
	/* Check root privileges */
	if (!suser())
		return;

	/* XXX Not implemented */
	u.u_error = EOPNOTSUPP;
	return;
}

void
sc_msec(void)
{
	/* XXX Not implemented */
	u.u_rval = 0;
}

/*
 * This was moved here when the TMSCP portion was added.  At that time it
 * became (even more) system specific and didn't belong in kern_sysctl.c
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int i, khz;
	const char *message;
#ifdef UARTUSB_ENABLED
	u_int count;
#endif
	dev_t dev;

	/* Every sysctl name at this level except mpu is terminal. */
	switch (name[0]) {
	case CPU_MPU:
		return mpu_sysctl(name + 1, namelen - 1, oldp, oldlenp, newp,
		    newlen);
	case CPU_WATCHDOG_REASON:
		if (namelen != 1)
			return ENOTDIR;
		i = (int)watchdog_last_reason;
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);

	case CPU_WATCHDOG_SITE:
		if (namelen != 1)
			return ENOTDIR;
		i = (int)watchdog_last_site;
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);

	case CPU_WATCHDOG_ARG:
		if (namelen != 1)
			return ENOTDIR;
		i = (int)watchdog_last_arg;
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);

	case CPU_CONSDEV:
		if (namelen != 1)
			return ENOTDIR;
		dev = makedev(CONS_MAJOR, CONS_MINOR);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof dev);
#if NTMSCP > 0
	case CPU_TMSCP:
		if (namelen != 2)
			return ENOTDIR;
		switch (name[1]) {
		case TMSCP_CACHE:
			return sysctl_int(oldp, oldlenp, newp, newlen,
			    &tmscpcache);
		case TMSCP_PRINTF:
			return sysctl_int(oldp, oldlenp, newp, newlen,
			    &tmscpprintf);
		default:
		}
#endif
	case CPU_ERRMSG:
		if (namelen != 2)
			return ENOTDIR;
		message = name[1] < 1 ? NULL : kernel_errmsg((u_int)name[1]);
		if (message == NULL)
			return EOPNOTSUPP;
		return sysctl_string(oldp, oldlenp, 0, 0,
		    (char *)message, 1 + strlen(message));

	case CPU_NLIST:
		for (i = 0; nlist[i].name; i++) {
			if (strncmp(newp, nlist[i].name, newlen) == 0) {
				int addr = nlist[i].addr;
				int error = 0;

				/*
				 * The node answers with an address and takes
				 * the symbol name as its new value, so it
				 * carries the length contract of the helpers
				 * in sys/kern/kern_sysctl.c rather than
				 * calling one: the prefix that fits is
				 * copied, the length the address needs is
				 * reported whether or not a buffer was
				 * offered, and __sysctl() decides ENOMEM.
				 */
				if (oldp)
					error = copyout((caddr_t)&addr,
					    (caddr_t)oldp,
					    MIN(sizeof(int), *oldlenp));
				*oldlenp = sizeof(int);
				return error;
			}
		}
		return EOPNOTSUPP;

	case CPU_FREQ_KHZ:
		if (namelen != 1)
			return ENOTDIR;
		khz = CPU_KHZ;
		return sysctl_rdstruct(oldp, oldlenp, newp, &khz, sizeof khz);

	case CPU_BUS_KHZ:
		if (namelen != 1)
			return ENOTDIR;
		khz = BUS_KHZ;
		return sysctl_rdstruct(oldp, oldlenp, newp, &khz, sizeof khz);

#ifdef UARTUSB_ENABLED
	/*
	 * RP2040-E15 accounting. usb_e15_deferred counts the bulk IN arms the
	 * guard held until the next SOF; usb_e15_bulkin_arms counts every bulk
	 * IN arm, so the ratio says how much of the console's output met the
	 * critical window.
	 */
	case CPU_USB_E15_DEFERRED:
		if (namelen != 1)
			return ENOTDIR;
		count = usb_e15_deferred;
		return sysctl_rdstruct(oldp, oldlenp, newp, &count,
		    sizeof count);

	case CPU_USB_BULKIN_ARMS:
		if (namelen != 1)
			return ENOTDIR;
		count = usb_e15_bulkin_arms;
		return sysctl_rdstruct(oldp, oldlenp, newp, &count,
		    sizeof count);

	case CPU_USB_SERVICE_REENTERED:
		if (namelen != 1)
			return ENOTDIR;
		count = usb_service_reentered;
		return sysctl_rdstruct(oldp, oldlenp, newp, &count,
		    sizeof count);

	case CPU_USB_TX_RECOVERED:
		if (namelen != 1)
			return ENOTDIR;
		count = usb_tx_recovered;
		return sysctl_rdstruct(oldp, oldlenp, newp, &count,
		    sizeof count);
#endif	/* UARTUSB_ENABLED */
#ifdef SWAPRAM
	case CPU_SWAPRAM_EVACUATE:
		if (namelen != 1)
			return ENOTDIR;
		i = sysctl_int(oldp, oldlenp, newp, newlen, &swapram_evac);
		if (i == 0 && newp != NULL) {
			/* Any write posts a request; the swapper answers. */
			swapram_evac = SWAPRAM_EVAC_PENDING;
			wakeup((caddr_t)&runout);
			wakeup((caddr_t)&runin);
		}
		return i;
	case CPU_SWAPRAM_IMAGES:
		if (namelen != 1)
			return ENOTDIR;
		i = swapram_images();
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);
	case CPU_SWAPRAM_EPOCH:
		if (namelen != 1)
			return ENOTDIR;
		i = swapram_epoch;
		if (newp != NULL) {
			int want = i;

			i = sysctl_int(oldp, oldlenp, newp, newlen, &want);
			if (i == 0)
				swapram_set_epoch(want == SWAPRAM_LARGE ?
				    SWAPRAM_LARGE : SWAPRAM_SMALL);
			return i;
		}
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);
	case CPU_SWAPRAM_LARGE:
		if (namelen != 1)
			return ENOTDIR;
		i = swapram_nlarge();
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);
#endif	/* SWAPRAM */

	default:
		return EOPNOTSUPP;
	}
	/* NOTREACHED */
}
