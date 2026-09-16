/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)machdep.c	2.4 (2.11BSD) 1999/9/13
 */

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/inode.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/fs.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/clist.h>
#include <sys/callout.h>
#include <sys/reboot.h>
#include <sys/msgbuf.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/config.h>
#include <sys/tty.h>

#include <machine/fault.h>

#include <machine/intr.h>
#include <machine/scb.h>

#include <rp2040/dev/uart.h>
#include <rp2040/dev/usb.h>
#include <rp2040/dev/flash.h>

#define	MREG32(a)		(*(volatile u_int *)(a))

/*
 * Kernel-specific uses of LEDs and buttons provided by the
 * board support package and defined in the kernel Config.
 * LED activity indicators: TTY, SWAP, DISK, KERNEL
 * User Button: Enter single user mode when pressed during boot.
 */
#if defined(BSP) && defined(BSP_LED_TTY)
#define LED_TTY_INIT()		BSP_LED_Init(BSP_LED_TTY)
#define LED_TTY_ON()		BSP_LED_On(BSP_LED_TTY)
#define LED_TTY_OFF()		BSP_LED_Off(BSP_LED_TTY)
#else
#define LED_TTY_INIT()		/* Nothing. */
#define LED_TTY_ON()		/* Nothing. */
#define LED_TTY_OFF()		/* Nothing. */
#endif

#if defined(BSP) && defined(BSP_LED_SWAP)
#define LED_SWAP_INIT()		BSP_LED_Init(BSP_LED_SWAP)
#define LED_SWAP_ON()		BSP_LED_On(BSP_LED_SWAP)
#define LED_SWAP_OFF()		BSP_LED_Off(BSP_LED_SWAP)
#else
#define LED_SWAP_INIT()		/* Nothing. */
#define LED_SWAP_ON()		/* Nothing. */
#define LED_SWAP_OFF()		/* Nothing. */
#endif

#if defined(BSP) && defined(BSP_LED_DISK)
#define LED_DISK_INIT()		BSP_LED_Init(BSP_LED_DISK)
#define LED_DISK_ON()		BSP_LED_On(BSP_LED_DISK)
#define LED_DISK_OFF()		BSP_LED_Off(BSP_LED_DISK)
#else
#define LED_DISK_INIT()		/* Nothing. */
#define LED_DISK_ON()		/* Nothing. */
#define LED_DISK_OFF()		/* Nothing. */
#endif

#if defined(BSP) && defined(BSP_LED_KERNEL)
#define LED_KERNEL_INIT()	BSP_LED_Init(BSP_LED_KERNEL)
#define LED_KERNEL_ON()		BSP_LED_On(BSP_LED_KERNEL)
#define LED_KERNEL_OFF()	BSP_LED_Off(BSP_LED_KERNEL)
#else
#define LED_KERNEL_INIT()	/* Nothing. */
#define LED_KERNEL_ON()		/* Nothing. */
#define LED_KERNEL_OFF()	/* Nothing. */
#endif

/*
 * The Pico's only button is BOOTSEL, wired to the flash chip select, so
 * reading it means floating that pin for a moment; see bootsel_pressed.
 */
#define BUTTON_USER_INIT()	/* Nothing. */
#define BUTTON_USER_PRESSED()	bootsel_pressed()

char	machine[] = MACHINE;		/* from <machine/machparam.h> */
char	machine_arch[] = MACHINE_ARCH;	/* from <machine/machparam.h> */
char	cpu_model[64];

int	hz = HZ;
int	usechz = (1000000L + HZ - 1) / HZ;

#ifdef TIMEZONE
struct timezone		tz = { TIMEZONE, DST };
#else
struct timezone		tz = { 8 * 60, 1 };
#endif

int			nproc = NPROC;

struct namecache	namecache[NNAMECACHE];
char			bufdata[NBUF * MAXBSIZE];
struct inode		inode[NINODE];
struct callout		callout[NCALL];
struct mount		mount[NMOUNT];
struct buf		buf[NBUF], bfreelist[BQUEUES];
#ifndef LINEAR_BUFFER_CACHE
struct bufhd		bufhash[BUFHSZ];
#endif
struct cblock		cfree[NCLIST];
struct proc		proc[NPROC];
struct file		file[NFILE];

/*
 * Remove the ifdef/endif to run the kernel in unsecure mode even when in
 * a multiuser state.  Normally 'init' raises the security level to 1
 * upon transitioning to multiuser.  Setting the securelevel to -1 prevents
 * the secure level from being raised by init.
 */
#ifdef PERMANENTLY_INSECURE
int	securelevel = -1;
#else
int	securelevel = 0;
#endif

struct mapent	swapent[SMAPSIZ];
#ifdef COMPACT_SWAPMAP
_Static_assert(sizeof(struct mapent) == 4,
    "compact RP2040 swap descriptors must remain four bytes");
_Static_assert(FLASH_SWAP_BYTES / FLASH_UNIT_BYTES <= (u_short)-1,
    "RP2040 raw swap must fit compact descriptors");
#endif
struct map	swapmap[1] = {
	{ swapent,
	  &swapent[SMAPSIZ - 1],
	  "swapmap" },
};

int	waittime = -1;

static int
nodump(dev_t dev)
{
	printf("\ndumping to dev %o off %D: not implemented\n",
	    dumpdev, dumplo);

	return 0;
}

int (*dump)(dev_t) = nodump;

dev_t	pipedev;
daddr_t	dumplo = (daddr_t)1024;


/*
 * RP2040 clock, reset, and pin hardware, datasheet chapters 2.14 to 2.19.
 */
#define	RESETS_BASE		0x4000c000UL
#define	RESETS_RESET		0x0
#define	RESETS_DONE		0x8
#define	RESETS_CLR		0x3000		/* Atomic clear alias. */
#define	RESETS_PLL_SYS		(1UL << 12)
#define	RESETS_PLL_USB		(1UL << 13)
#define	RESETS_IO_BANK0		(1UL << 5)
#define	RESETS_PADS_BANK0	(1UL << 8)
#define	RESETS_SYSINFO		(1UL << 19)
#define	RESETS_TIMER		(1UL << 21)

/*
 * The watchdog block divides clk_ref down to the 1 MHz tick that both the
 * timer and SysTick's external clock count. Datasheet section 4.6.4: the
 * timer does not count until this tick runs. Twelve cycles of the 12 MHz
 * crystal make one microsecond.
 */
#define	WATCHDOG_BASE		0x40058000UL
#define	WATCHDOG_SCRATCH0	0x0c
#define	WATCHDOG_TICK		0x2c
#define	WATCHDOG_TICK_ENABLE	(1UL << 9)
#define	WATCHDOG_TICK_CYCLES	12

#define	ROSC_BASE		0x40060000UL
#define	ROSC_RANDOMBIT		0x1c

/* "SWP" plus one sector index in the low byte. */
#define	SWAP_CURSOR_TAG		0x53575000UL
#define	SWAP_CURSOR_TAG_MASK	0xffffff00UL

/*
 * Watchdog scratch0 preserves the next-fit position across a software reset.
 * A cold boot rejects an untagged word and uses bounded ROSC rejection
 * sampling, spreading the first allocation without adding an SRAM table or
 * writing flash metadata.  Boot ROM custom reboot state occupies scratch4-7,
 * so scratch0 has a separate owner.
 */
void
swap_cursor_init(u_int blocks)
{
	u_int index, sample, sectors, scratch, tries;

	sectors = (blocks - SWAP_IMAGE_ALIGN) / SWAP_IMAGE_ALIGN;
	scratch = MREG32(WATCHDOG_BASE + WATCHDOG_SCRATCH0);
	index = scratch & 0xff;
	if ((scratch & SWAP_CURSOR_TAG_MASK) != SWAP_CURSOR_TAG ||
	    index >= sectors) {
		index = 0;
		for (tries = 0; tries < 8; tries++) {
			sample = 0;
			for (u_int bit = 0; bit < 7; bit++)
				sample = (sample << 1) |
				    (MREG32(ROSC_BASE + ROSC_RANDOMBIT) & 1);
			if (sample < sectors) {
				index = sample;
				break;
			}
		}
	}
	swapnext = SWAP_IMAGE_ALIGN + index * SWAP_IMAGE_ALIGN;
	swap_cursor_publish(swapnext);
}

void
swap_cursor_publish(size_t next)
{
	u_int index;

	if (next < SWAP_IMAGE_ALIGN || next > nswap ||
	    (next & (SWAP_IMAGE_ALIGN - 1)) != 0)
		index = 0;
	else if (next == nswap)
		index = 0;
	else
		index = (next - SWAP_IMAGE_ALIGN) / SWAP_IMAGE_ALIGN;
	MREG32(WATCHDOG_BASE + WATCHDOG_SCRATCH0) = SWAP_CURSOR_TAG | index;
}

#define	XOSC_BASE		0x40024000UL
#define	XOSC_CTRL		0x00
#define	XOSC_STATUS		0x04
#define	XOSC_STARTUP		0x0c
#define	XOSC_CTRL_FREQ_1_15MHZ	0xaa0UL
#define	XOSC_CTRL_ENABLE	(0xfabUL << 12)
#define	XOSC_STATUS_STABLE	(1UL << 31)

#define	PLL_SYS_BASE		0x40028000UL
#define	PLL_USB_BASE		0x4002c000UL
#define	PLL_CS			0x00
#define	PLL_PWR			0x04
#define	PLL_FBDIV_INT		0x08
#define	PLL_PRIM		0x0c
#define	PLL_CS_LOCK		(1UL << 31)
#define	PLL_PWR_PD		(1UL << 0)
#define	PLL_PWR_POSTDIVPD	(1UL << 3)
#define	PLL_PWR_VCOPD		(1UL << 5)

#define	CLOCKS_BASE		0x40008000UL
#define	CLK_REF_CTRL		0x30
#define	CLK_REF_SELECTED	0x38
#define	CLK_SYS_CTRL		0x3c
#define	CLK_SYS_SELECTED	0x44
#define	CLK_PERI_CTRL		0x48
#define	CLK_PERI_ENABLE		(1UL << 11)
#define	CLK_USB_CTRL		0x54
#define	CLK_USB_ENABLE		(1UL << 11)	/* Aux source 0 is PLL_USB. */

#define	IO_QSPI_BASE		0x40018000UL
#define	IO_QSPI_SS_CTRL		0x0c		/* GPIO_QSPI_SS control. */
#define	IO_QSPI_OEOVER_MASK	(3UL << 12)
#define	IO_QSPI_OEOVER_DISABLE	(2UL << 12)	/* Output disabled: floats. */
#define	SIO_GPIO_HI_IN		0x08
#define	SIO_QSPI_SS_BIT		(1UL << 1)

#define	SIO_BASE		0xd0000000UL
#define	SIO_GPIO_OUT_SET	0x14
#define	SIO_GPIO_OUT_CLR	0x18
#define	SIO_GPIO_OE_SET		0x24

#define	IO_BANK0_BASE		0x40014000UL
#define	IO_BANK0_CTRL(n)	(0x004 + 8 * (n))
#define	GPIO_FUNC_SIO		5

#define	PADS_BANK0_BASE		0x4001c000UL
#define	PADS_BANK0_GPIO(n)	(0x04 + 4 * (n))
#define	PADS_OD			(1UL << 7)

/* The Pico carries one LED, on GP25. */
#define	LED_GPIO		25

#define	SYSINFO_BASE		0x40000000UL
#define	SYSINFO_CHIP_ID		0x00

#define	AIRCR_VECTKEY		0x05fa0000UL	/* AIRCR write key. */
#define	AIRCR_SYSRESETREQ	(1UL << 2)

#define	TIMER_BASE		0x40054000UL
#define	TIMER_TIMERAWL		0x28		/* Free-running microseconds. */

static void
rp_unreset(u_int mask)
{
	MREG32(RESETS_BASE + RESETS_CLR + RESETS_RESET) = mask;
	while ((MREG32(RESETS_BASE + RESETS_DONE) & mask) != mask)
		continue;
}

/*
 * SystemInit()
 *
 * Called from Reset_Handler in locore0.S before main, where the STM32 tree
 * reaches system_stm32f4xx.c in the ST HAL. Only one thing has to happen
 * this early on the RP2040: point the vector table at the kernel's own.
 *
 * The boot ROM's second stage already writes this register with the same
 * value when it vectors into a flash image, so the store is usually a
 * rewrite. It is done anyway, because an image entered any other way, by a
 * chain loader or loaded straight into SRAM, arrives with VTOR still naming
 * whatever the previous image left. Clocks are not touched here; startup()
 * raises them once the kernel is running.
 */
extern u_int g_pfnVectors[];

void
SystemInit(void)
{
	SCB_REG32(SCB_VTOR) = (u_int)g_pfnVectors;
	arm_dsb();
	arm_isb();
}

/*
 * Bring the system clock up to CPU_KHZ.
 *
 * The STM32 edition programs RCC: enable HSE, set flash latency, configure
 * the PLL, switch SYSCLK. The RP2040 sequence differs in kind, so this is
 * written against its register map rather than ported. The Pico carries a
 * 12 MHz crystal; multiplying by 125 gives a 1500 MHz VCO, and dividing by
 * six and then two gives the 125 MHz the SDK also selects.
 */
static void
SystemClock_Config(void)
{
	/* Start the crystal oscillator and wait for it to settle. */
	MREG32(XOSC_BASE + XOSC_STARTUP) = 47;	/* About 1 ms. */
	MREG32(XOSC_BASE + XOSC_CTRL) =
	    XOSC_CTRL_FREQ_1_15MHZ | XOSC_CTRL_ENABLE;
	while ((MREG32(XOSC_BASE + XOSC_STATUS) & XOSC_STATUS_STABLE) == 0)
		continue;

	/* Run the reference clock from the crystal. */
	MREG32(CLOCKS_BASE + CLK_REF_CTRL) = 2;		/* xosc_clksrc */
	while (MREG32(CLOCKS_BASE + CLK_REF_SELECTED) != (1UL << 2))
		continue;

	/* Start the microsecond tick, then the timer that counts it. */
	MREG32(WATCHDOG_BASE + WATCHDOG_TICK) =
	    WATCHDOG_TICK_ENABLE | WATCHDOG_TICK_CYCLES;
	rp_unreset(RESETS_TIMER | RESETS_SYSINFO);

	/* Program the system PLL: 12 * 125 / (6 * 2) == 125 MHz. */
	rp_unreset(RESETS_PLL_SYS);
	MREG32(PLL_SYS_BASE + PLL_FBDIV_INT) = 125;
	MREG32(PLL_SYS_BASE + PLL_PRIM) = (6UL << 16) | (2UL << 12);
	MREG32(PLL_SYS_BASE + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
	while ((MREG32(PLL_SYS_BASE + PLL_CS) & PLL_CS_LOCK) == 0)
		continue;
	MREG32(PLL_SYS_BASE + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;

	/*
	 * Switch clk_sys onto the PLL. Source 1 selects the auxiliary mux,
	 * whose source 0 is the system PLL.
	 */
	MREG32(CLOCKS_BASE + CLK_SYS_CTRL) = 0;
	MREG32(CLOCKS_BASE + CLK_SYS_CTRL) = 1;
	while (MREG32(CLOCKS_BASE + CLK_SYS_SELECTED) != (1UL << 1))
		continue;

	/* The peripheral clock, which the UART divides for its baud rate. */
	MREG32(CLOCKS_BASE + CLK_PERI_CTRL) = CLK_PERI_ENABLE;

	/*
	 * The USB PLL: 12 * 100 / (5 * 5) == 48 MHz, the rate the USB
	 * controller requires of clk_usb (datasheet 4.1.2.1).
	 */
	rp_unreset(RESETS_PLL_USB);
	MREG32(PLL_USB_BASE + PLL_FBDIV_INT) = 100;
	MREG32(PLL_USB_BASE + PLL_PRIM) = (5UL << 16) | (5UL << 12);
	MREG32(PLL_USB_BASE + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
	while ((MREG32(PLL_USB_BASE + PLL_CS) & PLL_CS_LOCK) == 0)
		continue;
	MREG32(PLL_USB_BASE + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;
	MREG32(CLOCKS_BASE + CLK_USB_CTRL) = CLK_USB_ENABLE;

	/* The LED is a plain output driven from the single-cycle IO block. */
	rp_unreset(RESETS_IO_BANK0 | RESETS_PADS_BANK0);
	MREG32(PADS_BANK0_BASE + PADS_BANK0_GPIO(LED_GPIO)) &= ~PADS_OD;
	MREG32(IO_BANK0_BASE + IO_BANK0_CTRL(LED_GPIO)) = GPIO_FUNC_SIO;
	MREG32(SIO_BASE + SIO_GPIO_OE_SET) = 1UL << LED_GPIO;
}

/*
 * Read the BOOTSEL button. It is the flash chip select, pulled up on the
 * board and pulled low by the button, so the pad's output is disabled for
 * a moment and the level sampled through SIO. Flash is unreachable while
 * the select floats, which is why this runs from RAM with interrupts
 * masked and calls nothing. The sequence is the Pico SDK's
 * picoboard/button example, written against the IO_QSPI and SIO registers.
 */
static __ramfunc int
bootsel_pressed(void)
{
	volatile u_int *ctrl = (volatile u_int *)(IO_QSPI_BASE + IO_QSPI_SS_CTRL);
	volatile int i;
	u_int saved, level;
	int s;

	s = splhigh();
	saved = *ctrl;
	*ctrl = (saved & ~IO_QSPI_OEOVER_MASK) | IO_QSPI_OEOVER_DISABLE;
	for (i = 0; i < 1000; i++)
		continue;	/* Let the pull-up settle. */
	level = MREG32(SIO_BASE + SIO_GPIO_HI_IN);
	*ctrl = saved;
	splx(s);
	return (level & SIO_QSPI_SS_BIT) == 0;
}

/*
 * Report the chip. The STM32 edition reads a device and revision id from the
 * debug block and prints a part name. The RP2040 publishes one identifier in
 * SYSINFO holding a manufacturer, a part number, and a revision, so the
 * fields are reported as the hardware defines them.
 */
static void
cpuidentify(void)
{
	u_int chip_id, part, rev, mfr;

	chip_id = MREG32(SYSINFO_BASE + SYSINFO_CHIP_ID);
	mfr  = chip_id & 0x00000fff;
	part = (chip_id >> 12) & 0x0000ffff;
	rev  = (chip_id >> 28) & 0x0000000f;

	printf("cpu: RP%04x rev %u, manufacturer 0x%03x\n", part, rev, mfr);
	printf("cpu: Cortex-M0+, ARMv6-M, no MMU and no MPU\n");
	printf("cpu: %u MHz core, %u MHz peripheral\n",
	    (u_int)CPU_KHZ / 1000, (u_int)BUS_KHZ / 1000);
}


/*
 * Machine dependent startup code.
 */
void
startup(void)
{
	SystemClock_Config();

#ifdef UARTUSB_ENABLED
	/* The console first, so everything after it can be read. */
	usbinit();
#endif

	/*
	 * The STM32 edition enables the MemManage, BusFault, and UsageFault
	 * handlers here. ARMv6-M defines none of them: HardFault is the only
	 * fault exception, and it is always enabled.
	 *
	 * System exception priorities live in SHPR2 and SHPR3 rather than the
	 * NVIC array, so they are set through arm_set_exception_priority.
	 */
	arm_set_exception_priority(EXC_PENDSV, IPLTOREG(IPL_PENDSV));
	arm_set_exception_priority(EXC_SVCALL, IPLTOREG(IPL_SVCALL));

	/* SysTick is a system exception, not an NVIC interrupt. */
	arm_set_exception_priority(EXC_SYSTICK, IPLTOREG(IPL_SYSTICK));

	physmem = 264 * 1024;		/* Six SRAM banks, 264 kbytes. */

	/*
	 * conf/RP2040.ld's USERRAM and machparam.h's USER_DATA_SIZE state the
	 * user window once each; exec admits images against the latter and
	 * the linker laid out kernel data by the former, so a mismatch would
	 * let a process overrun the kernel's own bss.
	 */
	if ((size_t)__user_data_end - (size_t)__user_data_start !=
	    USER_DATA_SIZE)
		panic("user window: linker and machparam.h disagree");
#ifdef SWAPRAM
	swapram_init();
#endif

	/*
	 * Configure LED pins.
	 */
	LED_TTY_INIT();			/* Green.   Terminal i/o */
	LED_SWAP_INIT();		/* Orange.  Auxiliary swap */
	LED_DISK_INIT();		/* Red.	    Disk i/o */
	LED_KERNEL_INIT();		/* Blue.    Kernel activity */

	LED_TTY_ON();
	LED_SWAP_ON();
	LED_DISK_ON();
	LED_KERNEL_ON();

	LED_TTY_OFF();
	LED_SWAP_OFF();
	LED_DISK_OFF();
	LED_KERNEL_OFF();

	led_control(LED_ALL, 1);
	led_control(LED_ALL, 0);

	/*
	 * Configure User Button.
	 */
	BUTTON_USER_INIT();

	/*
	 * Early setup for console devices.
	 */
#if defined(UART_ENABLED) || defined(UART0_ENABLED)
	uartinit(0);
#endif

	/*
	 * When User button is pressed - boot to single user mode.
	 */
	boothowto = 0;
	if (BUTTON_USER_PRESSED()) {
		boothowto |= RB_SINGLE;
	}
}


/*
 * Check whether the controller has been successfully initialized.
 */
static int
is_controller_alive(struct driver *driver, int unit)
{
	struct conf_ctlr *ctlr;

	/* No controller - that's OK. */
	if (driver == 0)
		return 1;

	for (ctlr = conf_ctlr_init; ctlr->ctlr_driver; ctlr++) {
		if (ctlr->ctlr_driver == driver &&
		    ctlr->ctlr_unit == unit && ctlr->ctlr_alive) {
			return 1;
		}
	}

	return 0;
}

/*
 * Configure all controllers and devices as specified
 * in the kernel configuration file.
 */
void
config(void)
{
	struct conf_ctlr *ctlr;
	struct conf_device *dev;

	cpuidentify();

	/* Probe and initialize controllers first. */
	for (ctlr = conf_ctlr_init; ctlr->ctlr_driver; ctlr++) {
		if ((*ctlr->ctlr_driver->d_init)(ctlr)) {
			ctlr->ctlr_alive = 1;
		}
	}

	/* Probe and initialize devices. */
	for (dev = conf_device_init; dev->dev_driver; dev++) {
		if (is_controller_alive(dev->dev_cdriver, dev->dev_ctlr)) {
			if ((*dev->dev_driver->d_init)(dev)) {
				dev->dev_alive = 1;
			}
		}
	}
}

/*
 * Sit and wait for something to happen...
 */
void
idle(void)
{
	/* Indicate that no process is running. */
	noproc = 1;

	/* Set SPL low so we can be interrupted. */
	int x = spl0();

	led_control(LED_KERNEL, 0);

	/* Wait for something to happen. */
	arm_dsb();
	arm_isb();
	__asm__ volatile ("wfi" ::: "memory");

	/* Restore previous SPL. */
	splx(x);
}

void
boot(dev_t dev, int howto)
{
	if ((howto & RB_NOSYNC) == 0 && waittime < 0 &&
	    bfreelist[0].av_forw) {
		struct fs *fp;
		struct buf *bp;
		int iter, nbusy;

		/*
		 * Force the root filesystem's superblock to be updated,
		 * so the date will be as current as possible after
		 * rebooting.
		 */
		fp = getfs(rootdev);
		if (fp)
			fp->fs_fmod = 1;
		waittime = 0;
		printf("syncing disks... ");
		(void)splnet();
		sync();
		for (iter = 0; iter < 20; iter++) {
			nbusy = 0;
			for (bp = &buf[NBUF]; --bp >= buf;)
				if (bp->b_flags & B_BUSY)
					nbusy++;
			if (nbusy == 0)
				break;
			printf("%d ", nbusy);
			mdelay(40L * iter);
		}
		printf("done\n");
	}
	(void)splhigh();
	if (howto & RB_BOOTLOADER) {
		/*
		 * Enter the boot ROM's USB loader (datasheet 2.8.3.1,
		 * reset_usb_boot): the board reappears as the RPI-RP2
		 * mass-storage device and takes a UF2, on any host with no
		 * driver and no tool. Argument 0 keeps every interface and
		 * lights no LED. The USB cable is about to drop, so the
		 * console is drained first. The ROM never returns.
		 */
		void (*rom_reset)(u_int, u_int);

#ifdef UARTUSB_ENABLED
		usbdrain();
#endif
		rom_reset = (void (*)(u_int, u_int))
		    rom_func_lookup(ROM_CODE('U', 'B'));
		rom_reset(0, 0);
		/* NOTREACHED */
	}
	if (!(howto & RB_HALT)) {
		if ((howto & RB_DUMP) && dumpdev != NODEV) {
			/*
			 * Take a dump of memory by calling (*dump)(),
			 * which must correspond to dumpdev.
			 * It should dump from dumplo blocks to the end
			 * of memory or to the end of the logical device.
			 */
			(*dump)(dumpdev);
		}
		/* Restart from dev, howto. */

		/*
		 * Reset the microcontroller. AIRCR carries a write key in its
		 * high half and rejects the write without it.
		 */
#ifdef UARTUSB_ENABLED
		usbdrain();
#endif
		arm_dsb();
		SCB_REG32(SCB_AIRCR) = AIRCR_VECTKEY | AIRCR_SYSRESETREQ;
		arm_dsb();
		for (;;)
			continue;
		/* NOTREACHED */
	}
	printf("halted\n");

#ifdef HALTREBOOT
	printf("press any key to reboot...\n");
	cngetc();

	/*
	 * Reset the microcontroller. AIRCR carries a write key in its high
	 * half and rejects the write without it.
	 */
#ifdef UARTUSB_ENABLED
	usbdrain();
#endif
	arm_dsb();
	SCB_REG32(SCB_AIRCR) = AIRCR_VECTKEY | AIRCR_SYSRESETREQ;
	arm_dsb();
	/* NOTREACHED */
#endif

	printf("reboot failed; spinning\n");
	for (;;) {
		arm_dsb();
		arm_isb();
		__asm__ volatile ("wfi" ::: "memory");
	}
	/* NOTREACHED */
}

/*
 * Millisecond delay routine.
 *
 * Uses SysTick, which must be configured to a 1ms timebase.
 * This is a busy-wait blocking delay, so be wise with use.
 */
void
mdelay(u_int msec)
{
	/*
	 * The STM32 edition calls the ST HAL's delay, which counts a tick the
	 * HAL maintains. Nothing here keeps one, so this spins on the
	 * always-running microsecond timer, which needs no setup.
	 */
	u_int start = MREG32(TIMER_BASE + TIMER_TIMERAWL);
	u_int usec = msec * 1000;

	while ((u_int)(MREG32(TIMER_BASE + TIMER_TIMERAWL) - start) < usec)
		continue;
}

/*
 * Control LEDs, installed on the board.
 */
/*
 * The Pico has a single LED on GP25, so every activity class lights the same
 * one. The STM32 boards carry four and map one class to each.
 */
void
led_control(int mask, int on)
{
	if (mask == 0)
		return;

	if (on)
		MREG32(SIO_BASE + SIO_GPIO_OUT_SET) = 1UL << LED_GPIO;
	else
		MREG32(SIO_BASE + SIO_GPIO_OUT_CLR) = 1UL << LED_GPIO;
}

/*
 * Increment user profiling counters.
 */
void
addupc(caddr_t pc, struct uprof *pbuf, int ticks)
{
	u_int indx;

	if (pc < (caddr_t)pbuf->pr_off)
		return;

	indx = pc - (caddr_t)pbuf->pr_off;
	indx = (indx * pbuf->pr_scale) >> 16;
	if (indx >= pbuf->pr_size)
		return;

	pbuf->pr_base[indx] += ticks;
}

/*
 * ffs -- vax ffs instruction
 */
int
ffs(u_long mask)
{
	int cnt;

	if (mask == 0)
		return 0;
	for (cnt = 1; !(mask & 1); cnt++)
		mask >>= 1;

	return cnt;
}

/*
 * Copy a null terminated string from one point to another.
 * Returns zero on success, ENOENT if maxlength exceeded.
 * If lencopied is non-zero, *lencopied gets the length of the copy
 * (including the null terminating byte).
 */
int
copystr(caddr_t src, caddr_t dest, u_int maxlength, u_int *lencopied)
{
	caddr_t dest0 = dest;
	int error = ENOENT;

	if (maxlength != 0) {
		while ((*dest++ = *src++) != '\0') {
			if (--maxlength == 0) {
				/* Failed. */
				goto done;
			}
		}
		/* Succeeded. */
		error = 0;
	}
done:
	if (lencopied != 0)
		*lencopied = dest - dest0;

	return error;
}

/*
 * Calculate the length of a string.
 */
size_t
strlen(const char *s)
{
	const char *s0 = s;

	while (*s++ != '\0')
		;

	return s - s0 - 1;
}

/*
 * Return 0 if a user address is valid.
 * There is only one memory region allowed for user: RAM.
 */
int
baduaddr(caddr_t addr)
{
	if (addr >= (caddr_t)__user_data_start &&
	    addr < (caddr_t)USER_TOP(u.u_procp))
		return 0;

	return 1;
}

/*
 * Return 0 if a kernel address is valid.
 * There are two memory regions allowed for kernel: RAM and flash.
 */
int
badkaddr(caddr_t addr)
{
	if (addr >= (caddr_t)__kernel_data_start &&
	    addr < (caddr_t)__kernel_data_end)
		return 0;
	if (addr >= (caddr_t)__kernel_flash_start &&
	    addr < (caddr_t)__kernel_flash_end)
		return 0;

	return 1;
}

/*
 * Insert the specified element into a queue immediately after
 * the specified predecessor element.
 */
void
insque(void *element, void *predecessor)
{
	struct que {
		struct que *q_next;
		struct que *q_prev;
	};

	struct que *e = (struct que *)element;
	struct que *prev = (struct que *)predecessor;

	e->q_prev = prev;
	e->q_next = prev->q_next;
	prev->q_next->q_prev = e;
	prev->q_next = e;
}

/*
 * Remove the specified element from the queue.
 */
void
remque(void *element)
{
	struct que {
		struct que *q_next;
		struct que *q_prev;
	};

	struct que *e = (struct que *)element;

	e->q_prev->q_next = e->q_next;
	e->q_next->q_prev = e->q_prev;
}

/*
 * Compare strings.
 */
int
strncmp(const char *s1, const char *s2, size_t n)
{
	int ret, tmp;

	if (n == 0)
		return 0;

	do {
		ret = *s1++ - (tmp = *s2++);
	} while ((ret == 0) && (tmp != 0) && --n);

	return ret;
}

/* Nonzero if pointer is not aligned on a "sz" boundary. */
#define UNALIGNED(p, sz)	((u_int)(p) & ((sz) - 1))

/*
 * Copy data from the memory region pointed to by src0 to the memory
 * region pointed to by dst0.
 * If the regions overlap, the behavior is undefined.
 */
void
bcopy(const void *src0, void *dst0, size_t nbytes)
{
	u_char		*dst = dst0;
	const u_char	*src = src0;
	u_int		*aligned_dst;
	const u_int	*aligned_src;

	/* printf("bcopy (%08x, %08x, %d)\n", src0, dst0, nbytes); */
	/* If the size is small, or either SRC or DST is unaligned,
	 * then punt into the byte copy loop.  This should be rare. */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(src, sizeof(u_int)) &&
	    !UNALIGNED(dst, sizeof(u_int))) {
		aligned_dst = (u_int *)dst;
		aligned_src = (const u_int *)src;

		/* Copy 4X unsigned words at a time if possible. */
		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			nbytes -= 4 * sizeof(u_int);
		}

		/* Copy one unsigned word at a time if possible. */
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = *aligned_src++;
			nbytes -= sizeof(u_int);
		}

		/* Pick up any residual with a byte copier. */
		dst = (u_char *)aligned_dst;
		src = (const u_char *)aligned_src;
	}

	while (nbytes--)
		*dst++ = *src++;
}

void *
memcpy(void *dst, const void *src, size_t nbytes)
{
	bcopy(src, dst, nbytes);

	return dst;
}

/*
 * Fill the array with zeroes.
 */
void
bzero(void *dst0, size_t nbytes)
{
	u_char *dst;
	u_int *aligned_dst;

	dst = (u_char *)dst0;
	/*
	 * Return before the alignment prefix at length zero. Otherwise an
	 * unaligned dst writes one byte, then --nbytes wraps 0 to SIZE_MAX and
	 * the word loops below scribble the whole address space.
	 */
	if (nbytes == 0)
		return;
	while (UNALIGNED(dst, sizeof(u_int))) {
		*dst++ = 0;
		if (--nbytes == 0)
			return;
	}

	if (nbytes >= sizeof(u_int)) {
		/*
		 * If we get this far, we know that nbytes is large
		 * and dst is word-aligned.
		 */
		aligned_dst = (u_int *)dst;

		while (nbytes >= 4 * sizeof(u_int)) {
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			*aligned_dst++ = 0;
			nbytes -= 4 * sizeof(u_int);
		}
		while (nbytes >= sizeof(u_int)) {
			*aligned_dst++ = 0;
			nbytes -= sizeof(u_int);
		}
		dst = (u_char *)aligned_dst;
	}

	/* Pick up the remainder with a bytewise loop. */
	while (nbytes--)
		*dst++ = 0;
}

/*
 * Compare not more than nbytes of data pointed to by m1 with
 * the data pointed to by m2.
 * Return an integer greater than, equal to or less than zero
 * according to whether the object pointed to by m1 is greater
 * than, equal to or less than the object pointed to by m2.
 */
int
bcmp(const void *m1, const void *m2, size_t nbytes)
{
	const u_char *s1 = (const u_char *)m1;
	const u_char *s2 = (const u_char *)m2;
	const u_int *aligned1, *aligned2;

	/*
	 * If the size is too small, or either pointer is unaligned,
	 * then we punt to the byte compare loop.
	 * Hopefully this will not turn up in inner loops.
	 */
	if (nbytes >= 4 * sizeof(u_int) &&
	    !UNALIGNED(s1, sizeof(u_int)) &&
	    !UNALIGNED(s2, sizeof(u_int))) {
		/* Otherwise, load and compare the blocks of memory one
		   word at a time. */
		aligned1 = (const u_int *)s1;
		aligned2 = (const u_int *)s2;
		while (nbytes >= sizeof(u_int)) {
			if (*aligned1 != *aligned2)
				break;
			aligned1++;
			aligned2++;
			nbytes -= sizeof(u_int);
		}

		/* Check remaining characters. */
		s1 = (const u_char *)aligned1;
		s2 = (const u_char *)aligned2;
	}
	while (nbytes--) {
		if (*s1 != *s2)
			return *s1 - *s2;
		s1++;
		s2++;
	}

	return 0;
}

int
copyout(caddr_t from, caddr_t to, u_int nbytes)
{
	/* printf("copyout(from=%p, to=%p, nbytes=%u)\n", from, to, nbytes); */
	if (nbytes == 0)	/* Else to + nbytes - 1 underflows to to - 1. */
		return 0;
	if (baduaddr(to) || baduaddr(to + nbytes - 1))
		return EFAULT;
	bcopy(from, to, nbytes);

	return 0;
}

int
copyin(caddr_t from, caddr_t to, u_int nbytes)
{
	if (nbytes == 0)	/* Else from + nbytes - 1 underflows to from - 1. */
		return 0;
	if (baduaddr(from) || baduaddr(from + nbytes - 1))
		return EFAULT;
	bcopy(from, to, nbytes);

	return 0;
}
