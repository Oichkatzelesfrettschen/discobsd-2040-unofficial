/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)cpu.h	1.5 (2.11BSD GTE) 1998/4/3
 */

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_CONSDEV		1	/* dev_t: console terminal device */
#define	CPU_ERRMSG		2	/* get error message by errno */
#define	CPU_NLIST		3	/* get name address */
#define	CPU_FREQ_KHZ		4	/* processor clock in kHz */
#define	CPU_BUS_KHZ		5	/* i/o bus clock in kHz */
/*
 * The STM32 tree numbers its memory protection unit node CPU_MPU 6; here 6
 * and 7 carry USB accounting and the node is 12, below, so a mib compiled
 * for one port does not read as the other's.
 *
 * The two counters below come from the USB device driver's RP2040-E15 guard,
 * arch/rp2040/dev/usb.c. A kernel configured without uartusb does not link the
 * driver and answers both with EOPNOTSUPP.
 */
#define	CPU_USB_E15_DEFERRED	6	/* int: bulk IN arms the guard held */
#define	CPU_USB_BULKIN_ARMS	7	/* int: bulk IN arms attempted */
/*
 * The compressed swap tier, arch/rp2040/rp2040/swapram.c. Writing any
 * value to swapram_evacuate asks the swapper to move every pool image to
 * flash; reading gives SWAPRAM_EVAC_PENDING until it has, then DONE or
 * NOFLASH. A kernel without SWAPRAM answers both with EOPNOTSUPP.
 */
#define	CPU_SWAPRAM_EVACUATE	8	/* int: evacuation request and result */
#define	CPU_SWAPRAM_IMAGES	9	/* int: images the pool holds */
/* Machdep identifiers 10 and 11 remain reserved after window-epoch removal. */
/*
 * The Cortex-M0+ MPU as arch/rp2040/rp2040/mpu.c programmed it and read
 * it back; machine/mpuvar.h names the leaves.
 */
#define	CPU_MPU			12	/* node: memory protection unit */
/*
 * Console robustness accounting from arch/rp2040/dev/usb.c. Both read zero
 * on a healthy boot: the first counts services refused to a caller that
 * interrupted the driver, the second bulk IN buffers released after a
 * completion that never arrived.
 */
#define	CPU_USB_SERVICE_REENTERED 13	/* int: nested services refused */
#define	CPU_USB_TX_RECOVERED	14	/* int: bulk IN buffers recovered */
/*
 * What the previous reset left in the watchdog's scratch registers, from
 * arch/rp2040/rp2040/machdep.c. The console ring does not survive long
 * enough for a host to read the boot line, so the report is readable here
 * for the life of the boot. machine/watchdog.h names the sites.
 */
#define	CPU_WATCHDOG_REASON	15	/* int: REASON at boot */
#define	CPU_WATCHDOG_SITE	16	/* int: masked section at the reset */
#define	CPU_WATCHDOG_ARG	17	/* int: that section's argument */
#define CPU_STORAGE_STATS       18      /* struct: optional storage accounting */
/*
 * The PicoCalc keyboard controller's battery, backlight and power-off
 * registers (machine/picocalcvar.h); the PICOCALC kernel serves it.
 */
#define	CPU_PICOCALC		19	/* node: PicoCalc board registers */
#define	CPU_WATCHDOG_ARG2	20	/* int: that section's second argument */
/*
 * Core 1 as the kernel's observer, arch/rp2040/rp2040/core1.c, on kernels
 * built with "options CORE1". core1 reads 0 parked by request, 1 running,
 * 2 waiting to launch, 3 parked after repeated failures; writing 0 parks
 * core 1 and nonzero launches it. The counters report stalls of core 0's
 * clock with no masked section declared, the longest of them, the PC and LR
 * core 0 was found at, and exceptions core 1 itself took. Writing
 * core1_stall_ms masks core 0's interrupts for that long, up to 10 s, to
 * test the report.
 */
#define	CPU_CORE1		21	/* int: supervisor state */
#define	CPU_CORE1_RESTARTS	22	/* int: restarts since the last start */
#define	CPU_CORE1_STALLS	23	/* int: undeclared stalls of core 0 */
#define	CPU_CORE1_GAP_US	24	/* int: longest such stall */
#define	CPU_CORE1_PC		25	/* int: core 0 PC at the last probe */
#define	CPU_CORE1_LR		26	/* int: core 0 LR at the last probe */
#define	CPU_CORE1_FAULTS	27	/* int: exceptions core 1 took */
#define	CPU_CORE1_STALL_MS	28	/* int: write masks core 0 that long */
/*
 * Hardware event counters, for measuring a workload from the shell. xip_acc
 * and xip_hit are the XIP cache's access and hit counts (datasheet 2.6.3.1,
 * CTR_ACC and CTR_HIT): 32-bit, saturating, and counting every flash access
 * of the kernel as well as the workload's. bus_perfctr0 to bus_perfctr3 are
 * the bus fabric's four 24-bit saturating counters (datasheet 2.1.1.2), each
 * counting the event its bus_perfsel names: 0 to 19, an even number for the
 * contested accesses of one arbiter and the next odd number for all of them
 * (APB, FASTPERI, SRAM5 down to SRAM0, XIP_MAIN, ROM); 31, the reset value,
 * counts nothing. Writing any value to a counter clears it.
 */
#define	CPU_XIP_ACC		29	/* int: XIP accesses; a write clears */
#define	CPU_XIP_HIT		30	/* int: XIP cache hits; a write clears */
#define	CPU_BUS_PERFCTR0	31	/* int: bus event count; a write clears */
#define	CPU_BUS_PERFCTR1	32
#define	CPU_BUS_PERFCTR2	33
#define	CPU_BUS_PERFCTR3	34
#define	CPU_BUS_PERFSEL0	35	/* int: event bus_perfctr0 counts */
#define	CPU_BUS_PERFSEL1	36
#define	CPU_BUS_PERFSEL2	37
#define	CPU_BUS_PERFSEL3	38
#define	CPU_MAXID		39	/* number of valid machdep ids */

#ifndef	KERNEL
#define	CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "cpu_khz", CTLTYPE_INT }, \
	{ "bus_khz", CTLTYPE_INT }, \
	{ "usb_e15_deferred", CTLTYPE_INT }, \
	{ "usb_bulkin_arms", CTLTYPE_INT }, \
	{ "swapram_evacuate", CTLTYPE_INT }, \
	{ "swapram_images", CTLTYPE_INT }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "mpu", CTLTYPE_NODE }, \
	{ "usb_service_reentered", CTLTYPE_INT }, \
	{ "usb_tx_recovered", CTLTYPE_INT }, \
	{ "watchdog_reason", CTLTYPE_INT }, \
	{ "watchdog_site", CTLTYPE_INT }, \
	{ "watchdog_arg", CTLTYPE_INT }, \
	{ "storage_stats", CTLTYPE_STRUCT }, \
	{ "picocalc", CTLTYPE_NODE }, \
	{ "watchdog_arg2", CTLTYPE_INT }, \
	{ "core1", CTLTYPE_INT }, \
	{ "core1_restarts", CTLTYPE_INT }, \
	{ "core1_stalls", CTLTYPE_INT }, \
	{ "core1_gap_us", CTLTYPE_INT }, \
	{ "core1_pc", CTLTYPE_INT }, \
	{ "core1_lr", CTLTYPE_INT }, \
	{ "core1_faults", CTLTYPE_INT }, \
	{ "core1_stall_ms", CTLTYPE_INT }, \
	{ "xip_acc", CTLTYPE_INT }, \
	{ "xip_hit", CTLTYPE_INT }, \
	{ "bus_perfctr0", CTLTYPE_INT }, \
	{ "bus_perfctr1", CTLTYPE_INT }, \
	{ "bus_perfctr2", CTLTYPE_INT }, \
	{ "bus_perfctr3", CTLTYPE_INT }, \
	{ "bus_perfsel0", CTLTYPE_INT }, \
	{ "bus_perfsel1", CTLTYPE_INT }, \
	{ "bus_perfsel2", CTLTYPE_INT }, \
	{ "bus_perfsel3", CTLTYPE_INT }, \
}
#endif	/* !KERNEL */
