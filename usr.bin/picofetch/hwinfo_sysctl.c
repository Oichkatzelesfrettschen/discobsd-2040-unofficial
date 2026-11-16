/*
 * The readings of hwinfo.h from the running kernel: sysctl(3) records,
 * getfsstat(2) and the clock. Nothing here reads a kernel address or a
 * device node, so the programs run without privilege.
 *
 * The two ADC nodes are compiled in when <machine/cpu.h> names them; a
 * tree without them reports both readings RD_ABSENT.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/capacity.h>
#include <sys/storage_stats.h>
#include <sys/time.h>
#include <machine/cpu.h>
#include <machine/mpuvar.h>
#include <machine/picocalcvar.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hwinfo.h"

_Static_assert(sizeof(struct mapent) == 4,
    "the VM_SWAPMAP payload is the compact two-short map entry");

static int
state_of(int error)
{
	return error == EOPNOTSUPP || error == ENOTDIR || error == ENOENT ?
	    RD_ABSENT : RD_FAILED;
}

/* Read a record of exactly len bytes; 0, or the errno that explains it. */
static int
rd(const int *mib, unsigned n, void *buf, size_t len)
{
	size_t got = len;

	if (sysctl((int *)mib, n, buf, &got, NULL, 0) < 0)
		return errno != 0 ? errno : EIO;
	return got == len ? 0 : EIO;
}

static void
geti(struct rd_int *r, int a, int b, int c)
{
	int mib[3], v = 0, error;

	mib[0] = a;
	mib[1] = b;
	mib[2] = c;
	error = rd(mib, c < 0 ? 2 : 3, &v, sizeof v);
	r->state = error == 0 ? RD_OK : state_of(error);
	r->v = v;
}

static void
gets2(char *dst, int a, int b)
{
	int mib[2];
	size_t len = HWI_STR - 1;

	mib[0] = a;
	mib[1] = b;
	dst[0] = '\0';
	if (sysctl(mib, 2, dst, &len, NULL, 0) < 0)
		len = 0;
	dst[len < HWI_STR ? len : HWI_STR - 1] = '\0';
	len = strlen(dst);
	while (len > 0 && (dst[len - 1] == '\n' || dst[len - 1] == ' '))
		dst[--len] = '\0';
}

static void
set(struct rd_int *r, long v)
{
	r->state = RD_OK;
	r->v = v;
}

static void
collect_flash_id(struct hwinfo *hw)
{
	static const int mib[2] = { CTL_MACHDEP, CPU_FLASH_ID };
	struct cpu_flash_id id;
	int error;

	memset(&id, 0, sizeof id);
	error = rd(mib, 2, &id, sizeof id);
	hw->flash_id.state = error == 0 ? RD_OK : state_of(error);
	hw->flash_id.valid = id.valid != 0;
	memcpy(hw->flash_id.id, id.id, sizeof hw->flash_id.id);
}

static void
collect_pcm(struct hwinfo *hw)
{
	static const int mib[2] = { CTL_MACHDEP, CPU_PCM };
	struct cpu_pcm_info p;
	int error;

	memset(&p, 0, sizeof p);
	error = rd(mib, 2, &p, sizeof p);
	hw->pcm.state = error == 0 ? RD_OK : state_of(error);
	hw->pcm.rate = (long)p.rate;
	hw->pcm.channels = (long)p.channels;
	hw->pcm.bits = (long)p.bits;
	hw->pcm.ring = (long)p.ring;
	hw->pcm.queued = (long)p.queued;
	hw->pcm.open = p.open != 0;
}

static void
collect_static(struct hwinfo *hw)
{
	int mib[2];
	struct clockinfo clock;
	struct __timeval32 tv;		/* the kernel's layout, sys/sys/time.h */
	dev_t dev;
	int error;

	gets2(hw->ostype, CTL_KERN, KERN_OSTYPE);
	gets2(hw->osrelease, CTL_KERN, KERN_OSRELEASE);
	gets2(hw->osversion, CTL_KERN, KERN_OSVERSION);
	gets2(hw->hostname, CTL_KERN, KERN_HOSTNAME);
	gets2(hw->machine, CTL_HW, HW_MACHINE);
	gets2(hw->model, CTL_HW, HW_MODEL);
	gets2(hw->arch, CTL_HW, HW_MACHINE_ARCH);

	geti(&hw->ncpu, CTL_HW, HW_NCPU, -1);
	geti(&hw->physmem, CTL_HW, HW_PHYSMEM, -1);
	geti(&hw->pagesize, CTL_HW, HW_PAGESIZE, -1);
	geti(&hw->cpu_khz, CTL_MACHDEP, CPU_FREQ_KHZ, -1);
	geti(&hw->bus_khz, CTL_MACHDEP, CPU_BUS_KHZ, -1);
	geti(&hw->nswap, CTL_VM, VM_NSWAP, -1);
	set(&hw->window, USER_DATA_SIZE);

	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	error = rd(mib, 2, &clock, sizeof clock);
	hw->hz.state = error == 0 && clock.hz > 0 ? RD_OK : RD_FAILED;
	hw->hz.v = clock.hz;
	mib[1] = KERN_BOOTTIME;
	error = rd(mib, 2, &tv, sizeof tv);
	hw->boottime.state = error == 0 ? RD_OK : state_of(error);
	hw->boottime.v = (long)tv.tv_sec;

	geti(&hw->mpu_enable, CTL_MACHDEP, CPU_MPU, CPU_MPU_ENABLE);
	geti(&hw->mpu_nregions, CTL_MACHDEP, CPU_MPU, CPU_MPU_NREGIONS);
	geti(&hw->mpu_programmed, CTL_MACHDEP, CPU_MPU, CPU_MPU_PROGRAMMED);
	geti(&hw->image_length, CTL_MACHDEP, CPU_IMAGE_LENGTH, -1);
	geti(&hw->image_crc, CTL_MACHDEP, CPU_IMAGE_CRC, -1);
	geti(&hw->image_crc_stored, CTL_MACHDEP, CPU_IMAGE_CRC_STORED, -1);
	geti(&hw->watchdog_reason, CTL_MACHDEP, CPU_WATCHDOG_REASON, -1);
	collect_flash_id(hw);

	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_CONSDEV;
	error = rd(mib, 2, &dev, sizeof dev);
	hw->cons_major.state = hw->cons_minor.state =
	    error == 0 ? RD_OK : state_of(error);
	hw->cons_major.v = major(dev);
	hw->cons_minor.v = minor(dev);
}

static void
table(struct hwi_table *t, const struct capacity_metric *m, int state)
{
	t->state = state;
	t->cur = m->current;
	t->limit = m->limit;
	t->peak = m->peak;
}

static void
collect_capacity(struct hwinfo *hw)
{
	static const int mib[2] = { CTL_KERN, KERN_CAPACITY };
	struct kernel_capacity_stats cs;
	int error, state;

	memset(&cs, 0, sizeof cs);
	error = rd(mib, 2, &cs, sizeof cs);
	state = error == 0 ? RD_OK : state_of(error);
	table(&hw->t_proc, &cs.metric[CAPACITY_PROC], state);
	table(&hw->t_inode, &cs.metric[CAPACITY_INODE], state);
	table(&hw->t_file, &cs.metric[CAPACITY_FILE], state);
	table(&hw->t_buffer, &cs.metric[CAPACITY_BUFFER], state);
	hw->kstack_peak.state = hw->kstack_bytes.state = state;
	hw->kstack_peak.v = cs.stack_peak;
	hw->kstack_bytes.v = cs.stack_bytes;
}

static void
collect_storage(struct hwinfo *hw)
{
	static const int mib[2] = { CTL_MACHDEP, CPU_STORAGE_STATS };
	struct storage_stats ss;
	int error, state;

	memset(&ss, 0, sizeof ss);
	error = rd(mib, 2, &ss, sizeof ss);
	state = error == 0 ? RD_OK : state_of(error);
	hw->st_root_rd.state = hw->st_root_wr.state = hw->st_swap_rd.state =
	    hw->st_swap_wr.state = hw->st_erases.state = state;
	hw->st_root_rd.v = (long)(ss.value[STORAGE_ROOT_READ_BYTES] >> 10);
	hw->st_root_wr.v = (long)(ss.value[STORAGE_ROOT_WRITE_BYTES] >> 10);
	hw->st_swap_rd.v = (long)(ss.value[STORAGE_SWAP_READ_BYTES] >> 10);
	hw->st_swap_wr.v = (long)(ss.value[STORAGE_SWAP_WRITE_BYTES] >> 10);
	hw->st_erases.v = (long)(ss.value[STORAGE_ROOT_ERASE_SECTORS] +
	    ss.value[STORAGE_SWAP_ERASE_SECTORS]);
}

/*
 * VM_SWAPMAP lists the free runs of swap and ends with an entry of size
 * zero; the free total is their sum, in DEV_BSIZE blocks of one kbyte.
 */
static void
collect_swap(struct hwinfo *hw)
{
	static const int mib[2] = { CTL_VM, VM_SWAPMAP };
	static struct mapent map[64];
	size_t len = sizeof map, i;
	long sum = 0;

	if (sysctl((int *)mib, 2, map, &len, NULL, 0) < 0) {
		hw->swapfree.state = state_of(errno);
		hw->swapfree.v = 0;
		return;
	}
	for (i = 0; i < len / sizeof map[0] && map[i].m_size != 0; i++)
		sum += map[i].m_size;
	set(&hw->swapfree, sum * (DEV_BSIZE / 1024));
}

static void
collect_fs(struct hwinfo *hw)
{
	static struct statfs sf[HWI_NFS];
	int i, n;
	long unit;

	n = getfsstat(sf, sizeof sf, MNT_NOWAIT);
	hw->nfs = n < 0 ? 0 : n > HWI_NFS ? HWI_NFS : n;
	for (i = 0; i < hw->nfs; i++) {
		struct hwi_fs *f = &hw->fs[i];

		strncpy(f->on, sf[i].f_mntonname, HWI_NAME - 1);
		f->on[HWI_NAME - 1] = '\0';
		strncpy(f->from, sf[i].f_mntfromname, HWI_NAME - 1);
		f->from[HWI_NAME - 1] = '\0';
		unit = sf[i].f_bsize > 0 ? sf[i].f_bsize : DEV_BSIZE;
		if (unit >= 1024) {
			f->kb = (long)sf[i].f_blocks * (unit / 1024);
			f->kbfree = (long)sf[i].f_bfree * (unit / 1024);
		} else {
			f->kb = (long)sf[i].f_blocks / (1024 / unit);
			f->kbfree = (long)sf[i].f_bfree / (1024 / unit);
		}
		f->files = (long)sf[i].f_files;
		f->ffree = (long)sf[i].f_ffree;
	}
}

static void
collect_live(struct hwinfo *hw)
{
	int mib[2], error, i, state;
	struct kinfo_vmstat vs;
	struct vmtotal total;
	struct loadavg la;
	static struct kinfo_disk kd[HWI_NDISK];
	size_t len;

	hw->now = (long)time(NULL);

	mib[0] = CTL_VM;
	mib[1] = VM_STATS;
	memset(&vs, 0, sizeof vs);
	error = rd(mib, 2, &vs, sizeof vs);
	state = error == 0 ? RD_OK : state_of(error);
	for (i = 0; i < 4; i++) {
		hw->cp_time[i].state = state;
		hw->cp_time[i].v = (long)vs.vs_cp_time[i];
	}
	hw->r_swtch.state = hw->r_syscall.state = hw->r_intr.state =
	    hw->r_swpin.state = hw->r_swpout.state = state;
	hw->r_swtch.v = (long)vs.vs_rate.ve_swtch;
	hw->r_syscall.v = (long)vs.vs_rate.ve_syscall;
	hw->r_intr.v = (long)vs.vs_rate.ve_intr;
	hw->r_swpin.v = (long)vs.vs_rate.ve_swpin;
	hw->r_swpout.v = (long)vs.vs_rate.ve_swpout;

	mib[1] = VM_METER;
	memset(&total, 0, sizeof total);
	error = rd(mib, 2, &total, sizeof total);
	state = error == 0 ? RD_OK : state_of(error);
	hw->runq.state = hw->sleeping.state = hw->swapped.state =
	    hw->mem_real.state = hw->mem_virt.state = state;
	hw->runq.v = total.t_rq;
	hw->sleeping.v = total.t_sl;
	hw->swapped.v = total.t_sw;
	hw->mem_real.v = (long)total.t_rm;
	hw->mem_virt.v = total.t_vm;

	mib[1] = VM_LOADAVG;
	memset(&la, 0, sizeof la);
	error = rd(mib, 2, &la, sizeof la);
	state = error == 0 && la.fscale > 0 ? RD_OK :
	    error == 0 ? RD_FAILED : state_of(error);
	for (i = 0; i < 3; i++) {
		hw->load[i].state = state;
		hw->load[i].v = state == RD_OK ?
		    (long)la.ldavg[i] * 100 / la.fscale : 0;
	}

	collect_swap(hw);
	geti(&hw->swapram_images, CTL_MACHDEP, CPU_SWAPRAM_IMAGES, -1);
	geti(&hw->xip_cache, CTL_MACHDEP, CPU_XIP_CACHE, -1);
	geti(&hw->xip_acc, CTL_MACHDEP, CPU_XIP_ACC, -1);
	geti(&hw->xip_hit, CTL_MACHDEP, CPU_XIP_HIT, -1);
	geti(&hw->battery, CTL_MACHDEP, CPU_PICOCALC, CPU_PICOCALC_BATTERY);
	geti(&hw->charging, CTL_MACHDEP, CPU_PICOCALC, CPU_PICOCALC_CHARGING);
	geti(&hw->lcd_backlight, CTL_MACHDEP, CPU_PICOCALC,
	    CPU_PICOCALC_LCD_BACKLIGHT);
	geti(&hw->kbd_backlight, CTL_MACHDEP, CPU_PICOCALC,
	    CPU_PICOCALC_KBD_BACKLIGHT);
	geti(&hw->core1, CTL_MACHDEP, CPU_CORE1, -1);
	geti(&hw->core1_restarts, CTL_MACHDEP, CPU_CORE1_RESTARTS, -1);
	geti(&hw->core1_stalls, CTL_MACHDEP, CPU_CORE1_STALLS, -1);
	geti(&hw->core1_faults, CTL_MACHDEP, CPU_CORE1_FAULTS, -1);
	geti(&hw->temp_mc, CTL_MACHDEP, CPU_ADC_TEMP, -1);
	geti(&hw->vsys_mv, CTL_MACHDEP, CPU_ADC_VSYS, -1);
	geti(&hw->usb_bulkin_arms, CTL_MACHDEP, CPU_USB_BULKIN_ARMS, -1);
	geti(&hw->usb_tx_recovered, CTL_MACHDEP, CPU_USB_TX_RECOVERED, -1);
	collect_pcm(hw);
	collect_storage(hw);
	collect_capacity(hw);

	mib[0] = CTL_HW;
	mib[1] = HW_DISKSTATS;
	len = sizeof kd;
	if (sysctl(mib, 2, kd, &len, NULL, 0) < 0)
		len = 0;
	hw->ndisk = (int)(len / sizeof kd[0]);
	for (i = 0; i < hw->ndisk; i++) {
		memcpy(hw->disk[i].name, kd[i].kd_name, sizeof hw->disk[i].name);
		hw->disk[i].name[sizeof hw->disk[i].name - 1] = '\0';
		hw->disk[i].xfer = kd[i].kd_xfer;
		hw->disk[i].bytes = kd[i].kd_bytes;
	}
	collect_fs(hw);
}

static void
collect_procs(struct hwinfo *hw)
{
	static struct kinfo_proc kp[HWI_NPROC];
	int mib[5], i, n;
	size_t len = sizeof kp;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_ALL;
	mib[3] = 0;
	mib[4] = sizeof kp[0];
	hw->nproc = 0;
	if (sysctl(mib, 5, kp, &len, NULL, 0) < 0) {
		/* A table larger than HWI_NPROC still fills the buffer. */
		if (errno != ENOMEM) {
			hw->procs_state = state_of(errno);
			return;
		}
	}
	hw->procs_state = RD_OK;
	n = (int)(len / sizeof kp[0]);
	for (i = 0; i < n && hw->nproc < HWI_NPROC; i++) {
		struct hwi_proc *p = &hw->proc[hw->nproc];
		const struct kinfo_proc *k = &kp[i];

		if (k->kp_stat == 0)
			continue;
		p->pid = k->kp_pid;
		p->ppid = k->kp_ppid;
		p->uid = (int)k->kp_uid;
		p->stat = k->kp_stat;
		p->incore = (k->kp_flag & SLOAD) != 0;
		p->pri = k->kp_pri;
		p->nice = k->kp_nice;
		p->timed = (k->kp_valid & KP_VALID_TIMES) != 0;
		p->ticks = (unsigned long)k->kp_utime + k->kp_stime;
		p->bytes = (unsigned long)k->kp_tsize + k->kp_dsize +
		    k->kp_ssize;
		if (k->kp_valid & KP_VALID_COMM) {
			strncpy(p->comm, k->kp_comm, HWI_NAME - 1);
			p->comm[HWI_NAME - 1] = '\0';
		} else
			strcpy(p->comm, k->kp_stat == SZOMB ? "<defunct>" :
			    "?");
		hw->nproc++;
	}
}

void
hw_collect(struct hwinfo *hw, int groups)
{
	if (groups & HWI_STATIC)
		collect_static(hw);
	if (groups & HWI_LIVE)
		collect_live(hw);
	if (groups & HWI_PROCS)
		collect_procs(hw);
}
