/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)kern_pdp.c	1.4 (2.11BSD) 1998/5/12
 */

#include <sys/param.h>
#include <sys/storage_stats.h>
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
#include <machine/board.h>
#include <machine/mpuvar.h>
#include <machine/watchdog.h>
#ifdef DEV_ADC
#include <rp2040/dev/adc.h>
#include <machine/gpio.h>
#endif
#include <rp2040/dev/flash.h>
#ifdef CORE1
#include <rp2040/rp2040/core1.h>
#endif
#ifdef SWAPRAM
#include <machine/swapram.h>
#endif
#ifdef UARTUSB_ENABLED
#include <rp2040/dev/usb.h>
#endif
#ifdef PICOCALC_ENABLED
#include <machine/picocalcvar.h>
#ifdef DEV_PCM
#include <rp2040/dev/picopcm.h>
#endif
#endif

/*
 * Kernel symbol name list.
 */
static const struct sysctl_symbol nlist[] = {
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
 * The XIP cache counters, datasheet 2.6.3.1, and the bus fabric performance
 * counters and their event selects, datasheet 2.1.1.2.
 */
#define	XIP_CTRL_BASE		0x14000000UL
#define	XIP_CTR_HIT		0x0c
#define	XIP_CTR_ACC		0x10
#define	BUSCTRL_BASE		0x40030000UL
#define	BUSCTRL_PERFCTR0	0x08	/* PERFCTRn at 8n, PERFSELn 4 above. */
#define	BUSCTRL_PERFSEL_ROM	19	/* Highest event that counts. */
#define	BUSCTRL_PERFSEL_NONE	31	/* Reset value: no event. */

/*
 * machdep.xip_acc, xip_hit, bus_perfctr0-3 and bus_perfsel0-3, numbered from
 * 0 in that order. Each is one hardware register read as an int. A write to a
 * counter clears it, which is all the hardware accepts; a write to a select
 * takes an event number or BUSCTRL_PERFSEL_NONE.
 */
static int
hwcount_sysctl(int which, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	volatile u_int *reg;
	int error, val;

	if (which == 0)
		reg = (volatile u_int *)(XIP_CTRL_BASE + XIP_CTR_ACC);
	else if (which == 1)
		reg = (volatile u_int *)(XIP_CTRL_BASE + XIP_CTR_HIT);
	else
		reg = (volatile u_int *)(BUSCTRL_BASE + BUSCTRL_PERFCTR0 +
		    8 * ((which - 2) & 3) + (which >= 6 ? 4 : 0));
	val = (int)*reg;
	error = sysctl_int(oldp, oldlenp, newp, newlen, &val);
	if (error != 0 || newp == NULL)
		return error;
	if (which < 6) {
		*reg = 0;
		return 0;
	}
	if ((u_int)val > BUSCTRL_PERFSEL_ROM && val != BUSCTRL_PERFSEL_NONE)
		return EINVAL;
	*reg = (u_int)val;
	return 0;
}

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
		int (*routine)(int, int);
		int arg1;
		int arg2;
	} *uap = (struct a *)u.u_arg;

	int s;

	if (!suser())
		return;
	/*
	 * The routine runs at kernel privilege, which no write guard of the
	 * level covers; a raised level refuses the call.
	 */
	if (securelevel > 0) {
		u.u_error = EPERM;
		return;
	}
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

/* The ABI reserves ufetch, but RP2040 exposes no privileged fetch service. */
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
	int error, i, khz;
	char message[KERNEL_ERRMSG_MAX];
	u_int length;
#ifdef UARTUSB_ENABLED
	u_int count;
#endif
	dev_t dev;

	/* Every sysctl name at this level except mpu is terminal. */
	switch (name[0]) {
	case CPU_STORAGE_STATS:
		if (namelen != 1)
			return ENOTDIR;
#ifdef STORAGE_STATS
		{
			struct storage_stats stats;

			storage_snapshot(&stats);
			return sysctl_rdstruct(oldp, oldlenp, newp, &stats,
			    sizeof stats);
		}
#else
		return EOPNOTSUPP;
#endif
#ifdef STORAGE_STATS
	/* Without STORAGE_STATS the names fall to the default, EOPNOTSUPP. */
	case CPU_FLASH_RESET_PROGRAM:
	case CPU_FLASH_RESET_ERASE:
		if (namelen != 1)
			return ENOTDIR;
		/* A write needs root: __sysctl() refuses any other writer. */
		i = (int)flash_reset_hook_count(name[0] ==
		    CPU_FLASH_RESET_ERASE);
		error = sysctl_int(oldp, oldlenp, newp, newlen, &i);
		if (error != 0 || newp == NULL)
			return error;
		if (i < 0)
			return EINVAL;
		flash_reset_hook_arm(name[0] == CPU_FLASH_RESET_ERASE,
		    (u_int)i);
		return 0;

	case CPU_FLASH_SWAP_WEAR:
		if (namelen != 1)
			return ENOTDIR;
		/* Each counter is one halfword store under splhigh(). */
		return sysctl_rdstruct(oldp, oldlenp, newp, flash_swap_wear,
		    sizeof flash_swap_wear);
#endif
	case CPU_MPU:
		return mpu_sysctl(name + 1, namelen - 1, oldp, oldlenp, newp,
		    newlen);
#ifdef PICOCALC_ENABLED
	case CPU_PICOCALC:
		return picocalc_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen);

#ifdef DEV_PCM
	case CPU_PCM:
		{
			struct cpu_pcm_info pcm_info;

			if (namelen != 1)
				return ENOTDIR;
			if (board_id() != BOARD_ID_PICOCALC)
				return EOPNOTSUPP;
			pcm_info.rate = PCM_RATE;
			pcm_info.channels = PCM_CHANNELS;
			pcm_info.bits = PCM_BITS;
			pcm_info.ring = PCM_RING - 1;
			pcm_info.queued = pcm_queued();
			pcm_info.open = picoaudio_pcm != 0;
			return sysctl_rdstruct(oldp, oldlenp, newp, &pcm_info,
			    sizeof pcm_info);
		}
#endif /* DEV_PCM */
#endif
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

	case CPU_WATCHDOG_ARG2:
		if (namelen != 1)
			return ENOTDIR;
		i = (int)watchdog_last_arg2;
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);

	case CPU_FLASH_UNIQUE_HI:
	case CPU_FLASH_UNIQUE_LO:
	case CPU_FLASH_JEDEC:
		{
			const u_char *jedec, *unique;

			if (namelen != 1)
				return ENOTDIR;
			if (flash_identity(&jedec, &unique) != 0)
				return EIO;
			if (name[0] == CPU_FLASH_JEDEC) {
				i = (int)((u_int)jedec[0] << 16 |
				    (u_int)jedec[1] << 8 | jedec[2]);
				return sysctl_rdstruct(oldp, oldlenp, newp, &i,
				    sizeof i);
			}
			if (name[0] == CPU_FLASH_UNIQUE_LO)
				unique += 4;
			i = (int)((u_int)unique[0] << 24 |
			    (u_int)unique[1] << 16 | (u_int)unique[2] << 8 |
			    unique[3]);
			return sysctl_rdstruct(oldp, oldlenp, newp, &i,
			    sizeof i);
		}

	case CPU_IMAGE_LENGTH:
	case CPU_IMAGE_CRC:
	case CPU_IMAGE_CRC_STORED:
		if (namelen != 1)
			return ENOTDIR;
		i = (int)(name[0] == CPU_IMAGE_LENGTH ? image_length :
		    name[0] == CPU_IMAGE_CRC ? image_crc : image_crc_stored);
		return sysctl_rdstruct(oldp, oldlenp, newp, &i, sizeof i);
#ifdef DEV_ADC
	case CPU_ADC_TEMP:
	case CPU_ADC_VSYS:
		{
			u_int raw;

			if (namelen != 1)
				return ENOTDIR;
			if (adc_read(name[0] == CPU_ADC_TEMP ? ADC_INPUT_TEMP :
			    ADC_INPUT_VSYS, &raw) != 0)
				return EIO;
			i = name[0] == CPU_ADC_TEMP ?
			    adc_temp_millicelsius(raw) :
			    (int)adc_vsys_millivolts(raw);
			return sysctl_rdstruct(oldp, oldlenp, newp, &i,
			    sizeof i);
		}
	case CPU_ADC:
		{
			u_int raw;

			if (namelen != 2)
				return ENOTDIR;
			/* Pin-owner refusal: board pins and the GPIO device. */
			error = adc_input_check(gpio_board_pins(board_id()),
#ifdef DEV_GPIO
			    gpio_owned,
#else
			    0,
#endif
			    name[1]);
			if (error != 0)
				return error;
			if (adc_read(name[1], &raw) != 0)
				return EIO;
			i = (int)raw;
			return sysctl_rdstruct(oldp, oldlenp, newp, &i,
			    sizeof i);
		}
#endif
#ifdef CORE1
	case CPU_CORE1:
	case CPU_CORE1_RESTARTS:
	case CPU_CORE1_STALLS:
	case CPU_CORE1_GAP_US:
	case CPU_CORE1_PC:
	case CPU_CORE1_LR:
	case CPU_CORE1_FAULTS:
	case CPU_CORE1_STALL_MS:
		if (namelen != 1)
			return ENOTDIR;
		return core1_sysctl(name[0] - CPU_CORE1, oldp, oldlenp, newp,
		    newlen);
#endif

	case CPU_CONSDEV:
		{
			int console_major, console_minor;

		if (namelen != 1)
			return ENOTDIR;
		board_console(&console_major, &console_minor);
		dev = makedev(console_major, console_minor);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof dev);
		}
#if defined(NTMSCP) && NTMSCP > 0
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
		length = name[1] < 1 ? 0 :
		    kernel_errmsg((u_int)name[1], message);
		if (length == 0)
			return EOPNOTSUPP;
		return sysctl_string(oldp, oldlenp, 0, 0, message, length);

	case CPU_NLIST:
		return sysctl_nlist(nlist, name + 1, namelen - 1, oldp,
		    oldlenp, newp);

	case CPU_XIP_ACC:
	case CPU_XIP_HIT:
	case CPU_BUS_PERFCTR0:
	case CPU_BUS_PERFCTR1:
	case CPU_BUS_PERFCTR2:
	case CPU_BUS_PERFCTR3:
	case CPU_BUS_PERFSEL0:
	case CPU_BUS_PERFSEL1:
	case CPU_BUS_PERFSEL2:
	case CPU_BUS_PERFSEL3:
		if (namelen != 1)
			return ENOTDIR;
		return hwcount_sysctl(name[0] - CPU_XIP_ACC, oldp, oldlenp,
		    newp, newlen);

	case CPU_XIP_CACHE:
		if (namelen != 1)
			return ENOTDIR;
		i = flash_xip_cache(-1);
		error = sysctl_int(oldp, oldlenp, newp, newlen, &i);
		if (error == 0 && newp != NULL)
			(void)flash_xip_cache(i != 0);
		return error;

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

	case CPU_USB_TX_PEAK:
	case CPU_USB_TX_DROPPED:
		if (namelen != 1)
			return ENOTDIR;
		count = name[0] == CPU_USB_TX_PEAK ? usb_tx_peak :
		    usb_tx_dropped;
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
#endif	/* SWAPRAM */

	default:
		return EOPNOTSUPP;
	}
	/* NOTREACHED */
}
