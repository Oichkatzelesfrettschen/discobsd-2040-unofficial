/*
 * Fixture readings for the host builds of picofetch and picotop. The
 * scenario comes from PICO_FIXTURE:
 *
 *   picocalc  (default) a PICOCALC_PSRAM kernel: board nodes present, no
 *             core 1 observer, no ADC, no storage counters, no USB console
 *   pico      a PICO kernel: core 1 observer, storage counters and USB
 *             present, the PicoCalc nodes absent
 *   sensors   picocalc plus the ADC readings
 *   failed    picocalc with the battery read failing
 *   full      every reading present, levels near their limits
 *   unread    picocalc whose flash chip returned no identity at boot
 *   oldkernel picocalc on a kernel without machdep.flash_id
 *   busdiff   picocalc whose peripheral clock is half the core clock
 *   longrun   picocalc after a long uptime: the tick counters start two
 *             thousand million ticks up, where a 32-bit product with 1000
 *             overflows
 *
 * Each HWI_LIVE collection is two seconds after the one before it and
 * moves the counters by fixed amounts, so a test computes the rates it
 * expects: of 200 ticks, 50 user, 0 nice, 30 system and 120 idle.
 */
#include <stdlib.h>
#include <string.h>

#include "hwinfo.h"

static int step;

static void
ok(struct rd_int *r, long v)
{
	r->state = RD_OK;
	r->v = v;
}

static void
no(struct rd_int *r, int state)
{
	r->state = state;
	r->v = 0;
}

static void
tab(struct hwi_table *t, int cur, int limit, int peak)
{
	t->state = RD_OK;
	t->cur = cur;
	t->limit = limit;
	t->peak = peak;
}

static void
proc(struct hwinfo *hw, int pid, int ppid, int uid, int stat, int incore,
    unsigned long ticks, unsigned long bytes, const char *comm)
{
	struct hwi_proc *p = &hw->proc[hw->nproc++];

	memset(p, 0, sizeof *p);
	p->pid = pid;
	p->ppid = ppid;
	p->uid = uid;
	p->stat = stat;
	p->incore = incore;
	p->pri = stat == 3 ? 50 : 28;
	p->timed = stat != 5;
	p->ticks = ticks;
	p->bytes = bytes;
	strncpy(p->comm, comm, HWI_NAME - 1);
}

void
hw_collect(struct hwinfo *hw, int groups)
{
	const char *sc = getenv("PICO_FIXTURE");
	int pico, sensors, failed, full, unread, oldkernel, busdiff;
	long base;

	if (sc == NULL)
		sc = "picocalc";
	pico = strcmp(sc, "pico") == 0;
	sensors = strcmp(sc, "sensors") == 0;
	failed = strcmp(sc, "failed") == 0;
	full = strcmp(sc, "full") == 0;
	unread = strcmp(sc, "unread") == 0;
	oldkernel = strcmp(sc, "oldkernel") == 0;
	busdiff = strcmp(sc, "busdiff") == 0;
	base = strcmp(sc, "longrun") == 0 ? 10000000L : 0;

	if (groups & HWI_STATIC) {
		strcpy(hw->ostype, "DiscoBSD");
		strcpy(hw->osrelease, "2.7");
		strcpy(hw->osversion, pico ? "PICO#812" : "PICOCALC_PSRAM#812");
		strcpy(hw->hostname, "pico");
		strcpy(hw->machine, "rp2040");
		strcpy(hw->model, pico ? "Raspberry Pi Pico" :
		    "ClockworkPi PicoCalc");
		strcpy(hw->arch, "arm");
		ok(&hw->ncpu, 1);
		ok(&hw->physmem, 264L * 1024);
		ok(&hw->pagesize, 1024);
		ok(&hw->cpu_khz, 125000);
		ok(&hw->bus_khz, busdiff ? 62500 : 125000);
		ok(&hw->hz, 100);
		ok(&hw->boottime, 1000000);
		ok(&hw->nswap, 384);
		ok(&hw->window, 192L * 1024);
		ok(&hw->mpu_enable, 1);
		ok(&hw->mpu_nregions, 8);
		ok(&hw->mpu_programmed, 4);
		ok(&hw->image_length, 113880);
		ok(&hw->image_crc, 0x1234abcd);
		ok(&hw->image_crc_stored, full ? 0x1234abce : 0x1234abcd);
		ok(&hw->watchdog_reason, full ? 1 : 0);
		ok(&hw->cons_major, pico ? 6 : 10);
		ok(&hw->cons_minor, 0);
		memset(&hw->flash_id, 0, sizeof hw->flash_id);
		hw->flash_id.state = oldkernel ? RD_ABSENT : failed ? RD_FAILED :
		    RD_OK;
		if (hw->flash_id.state == RD_OK && !unread) {
			static const unsigned char jedec[3] = { 0xef, 0x40, 0x15 };
			static const unsigned char unique[8] = {
				0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
			};

			hw->flash_id.valid = 1;
			memcpy(hw->flash_id.jedec, jedec, sizeof jedec);
			memcpy(hw->flash_id.unique, unique, sizeof unique);
		}
	}
	if (groups & HWI_LIVE) {
		step++;
		hw->now = 1000000 + 93784 + 2L * step;	/* 1d 02:03:04 + 2 s */
		ok(&hw->cp_time[0], 50L * (base + step));
		ok(&hw->cp_time[1], 0);
		ok(&hw->cp_time[2], 30L * (base + step));
		ok(&hw->cp_time[3], 120L * (base + step));
		ok(&hw->r_swtch, 41);
		ok(&hw->r_syscall, 230);
		ok(&hw->r_intr, 112);
		ok(&hw->r_swpin, 1);
		ok(&hw->r_swpout, 2);
		ok(&hw->load[0], 112);
		ok(&hw->load[1], 45);
		ok(&hw->load[2], 7);
		ok(&hw->runq, 1);
		ok(&hw->sleeping, 4);
		ok(&hw->swapped, 2);
		ok(&hw->mem_real, full ? 190L * 1024 : 96L * 1024);
		ok(&hw->mem_virt, 301L * 1024);
		ok(&hw->swapfree, full ? 12 : 288);
		ok(&hw->swapram_images, 1);
		ok(&hw->xip_cache, 1);
		ok(&hw->xip_acc, 2000000);
		ok(&hw->xip_hit, 1964000);
		if (pico) {
			no(&hw->battery, RD_ABSENT);
			no(&hw->charging, RD_ABSENT);
			no(&hw->lcd_backlight, RD_ABSENT);
			no(&hw->kbd_backlight, RD_ABSENT);
		} else {
			if (failed)
				no(&hw->battery, RD_FAILED);
			else
				ok(&hw->battery, full ? 9 : 75);
			ok(&hw->charging, !full);
			ok(&hw->lcd_backlight, 128);
			ok(&hw->kbd_backlight, 16);
		}
		memset(&hw->pcm, 0, sizeof hw->pcm);
		if (pico)
			hw->pcm.state = RD_ABSENT;
		else if (failed)
			hw->pcm.state = RD_FAILED;
		else {
			hw->pcm.state = RD_OK;
			hw->pcm.rate = 8000;
			hw->pcm.channels = 1;
			hw->pcm.bits = 8;
			hw->pcm.ring = 255;
			hw->pcm.open = full;
			hw->pcm.queued = full ? 200 : 0;
		}
		if (pico || full) {
			ok(&hw->core1, 1);
			ok(&hw->core1_restarts, 0);
			ok(&hw->core1_stalls, 3);
			ok(&hw->core1_faults, 0);
			ok(&hw->usb_bulkin_arms, 5120);
			ok(&hw->usb_tx_recovered, 0);
			ok(&hw->st_root_rd, 4096);
			ok(&hw->st_root_wr, 612);
			ok(&hw->st_swap_rd, 300);
			ok(&hw->st_swap_wr, 12288);
			ok(&hw->st_erases, 47);
		} else {
			no(&hw->core1, RD_ABSENT);
			no(&hw->core1_restarts, RD_ABSENT);
			no(&hw->core1_stalls, RD_ABSENT);
			no(&hw->core1_faults, RD_ABSENT);
			no(&hw->usb_bulkin_arms, RD_ABSENT);
			no(&hw->usb_tx_recovered, RD_ABSENT);
			no(&hw->st_root_rd, RD_ABSENT);
			no(&hw->st_root_wr, RD_ABSENT);
			no(&hw->st_swap_rd, RD_ABSENT);
			no(&hw->st_swap_wr, RD_ABSENT);
			no(&hw->st_erases, RD_ABSENT);
		}
		if (sensors || full) {
			ok(&hw->temp_mc, 31437);
			ok(&hw->vsys_mv, 4987);
		} else {
			no(&hw->temp_mc, RD_ABSENT);
			no(&hw->vsys_mv, RD_ABSENT);
		}
		ok(&hw->kstack_peak, 812);
		ok(&hw->kstack_bytes, 2048);
		tab(&hw->t_proc, 7, 20, 9);
		tab(&hw->t_inode, 12, 24, 15);
		tab(&hw->t_file, 9, 20, 11);
		tab(&hw->t_buffer, 4, 4, 4);
		hw->ndisk = pico ? 2 : 3;
		strcpy(hw->disk[0].name, "fl0");
		hw->disk[0].xfer = 100UL + 10UL * (unsigned long)step;
		hw->disk[0].bytes = 100000UL + 20480UL * (unsigned long)step;
		strcpy(hw->disk[1].name, "fl1");
		hw->disk[1].xfer = 40;
		hw->disk[1].bytes = 163840;
		strcpy(hw->disk[2].name, "sd0");
		hw->disk[2].xfer = 7;
		hw->disk[2].bytes = 7168;
		hw->nfs = pico ? 1 : 2;
		strcpy(hw->fs[0].on, "/");
		strcpy(hw->fs[0].from, "/dev/fl0a");
		hw->fs[0].kb = 988;
		hw->fs[0].kbfree = full ? 20 : 494;
		hw->fs[0].files = 300;
		hw->fs[0].ffree = 120;
		strcpy(hw->fs[1].on, "/mnt");
		strcpy(hw->fs[1].from, "/dev/sd0a");
		hw->fs[1].kb = 31162368;
		hw->fs[1].kbfree = 31162368 / 4 * 3;
		hw->fs[1].files = 65535;
		hw->fs[1].ffree = 65000;
	}
	if (groups & HWI_PROCS) {
		unsigned long s = (unsigned long)step;

		hw->procs_state = RD_OK;
		hw->nproc = 0;
		proc(hw, 0, 0, 0, 1, 1, 30 * s, 0, "swapper");
		proc(hw, 1, 0, 0, 1, 0, 2, 20480, "init");
		proc(hw, 12, 1, 0, 1, 0, 40, 30720, "sh");
		proc(hw, 31, 12, 100, 1, 0, 300 + 10 * s +
		    (base != 0 ? 3000000000UL : 0), 150528, "cc");
		proc(hw, 33, 12, 0, 3, 1, 5 + 40 * s, 40960, "picotop");
		proc(hw, 30, 12, 100, 5, 0, 0, 0, "<defunct>");
		proc(hw, 29, 12, 100, 6, 0, 17, 61440, "stevie");
	}
}
