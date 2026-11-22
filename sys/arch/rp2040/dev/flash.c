/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Root filesystem on the RP2040's QSPI flash.
 *
 * DiscoBSD's other targets boot from an SD card, and this board lacks an
 * socket, so the onboard flash becomes the block device instead. The idea of
 * putting a Unix root on a Pico's flash behind a wear-leveling translation
 * layer is FUZIX's; none of its code is used here, because the FUZIX kernel
 * is GPL-2.0 and this tree is not. Its implementation was deliberately not
 * read. What both need is the same published library, Dhara, which is ISC
 * licensed and vendored under sys/arch/rp2040/dhara.
 *
 * The layering is:
 *
 *	bdevsw			block requests in DEV_BSIZE units
 *	  dhara_map_*		logical sectors, wear leveling, garbage
 *				collection			(ISC, vendored)
 *	    dhara_nand_*	this file: NOR geometry and erase or program
 *	      bootrom		flash_range_erase, flash_range_program
 *
 * Dhara is written for NAND, and this part is NOR, which suits Dhara at zero cost and
 * saves something. Its contract wants pages programmed sequentially within an
 * eraseblock and never reprogrammed, which NOR satisfies. NOR ships without
 * factory bad blocks and lacks ECC, so the bad-block callbacks below are honest
 * constants rather than stubs awaiting work.
 *
 * The RP2040 constraint that shapes everything here: erasing or programming
 * flash takes the QSPI interface out of execute-in-place, so code driving one
 * cannot be fetched from flash while it runs. Those functions carry
 * __ramfunc, which conf/kern.ldscript places in RAM. Reads go through the XIP
 * window normally, and only while no erase or program is in flight.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/storage_stats.h>
#include <sys/conf.h>
#include <sys/dk.h>
#include <sys/disk.h>

#include <machine/intr.h>
#include <machine/scb.h>
#include <machine/watchdog.h>

#include <rp2040/dev/flash.h>
#include <rp2040/dev/flash_swap.h>
#include <rp2040/dev/loader_stamp.h>
#include <rp2040/dhara/map.h>
#include <rp2040/rp2040/clock_catchup.h>

#ifdef STORAGE_STATS
#include <rp2040/dev/flash_reset_hook.h>
#include <rp2040/dev/flash_wear.h>
#include <rp2040/rp2040/core1.h>

u_short flash_swap_wear[FLASH_SWAP_SECTORS];

static struct storage_stats flstats = {
    .version = STORAGE_STATS_VERSION,
    .size = sizeof(struct storage_stats)
};

void
storage_note(enum storage_metric metric, uint32_t amount)
{
    int s = splhigh();

    storage_stats_add(&flstats, metric, amount);
    splx(s);
}

void
storage_dirty_sample(void)
{
    unsigned i, count = 0;
    int s = splhigh();

    for (i = 0; i < NBUF; i++)
        if (buf[i].b_flags & B_DELWRI)
            count++;
    flstats.dirty_current = count;
    if (count > flstats.dirty_peak)
        flstats.dirty_peak = count;
    splx(s);
}

void
storage_snapshot(struct storage_stats *stats)
{
    int s = splhigh();

    storage_dirty_sample();
    *stats = flstats;
    splx(s);
}
#endif

/*
 * Bootrom entry points. The RP2040 boot ROM publishes a function table whose
 * entries are found by a two-character code; see the RP2040 datasheet section
 * 2.8.3. Calling the ROM keeps this driver self-contained, since the flash
 * routines in the Pico SDK are themselves thin wrappers over these.
 */
#define	ROM_FUNC_TABLE_ADDR	0x14
#define	ROM_TABLE_LOOKUP_ADDR	0x18

#define	ROM_CONNECT_INTERNAL_FLASH	ROM_CODE('I', 'F')
#define	ROM_FLASH_EXIT_XIP		ROM_CODE('E', 'X')
#define	ROM_FLASH_RANGE_ERASE		ROM_CODE('R', 'E')
#define	ROM_FLASH_RANGE_PROGRAM		ROM_CODE('R', 'P')
#define	ROM_FLASH_FLUSH_CACHE		ROM_CODE('F', 'C')

typedef void *(*rom_lookup_fn)(u_short *table, u_int code);
typedef void (*rom_void_fn)(void);
typedef void (*rom_erase_fn)(u_int addr, u_int count, u_int blk, u_char cmd);
typedef void (*rom_program_fn)(u_int addr, const u_char *data, u_int count);

struct flash_rom {
	rom_void_fn	connect;
	rom_void_fn	exit_xip;
	rom_erase_fn	erase;
	rom_program_fn	program;
	rom_void_fn	flush;
};

static struct flash_rom flrom;
static int flrom_ready;

/*
 * The writable flash end and the raw swap extent below it, which
 * flash_limit_init() sets from the PicoCalc loader's stamp before any flash
 * device opens. Both start at zero in bss, where flash_write_admitted()
 * refuses every erase and program, so a write issued before the stamp is
 * read fails rather than reaching the loader's flash. flash_loader_boot2
 * records that the stamp's magic is present: the loader then replaced flash
 * bytes 0-255 with its own second stage, which boot2_copy holds.
 */
static uint32_t flash_write_end;
static uint32_t flswap_bytes;
static u_char flash_loader_boot2;

/*
 * The second stage, copied out so XIP can be restored after a write.
 * flash_enter_xip calls the copy, so it needs executable SRAM: USB DPSRAM
 * sits in the ARMv6-M peripheral region, which is execute-never, and the
 * USB console's transmit ring fills both scratch banks.  The USB kernel
 * therefore keeps the copy in bss; the UART-only kernel places it in SRAM4
 * through kern.ldscript.
 */
#define	BOOT2_WORDS	64
#ifdef UARTUSB_ENABLED
u_int boot2_copy[BOOT2_WORDS];	/* Global: kern.ldscript's PROVIDE yields. */
#else
extern u_int boot2_copy[BOOT2_WORDS];
#endif

static struct dhara_map flmap;
/* Dhara owns this page for the map's lifetime; the linker keeps it out of bss. */
extern u_char flpage[FLASH_UNIT_BYTES];
static int flmap_ready;

/*
 * One physical program page shared by the raw-swap tail and Dhara's page
 * copy. Both paths run under splbio from entry to exit, inside flstrategy or
 * fl_checkpoint, and neither sleeps, since flash_erase and flash_program
 * spin. flscratch_owner names the path holding the page: each of those paths
 * refuses to enter while it is held, and dhara_nand_copy, which Dhara also
 * reaches from dhara_map_resume at setup, refuses to run while the swap path
 * holds it. The owner check turns a broken lifetime argument into a panic
 * rather than silent corruption.
 */
/*
 * The linker places this physical program page outside ordinary SRAM:
 * in the unused top of USB DPSRAM for the USB kernel, or at the top of
 * SRAM5 for the UART-only kernel.  DPSRAM supports byte, halfword, and
 * word accesses (RP2040 datasheet 4.1.2.7), no flash operation sleeps
 * while it owns the page, and the USB endpoint descriptors never address
 * its DPSRAM range.  Keeping the address in the linker script makes both
 * placements visible to the linked-image verifier and returns the page to
 * kernel RAM.
 */
extern u_char flscratch[FLASH_PROG_BYTES];
static int flscratch_owner;
#define	FLS_FREE	0
#define	FLS_SWAP	1			/* fl_raw programs a tail. */
#define	FLS_MAP		2			/* Dhara may copy a page. */

static void
flscratch_take(int owner)
{
	if (flscratch_owner != FLS_FREE)
		panic("R01");
	flscratch_owner = owner;
}
static daddr_t flblocks;		/* Capacity in DEV_BSIZE blocks. */

/* The filesystem region as Dhara sees it; see flash.h for the choice. */
const struct dhara_nand flnand = {
	FLASH_LOG2_UNIT,
	FLASH_LOG2_UPB,
	FLASH_FS_BLOCKS,
};

#if DEV_BSIZE % FLASH_UNIT_BYTES != 0
#error "DEV_BSIZE must be a multiple of the Dhara page"
#endif
#if FLASH_UNIT_BYTES % FLASH_PROG_BYTES != 0
#error "The Dhara page must contain whole flash program pages"
#endif
#if FLASH_SECTOR_BYTES % FLASH_PROG_BYTES != 0
#error "The erase sector must contain whole flash program pages"
#endif
#if SWAP_IMAGE_ALIGN * DEV_BSIZE != FLASH_SECTOR_BYTES
#error "SWAP_IMAGE_ALIGN must describe one flash erase sector"
#endif

/*
 * The two reads below take 16-bit pointers from fixed boot ROM addresses.
 * GCC models a constant cast to a pointer as a zero-length array and reports
 * every such read as out of bounds, which volatile does not suppress because
 * the objection is to the address rather than to caching. The addresses are
 * architectural, so the diagnostic is disabled across exactly these two lines
 * rather than the file.
 */
#pragma GCC diagnostic push
#if defined(__GNUC__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

/*
 * Each ROM entry is a 16-bit address widened through u_int to a pointer,
 * the role uintptr_t has in the Pico SDK's rom_func_lookup(). The kernel's
 * header set lacks uintptr_t, so the width it relies on is asserted here.
 */
_Static_assert(sizeof(u_int) == sizeof(void *) &&
    sizeof(u_short) == 2, "ROM table entries widen through u_int");

void *
rom_func_lookup(u_int code)
{
	rom_lookup_fn lookup;
	u_short *table;

	/*
	 * Both are fixed locations in the boot ROM holding 16-bit pointers,
	 * so they are read through volatile: the compiler lacks an object to
	 * reason about at an absolute address and warns about bounds if
	 * allowed to try.
	 */
	lookup = (rom_lookup_fn)(u_int)
	    *(volatile u_short *)ROM_TABLE_LOOKUP_ADDR;
	table = (u_short *)(u_int)
	    *(volatile u_short *)ROM_FUNC_TABLE_ADDR;
	return lookup(table, code);
}

#pragma GCC diagnostic pop

static void
flash_rom_init(void)
{
	const u_int *xip = (const u_int *)FLASH_XIP_BASE;
	int i;

	if (flrom_ready)
		return;

	flrom.connect = (rom_void_fn)rom_func_lookup(
	    ROM_CONNECT_INTERNAL_FLASH);
	flrom.exit_xip = (rom_void_fn)rom_func_lookup(ROM_FLASH_EXIT_XIP);
	flrom.erase = (rom_erase_fn)rom_func_lookup(ROM_FLASH_RANGE_ERASE);
	flrom.program = (rom_program_fn)rom_func_lookup(
	    ROM_FLASH_RANGE_PROGRAM);
	flrom.flush = (rom_void_fn)rom_func_lookup(ROM_FLASH_FLUSH_CACHE);

	/*
	 * Take a copy of the second stage before any write disables XIP,
	 * because restoring XIP afterwards means calling it, and by then it
	 * cannot be read from flash.
	 */
	for (i = 0; i < BOOT2_WORDS; i++)
		boot2_copy[i] = xip[i];

	flrom_ready = 1;
}

/*
 * The XIP cache control register, datasheet 2.6.3.1. EN resets to 1 and the
 * boot ROM's flush routine sets it again after every erase and program, so a
 * request to run uncached is kept here and reapplied by flash_enter_xip().
 */
#define	XIP_CTRL_REG	(*(volatile u_int *)0x14000000UL)
#define	XIP_CTRL_FLUSH	(*(volatile u_int *)0x14000004UL)
#define	XIP_CTRL_EN	1U

static u_char flash_xip_uncached;

/*
 * Turn the XIP cache off (request 0) or on (request positive), or leave it
 * alone (request negative); return whether it is enabled. With the cache off
 * every flash access, kernel text included, goes to the chip, which is the
 * control for a measurement of what the cache saves. Turning it on flushes
 * first: the tags kept from before would otherwise answer for flash written
 * in between.
 */
int
flash_xip_cache(int request)
{
	int s;

	if (request >= 0) {
		s = splhigh();
		flash_xip_uncached = request == 0;
		if (request == 0) {
			XIP_CTRL_REG &= ~XIP_CTRL_EN;
		} else {
			XIP_CTRL_FLUSH = 1;
			(void)XIP_CTRL_FLUSH;
			XIP_CTRL_REG |= XIP_CTRL_EN;
		}
		splx(s);
	}
	return (XIP_CTRL_REG & XIP_CTRL_EN) != 0;
}

/*
 * The SSI that serves execute-in-place, datasheet 4.10. BAUDR divides clk_sys
 * down to the flash clock and is written only while SSIENR is 0 (4.10.4 and
 * the DW_apb_ssi programming sequence in 4.10). boot2/boot2_w25q080.S sets
 * the divider to 2 and the receive sample delay to 1; the PicoCalc UF2
 * loader's second stage (pelrun/uf2loader stage3/boot2_custom.S) builds the
 * same continuous-read configuration with a divider of 4.
 */
#define	XIP_SSI_REG(off)	(*(volatile u_int *)(0x18000000UL + (off)))
#define	SSI_SSIENR		0x08
#define	SSI_BAUDR		0x14
#define	SSI_SR			0x28
#define	SSI_SR_BUSY		1U
#define	SSI_RX_SAMPLE_DLY	0xf0
#define	SSI_XIP_CLKDIV		2
#define	SSI_XIP_SAMPLE_DLY	1

/*
 * Put the XIP read clock back to boot2_w25q080.S's divider after the loader's
 * second stage has run. The SSI serves no fetch while disabled, so this runs
 * from RAM, and its callers mask interrupts, whose vectors are in flash. The
 * flash stays in continuous-read mode throughout: only the clock divider and
 * sample point change, and the next XIP access carries the same address and
 * mode bits.
 */
static __ramfunc void
flash_ssi_restore(void)
{
	while ((XIP_SSI_REG(SSI_SR) & SSI_SR_BUSY) != 0)
		continue;
	XIP_SSI_REG(SSI_SSIENR) = 0;
	XIP_SSI_REG(SSI_BAUDR) = SSI_XIP_CLKDIV;
	XIP_SSI_REG(SSI_RX_SAMPLE_DLY) = SSI_XIP_SAMPLE_DLY;
	XIP_SSI_REG(SSI_SSIENR) = 1;
}

/*
 * Re-enter execute-in-place by calling the copied second stage. It returns to
 * its caller when entered with a non-zero link register, which is how the
 * Pico SDK re-enables XIP after a write, and is why the copy is kept. Under
 * the PicoCalc loader the copy is the loader's second stage, whose divider
 * flash_ssi_restore() replaces.
 */
static __ramfunc void
flash_enter_xip(void)
{
	if (flash_xip_uncached)
		XIP_CTRL_REG &= ~XIP_CTRL_EN;
	((void (*)(void))((u_int)boot2_copy + 1))();
	if (flash_loader_boot2)
		flash_ssi_restore();
}

#ifdef STORAGE_STATS
#define	AIRCR_VECTKEY		0x05fa0000UL
#define	AIRCR_SYSRESETREQ	(1UL << 2)

static struct flash_reset_hook flhook;

/*
 * Reset the chip after the command the hook counted, from inside the window
 * of flash_erase() or flash_program(): the ROM call has returned, so the
 * chip is idle (the ROM waits out each command's busy bit), and XIP is down.
 * The routine finishes the ROM sequence the request would have ended with,
 * flush_cache and the second stage, which leaves the cache and the QSPI pads
 * as a kernel running normally leaves them when usb_reset_to_flash() and
 * boot() write AIRCR, and puts the flash-resident core1_park() and
 * watchdog_site() back in reach. Datasheet 2.4.2.9: SYSRESETREQ resets the
 * processor core and nothing else, so core 1 is parked first and the QSPI
 * interface keeps the state left here. The warm boot that follows issues
 * its own XIP exit sequence (datasheet 2.8.1.2), written for a flash left
 * in continuous-read mode, which is the state every other reset of a
 * running kernel hands it. watchdog_site() leaves the reset's kind and the
 * offset of the last command in the scratch pair for the next boot
 * (machdep.watchdog_site and machdep.watchdog_arg). The AIRCR write and the
 * wait for the reset are RAM-resident whichever way XIP was left. Interrupts
 * stay masked throughout.
 */
static __ramfunc void
flash_hook_reset(u_int erase, u_int offset)
{
	flrom.flush();
	flash_enter_xip();
	watchdog_site(erase ? WD_SITE_RESET_AT_ERASE :
	    WD_SITE_RESET_AT_PROGRAM, offset);
#ifdef CORE1
	core1_park();
#endif
	arm_dsb();
	SCB_REG32(SCB_AIRCR) = AIRCR_VECTKEY | AIRCR_SYSRESETREQ;
	arm_dsb();
	for (;;)
		continue;
}

/*
 * The two accessors behind machdep.flash_reset_program and
 * machdep.flash_reset_erase (rp2040/sysctl.c): the commands of one kind
 * issued since arming, and arming that kind to reset after its Nth command
 * from now, or disarming it with 0.
 */
u_int
flash_reset_hook_count(u_int erase)
{
	struct flash_hook_kind *kind = erase ? &flhook.erase : &flhook.program;
	u_int count;
	int s;

	s = splhigh();
	count = kind->count;
	splx(s);
	return count;
}

void
flash_reset_hook_arm(u_int erase, u_int n)
{
	struct flash_hook_kind *kind = erase ? &flhook.erase : &flhook.program;
	int s;

	s = splhigh();
	flash_hook_arm(kind, n);
	splx(s);
}
#endif

/*
 * Chip identity. JEDEC ID (9Fh) returns the manufacturer and two device
 * bytes, the second of which is the capacity as a power of two; Read Unique
 * ID (4Bh) returns 64 factory-set bits after four dummy bytes
 * (W25Q16JV revision G, instruction set table 1 and section 8.2.24).
 *
 * The boot ROM lacks an entry for an arbitrary command, so the exchange runs
 * on the SSI directly, as the Pico SDK's flash_do_cmd() does: the ROM's
 * exit_xip leaves the SSI in eight-bit serial mode, chip select is held low
 * through the IO_QSPI output override because the SSI releases it whenever
 * its transmit FIFO empties, and the ROM's flush routine returns the
 * override to normal before the second stage restores execute-in-place.
 * At most 14 bytes are in flight, under the 16-entry receive FIFO. The loop
 * is bounded: an SSI that stops answering leaves the identity unread
 * instead of stopping the kernel under splhigh(). The exchange is a
 * declared section like an erase or a program (watchdog_site()), so core 1
 * reads the stopped clock as declared and its NMI, whose vector is in
 * flash, stays withdrawn; startup() captures the previous boot's record
 * before the first exchange overwrites the scratch pair.
 */
#define	IO_QSPI_SS_CTRL_REG	(*(volatile u_int *)0x4001800cUL)
#define	QSPI_SS_OUTOVER_MASK	(3U << 8)
#define	QSPI_SS_OUTOVER_LOW	(2U << 8)
#define	SSI_DR0			0x60
#define	SSI_SR_TFNF		2U
#define	SSI_SR_RFNE		8U
#define	FLASH_CMD_INFLIGHT	14
#define	FLASH_CMD_SPINS		200000U

#define	FLASH_CMD_JEDEC_ID	0x9f
#define	FLASH_CMD_UNIQUE_ID	0x4b
#define	FLASH_UNIQUE_DUMMY	4

static u_char flash_jedec[FLASH_JEDEC_BYTES];
static u_char flash_unique[FLASH_UNIQUE_BYTES];
static signed char flash_id_state;	/* 0 unread, 1 read, -1 unreadable. */

/*
 * Clock count bytes of frame out and replace each with the byte the chip
 * returned during it. Returns 0 once every byte has come back.
 */
static __ramfunc int
flash_cmd(u_char *frame, u_int count)
{
	const u_char *out = frame;
	u_char *in = frame;
	u_int tx = count, rx = count, spins = FLASH_CMD_SPINS, sr;
	int s;

	s = splhigh();
	watchdog_site(WD_SITE_FLASH_COMMAND, frame[0]);
	flrom.connect();
	flrom.exit_xip();
	IO_QSPI_SS_CTRL_REG = (IO_QSPI_SS_CTRL_REG & ~QSPI_SS_OUTOVER_MASK) |
	    QSPI_SS_OUTOVER_LOW;
	while (rx != 0 && --spins != 0) {
		sr = XIP_SSI_REG(SSI_SR);
		if ((sr & SSI_SR_TFNF) != 0 && tx != 0 &&
		    rx - tx < FLASH_CMD_INFLIGHT) {
			XIP_SSI_REG(SSI_DR0) = *out++;
			tx--;
		}
		if ((sr & SSI_SR_RFNE) != 0) {
			*in++ = (u_char)XIP_SSI_REG(SSI_DR0);
			rx--;
		}
	}
	flrom.flush();
	flash_enter_xip();
	watchdog_site(WD_SITE_NONE, 0);
	splx(s);
	return rx == 0 ? 0 : -1;
}

/*
 * Read the identity once and keep it. A reply of all zeros or all ones is a
 * bus with no chip driving it, and counts as unread. Returns 0 when jedec
 * and unique hold the chip's answers. Interrupts are masked for the
 * exchange and core 1 executes from RAM alone (rp2040/core1.c), so nothing
 * fetches from flash while execute-in-place is down.
 */
int
flash_identity(const u_char **jedec, const u_char **unique)
{
	u_char frame[1 + FLASH_UNIQUE_DUMMY + FLASH_UNIQUE_BYTES];
	u_int i;

	if (flash_id_state == 0) {
		flash_rom_init();
		flash_id_state = -1;
		bzero(frame, sizeof(frame));
		frame[0] = FLASH_CMD_JEDEC_ID;
		if (flash_cmd(frame, 1 + FLASH_JEDEC_BYTES) == 0 &&
		    flash_id_plausible(frame + 1, FLASH_JEDEC_BYTES)) {
			for (i = 0; i < FLASH_JEDEC_BYTES; i++)
				flash_jedec[i] = frame[1 + i];
			bzero(frame, sizeof(frame));
			frame[0] = FLASH_CMD_UNIQUE_ID;
			if (flash_cmd(frame, sizeof(frame)) == 0 &&
			    flash_id_plausible(frame + 1 + FLASH_UNIQUE_DUMMY,
			    FLASH_UNIQUE_BYTES)) {
				for (i = 0; i < FLASH_UNIQUE_BYTES; i++)
					flash_unique[i] =
					    frame[1 + FLASH_UNIQUE_DUMMY + i];
				flash_id_state = 1;
			}
		}
	}
	if (flash_id_state != 1)
		return -1;
	*jedec = flash_jedec;
	*unique = flash_unique;
	return 0;
}

/*
 * Read the PicoCalc loader's stamp from vector entries 4 and 5 and fix the
 * writable flash end for the life of the kernel; loader_stamp.h states the
 * stamp and the decode. startup() calls this once, after the console and
 * before config() attaches any device, so no flash write precedes it.
 */
void
flash_limit_init(void)
{
	const volatile u_int *xip = (const volatile u_int *)FLASH_XIP_BASE;
	struct loader_layout ll;
	uint32_t magic, limit;
	int s;

	magic = xip[LOADER_STAMP_MAGIC_OFFSET / sizeof(u_int)];
	limit = xip[LOADER_STAMP_LIMIT_OFFSET / sizeof(u_int)];
	loader_layout_decode(magic, limit, &ll);
	if (ll.status == LOADER_STAMP_INVALID)
		printf("fl: loader stamp limit %x is outside the writable "
		    "flash; flash writes refused\n", (u_int)limit);
	flswap_bytes = ll.swap_bytes;
	flash_write_end = ll.write_end;
	if (ll.loader_boot2) {
		flash_loader_boot2 = 1;
		s = splhigh();
		flash_ssi_restore();
		splx(s);
	}
}

/*
 * Nonzero when the PicoCalc loader started this kernel: flash_limit_init()
 * found the stamp's magic. The loader programs only the kernel's own UF2,
 * so the flash root region then holds whatever other applications left.
 */
int
flash_loader_started(void)
{
	return flash_loader_boot2;
}

/*
 * Erase len bytes at offset, both multiples of the chip sector. Runs from
 * RAM with interrupts masked, because XIP is down for the duration and an
 * interrupt vectoring into flash would fetch from a disabled interface.
 * SysTick holds one pending tick across the window, so clock_catchup()
 * accounts the rest before the mask lifts; flstrategy and fl_raw repeat the
 * call for the Dhara work between the erases and programs. The catch-up
 * precedes the clearing of the section name: core 1 (rp2040/core1.c) reads
 * a tick record that stood still with no section named as a stall.
 */
static __ramfunc int
flash_erase(u_int offset, u_int len)
{
	int s;

	/*
	 * Every flash write path (Dhara, raw swap, /dev/temp) reaches the chip
	 * through this routine or flash_program(), so the writable end is
	 * enforced here, before the boot ROM is called. A refusal reaches
	 * Dhara as a bad block (dhara_nand_erase, dhara_nand_prog), whose
	 * journal retries on other blocks up to DHARA_MAX_RETRIES times and
	 * then fails the request, which flstrategy() reports as EIO; the raw
	 * swap path fails its transfer with EIO directly.
	 */
	if (!flash_write_admitted(offset, len, flash_write_end))
		return -1;
	s = splhigh();
#ifdef STORAGE_STATS
	flash_wear_note(flash_swap_wear, FLASH_SWAP_SECTORS, FLASH_SWAP_OFFSET,
	    offset, len);
#endif
	storage_note(offset >= FLASH_SWAP_OFFSET ?
	    STORAGE_SWAP_ERASE_SECTORS : STORAGE_ROOT_ERASE_SECTORS,
	    len / FLASH_SECTOR_BYTES);
	storage_note(offset >= FLASH_SWAP_OFFSET ?
	    STORAGE_SWAP_ERASE_BYTES : STORAGE_ROOT_ERASE_BYTES, len);
	watchdog_site(WD_SITE_FLASH_ERASE, offset);
	flrom.connect();
	flrom.exit_xip();
#ifdef STORAGE_STATS
	(void)flash_hook_erase(&flhook, flrom.erase, flash_hook_reset, offset,
	    len);
#else
	flrom.erase(offset, len, FLASH_BLOCK_BYTES, FLASH_BLOCK_ERASE_CMD);
#endif
	flrom.flush();
	flash_enter_xip();
	clock_catchup();
	watchdog_site(WD_SITE_NONE, 0);
	splx(s);
	return 0;
}

/*
 * Program len bytes at offset, a multiple of the chip page, under the same
 * constraints and the same writable end as the erase above.
 */
static __ramfunc int
flash_program(u_int offset, const u_char *data, u_int len)
{
	int s;

	if (!flash_write_admitted(offset, len, flash_write_end))
		return -1;
	s = splhigh();
	storage_note(offset >= FLASH_SWAP_OFFSET ?
	    STORAGE_SWAP_PROGRAM_PAGES : STORAGE_ROOT_PROGRAM_PAGES,
	    len / FLASH_PROG_BYTES);
	storage_note(offset >= FLASH_SWAP_OFFSET ?
	    STORAGE_SWAP_PROGRAM_BYTES : STORAGE_ROOT_PROGRAM_BYTES, len);
	watchdog_site(WD_SITE_FLASH_PROGRAM, offset);
	flrom.connect();
	flrom.exit_xip();
#ifdef STORAGE_STATS
	(void)flash_hook_program(&flhook, flrom.program, flash_hook_reset,
	    offset, data, len);
#else
	flrom.program(offset, data, len);
#endif
	flrom.flush();
	flash_enter_xip();
	clock_catchup();
	watchdog_site(WD_SITE_NONE, 0);
	splx(s);
	return 0;
}

static int
flash_read(u_int offset, u_char *data, u_int len)
{
	if (offset > FLASH_TOTAL_BYTES || len > FLASH_TOTAL_BYTES - offset)
		return -1;
	bcopy((const void *)(FLASH_XIP_BASE + offset), data, len);
	return 0;
}

static const struct flash_swap_ops flswap_ops = {
	flash_read,
	flash_erase,
	flash_program,
};

/*
 * Dhara's driver interface. Pages and blocks below are numbered within the
 * filesystem region, so every address gains FLASH_FS_OFFSET before it reaches
 * the chip, and the kernel below that offset is unreachable from here.
 */

int
dhara_nand_is_bad(const struct dhara_nand *n __unused, dhara_block_t b __unused)
{
	/* NOR ships without factory bad blocks and is never marked bad at runtime. */
	return 0;
}

void
dhara_nand_mark_bad(const struct dhara_nand *n __unused,
    dhara_block_t b __unused)
{
	/* Nothing to record: see dhara_nand_is_bad. */
}

int
dhara_nand_erase(const struct dhara_nand *n __unused, dhara_block_t b,
    dhara_error_t *err)
{
	if (b >= FLASH_FS_BLOCKS) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	if (flash_erase(FLASH_FS_OFFSET + b * FLASH_ERASE_BYTES,
	    FLASH_ERASE_BYTES) != 0) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	return 0;
}

int
dhara_nand_prog(const struct dhara_nand *n __unused, dhara_page_t p,
    const u_char *data, dhara_error_t *err)
{
	u_int offset;

	if (!flash_nand_range_valid(p, 0, FLASH_UNIT_BYTES)) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	offset = p * FLASH_UNIT_BYTES;
	if (flash_program(FLASH_FS_OFFSET + offset, data,
	    FLASH_UNIT_BYTES) != 0) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	return 0;
}

int
dhara_nand_is_free(const struct dhara_nand *n __unused, dhara_page_t p)
{
	const u_char *q;
	u_int i;

	if (!flash_nand_range_valid(p, 0, FLASH_UNIT_BYTES))
		return 0;
	q = (const u_char *)(FLASH_XIP_BASE + FLASH_FS_OFFSET +
	    p * FLASH_UNIT_BYTES);
	for (i = 0; i < FLASH_UNIT_BYTES; i++)
		if (q[i] != 0xff)
			return 0;
	return 1;
}

int
dhara_nand_read(const struct dhara_nand *n __unused, dhara_page_t p,
    size_t offset, size_t length, u_char *data, dhara_error_t *err)
{
	const u_char *q;

	if (!flash_nand_range_valid(p, offset, length)) {
		dhara_set_error(err, DHARA_E_ECC);
		return -1;
	}
	/* Reads come straight out of the XIP window; NOR lacks ECC. */
	q = (const u_char *)(FLASH_XIP_BASE + FLASH_FS_OFFSET +
	    p * FLASH_UNIT_BYTES + offset);
	bcopy(q, data, length);
	return 0;
}

int
dhara_nand_copy(const struct dhara_nand *n, dhara_page_t src,
    dhara_page_t dst, dhara_error_t *err)
{
	u_int offset;

	/*
	 * Move one physical program page at a time. The source cannot be read
	 * through the XIP window while the destination is being programmed,
	 * because the program takes XIP down. Every caller holds splbio, and the
	 * one-page static buffer is written before each program call.
	 */
	if (flscratch_owner == FLS_SWAP)
		panic("R02");
	if (dst >= FLASH_FS_BYTES / FLASH_UNIT_BYTES) {
		dhara_set_error(err, DHARA_E_BAD_BLOCK);
		return -1;
	}
	for (offset = 0; offset < FLASH_UNIT_BYTES;
	    offset += FLASH_PROG_BYTES) {
		if (dhara_nand_read(n, src, offset, FLASH_PROG_BYTES,
		    flscratch, err) < 0)
			return -1;
		if (flash_program(FLASH_FS_OFFSET + dst * FLASH_UNIT_BYTES +
		    offset, flscratch, FLASH_PROG_BYTES) < 0) {
			dhara_set_error(err, DHARA_E_BAD_BLOCK);
			return -1;
		}
	}
	return 0;
}

/*
 * Block device.
 *
 * Minor numbers follow the SD driver: unit in bits 3 and up, partition in
 * the low three, with partition 0 the whole device and 1 to 4 the entries
 * of a PC partition table in the first 512 bytes of logical sector 0, as
 * tools/fsutil --repartition writes it. Offsets and sizes in that table are
 * in 512-byte sectors and are halved for DEV_BSIZE blocks, so fsutil's
 * even-numbered layout is a requirement rather than a convention.
 */

#define	NPARTITIONS	FLASH_PARTITION_COUNT
#define	RAWPART		0			/* Whole device. */

#define	FL_UNIT_FS	0			/* Dhara region, partitioned. */
#define	FL_UNIT_SWAP	1			/* Raw region, whole device. */

#define	flunit(dev)	(minor(dev) >> 3)
#define	flpart(dev)	(minor(dev) & 7)

struct fldisk {
	struct diskpart	part[NPARTITIONS + 1];	/* [0] is the whole device. */
	u_int		openpart;		/* Bit per open partition. */
	u_int		wrpart;			/* Opened for writing since its last close. */
};

static struct fldisk fldrives[NFL];

/*
 * Dhara makes a write durable only at a checkpoint: the metadata page that
 * closes a checkpoint group of seven user pages, here one 8 KiB erase block.
 * dhara_map_write() programs it whenever a write fills a group, and
 * dhara_map_sync() pads the open group with copies of live pages until it
 * can, so forcing a checkpoint after one isolated 1 KiB write costs a whole
 * group: eight Dhara pages, 32 program pages and a block erase.
 * flstrategy() therefore forces none. ufs_sync() asks for one through
 * DIOCCHECKPOINT, which sync(2), update(8), fsync(2), synchronous writes,
 * unmount and shutdown all reach, and the driver's last close takes one.
 * A reset discards the writes since the last checkpoint, and
 * dhara_map_resume() returns the map to the state that checkpoint recorded.
 * The padding copies pages through flscratch and its programs stop the clock,
 * so the bracket is flstrategy()'s. It runs in the requester's context.
 */
static int
fl_checkpoint(void)
{
	dhara_error_t err = DHARA_E_NONE;
	int s, error = 0;

	if (!flmap_ready)
		return 0;
	storage_note(STORAGE_CHECKPOINT_ATTEMPTS, 1);
	s = splbio();
	flscratch_take(FLS_MAP);
	if (dhara_map_sync(&flmap, &err) < 0)
		error = EIO;
	flscratch_owner = FLS_FREE;
	clock_catchup();
	splx(s);
	storage_note(error != 0 ? STORAGE_CHECKPOINT_ERRORS :
	    STORAGE_CHECKPOINT_SUCCESSES, 1);
	return error;
}

/*
 * The four 16-byte partition entries and the two-byte MBR_MAGIC occupy
 * bytes 446 to 511 of the table sector.
 */
#define	FL_MBR_TAIL_OFFSET	446
#define	FL_MBR_TAIL_BYTES	66

_Static_assert(FL_MBR_TAIL_OFFSET + FL_MBR_TAIL_BYTES <= FLASH_UNIT_BYTES &&
    FL_MBR_TAIL_BYTES == NPARTITIONS * 16 + 2,
    "the MBR tail lies inside one Dhara page");

/*
 * Print the chip's identity and say so when its capacity byte disagrees
 * with the layout flash.h fixes.
 */
static void
fl_report_identity(int unit)
{
	const u_char *jedec, *unique;
	u_int i;

	if (flash_identity(&jedec, &unique) != 0) {
		printf("fl%d: chip identity unread\n", unit);
		return;
	}
	printf("fl%d: JEDEC id %02x%02x%02x, unique id ", unit,
	    jedec[0], jedec[1], jedec[2]);
	for (i = 0; i < FLASH_UNIQUE_BYTES; i++)
		printf("%02x", unique[i]);
	printf("\n");
	if (!flash_capacity_matches(jedec[2]))
		printf("fl%d: capacity code %02x is not the %u kbytes this "
		    "kernel lays out\n", unit, jedec[2],
		    (u_int)(FLASH_TOTAL_BYTES / 1024));
}

/*
 * Bring the map up and read the partition table. Returns 0 on success.
 */
static int
fl_setup(int unit)
{
	struct fldisk *du = &fldrives[unit];
	dhara_error_t err = DHARA_E_NONE;
	u_short mbr_tail[FL_MBR_TAIL_BYTES / sizeof(u_short)];
	dhara_page_t page;
	u_int i;

	if (unit == FL_UNIT_SWAP) {
		/*
		 * The region ends at the writable end, and with no writable
		 * flash above FLASH_SWAP_OFFSET the unit does not exist.
		 */
		if (flswap_bytes == 0)
			return ENXIO;
		flash_rom_init();
		bzero(du->part, sizeof(du->part));
		du->part[RAWPART].dp_offset = 0;
		du->part[RAWPART].dp_nsectors = flswap_bytes / 512;
		printf("fl%d: %u kbytes raw QSPI flash for swap\n", unit,
		    (u_int)(flswap_bytes / 1024));
		return 0;
	}

	if (! flmap_ready) {
		flash_rom_init();
		dhara_map_init(&flmap, &flnand, flpage, FLASH_GC_RATIO);
		/*
		 * Preserve provisioning when no checkpoint is found. If a
		 * checkpoint header exists but all candidate roots fail
		 * validation, refuse to publish a writable empty map.
		 */
		if (dhara_map_resume(&flmap, &err) < 0 &&
		    err == DHARA_E_CORRUPT_MAP) {
			printf("fl%d: corrupt checkpoint map\n", unit);
			return EIO;
		}
		flblocks = (daddr_t)dhara_map_capacity(&flmap) /
		    (DEV_BSIZE / FLASH_UNIT_BYTES);
		flmap_ready = 1;
	}

	bzero(du->part, sizeof(du->part));
	du->part[RAWPART].dp_offset = 0;
	du->part[RAWPART].dp_nsectors = (u_int)flblocks * 2;
	printf("fl%d: %u kbytes on QSPI flash, %u kbytes raw\n", unit,
	    (u_int)flblocks, (u_int)(FLASH_FS_BYTES / 1024));
	fl_report_identity(unit);

	/*
	 * The table lives in the first 512 bytes of logical sector 0. Only
	 * its tail is read, from the physical page Dhara maps the sector to,
	 * so the frame holds 66 bytes rather than a whole sector. An unmapped
	 * sector lacks a table, as the blank page dhara_map_read would
	 * return for it lacks MBR_MAGIC.
	 */
	err = DHARA_E_NONE;
	if (dhara_map_find(&flmap, 0, &page, &err) < 0) {
		if (err == DHARA_E_NOT_FOUND)
			return 0;
		printf("fl%d: cannot read partition table\n", unit);
		return ENXIO;
	}
	err = DHARA_E_NONE;
	if (dhara_nand_read(&flnand, page, FL_MBR_TAIL_OFFSET,
	    FL_MBR_TAIL_BYTES, (u_char *)mbr_tail, &err) < 0) {
		printf("fl%d: cannot read partition table\n", unit);
		return ENXIO;
	}
	if (mbr_tail[FL_MBR_TAIL_BYTES / sizeof(u_short) - 1] == MBR_MAGIC) {
		bcopy(mbr_tail, &du->part[1], 64);
		for (i = 1; i <= NPARTITIONS; i++) {
			if (du->part[i].dp_type != 0)
				printf("fl%d%c: partition type %02x, "
				    "sector %u, size %u kbytes\n",
				    unit, i + 'a' - 1, du->part[i].dp_type,
				    du->part[i].dp_offset,
				    du->part[i].dp_nsectors / 2);
		}
	}
	return 0;
}

int
flopen(dev_t dev, int flags, int mode __unused)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	struct fldisk *du;
	int error;

	if (unit >= NFL || part > NPARTITIONS)
		return ENXIO;
	du = &fldrives[unit];

	if (du->part[RAWPART].dp_nsectors == 0) {
		error = fl_setup(unit);
		if (error)
			return error;
	}
	du->openpart |= 1 << part;
	if (flags & FWRITE)
		du->wrpart |= 1 << part;
	return 0;
}

/*
 * The last close of a root-unit node checkpoints the map when any open since
 * the node's previous last close asked for writing. The last descriptor's own
 * flags do not decide it: a writer may close first and leave a reader to make
 * the last close. A node only ever held for reading forces no padding
 * programs, and the swap unit writes without a map.
 */
int
flclose(dev_t dev, int flag __unused, int mode __unused)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	u_int written;

	if (unit >= NFL || part > NPARTITIONS)
		return ENXIO;
	written = fldrives[unit].wrpart & (1U << part);
	fldrives[unit].openpart &= ~(1 << part);
	fldrives[unit].wrpart &= ~(1U << part);
	if (unit == FL_UNIT_SWAP || written == 0)
		return 0;
	return fl_checkpoint();
}

/* Size in DEV_BSIZE blocks, which is what the block layer asks for. */
daddr_t
flsize(dev_t dev)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	struct fldisk *du;

	if (unit >= NFL || part > NPARTITIONS)
		return 0;
	du = &fldrives[unit];
	if (du->part[RAWPART].dp_nsectors == 0 && fl_setup(unit) != 0)
		return 0;
	return du->part[part].dp_nsectors >> 1;
}

/*
 * The raw swap region: reads come out of the XIP window. A process image owns
 * a contiguous erase-aligned run, so flash_swap_append can erase sectors as
 * its pieces reach them. Other callers retain ordinary rewrite semantics by
 * copying each affected sector through the first erase sector, which the
 * resource map cannot allocate. Returns nonzero on failure.
 */
static int
fl_raw(struct buf *bp, u_int off, u_int len)
{
	u_char *addr = (u_char *)bp->b_addr;
	u_int base;
	int s;

	if (off > flswap_bytes || len > flswap_bytes - off)
		return 1;
	base = FLASH_SWAP_OFFSET + off;

	if (bp->b_flags & B_READ) {
		bcopy((const void *)(FLASH_XIP_BASE + base), addr, len);
		return 0;
	}
	if (off < FLASH_SECTOR_BYTES)
		return 1;

	s = splbio();
	flscratch_take(FLS_SWAP);
	if ((bp->b_flags & B_SWAPIMAGE) != 0) {
		if (flash_swap_append(&flswap_ops, base, addr, len,
		    flscratch) != 0)
			goto fail;
	} else if (flash_swap_rewrite(&flswap_ops, base, addr, len,
	    FLASH_SWAP_SCRATCH_OFFSET, flscratch) != 0) {
	fail:
		flscratch_owner = FLS_FREE;
		clock_catchup();
		splx(s);
		return 1;
	}
	flscratch_owner = FLS_FREE;
	clock_catchup();
	splx(s);
	return 0;
}

void
flstrategy(struct buf *bp)
{
	int unit, part;
	struct fldisk *du;
	struct diskpart *p;
	dhara_error_t err = DHARA_E_NONE;
	u_int per_blk = DEV_BSIZE / FLASH_UNIT_BYTES;
	u_int sector, nsect, i;
	u_char *addr;
	daddr_t part_size, offset;
	long nblk;
	int s, fail = 0;

	unit = flunit(bp->b_dev);
	part = flpart(bp->b_dev);
	if (!flash_indices_valid((unsigned int)unit, (unsigned int)part, NFL)) {
		bp->b_error = ENXIO;
		goto bad;
	}
	du = &fldrives[unit];
	p = &du->part[part];
	if (du->part[RAWPART].dp_nsectors == 0 && fl_setup(unit) != 0) {
		bp->b_error = ENXIO;
		goto bad;
	}

	/*
	 * Determine the size of the transfer, and make sure it is within
	 * the boundaries of the partition.
	 */
	part_size = p->dp_nsectors >> 1;
	offset = bp->b_blkno + (p->dp_offset >> 1);
	nblk = btod(bp->b_bcount);
	if (bp->b_blkno + nblk > part_size) {
		/* Exactly at the end reads as end of file. */
		if (bp->b_blkno == part_size) {
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			return;
		}
		/* Or truncate if part of it fits. */
		nblk = part_size - bp->b_blkno;
		if (nblk <= 0) {
			bp->b_error = EINVAL;
			goto bad;
		}
		bp->b_bcount = nblk << DEV_BSHIFT;
	}

	if (bp->b_dev == swapdev)
		led_control(LED_SWAP, 1);
	else
		led_control(LED_DISK, 1);

#ifdef FLASH_DEBUG
	printf("fl%d%c: %s blk %u n %u\n", unit, flpart(bp->b_dev) + 'a' - 1,
	    (bp->b_flags & B_READ) ? "read" : "write", (u_int)bp->b_blkno,
	    (u_int)nblk);
#endif

	/*
	 * b_bcount is a byte count and swap moves segments of any length,
	 * so the transfer is exactly that many bytes. Rounding it up to
	 * whole blocks would run past the buffer: a stack segment ends at
	 * the top of the user window, and the kernel's data starts there.
	 */
	if (unit == FL_UNIT_SWAP) {
		storage_note((bp->b_flags & B_READ) ? STORAGE_SWAP_READS :
		    STORAGE_SWAP_WRITES, 1);
		storage_note((bp->b_flags & B_READ) ? STORAGE_SWAP_READ_BYTES :
		    STORAGE_SWAP_WRITE_BYTES, bp->b_bcount);
		fail = fl_raw(bp, (u_int)offset * DEV_BSIZE, bp->b_bcount);
		goto done;
	}

	sector = (u_int)offset * per_blk;
	if (bp->b_bcount % FLASH_UNIT_BYTES != 0) {
		fail = 1;
		goto done;
	}
	storage_note((bp->b_flags & B_READ) ? STORAGE_ROOT_READS :
	    STORAGE_ROOT_WRITES, 1);
	storage_note((bp->b_flags & B_READ) ? STORAGE_ROOT_READ_BYTES :
	    STORAGE_ROOT_WRITE_BYTES, bp->b_bcount);
	nsect = bp->b_bcount / FLASH_UNIT_BYTES;
	addr = (u_char *)bp->b_addr;

	s = splbio();
	flscratch_take(FLS_MAP);
	for (i = 0; i < nsect && ! fail; i++) {
		if (bp->b_flags & B_READ) {
			if (dhara_map_read(&flmap, sector + i,
			    addr + i * FLASH_UNIT_BYTES, &err) < 0)
				fail = 1;
		} else {
			storage_note(STORAGE_MAP_WRITES, 1);
			if (dhara_map_write(&flmap, sector + i,
			    addr + i * FLASH_UNIT_BYTES, &err) < 0)
				fail = 1;
		}
	}
	flscratch_owner = FLS_FREE;
	clock_catchup();
	splx(s);

done:
	if (bp->b_dev == swapdev)
		led_control(LED_SWAP, 0);
	else
		led_control(LED_DISK, 0);

	if (fail) {
		bp->b_error = EIO;
		goto bad;
	}
	biodone(bp);
	return;

bad:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

int
flioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	int unit = flunit(dev);
	int part = flpart(dev);
	struct diskpart *dp;

	if (unit >= NFL || part > NPARTITIONS)
		return ENXIO;
	dp = &fldrives[unit].part[part];

	switch (cmd) {
	case DIOCGETMEDIASIZE:
		/* Partition size in kbytes. */
		*(int *)addr = dp->dp_nsectors >> 1;
		return 0;

	case DIOCGETPART:
		*(struct diskpart *)addr = *dp;
		return 0;

	case DIOCREINIT:
		/*
		 * Forget the table so the next transfer rereads it. A mounted
		 * filesystem or a descriptor on a partition addresses its blocks
		 * through the current entries, and flstrategy() would read the new
		 * ones under it, so the request waits until no partition is open.
		 * fl lacks a raw node: the whole-device node is the one fdisk opens
		 * to rewrite the table, so its open does not hold the table.
		 */
		if ((fldrives[unit].openpart & ~(1U << RAWPART)) != 0)
			return EBUSY;
		fldrives[unit].part[RAWPART].dp_nsectors = 0;
		return 0;

	case DIOCCHECKPOINT:
		/*
		 * The swap unit writes the chip directly, without a map.
		 * ioctl(2) passes the descriptor's open flags, so a process must
		 * hold the node open for writing; ufs_sync() passes 0.
		 */
		if (unit == FL_UNIT_SWAP)
			return EINVAL;
		if (flag != 0 && (flag & FWRITE) == 0)
			return EBADF;
		*(int *)addr = fl_checkpoint();
		return 0;
	}
	return EINVAL;
}
