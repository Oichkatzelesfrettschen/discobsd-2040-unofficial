/*
 * picotop -- a full-screen monitor of the processor, memory, swap,
 * storage, board levels and processes, sized for the PicoCalc's 53 by 32
 * console and usable on any terminal.
 *
 * usage: picotop [-np] [-d seconds] [-c count] [-s cpu|mem|pid]
 *	-d	seconds between refreshes, 1 to 60 (default 2)
 *	-c	exit after that many frames
 *	-s	the process order: processor share, size or process id
 *	-n	no color
 *	-p	plain: frames as ASCII lines with no escape sequence (the
 *		default when the output is not a terminal)
 *
 * Keys: q quits; c, m and p choose the process order; + and - change the
 * interval; space refreshes at once.
 *
 * The readings and their states are those of ../picofetch/hwinfo.h. Rates
 * are differences between two collections: the processor shares divide
 * the change of each state's tick count by the change of their sum, and a
 * process's share divides the change of its user plus system ticks by the
 * same sum. The first frame has no earlier collection and shows the
 * shares since boot. The history graph keeps one busy share per frame,
 * newest at the right.
 *
 * One process is resident in the user window at a time, so each refresh
 * that finds another process running costs that process a swap exchange;
 * the interval is the control over that cost.
 */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>

#ifdef HOSTBUILD
#include <termios.h>
#else
#include <sgtty.h>
#include <sys/ioctl.h>
#endif

#include "hwinfo.h"
#include "pcdraw.h"

#define	HIST		160		/* graph columns kept */
#define	SORT_CPU	0
#define	SORT_MEM	1
#define	SORT_PID	2

static struct pcterm term;
static struct hwinfo hw;

static long	prev_cp[4];
static int	have_prev;
static long	share[4];		/* tenths of a percent, per state */
static long	busy;			/* tenths of a percent */
static long	dsecs;			/* seconds between the collections */
static long	prev_now;

static struct { int pid; unsigned long ticks; } prev_proc[HWI_NPROC];
static int	nprev;
static long	proc_share[HWI_NPROC];	/* tenths of a percent */
static int	order[HWI_NPROC];

static struct hwi_disk prev_disk[HWI_NDISK];
static int	nprev_disk;

static unsigned char hist[HIST];	/* percent, oldest first */
static int	nhist;

static int	interval = 2;
static int	sortkey = SORT_CPU;
static int	raw_set;

#ifdef HOSTBUILD
static struct termios saved_tty;
#else
static struct sgttyb saved_tty;
#endif

static void
tty_raw(void)
{
#ifdef HOSTBUILD
	struct termios t;

	if (tcgetattr(0, &saved_tty) < 0)
		return;
	t = saved_tty;
	t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	raw_set = tcsetattr(0, TCSANOW, &t) == 0;
#else
	struct sgttyb sg;

	if (ioctl(0, TIOCGETP, &saved_tty) < 0)
		return;
	sg = saved_tty;
	sg.sg_flags |= CBREAK;
	sg.sg_flags &= ~ECHO;
	raw_set = ioctl(0, TIOCSETP, &sg) == 0;
#endif
}

static void
tty_restore(void)
{
	if (!raw_set)
		return;
#ifdef HOSTBUILD
	(void)tcsetattr(0, TCSANOW, &saved_tty);
#else
	(void)ioctl(0, TIOCSETP, &saved_tty);
#endif
	raw_set = 0;
}

static void
onsig(int sig)
{
	tty_restore();
	pc_done();
	_exit(128 + sig);
}

/*
 * part of whole in tenths of a percent, rounded. Tick counts reach 2^32 and long is
 * 32 bits on the target, so both are halved until the product fits.
 */
static long
permille(unsigned long part, unsigned long whole)
{
	while (whole > 1UL << 20) {
		whole >>= 1;
		part >>= 1;
	}
	if (whole == 0)
		return 0;
	return part >= whole ? 1000 :
	    (long)((part * 1000 + whole / 2) / whole);
}

/*
 * Shares and rates from this collection and the one before it. The tick
 * counters wrap at 2^32, which unsigned subtraction absorbs.
 */
static void
compute(void)
{
	unsigned long d[4], sum = 0;
	int i, j;

	for (i = 0; i < 4; i++) {
		d[i] = ((unsigned long)hw.cp_time[i].v -
		    (have_prev ? (unsigned long)prev_cp[i] : 0)) & 0xffffffffUL;
		sum += d[i];
		prev_cp[i] = hw.cp_time[i].v;
	}
	for (i = 0; i < 4; i++)
		share[i] = permille(d[i], sum);
	busy = permille(sum - d[3], sum);
	dsecs = have_prev ? hw.now - prev_now : 0;
	prev_now = hw.now;

	if (nhist == HIST) {
		memmove(hist, hist + 1, HIST - 1);
		nhist--;
	}
	hist[nhist++] = (unsigned char)((busy + 5) / 10);

	for (i = 0; i < hw.nproc; i++) {
		unsigned long was = 0;

		for (j = 0; j < nprev; j++)
			if (prev_proc[j].pid == hw.proc[i].pid) {
				was = prev_proc[j].ticks;
				break;
			}
		if (!have_prev || !hw.proc[i].timed || hw.proc[i].ticks < was)
			proc_share[i] = 0;
		else
			proc_share[i] = permille(hw.proc[i].ticks - was, sum);
	}
	nprev = hw.nproc;
	for (i = 0; i < hw.nproc; i++) {
		prev_proc[i].pid = hw.proc[i].pid;
		prev_proc[i].ticks = hw.proc[i].ticks;
	}
	have_prev = 1;
}

/* Nonzero when process a sorts before process b. */
static int
before(int a, int b)
{
	if (sortkey == SORT_CPU && proc_share[a] != proc_share[b])
		return proc_share[a] > proc_share[b];
	if (sortkey == SORT_MEM && hw.proc[a].bytes != hw.proc[b].bytes)
		return hw.proc[a].bytes > hw.proc[b].bytes;
	return hw.proc[a].pid < hw.proc[b].pid;
}

static void
sort_procs(void)
{
	int i, j, t;

	for (i = 0; i < hw.nproc; i++)
		order[i] = i;
	for (i = 1; i < hw.nproc; i++)
		for (j = i; j > 0 && before(order[j], order[j - 1]); j--) {
			t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
}

static int	row;			/* the row being drawn */
static int	left;			/* cells left in it */

/* Formatted text clipped to the cells the row has left. */
static void
emit(const char *fmt, ...)
{
	char buf[200];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof buf - 1)
		n = (int)sizeof buf - 1;
	if (n > left)
		n = left;
	if (n > 0)
		fwrite(buf, 1, (size_t)n, stdout);
	left -= n > 0 ? n : 0;
}

static void
begin(void)
{
	left = term.cols;
	if (term.tier != PC_PLAIN)
		printf("\033[%d;1H", row + 1);
}

static void
end(void)
{
	/*
	 * A row written through its last cell leaves the cursor on that
	 * cell, where an erase to the end of the line would remove it.
	 */
	if (term.tier != PC_PLAIN)
		fputs(left > 0 ? "\033[0m\033[K" : "\033[0m", stdout);
	else
		putchar('\n');
	row++;
}

static void
label(const char *s)
{
	pc_fg(&term, 14);
	emit("%-4.4s ", s);
	pc_reset(&term);
}

static const char *
why(int state)
{
	return state == RD_ABSENT ? "not in this kernel" : "read failed";
}

static int
level_color(long part, long total, int inverse)
{
	long pct = total > 0 ? part / (total / 100 > 0 ? total / 100 : 1) : 0;

	if (inverse)
		return pct > 40 ? 2 : pct > 15 ? 3 : 1;
	return pct < 60 ? 2 : pct < 85 ? 3 : 1;
}

static void
level(const char *name, long part, long total, int inverse, int cells)
{
	int color = level_color(part, total, inverse);

	label(name);
	pc_bar(&term, cells, 1, &part, &color, total);
	left -= cells;
}

static void
draw_title(void)
{
	char up[24], line[160];

	if (hw.boottime.state == RD_OK)
		pc_uptime(up, sizeof up, hw.now - hw.boottime.v);
	else
		snprintf(up, sizeof up, "?");
	snprintf(line, sizeof line, " picotop  %s  up %s  load %ld.%02ld",
	    hw.hostname, up, hw.load[0].v / 100, hw.load[0].v % 100);
	begin();
	pc_reverse(&term);
	pc_bold(&term);
	emit("%-*.*s", term.cols, term.cols, line);
	end();
}

static void
draw_cpu(int cells)
{
	static const int col[3] = { 2, 6, 1 };	/* user, nice, system */
	long val[3];

	val[0] = share[0];
	val[1] = share[1];
	val[2] = share[2];
	begin();
	label("CPU");
	if (hw.cp_time[0].state != RD_OK) {
		emit("%s", why(hw.cp_time[0].state));
		end();
		return;
	}
	pc_bar(&term, cells, 3, val, col, 1000);
	left -= cells;
	emit(" %3ld.%ld%%", busy / 10, busy % 10);
	end();

	begin();
	emit("     ");
	pc_fg(&term, 2);
	emit("user %ld.%ld ", share[0] / 10, share[0] % 10);
	pc_fg(&term, 6);
	emit("nice %ld.%ld ", share[1] / 10, share[1] % 10);
	pc_fg(&term, 1);
	emit("sys %ld.%ld ", share[2] / 10, share[2] % 10);
	pc_reset(&term);
	emit("idle %ld.%ld", share[3] / 10, share[3] % 10);
	end();
}

static void
draw_graph(int grows)
{
	int g, c, first, v, h, lvl;

	first = nhist > term.cols ? nhist - term.cols : 0;
	for (g = 0; g < grows; g++) {
		begin();
		for (c = 0; c < term.cols; c++) {
			int at = first + c - (term.cols - (nhist - first));

			if (at < first || at >= nhist) {
				putchar(' ');
				continue;
			}
			v = hist[at];
			h = v * grows * 9 / 100;
			if (v > 0 && h == 0)
				h = 1;
			lvl = h - (grows - 1 - g) * 9;
			pc_fg(&term, v < 50 ? 10 : v < 80 ? 11 : 9);
			pc_vcell(&term, lvl);
		}
		left = 0;
		end();
	}
}

static void
draw_levels(int cells)
{
	char a[16], b[16];
	int i;

	begin();
	if (hw.mem_real.state == RD_OK) {
		level("Mem", hw.mem_real.v, hw.window.v, 0, cells);
		pc_size(a, sizeof a, (hw.mem_real.v + 1023) / 1024);
		pc_size(b, sizeof b, hw.window.v / 1024);
		emit(" %s/%s core", a, b);
	} else {
		label("Mem");
		emit("%s", why(hw.mem_real.state));
	}
	end();

	begin();
	if (hw.nswap.state == RD_OK && hw.swapfree.state == RD_OK) {
		level("Swp", hw.nswap.v - hw.swapfree.v, hw.nswap.v, 0, cells);
		pc_size(a, sizeof a, hw.nswap.v - hw.swapfree.v);
		pc_size(b, sizeof b, hw.nswap.v);
		emit(" %s/%s i%ld o%ld", a, b, hw.r_swpin.v, hw.r_swpout.v);
	} else {
		label("Swp");
		emit("%s", why(hw.swapfree.state));
	}
	end();

	for (i = 0; i < hw.nfs && i < 2; i++) {
		const struct hwi_fs *f = &hw.fs[i];

		begin();
		level(f->on, f->kb - f->kbfree, f->kb, 0, cells);
		pc_size(a, sizeof a, f->kb - f->kbfree);
		pc_size(b, sizeof b, f->kb);
		emit(" %s/%s", a, b);
		end();
	}

	begin();
	if (hw.battery.state == RD_OK) {
		level("Bat", hw.battery.v, 100, 1, cells);
		emit(" %ld%%%s", hw.battery.v, hw.charging.state == RD_OK &&
		    hw.charging.v ? " charging" : "");
	} else {
		label("Bat");
		emit("%s", why(hw.battery.state));
	}
	end();
}

static void
draw_board(void)
{
	static const char *const st[4] = {
		"parked", "observer", "restarting", "dead"
	};
	int i;

	begin();
	label("Tmp");
	if (hw.temp_mc.state == RD_OK)
		emit("%ld.%ld C  VSYS %ld.%02ld V", hw.temp_mc.v / 1000,
		    labs(hw.temp_mc.v) % 1000 / 100, hw.vsys_mv.v / 1000,
		    hw.vsys_mv.v % 1000 / 10);
	else
		emit("%s", why(hw.temp_mc.state));
	end();

	begin();
	label("Dsk");
	for (i = 0; i < hw.ndisk; i++) {
		unsigned long dx = 0, db = 0;

		if (i < nprev_disk && dsecs > 0 &&
		    hw.disk[i].xfer >= prev_disk[i].xfer &&
		    hw.disk[i].bytes >= prev_disk[i].bytes) {
			dx = (hw.disk[i].xfer - prev_disk[i].xfer) /
			    (unsigned long)dsecs;
			db = (hw.disk[i].bytes - prev_disk[i].bytes) /
			    (unsigned long)dsecs / 1024;
		}
		pc_fg(&term, dx > 0 ? 11 : 7);
		emit("%s %lu/s %luK  ", hw.disk[i].name, dx, db);
		prev_disk[i] = hw.disk[i];
	}
	nprev_disk = hw.ndisk;
	if (hw.ndisk == 0)
		emit("none reported");
	end();

	begin();
	label("Evt");
	if (hw.r_swtch.state == RD_OK)
		emit("cs %ld sc %ld in %ld /s  run %ld slp %ld swp %ld",
		    hw.r_swtch.v, hw.r_syscall.v, hw.r_intr.v, hw.runq.v,
		    hw.sleeping.v, hw.swapped.v);
	else
		emit("%s", why(hw.r_swtch.state));
	end();

	begin();
	label("Sys");
	emit("core1 %s", hw.core1.state == RD_OK ? st[hw.core1.v & 3] :
	    hw.core1.state == RD_ABSENT ? "absent" : "failed");
	if (hw.xip_cache.state == RD_OK) {
		unsigned long acc = (unsigned long)hw.xip_acc.v;
		unsigned long per = acc / 1000 > 0 ?
		    (unsigned long)hw.xip_hit.v / (acc / 1000) : 0;

		emit("  xip %lu.%lu%%", per / 10, per % 10);
	}
	if (hw.t_proc.state == RD_OK)
		emit("  procs %d/%d", hw.t_proc.cur, hw.t_proc.limit);
	if (hw.swapram_images.state == RD_OK)
		emit("  ram %ld", hw.swapram_images.v);
	end();
}

/*
 * The command a row shows. The kernel copies u_comm into the record, and
 * only exec sets u_comm, so process 0 (the swapper) carries an empty name.
 * ps(1) shows "swapper" for that row too; a name the kernel does report
 * is shown as it is.
 */
static const char *
comm_of(const struct hwi_proc *p)
{
	return p->pid == 0 && p->comm[0] == '\0' ? "swapper" : p->comm;
}

static void
draw_procs(int last)
{
	static const char states[] = "?SWRIZT";
	char size[16];
	int i, w;

	begin();
	pc_reverse(&term);
	emit("%-*.*s", term.cols, term.cols,
	    sortkey == SORT_CPU ? "  PID UID PRI  NI  SIZE ST >CPU%   TIME COMMAND" :
	    sortkey == SORT_MEM ? "  PID UID PRI  NI >SIZE ST  CPU%   TIME COMMAND" :
	    " >PID UID PRI  NI  SIZE ST  CPU%   TIME COMMAND");
	end();
	if (hw.procs_state != RD_OK) {
		begin();
		emit("process table: %s", why(hw.procs_state));
		end();
	}
	w = term.cols - 41;
	if (w < 1)
		w = 1;
	for (i = 0; i < hw.nproc && row < last; i++) {
		const struct hwi_proc *p = &hw.proc[order[i]];
		long secs = hw.hz.v > 0 ? (long)(p->ticks / (unsigned long)hw.hz.v) : 0;

		begin();
		pc_size(size, sizeof size, (long)((p->bytes + 1023) / 1024));
		if (p->stat == 3)
			pc_fg(&term, 10);
		else if (p->stat == 5)
			pc_fg(&term, 9);
		else if (!p->incore)
			pc_fg(&term, 8);
		emit("%5d %3d %3d %3d %5s %c%c %3ld.%ld %3ld:%02ld %.*s",
		    p->pid, p->uid, p->pri, p->nice, size,
		    states[p->stat >= 0 && p->stat <= 6 ? p->stat : 0],
		    p->incore ? '+' : ' ', proc_share[order[i]] / 10,
		    proc_share[order[i]] % 10, secs / 60, secs % 60, w,
		    comm_of(p));
		end();
	}
	while (term.tier != PC_PLAIN && row < last) {
		begin();
		end();
	}
}

static void
draw(void)
{
	int cells, grows;

	row = 0;
	cells = term.cols - 5 - 21;
	if (cells < 8)
		cells = 8;
	grows = term.tier == PC_PLAIN ? 0 : term.rows >= 30 ? 5 :
	    term.rows >= 22 ? 3 : 0;

	draw_title();
	draw_cpu(term.cols - 5 - 7);
	draw_graph(grows);
	draw_levels(cells);
	draw_board();
	sort_procs();
	draw_procs(term.tier == PC_PLAIN ? 1000 : term.rows - 1);
	if (term.tier != PC_PLAIN) {
		begin();
		pc_fg(&term, 8);
		emit("q quit  c/m/p order  +/- interval  space now %ds",
		    interval);
		end();
	} else
		putchar('\n');
	fflush(stdout);
}

/* Wait up to the interval for a key; the key, or -1 at the timeout. */
static int
wait_key(void)
{
	fd_set rd;
	struct timeval tv;
	char c;

	FD_ZERO(&rd);
	FD_SET(0, &rd);
	tv.tv_sec = interval;
	tv.tv_usec = 0;
	if (select(1, &rd, NULL, NULL, &tv) <= 0)
		return -1;
	if (read(0, &c, 1) != 1)
		return 'q';		/* End of input. */
	return (unsigned char)c;
}

static void
usage(void)
{
	fprintf(stderr, "usage: picotop [-np] [-d seconds] [-c count] "
	    "[-s cpu|mem|pid]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int c, color = -1, plain = 0, count = 0, frames = 0, keys;

	while ((c = getopt(argc, argv, "c:d:nps:")) != -1)
		switch (c) {
		case 'c':
			count = atoi(optarg);
			if (count < 1)
				usage();
			break;
		case 'd':
			interval = atoi(optarg);
			if (interval < 1 || interval > 60)
				usage();
			break;
		case 'n':
			color = 0;
			break;
		case 'p':
			plain = 1;
			break;
		case 's':
			if (strcmp(optarg, "cpu") == 0)
				sortkey = SORT_CPU;
			else if (strcmp(optarg, "mem") == 0)
				sortkey = SORT_MEM;
			else if (strcmp(optarg, "pid") == 0)
				sortkey = SORT_PID;
			else
				usage();
			break;
		default:
			usage();
		}
	if (optind != argc)
		usage();

	signal(SIGINT, onsig);
	signal(SIGTERM, onsig);
	signal(SIGHUP, onsig);
	signal(SIGQUIT, onsig);
	pc_init(&term, 1, color, plain);
	keys = term.tier != PC_PLAIN && isatty(0);
	if (keys)
		tty_raw();
	if (term.tier != PC_PLAIN)
		fputs("\033[2J", stdout);
	hw_collect(&hw, HWI_STATIC);

	for (;;) {
		hw_collect(&hw, HWI_LIVE | HWI_PROCS);
		compute();
		draw();
		if (count > 0 && ++frames >= count)
			break;
		if (!keys) {
			sleep((unsigned)interval);
			continue;
		}
		c = wait_key();
		if (c == 'q' || c == 'Q' || c == 3 || c == 4)
			break;
		if (c == 'c')
			sortkey = SORT_CPU;
		else if (c == 'm')
			sortkey = SORT_MEM;
		else if (c == 'p')
			sortkey = SORT_PID;
		else if (c == '+' && interval < 60)
			interval++;
		else if (c == '-' && interval > 1)
			interval--;
	}
	/* Leave the frame on the screen; the prompt replaces the key line. */
	if (term.tier != PC_PLAIN)
		printf("\033[%d;1H\033[K", term.rows);
	fflush(stdout);
	tty_restore();
	pc_done();
	return 0;
}
