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
#include <machine/cpu.h>
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
#   include <sys/pty.h>
#endif

/*
 * Kernel symbol name list.
 */
static const struct {
    const char *name;
    int addr;
} nlist[] = {
    { "_boottime",      (int) &boottime     },  /* vmstat */
    { "_cnttys",        (int) &cnttys       },  /* pstat */
    { "_cp_time",       (int) &cp_time      },  /* iostat  vmstat */
    { "_dk_busy",       (int) &dk_busy      },  /* iostat */
    { "_dk_name",       (int) &dk_name      },  /* iostat  vmstat */
    { "_dk_ndrive",     (int) &dk_ndrive    },  /* iostat  vmstat */
    { "_dk_unit",       (int) &dk_unit      },  /* iostat  vmstat */
    { "_dk_bytes",      (int) &dk_bytes     },  /* iostat */
    { "_dk_xfer",       (int) &dk_xfer      },  /* iostat  vmstat */
    { "_file",          (int) &file         },  /* pstat */
    { "_forkstat",      (int) &forkstat     },  /* vmstat */
#ifdef UCB_METER
    { "_freemem",       (int) &freemem      },  /* vmstat */
#endif
    { "_hz",            (int) &hz           },  /* ps */
    { "_inode",         (int) &inode        },  /* pstat */
    { "_ipc",           (int) &ipc          },  /* ps */
    { "_lbolt",         (int) &lbolt        },  /* ps */
    { "_memlock",       (int) &memlock      },  /* ps */
    { "_nchstats",      (int) &nchstats     },  /* vmstat */
    { "_nproc",         (int) &nproc        },  /* ps      pstat */
    { "_nswap",         (int) &nswap        },  /* pstat */
    { "_proc",          (int) &proc         },  /* ps      pstat */
    { "_runin",         (int) &runin        },  /* ps */
    { "_runout",        (int) &runout       },  /* ps */
    { "_selwait",       (int) &selwait      },  /* ps */
    { "_swapmap",       (int) &swapmap      },  /* pstat */
    { "_tk_nin",        (int) &tk_nin       },  /* iostat */
    { "_tk_nout",       (int) &tk_nout      },  /* iostat */
    { "_total",         (int) &total        },  /* vmstat */
    { "_u",             (int) &u            },  /* ps */
#ifdef PTY_ENABLED
    { "_npty",          (int) &npty         },  /* pstat */
    { "_pt_tty",        (int) &pt_tty       },  /* pstat */
#endif
#ifdef UCB_METER
    { "_rate",          (int) &rate         },  /* vmstat */
    { "_sum",           (int) &sum          },  /* vmstat */
#endif
    { "_bdevsw",        (int) &bdevsw       },  /* devupdate */
    { "_cdevsw",        (int) &cdevsw       },  /* devupdate */
    { "_nblkdev",       (int) &nblkdev      },  /* devupdate */
    { "_nchrdev",       (int) &nchrdev      },  /* devupdate */
    { 0, 0 },
};

/*
 * ucall allows user level code to call various kernel functions.
 * Autoconfig uses it to call the probe and attach routines of the
 * various device drivers.
 */
void
ucall()
{
    register struct a {
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
    u.u_rval = (*uap->routine) (uap->arg1, uap->arg2);
    splx(s);
}

/*
 * Fetch the word at addr from flash memory or i/o port.
 * This system call is required on PIC32 because in user mode
 * the access to flash memory region is not allowed.
 */
void
ufetch()
{
    unsigned addr = *(unsigned*) u.u_arg & ~3;

    /* Check root privileges */
    if (! suser())
        return;

    /* Low memory address - assume peripheral i/o space.  */
    if (addr < 0x90000)
        addr += 0xbf800000;

    /* Check address */
    if (! (addr >= 0x9d000000 && addr < 0x9d000000 + FLASH_SIZE) &&
        ! (addr >= 0xbd000000 && addr < 0xbd000000 + FLASH_SIZE) &&

        /* Boot flash memory */
        ! (addr >= 0x9fc00000 && addr < 0x9fc00000 + 12*1024) &&
        ! (addr >= 0xbfc00000 && addr < 0xbfc00000 + 12*1024) &&

        /* Peripheral registers */
        ! (addr >= 0xbf800000 && addr < 0xbf810000) &&
        ! (addr >= 0xbf880000 && addr < 0xbf890000)) {
        u.u_error = EFAULT;
        return;
    }
    u.u_rval = *(unsigned*) addr;
}

/*
 * Store the word at addr of i/o port.
 */
void
ustore()
{
    register struct a {
        unsigned addr;
        unsigned value;
    } *uap = (struct a *)u.u_arg;
    unsigned addr = uap->addr & ~3;

    /* Check root privileges */
    if (! suser())
        return;

    /* Low memory address - assume peripheral i/o space.  */
    if (addr < 0x90000)
        addr += 0xbf800000;

    /* Check address */
    if (! (addr >= 0xbf800000 && addr < 0xbf810000) &&
        ! (addr >= 0xbf880000 && addr < 0xbf890000)) {
        u.u_error = EFAULT;
        return;
    }
    *(unsigned*) addr = uap->value;
}

/*
 * This was moved here when the TMSCP portion was added.  At that time it
 * became (even more) system specific and didn't belong in kern_sysctl.c
 */
int
cpu_sysctl (name, namelen, oldp, oldlenp, newp, newlen)
    int *name;
    u_int namelen;
    void *oldp;
    size_t *oldlenp;
    void *newp;
    size_t newlen;
{
    int i, khz;
    const char *message;
    dev_t dev;

    switch (name[0]) {
    case CPU_CONSDEV:
        if (namelen != 1)
            return ENOTDIR;
        dev = makedev(CONS_MAJOR, CONS_MINOR);
        return sysctl_rdstruct (oldp, oldlenp, newp, &dev, sizeof dev);
#if NTMSCP > 0
    case CPU_TMSCP:
        /* All sysctl names at this level are terminal */
        if (namelen != 2)
            return ENOTDIR;
        switch (name[1]) {
        case TMSCP_CACHE:
            return sysctl_int (oldp, oldlenp, newp,
                newlen, &tmscpcache);
        case TMSCP_PRINTF:
            return sysctl_int (oldp, oldlenp, newp,
                newlen, &tmscpprintf);
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
            (char*) message, 1 + strlen(message));

    case CPU_NLIST:
        for (i=0; nlist[i].name; i++) {
            if (strncmp (newp, nlist[i].name, newlen) == 0) {
                int addr = nlist[i].addr;
                int error = 0;

                /*
                 * The node answers with an address and takes the symbol
                 * name as its new value, so it carries the length contract
                 * of the helpers in sys/kern/kern_sysctl.c rather than
                 * calling one: the prefix that fits is copied, the length
                 * the address needs is reported whether or not a buffer was
                 * offered, and __sysctl() decides ENOMEM.
                 */
                if (oldp)
                    error = copyout ((caddr_t) &addr, (caddr_t) oldp,
                        MIN (sizeof(int), *oldlenp));
                *oldlenp = sizeof(int);
                return error;
            }
        }
        return EOPNOTSUPP;

    case CPU_TIMO_CMD:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_cmd);
    case CPU_TIMO_SEND_OP:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_send_op);
    case CPU_TIMO_SEND_CSD:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_send_csd);
    case CPU_TIMO_READ:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_read);
    case CPU_TIMO_WAIT_CMD:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_wait_cmd);
    case CPU_TIMO_WAIT_WDATA:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_wait_wdata);
    case CPU_TIMO_WAIT_WDONE:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_wait_wdone);
    case CPU_TIMO_WAIT_WSTOP:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_wait_wstop);
    case CPU_TIMO_WAIT_WIDLE:
        return sysctl_int(oldp, oldlenp, newp, newlen, &sd_timo_wait_widle);

    case CPU_FREQ_KHZ:
        if (namelen != 1)
            return ENOTDIR;
        khz = CPU_KHZ;
        return sysctl_rdstruct (oldp, oldlenp, newp, &khz, sizeof khz);
    case CPU_BUS_KHZ:
        if (namelen != 1)
            return ENOTDIR;
        khz = BUS_KHZ;
        return sysctl_rdstruct (oldp, oldlenp, newp, &khz, sizeof khz);

    default:
        return EOPNOTSUPP;
    }
    /* NOTREACHED */
}
