/*
 * Machine dependent constants for STM32.
 *
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)machparam.h	1.4 (2.11BSD GTE) 1998/9/15
 */

#ifndef ENDIAN

#define MACHINE         "rp2040"
#define MACHINE_ARCH    "arm"

/*
 * Definitions for byte order,
 * according to byte significance from low address to high.
 */
#define LITTLE          1234            /* least-significant byte first (vax) */
#define BIG             4321            /* most-significant byte first */
#define PDP             3412            /* LSB first in word, MSW first in long (pdp) */
#define ENDIAN          LITTLE          /* byte order on stm32 */

/*
 * The time for a process to be blocked before being very swappable.
 * This is a number of seconds which the system takes as being a non-trivial
 * amount of real time.  You probably shouldn't change this;
 * it is used in subtle ways (fractions and multiples of it are, that is, like
 * half of a ``long time'', almost a long time, etc.)
 * It is related to human patience and other factors which don't really
 * change over time.
 */
#define MAXSLP          20

/*
 * Clock ticks per second. The HZ value must be an integer factor of 1000.
 * Cortex-M SysTick operates with a 1ms time base, hence 1000 for HZ.
 */
#ifndef HZ
#define HZ              1000
#endif

/*
 * System parameter formulae.
 */
/*
 * A packed exec can hold all six exec allocator blocks, one decoder block,
 * and one filesystem I/O block. Ten buffers preserve two additional blocks
 * for work that runs while rdwri sleeps; changing that bound needs a separate
 * reservation or exec-scratch design.
 */
#ifndef NBUF
#define NBUF            10                      /* number of i/o buffers */
#endif
/*
 * Four buffer hash heads retain constant-time bucket selection for ten data
 * blocks. Longer collision chains cost cycles, while data capacity stays
 * unchanged.
 */
#ifndef BUFHSZ
#define BUFHSZ          4                       /* buffer hash buckets */
#endif
#ifndef MAXUSERS
#define MAXUSERS        1                       /* number of user logins */
#endif
#ifndef NPROC
#define NPROC           25                      /* number of processes */
#endif
#ifndef NINODE
#define NINODE          24
#endif
#ifndef NFILE
#define NFILE           24
#endif
/*
 * Four pathname entries cover a short working set. Four hash heads avoid
 * spending more SRAM on empty buckets than the cache can populate.
 */
#ifndef NNAMECACHE
#define NNAMECACHE      4                       /* pathname cache entries */
#endif
#ifndef NCHHASH
#define NCHHASH         4                       /* pathname hash buckets */
#endif
#define NCALL           (16 + 2 * MAXUSERS)
#define NCLIST          32                      /* number or CBSIZE blocks */
#ifndef SMAPSIZ
#define SMAPSIZ         NPROC                   /* size of swap allocation map */
#endif

/*
 * Disk blocks.
 */
#define DEV_BSIZE       1024            /* the same as MAXBSIZE */
#define DEV_BSHIFT      10              /* log2(DEV_BSIZE) */
#define DEV_BMASK       (DEV_BSIZE-1)

/* Bytes to disk blocks */
#define btod(x)         (((x) + DEV_BSIZE-1) >> DEV_BSHIFT)

/*
 * Raw swap images occupy one contiguous run rounded to the QSPI flash erase
 * sector. The first run starts at this alignment because resource maps reserve
 * address zero as their terminator. Every later allocation and free preserves
 * the alignment, so two live images never share an erase sector.
 */
#define SWAP_IMAGE_ALIGN        4       /* 4096 bytes in DEV_BSIZE blocks. */

/*
 * The user window: one resident process image lives in the 144 KB at
 * 0x20000000. exec_estab (sys/kern/exec_subr.c) rejects an image whose
 * text, data, bss, heap and stack exceed MAXMEM, which sys/param.h takes
 * from here, and exec_aout loads data at USER_DATA_START and places the
 * stack against USER_DATA_END. conf/RP2040.ld's USERRAM region states
 * the same size for the linker and machdep.c panics at boot when the two
 * disagree; lib/libc/arm/gen/rom_float_resolver.S bounds a descriptor
 * check with USER_DATA_END.
 */
#define USER_DATA_START         (0x20000000)
#define USER_DATA_SIZE          (144 * 1024)    /* 144kb for user RAM. */
#define USER_DATA_END           (USER_DATA_START + USER_DATA_SIZE)
#define MAXMEM                  USER_DATA_SIZE

#define stacktop(siz)           (USER_DATA_END)
#define stackbas(siz)           (USER_DATA_END-(siz))

/*
 * User area: a user structure, followed by the kernel
 * stack.  The number for USIZE is determined empirically.
 *
 * Note that the SBASE and STOP constants are only used by the assembly code,
 * but are defined here to localize information about the user area's
 * layout (see pdp/genassym.c).  Note also that a networking stack is always
 * allocated even for non-networking systems.  This prevents problems with
 * applications having to be recompiled for networking versus non-networking
 * systems.
 */
#define USIZE           3072
#define SSIZE           2048            /* initial stack size (bytes) */

/*
 * Collect kernel statistics by default.
 */
#if !defined(UCB_METER) && !defined(NO_UCB_METER)
#define UCB_METER
#endif

#ifdef KERNEL
#include <machine/intr.h>
#include <machine/scb.h>

/*
 * Macros to decode processor status word.
 */
#define USERMODE(psr)   ((psr & IPSR_ISR_MASK) == 0)    /* No exceptions. */
/*
 * ARMv6-M has no BASEPRI register, so "nothing is masked" is PRIMASK zero.
 * The machine-independent kernel calls this macro by its Cortex-M4 name from
 * kern_clock.c, so the name stays and only the test underneath changes.
 */
#define BASEPRI(psr)    (arm_get_primask() == 0)         /* No masking. */

#define noop()          asm volatile("nop")

/*
 * Wait for something to happen.
 */
void idle(void);

/*
 * Millisecond delay routine.
 */
void mdelay(unsigned msec);

/*
 * Setup system timer for `hz' timer interrupts per second.
 */
void clkstart(void);

/*
 * Control LEDs, installed on the board.
 */
#define LED_TTY         0x08
#define LED_SWAP        0x04
#define LED_DISK        0x02
#define LED_KERNEL      0x01
#define LED_ALL         (LED_TTY | LED_SWAP | LED_DISK | LED_KERNEL)

/*
 * The Pico carries one LED on GP25, so every mask above lights the same one.
 * The STM32 header also declared LL_GPIO_EnableClock here, which is an ST HAL
 * entry point for gating a GPIO port's clock. The RP2040 has no such gate and
 * no such type, so the declaration is gone rather than ported.
 */
void led_control(int mask, int on);

#endif /* KERNEL */

#endif /* ENDIAN */
