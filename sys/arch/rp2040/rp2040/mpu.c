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
 * same 144 KB at USER_DATA_START (machparam.h, conf/RP2040.ld), so the
 * protection map is static: the kernel programs it once at startup and no
 * context switch touches it. Unprivileged code, which locore0.S enters by
 * setting CONTROL.nPRIV before the first user instruction, may reach four
 * regions and nothing else:
 *
 *   region  base         size    unprivileged  holds
 *   0       0x00000000   16 KB   r-x           the boot ROM, whose float
 *                                              routines libc calls through
 *                                              lib/libc/arm/gen/rom_float_resolver.S
 *   1       0x20000000   128 KB  rwx           the user window, low part
 *   2       0x20020000   16 KB   rwx           the user window, high part
 *   3       0xd0000000   256 B   rw-           the SIO hardware divider, one
 *                                              32-byte subregion of seven
 *
 * 144 KB is not a power of two and PMSAv6 regions are (datasheet table
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
 * because no instruction is fetched from a peripheral. The divider has one
 * owner: the kernel, its interrupts and its callouts contain no divider
 * consumer, which tools/verify_rp2040_divider_ownership.py enforces on
 * every linked kernel.
 *
 * The kernel runs privileged and MPU_CTRL.PRIVDEFENA keeps the default
 * memory map as its background region, so kernel text in XIP flash, kernel
 * data above the window, the peripherals and the PPB stay reachable to the
 * kernel and closed to a process. A user access outside the four regions
 * raises HardFault (datasheet 2.4.6.1), which fault.c delivers as SIGSEGV
 * to the process on its own stack.
 *
 * Every write is read back. A region counts as programmed only when RBAR
 * and RASR return what was written, and the MPU is enabled only when all
 * four do: a map missing a user region would fault the first user
 * instruction, so a core or emulator that drops the writes runs with the
 * MPU off and says so through mpu_identify and machdep.mpu. The memory
 * attribute bits S, C and B stay zero; the RP2040 bus fabric consumes none
 * of them (the XIP cache is an address-range cache), and the Cortex-M0+
 * reorders nothing.
 *
 * usr.bin/mputest is the deliberate fault test: it forks children that
 * read kernel RAM, kernel flash and SIO CPUID and expects each to die of
 * SIGSEGV when machdep.mpu.enable reads 1, while the divider register
 * 0x18 bytes above that closed CPUID reads without harm.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/sysctl.h>

#include <machine/mpu.h>
#include <machine/mpuvar.h>

struct mpu_state mpu_state;

struct mpu_region {
	u_int	base;			/* Aligned to 2^log2. */
	u_int	log2;			/* Size as a power of two. */
	u_int	attrs;			/* RASR AP and XN bits. */
	u_int	srd;			/* One bit per eighth; 1 disables. */
};

/*
 * The boot ROM is 16 KB at address 0 (datasheet 2.8); the two window
 * regions tile USER_DATA_SIZE, which the static assertion below pins.
 */
#define	MPU_ROM_LOG2		14
#define	MPU_USER_LOW_LOG2	17
#define	MPU_USER_HIGH_LOG2	14

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
_Static_assert(MPU_SIO_DIV_SRD == 0xf7UL,
    "only the subregion holding SIO offsets 0x060 to 0x07f may be enabled");

static const struct mpu_region mpu_map[] = {
	{ 0x00000000UL, MPU_ROM_LOG2, MPU_AP_RO, 0 },
	{ USER_DATA_START, MPU_USER_LOW_LOG2, MPU_AP_RW, 0 },
	{ USER_DATA_START + (1UL << MPU_USER_LOW_LOG2), MPU_USER_HIGH_LOG2,
	    MPU_AP_RW, 0 },
	{ MPU_SIO_BASE, MPU_SIO_LOG2, MPU_AP_RW | MPU_RASR_XN,
	    MPU_SIO_DIV_SRD },
};
#define	MPU_NMAP	(sizeof mpu_map / sizeof mpu_map[0])

/*
 * Write one region through RBAR with VALID set, which selects the region
 * and sets RNR in the same write (table 116), then its RASR; return 1 when
 * both read back as written.
 */
static int
mpu_program(u_int n, const struct mpu_region *r)
{
	u_int rbar, rasr;

	rbar = (r->base & MPU_RBAR_ADDR_MASK) | MPU_RBAR_VALID |
	    (n & MPU_RBAR_REGION_MASK);
	rasr = r->attrs | ((r->srd << MPU_RASR_SRD_SHIFT) & MPU_RASR_SRD_MASK) |
	    MPU_RASR_SIZE(r->log2) | MPU_RASR_ENABLE;

	MPU_REG32(MPU_RBAR) = rbar;
	MPU_REG32(MPU_RASR) = rasr;
	arm_dsb();

	/* VALID always reads as zero; RNR reads back through REGION. */
	return MPU_REG32(MPU_RNR) == n &&
	    MPU_REG32(MPU_RBAR) == ((r->base & MPU_RBAR_ADDR_MASK) | n) &&
	    MPU_REG32(MPU_RASR) == rasr;
}

void
mpu_init(void)
{
	u_int n;

	mpu_state.type = MPU_REG32(MPU_TYPE);
	mpu_state.nregions = (mpu_state.type & MPU_TYPE_DREGION_MASK) >>
	    MPU_TYPE_DREGION_SHIFT;
	mpu_state.programmed = 0;
	mpu_state.ctrl = 0;
	if (mpu_state.nregions < MPU_NMAP)
		return;

	/*
	 * Program with the MPU disabled, as the ARMv6-M ARM B3.5.4 orders
	 * it, then enable with DSB and ISB so no access after the enable is
	 * checked against the old map.
	 */
	MPU_REG32(MPU_CTRL) = 0;
	arm_dsb();
	for (n = 0; n < MPU_NMAP; n++)
		mpu_state.programmed += mpu_program(n, &mpu_map[n]);
	for (; n < mpu_state.nregions; n++) {
		MPU_REG32(MPU_RBAR) = MPU_RBAR_VALID | n;
		MPU_REG32(MPU_RASR) = 0;
	}
	if (mpu_state.programmed != MPU_NMAP) {
		mpu_state.ctrl = MPU_REG32(MPU_CTRL);
		return;
	}
	MPU_REG32(MPU_CTRL) = MPU_CTRL_PRIVDEFENA | MPU_CTRL_ENABLE;
	arm_dsb();
	arm_isb();
	mpu_state.ctrl = MPU_REG32(MPU_CTRL);
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
	default:
		return EOPNOTSUPP;
	}
	return sysctl_rdstruct(oldp, oldlenp, newp, &v, sizeof v);
}
