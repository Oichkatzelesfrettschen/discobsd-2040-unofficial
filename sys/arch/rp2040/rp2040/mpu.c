/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The user window under the Cortex-M0+ MPU.
 *
 * One process is resident at a time and every process image occupies the
 * same 192 KB at USER_DATA_START (machparam.h, conf/RP2040.ld), so regions 0
 * to 3 are static: the kernel programs them once at startup and no context
 * switch touches them. Region 7, the break guard below, is the one slot
 * rewritten afterwards. Unprivileged code, which icode in locore.S enters
 * by setting CONTROL.nPRIV in the user window, may reach four
 * regions and nothing else:
 *
 *   region  base         size    unprivileged  holds
 *   0       0x00000000   16 KB   r-x           the boot ROM, whose float
 *                                              routines libc calls through
 *                                              lib/libc/arm/gen/rom_float_resolver.S
 *   1       0x20000000   128 KB  rwx           the user window, low part
 *   2       0x20020000   64 KB   rwx           the user window, high part
 *   3       0xd0000000   256 B   rw-           the SIO hardware divider, one
 *                                              32-byte subregion of eight
 *
 * 192 KB is not a power of two and PMSAv6 regions are (datasheet table
 * 117, SIZE gives 2^(SIZE+1) bytes), so the window takes two regions, each
 * at a base aligned to its own size. Text runs from the window because
 * a.out images are loaded into SRAM, so XN stays clear on both.
 *
 * Region 3 carries the divider because the boot ROM's float division runs
 * in the calling process's context: mufp_fdiv, and every transcendental
 * that branches into fdiv_n, writes DIV_UDIVIDEND and DIV_UDIVISOR and
 * reads DIV_QUOTIENT (doc/research/float-libs.md section 4.1, from
 * pico-bootrom-rp2040's mufplib.S; datasheet 2.3.1.5 places those
 * registers at SIO offsets 0x060 to 0x078), so a map that closes SIO to
 * unprivileged code faults every user float and double division while
 * fadd, fsub, fmul and the divider-free conversions keep working. The
 * grant is one 32-byte subregion: a 256-byte region divides into eight,
 * and only subregion 3, offsets 0x060 to 0x07f, is enabled, so the divider
 * is reachable and CPUID, the GPIO control registers, the inter-core FIFO,
 * the spinlocks and the interpolators next to it stay closed. XN is set
 * because no instruction is fetched from a peripheral. User code is the
 * divider's only consumer, through that boot ROM division and the libc
 * integer helpers (lib/libc/arm/gen/aeabi_div.S): the kernel, its
 * interrupts and its callouts contain none, which
 * tools/verify_rp2040_divider_ownership.py enforces on every linked
 * kernel. A clock tick can interrupt a user divide sequence. The syscall,
 * asynchronous trap and user fault paths save and restore the divider
 * through the user frame.
 *
 * The kernel runs privileged and MPU_CTRL.PRIVDEFENA keeps the default
 * memory map as its background region, so kernel text in XIP flash, kernel
 * data above the window, the peripherals and the PPB stay reachable to the
 * kernel and closed to a process. A user access outside the four regions
 * raises HardFault (datasheet 2.4.6.1), which fault.c delivers as SIGSEGV
 * to the process on its own stack.
 *
 * A grant counts as programmed only when RNR, RBAR and RASR match the
 * requested region. The kernel requests MPU enable only when all
 * four match and the remaining slots read back disabled. A map missing
 * a user region could fault the first user instruction. mpu_identify and
 * machdep.mpu report the control register after verification. The memory
 * attribute bits S, C and B stay zero; the RP2040 bus fabric consumes none
 * of them (the XIP cache is an address-range cache), and the Cortex-M0+
 * reorders nothing.
 *
 * Region 7 is the break guard (machine/breakguard.h): 32 bytes at the
 * process break rounded up to 32, inside the window, that unprivileged
 * code may read but not write. It wins over region 1 or 2 as the higher
 * number, so a user store into those 32 bytes faults, and
 * fault.c delivers SIGSEGV as for any other bad address; a handler that
 * grows the break and returns replays the store. AP 010 keeps the bytes
 * writable to the kernel, so copyout, swapin, exec_clear and the frame
 * writes of an exception return never fault there. mpu_guard_update
 * recomputes the guard at every return to a process (syscall.c ast() and
 * syscall(), fault.c arm_fault()) from the exact break, p_daddr +
 * u_dsize as user_entry() compares it, and arms it only while the stack
 * stays BREAK_GUARD_MARGIN bytes clear of it; the registers are written
 * only when that answer changes. systick() turns it off when a tick finds
 * the process stack below the armed floor, and it stays off for that
 * image while p_saddr records the deeper stack. The guard takes no byte
 * below the break and none above the stack pointer: the pad between the
 * exact break and the rounded base stays writable. A stack that jumps into
 * the guard between two kernel entries cannot be resumed, because the
 * HardFault frame would land in it; arm_fault kills that process with
 * SIGKILL. sys/arch/rp2040/doc/MPU.md records the kill window.
 *
 * usr.bin/mputest is the deliberate fault test: it forks children that
 * read kernel RAM, kernel flash and SIO CPUID and expects each to die of
 * SIGSEGV when machdep.mpu.enable reads 1, while the divider register
 * 0x78 bytes above that closed CPUID reads without harm.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/sysctl.h>

#include <machine/breakguard.h>
#include <machine/mpu.h>
#include <machine/mpuvar.h>
#include <machine/usercopy.h>

/* Boot-time register readback for the console and machdep.mpu. */
static struct {
	u_int	type;
	u_int	ctrl;
	u_char	nregions;	/* MPU_TYPE.DREGION is eight bits. */
	u_char	programmed;	/* At most one per selectable slot. */
} mpu_state;

struct mpu_region {
	u_int	base;			/* Aligned to 2^log2. */
	u_int	rasr;			/* Encoded attributes, size and enable. */
};

/*
 * The boot ROM is 16 KB at address 0 (datasheet 2.8); the two window
 * regions tile USER_DATA_SIZE, which the static assertion below pins.
 */
#define	MPU_ROM_LOG2		14
#define	MPU_USER_LOW_LOG2	17
#define	MPU_USER_HIGH_LOG2	16

/*
 * The divider grant: the smallest region PMSAv6 admits, 256 bytes, at the
 * base of SIO, with every subregion disabled but the one holding
 * DIV_UDIVIDEND through DIV_CSR.
 */
#define	MPU_SIO_BASE		0xd0000000UL
#define	MPU_SIO_LOG2		8		/* 256 bytes, the minimum. */
#define	MPU_SIO_DIV_OFFSET	0x060UL		/* Datasheet 2.3.1.5. */
#define	MPU_SIO_DIV_SUBREGION	(MPU_SIO_DIV_OFFSET / (1UL << (MPU_SIO_LOG2 - 3)))
#define	MPU_SIO_DIV_SRD		(0xffUL & ~(1UL << MPU_SIO_DIV_SUBREGION))

_Static_assert((1UL << MPU_USER_LOW_LOG2) + (1UL << MPU_USER_HIGH_LOG2) ==
    USER_DATA_SIZE, "the two MPU window regions must tile USER_DATA_SIZE");
_Static_assert((USER_DATA_START & ((1UL << MPU_USER_LOW_LOG2) - 1)) == 0,
    "USER_DATA_START must be aligned to the low window region");
_Static_assert(((USER_DATA_START + (1UL << MPU_USER_LOW_LOG2)) &
    ((1UL << MPU_USER_HIGH_LOG2) - 1)) == 0,
    "the high window region base must be aligned to its size");
_Static_assert(MPU_SIO_DIV_SRD == 0xf7UL,
    "only the subregion holding SIO offsets 0x060 to 0x07f may be enabled");
_Static_assert(BREAK_GUARD_RASR_AP == MPU_AP_PRIV_RW_USER_RO &&
    BREAK_GUARD_RASR_XN == MPU_RASR_XN &&
    BREAK_GUARD_RASR_ENABLE == MPU_RASR_ENABLE &&
    BREAK_GUARD_RASR_SIZE == MPU_RASR_SIZE(8) &&
    BREAK_GUARD_RASR_SRD_SHIFT == MPU_RASR_SRD_SHIFT &&
    BREAK_GUARD_RBAR_VALID == MPU_RBAR_VALID &&
    BREAK_GUARD_RBAR_ADDR == MPU_RBAR_ADDR_MASK,
    "breakguard.h encodes region 7 with the mpu.h field values");
_Static_assert(BREAK_GUARD_BYTES == (1UL << (8 - 3)),
    "the guard is one subregion of a 256-byte region");

#define	MPU_GRANT(log2, attrs, srd) \
	(MPU_RASR_SIZE(log2) | (attrs) | ((srd) << MPU_RASR_SRD_SHIFT) | \
	    MPU_RASR_ENABLE)

static const struct mpu_region mpu_map[] = {
	{ 0, MPU_GRANT(MPU_ROM_LOG2, MPU_AP_RO, 0) },
	{ USER_DATA_START, MPU_GRANT(MPU_USER_LOW_LOG2, MPU_AP_RW, 0) },
	{ USER_DATA_START + (1UL << MPU_USER_LOW_LOG2),
	    MPU_GRANT(MPU_USER_HIGH_LOG2, MPU_AP_RW, 0) },
	{ MPU_SIO_BASE, MPU_GRANT(MPU_SIO_LOG2, MPU_AP_RW | MPU_RASR_XN,
	    MPU_SIO_DIV_SRD) },
};
#define	MPU_NMAP	(sizeof mpu_map / sizeof mpu_map[0])

/*
 * Write one region through RBAR with VALID set, which selects the region
 * and sets RNR in the same write (table 116), then its RASR; return 1 when
 * both read back as written.
 */
static int
mpu_program(u_int n, u_int base, u_int rasr)
{
	MPU_REG32(MPU_RBAR) = base | MPU_RBAR_VALID | n;
	MPU_REG32(MPU_RASR) = rasr;
	arm_dsb();

	/* VALID reads as zero and REGION reads as RNR. */
	return MPU_REG32(MPU_RNR) == n &&
	    MPU_REG32(MPU_RBAR) == (base | n) && MPU_REG32(MPU_RASR) == rasr;
}

static void
mpu_control(u_int ctrl)
{
	MPU_REG32(MPU_CTRL) = ctrl;
	arm_dsb();
	arm_isb();
	mpu_state.ctrl = MPU_REG32(MPU_CTRL);
}

static void
mpu_disable(void)
{
	mpu_control(0);
	if (mpu_state.ctrl != 0)
		panic("mpu: cannot disable");
}

void
mpu_init(void)
{
	const u_int ctrl = MPU_CTRL_PRIVDEFENA | MPU_CTRL_ENABLE;
	const struct mpu_region *r;
	u_int n;

	mpu_state.type = MPU_REG32(MPU_TYPE);
	mpu_state.nregions = (mpu_state.type & MPU_TYPE_DREGION_MASK) >>
	    MPU_TYPE_DREGION_SHIFT;
	mpu_state.programmed = 0;
	mpu_state.ctrl = 0;
	if (mpu_state.nregions == 0)
		return;

	mpu_disable();
	if (mpu_state.nregions < MPU_NMAP ||
	    mpu_state.nregions > MPU_RBAR_REGION_MASK + 1)
		return;

	for (n = 0; n < MPU_NMAP; n++) {
		r = &mpu_map[n];
		mpu_state.programmed += mpu_program(n, r->base, r->rasr);
	}
	for (; n < mpu_state.nregions; n++) {
		if (!mpu_program(n, 0, 0))
			return;
	}
	if (mpu_state.programmed == MPU_NMAP) {
		mpu_control(ctrl);
		if (mpu_state.ctrl != ctrl)
			mpu_disable();
	}
}

/*
 * The armed guard's floor (rp2040_break_guard_floor), zero while region 7
 * is disabled. It is the only state the guard keeps: the base, RBAR and
 * RASR follow from it.
 */
u_int mpu_guard_floor;

/*
 * Region 7 is disabled before its base moves, so no instant covers a base
 * with another base's subregion. RNR selects the slot for the RASR write;
 * the RBAR write selects it again through VALID.
 */
static void
mpu_guard_set(u_int floor)
{
	MPU_REG32(MPU_RNR) = BREAK_GUARD_REGION;
	MPU_REG32(MPU_RASR) = 0;
	MPU_REG32(MPU_RBAR) = rp2040_break_guard_rbar(floor);
	MPU_REG32(MPU_RASR) = rp2040_break_guard_rasr(floor);
	arm_dsb();
	arm_isb();
	mpu_guard_floor = floor;
}

/*
 * Place the guard for the current process as it resumes with stack
 * pointer sp. A map that failed verification, or a core without region 7,
 * leaves it off.
 */
void
mpu_guard_update(u_int sp)
{
	const struct proc *p = u.u_procp;
	u_int floor = 0;

	if ((mpu_state.ctrl & MPU_CTRL_ENABLE) &&
	    mpu_state.nregions > BREAK_GUARD_REGION)
		floor = rp2040_break_guard_floor(p->p_daddr + u.u_dsize,
		    p->p_saddr, sp);
	if (floor != mpu_guard_floor)
		mpu_guard_set(floor);
}

/* systick() calls this from the tick that finds the stack near the guard. */
void
mpu_guard_off(void)
{
	mpu_guard_set(0);
}

/*
 * Return 1 when [start, start + nbytes) lies inside one region of the map
 * that grants unprivileged code read and denies it write (AP 110), across
 * enabled subregions only. Those are the bytes a process may read but the
 * kernel must never write on its behalf: copyin and the source vectors of a
 * write admit them through baduaddr_rspan (machdep.c), while copyout, the
 * targets of a read and physio stay inside the window, the mvalid model
 * of a read-only mapping. The answer comes from the static map rather than
 * from the registers, so a core that dropped the writes and runs with the
 * MPU off admits no more than one that took them. The boot ROM is the
 * one such region in the map.
 */
int
mpu_user_rospan(u_int start, u_int nbytes)
{
	const struct mpu_region *r;

	/* Unrolling lets the compiler fold each fixed grant and its bounds. */
#pragma GCC unroll 4
	for (r = mpu_map; r < &mpu_map[MPU_NMAP]; r++)
		if ((r->rasr & MPU_RASR_AP_MASK) == MPU_AP_RO &&
		    rp2040_user_region_span_contains(r->base,
		    1 + ((r->rasr & MPU_RASR_SIZE_MASK) >> MPU_RASR_SIZE_SHIFT),
		    (r->rasr & MPU_RASR_SRD_MASK) >> MPU_RASR_SRD_SHIFT,
		    start, nbytes))
			return 1;
	return 0;
}

/*
 * One console line after the cpu: banner, stating what the registers read
 * back rather than what was written; the Renode gate asserts it.
 */
void
mpu_identify(void)
{
	if (mpu_state.nregions == 0) {
		printf("mpu: MPU_TYPE 0x%08x reports no regions, protection off\n",
		    mpu_state.type);
		return;
	}
	printf("mpu: %u regions, %u programmed, MPU_CTRL 0x%x: ",
	    mpu_state.nregions, mpu_state.programmed, mpu_state.ctrl);
	if (mpu_state.ctrl & MPU_CTRL_ENABLE)
		printf("rom %uK r-x, user %uK rwx, sio div rw\n",
		    (1U << MPU_ROM_LOG2) / 1024, USER_DATA_SIZE / 1024);
	else
		printf("protection off\n");
}

int
mpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int v;

	(void)newlen;
	if (namelen != 1)
		return ENOTDIR;

	switch (name[0]) {
	case CPU_MPU_ENABLE:
		v = (mpu_state.ctrl & MPU_CTRL_ENABLE) != 0;
		break;
	case CPU_MPU_CTRL:
		v = mpu_state.ctrl;
		break;
	case CPU_MPU_NREGIONS:
		v = mpu_state.nregions;
		break;
	case CPU_MPU_SEPARATE:
		v = (mpu_state.type & MPU_TYPE_SEPARATE) != 0;
		break;
	case CPU_MPU_PROGRAMMED:
		v = mpu_state.programmed;
		break;
	case CPU_MPU_GUARDBASE:
	case CPU_MPU_GUARDATTR:
		/* Region 7 as read back, while this process runs. */
		v = 0;
		if (mpu_state.nregions > BREAK_GUARD_REGION) {
			MPU_REG32(MPU_RNR) = BREAK_GUARD_REGION;
			v = MPU_REG32(name[0] == CPU_MPU_GUARDBASE ?
			    MPU_RBAR : MPU_RASR);
		}
		break;
	default:
		return EOPNOTSUPP;
	}
	return sysctl_rdstruct(oldp, oldlenp, newp, &v, sizeof v);
}
