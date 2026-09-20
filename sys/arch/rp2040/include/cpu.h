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
#define	CPU_SWAPRAM_EPOCH	10	/* int: 0 SMALL, 1 LARGE; a write asks */
#define	CPU_SWAPRAM_LARGE	11	/* int: processes holding the bonus */
/*
 * The Cortex-M0+ MPU as arch/rp2040/rp2040/mpu.c programmed it and read
 * it back; machine/mpuvar.h names the leaves.
 */
#define	CPU_MPU			12	/* node: memory protection unit */
/*
 * What the previous reset left in the watchdog's scratch registers, from
 * arch/rp2040/rp2040/machdep.c. The console ring does not survive long
 * enough for a host to read the boot line, so the report is readable here
 * for the life of the boot. machine/watchdog.h names the sites.
 */
#define	CPU_WATCHDOG_REASON	13	/* int: REASON at boot */
#define	CPU_WATCHDOG_SITE	14	/* int: masked section at the reset */
#define	CPU_WATCHDOG_ARG	15	/* int: that section's argument */
#define	CPU_MAXID		16	/* number of valid machdep ids */

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
	{ "swapram_epoch", CTLTYPE_INT }, \
	{ "swapram_large", CTLTYPE_INT }, \
	{ "mpu", CTLTYPE_NODE }, \
	{ "watchdog_reason", CTLTYPE_INT }, \
	{ "watchdog_site", CTLTYPE_INT }, \
	{ "watchdog_arg", CTLTYPE_INT }, \
}
#endif	/* !KERNEL */
