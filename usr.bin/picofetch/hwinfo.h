/*
 * Hardware and kernel readings shared by picofetch and picotop.
 *
 * hw_collect() fills one struct hwinfo. Every scalar reading carries a
 * state beside its value, so a caller prints "not in this kernel" for a
 * node the running kernel does not serve (RD_ABSENT: EOPNOTSUPP, ENOTDIR
 * or ENOENT from sysctl) and "read failed" for any other error
 * (RD_FAILED), and never a zero that was not read. The target build's
 * collector is hwinfo_sysctl.c; the host test build links
 * tests/hwinfo_fixture.c, which fills the same structure from a named
 * scenario, so the renderers run on a build host without a kernel.
 */
#ifndef PICOFETCH_HWINFO_H
#define PICOFETCH_HWINFO_H

enum rd_state { RD_OK, RD_ABSENT, RD_FAILED };

struct rd_int {
	int	state;			/* enum rd_state */
	long	v;
};

#define	HWI_STR		48
#define	HWI_NDISK	4
#define	HWI_NFS		3
#define	HWI_NPROC	40
#define	HWI_NAME	16

/* The groups hw_collect() refreshes. */
#define	HWI_STATIC	1		/* identity and limits: read once */
#define	HWI_LIVE	2		/* counters, levels and tables */
#define	HWI_PROCS	4		/* the process table */

struct hwi_fs {
	char	on[HWI_NAME];		/* mount point */
	char	from[HWI_NAME];		/* device */
	long	kb;			/* size, kbytes */
	long	kbfree;			/* free, kbytes */
	long	files;			/* inodes */
	long	ffree;			/* free inodes */
};

struct hwi_disk {
	char		name[8];
	unsigned long	xfer;		/* transfers since boot */
	unsigned long	bytes;		/* bytes since boot */
};

struct hwi_proc {
	int		pid, ppid, uid;
	int		stat;		/* p_stat */
	int		incore;		/* resident in the user window */
	int		pri, nice;
	int		timed;		/* ticks is valid */
	unsigned long	ticks;		/* user plus system clock ticks */
	unsigned long	bytes;		/* text, data and stack */
	char		comm[HWI_NAME];
};

/* A fixed kernel table: entries in use, its limit and the peak. */
struct hwi_table {
	int	state;
	int	cur, limit, peak;
};

/*
 * machdep.flash_jedec and flash_unique_hi/lo: valid is 0 when the kernel
 * served the nodes and the chip had returned no plausible identity at boot.
 */
struct hwi_flash_id {
	int		state;		/* enum rd_state */
	int		valid;
	unsigned char	id[11];		/* JEDEC bytes, then the unique ID */
};

/* machdep.pcm: the /dev/audio device of a PicoCalc kernel. */
struct hwi_pcm {
	int	state;			/* enum rd_state */
	long	rate, channels, bits;
	long	ring, queued;
	int	open;
};

struct hwinfo {
	/* HWI_STATIC */
	char	ostype[HWI_STR], osrelease[HWI_STR], osversion[HWI_STR];
	char	hostname[HWI_STR], machine[HWI_STR], model[HWI_STR];
	char	arch[HWI_STR];
	struct rd_int ncpu, physmem, pagesize, cpu_khz, bus_khz, hz;
	struct rd_int boottime, nswap, window;
	struct rd_int mpu_enable, mpu_nregions, mpu_programmed;
	struct rd_int image_length, image_crc, image_crc_stored;
	struct rd_int watchdog_reason;
	struct rd_int cons_major, cons_minor;
	struct hwi_flash_id flash_id;

	/* HWI_LIVE */
	long	now;				/* seconds since the epoch */
	struct rd_int cp_time[4];		/* user, nice, system, idle */
	struct rd_int r_swtch, r_syscall, r_intr, r_swpin, r_swpout;
	struct rd_int load[3];			/* hundredths */
	struct rd_int runq, sleeping, swapped;
	struct rd_int mem_real, mem_virt;	/* bytes */
	struct rd_int swapfree;			/* kbytes */
	struct rd_int swapram_images;
	struct rd_int xip_cache, xip_acc, xip_hit;
	struct rd_int battery, charging, lcd_backlight, kbd_backlight;
	struct rd_int core1, core1_restarts, core1_stalls, core1_faults;
	struct rd_int temp_mc, vsys_mv;
	struct rd_int usb_bulkin_arms, usb_tx_recovered;
	struct hwi_pcm pcm;
	struct rd_int st_root_rd, st_root_wr, st_swap_rd, st_swap_wr;
	struct rd_int st_erases;		/* root plus swap sectors */
	struct rd_int kstack_peak, kstack_bytes;
	struct hwi_table t_proc, t_inode, t_file, t_buffer;
	int	ndisk;
	struct hwi_disk disk[HWI_NDISK];
	int	nfs;
	struct hwi_fs fs[HWI_NFS];

	/* HWI_PROCS */
	int	procs_state;			/* enum rd_state */
	int	nproc;
	struct hwi_proc proc[HWI_NPROC];
};

void	hw_collect(struct hwinfo *hw, int groups);

#endif /* !PICOFETCH_HWINFO_H */
