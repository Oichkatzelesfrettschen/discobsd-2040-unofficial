/*
 * Print system stuff
 *
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * pstat reads the kernel's sysctl(3) records and holds no kernel address:
 * KERN_INODE, KERN_FILE (KERN_FILE_BYFILE), KERN_TTY, KERN_PROC, VM_SWAPMAP,
 * VM_NSWAP and KERN_CAPACITY (sys/sysctl.h, sys/capacity.h). The kernel
 * returns the inode, file and terminal tables to the super-user alone, so
 * those views fail with EPERM for any other caller; the process, swap and
 * capacity records need no privilege.
 *
 * Every RP2040 kernel configuration builds with COMPACT_SWAPMAP, and the
 * VM_SWAPMAP payload carries that four-byte struct mapent. The userland
 * CFLAGS define COMPACT_SWAPMAP; the guard below keeps the layout correct
 * when sys/map.h is reached without them.
 */
#ifndef	COMPACT_SWAPMAP
#define	COMPACT_SWAPMAP
#endif
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/capacity.h>
#include <sys/tty.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * f_flag bits the kernel keeps for its file-table sweep; sys/fcntl.h
 * exposes them to the kernel alone, and kf_flag carries them unchanged.
 */
#ifndef	FMARK
#define	FMARK		0x1000
#endif
#ifndef	FDEFER
#define	FDEFER		0x2000
#endif

_Static_assert(sizeof(struct mapent) == 4,
    "pstat must decode the compact RP2040 swap descriptors");

#define	KBYTES(blocks)	((unsigned long)(blocks) * DEV_BSIZE / 1024)

void	usage(void);
void	fail(const char *);
void	*fetchrecords(const char *, int *, u_int, size_t *);
void	fetchfixed(const char *, int *, u_int, void *, size_t);
int	capacitylimit(int);
void	getcapacity(struct kernel_capacity_stats *);
void	doinode(void);
void	putf(long, char);
void	doproc(void);
void	dotty(void);
void	ttyprt(const struct kinfo_tty *);
void	dofile(void);
void	doswap(void);
void	dototals(void);

int	inof;
int	prcf;
int	ttyf;
int	filf;
int	swpf;
int	totflg;

int
main(int argc, char **argv)
{
	register char *argp;

	argc--, argv++;
	while (argc > 0 && **argv == '-') {
		argp = *argv++;
		argp++;
		argc--;
		while (*argp++)
		switch (argp[-1]) {

		case 'T':
			totflg++;
			break;

		case 'i':
			inof++;
			break;

		case 'p':
			prcf++;
			break;

		case 't':
			ttyf++;
			break;

		case 'f':
			filf++;
			break;
		case 's':
			swpf++;
			break;
		default:
			usage();
			exit(1);
		}
	}
	if (argc > 0) {
		usage();
		exit(1);
	}
	if (! (filf | totflg | inof | prcf | ttyf | swpf))
		filf++;

	if (totflg)
		dototals();
	else {
		if (filf)
			dofile();
		if (inof)
			doinode();
		if (prcf)
			doproc();
	}
	if (ttyf)
		dotty();
	if (swpf||totflg)
		doswap();
	return (0);
}

void
usage(void)
{
	fprintf(stderr, "usage: pstat -[iptfsT]\n");
}

/* Report the failed sysctl(3) call for a view and exit. */
void
fail(const char *view)
{
	fprintf(stderr, "pstat: %s: %s\n", view, strerror(errno));
	exit(1);
}

/*
 * Read the whole record array a name selects: a size query sizes the
 * buffer, and a table that grew between the query and the read (ENOMEM)
 * is sized again. The caller frees the buffer; *lenp is the bytes read.
 */
void *
fetchrecords(const char *view, int *mib, u_int miblen, size_t *lenp)
{
	void *buf;
	size_t size;
	int attempt, error;

	for (attempt = 0; attempt < 4; attempt++) {
		size = 0;
		if (sysctl(mib, miblen, NULL, &size, NULL, 0) < 0)
			fail(view);
		buf = malloc(size ? size : 1);
		if (buf == NULL) {
			fprintf(stderr, "pstat: out of memory\n");
			exit(1);
		}
		if (sysctl(mib, miblen, buf, &size, NULL, 0) >= 0) {
			*lenp = size;
			return (buf);
		}
		error = errno;
		free(buf);
		if (error != ENOMEM) {
			errno = error;
			fail(view);
		}
	}
	errno = ENOMEM;
	fail(view);
	return (NULL);
}

/* Read a value of exactly want bytes into dst, or exit. */
void
fetchfixed(const char *view, int *mib, u_int miblen, void *dst, size_t want)
{
	size_t size = want;

	if (sysctl(mib, miblen, dst, &size, NULL, 0) < 0)
		fail(view);
	if (size != want) {
		fprintf(stderr, "pstat: %s: value is %u bytes, expected %u\n",
		    view, (unsigned)size, (unsigned)want);
		exit(1);
	}
}

void
getcapacity(struct kernel_capacity_stats *stats)
{
	int mib[2];

	mib[0] = CTL_KERN;
	mib[1] = KERN_CAPACITY;
	fetchfixed("capacity", mib, 2, stats, sizeof(*stats));
	if (stats->version != KERNEL_CAPACITY_VERSION) {
		fprintf(stderr, "pstat: capacity: version %u, expected %u\n",
		    (unsigned)stats->version, (unsigned)KERNEL_CAPACITY_VERSION);
		exit(1);
	}
}

/*
 * The table limit KERN_CAPACITY reports for a kind, or -1 when the kernel
 * serves no capacity record; a view prints its denominator only when known.
 */
int
capacitylimit(int kind)
{
	struct kernel_capacity_stats stats;
	size_t size = sizeof(stats);
	int mib[2];

	mib[0] = CTL_KERN;
	mib[1] = KERN_CAPACITY;
	if (sysctl(mib, 2, &stats, &size, NULL, 0) < 0 ||
	    size != sizeof(stats) || stats.version != KERNEL_CAPACITY_VERSION)
		return (-1);
	return (stats.metric[kind].limit);
}

/* Print "n/limit what", or "n what" when the limit is unknown. */
static void
putcount(int n, int limit, const char *what)
{
	if (limit < 0)
		printf("%d %s\n", n, what);
	else
		printf("%d/%d %s\n", n, limit, what);
}

void
doinode(void)
{
	static const int mib[2] = { CTL_KERN, KERN_INODE };
	struct kinfo_inode *ip, *xinode;
	size_t len;
	int nin;

	xinode = fetchrecords("inodes", (int *)mib, 2, &len);
	if (len % sizeof(*xinode) != 0) {
		fprintf(stderr, "pstat: inodes: %u bytes is not a whole "
		    "number of %u-byte records\n", (unsigned)len,
		    (unsigned)sizeof(*xinode));
		exit(1);
	}
	nin = (int)(len / sizeof(*xinode));
	putcount(nin, capacitylimit(CAPACITY_INODE), "active inodes");
	printf("%-14s%4s%8s%4s%4s%7s%7s%4s%5s%10s\n", "FLAGS", "CNT",
	    "DEVICE", "RDC", "WRC", "INO", "MODE", "NLK", "UID", "SIZE/DEV");
	for (ip = xinode; ip < &xinode[nin]; ip++) {
		putf((long)ip->ki_flag&ILOCKED, 'L');
		putf((long)ip->ki_flag&IUPD, 'U');
		putf((long)ip->ki_flag&IACC, 'A');
		putf((long)ip->ki_flag&IMOUNT, 'M');
		putf((long)ip->ki_flag&IWANT, 'W');
		putf((long)ip->ki_flag&ITEXT, 'T');
		putf((long)ip->ki_flag&ICHG, 'C');
		putf((long)ip->ki_flag&ISHLOCK, 'S');
		putf((long)ip->ki_flag&IEXLOCK, 'E');
		putf((long)ip->ki_flag&ILWAIT, 'Z');
		putf((long)ip->ki_flag&IPIPE, 'P');
		putf((long)ip->ki_flag&IMOD, 'm');
		putf((long)ip->ki_flag&IRENAME, 'r');
		putf((long)ip->ki_flag&IXMOD, 'x');
		printf("%4d", (int)ip->ki_count);
		printf("%4d,%3d", major(ip->ki_dev), minor(ip->ki_dev));
		printf("%4d", (int)ip->ki_shlockc);
		printf("%4d", (int)ip->ki_exlockc);
		printf("%6u ", (unsigned)ip->ki_number);
		printf("%7.1o", (unsigned)ip->ki_mode);
		printf("%4d", (int)ip->ki_nlink);
		printf("%5u", (unsigned)ip->ki_uid);
		if ((ip->ki_mode&IFMT)==IFBLK || (ip->ki_mode&IFMT)==IFCHR)
			printf("%6d,%3d", major(ip->ki_isize),
			    minor(ip->ki_isize));
		else
			printf("%10ld", (long)ip->ki_isize);
		printf("\n");
	}
	free(xinode);
}

void
putf(long v, char n)
{
	if (v)
		printf("%c", n);
	else
		printf(" ");
}

void
doproc(void)
{
	static const int mib[5] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0,
	    sizeof(struct kinfo_proc) };
	struct kinfo_proc *xproc, *pp;
	size_t len;
	int np;

	xproc = fetchrecords("processes", (int *)mib, 5, &len);
	if (len % sizeof(*xproc) != 0) {
		fprintf(stderr, "pstat: processes: %u bytes is not a whole "
		    "number of %u-byte records\n", (unsigned)len,
		    (unsigned)sizeof(*xproc));
		exit(1);
	}
	np = (int)(len / sizeof(*xproc));
	putcount(np, capacitylimit(CAPACITY_PROC), "processes");
	printf("%2s %7s %3s %5s %4s %3s %6s %6s %6s %6s\n", "S", "F", "PRI",
	    "UID", "CPU", "NI", "PGRP", "PID", "PPID", "SIZE");
	for (pp = xproc; pp < &xproc[np]; pp++) {
		printf("%2d", (int)pp->kp_stat);
		printf(" %7.1x", (unsigned)pp->kp_flag);
		printf(" %3d", (int)pp->kp_pri);
		printf(" %5u", (unsigned)pp->kp_uid);
		printf(" %4d", (int)pp->kp_cpu);
		printf(" %3d", (int)pp->kp_nice);
		printf(" %6d", (int)pp->kp_pgid);
		printf(" %6d", (int)pp->kp_pid);
		printf(" %6d", (int)pp->kp_ppid);
		printf(" %6x", (unsigned)(pp->kp_dsize + pp->kp_ssize));
		printf("\n");
	}
	free(xproc);
}

void
dotty(void)
{
	static const int mib[2] = { CTL_KERN, KERN_TTY };
	struct kinfo_tty kt;

	fetchfixed("tty", (int *)mib, 2, &kt, sizeof(kt));
	printf("cn line\n");
	printf("%4s%4s%4s %12s %4s %4s %-14s%6s\n", "RAW", "CAN", "OUT",
	    "MODE", "DEL", "COL", "STATE", "PGRP");
	ttyprt(&kt);
}

void
ttyprt(const struct kinfo_tty *tp)
{
	printf("%4d%4d", (int)tp->kt_rawcc, (int)tp->kt_cancc);
	printf("%4d %12.1lo %4d %4d ", (int)tp->kt_outcc,
		(unsigned long)tp->kt_flags,
		(int)tp->kt_delct, (int)tp->kt_col);
	putf((long)tp->kt_state&TS_TIMEOUT, 'T');
	putf((long)tp->kt_state&TS_WOPEN, 'W');
	putf((long)tp->kt_state&TS_ISOPEN, 'O');
	putf((long)tp->kt_state&TS_FLUSH, 'F');
	putf((long)tp->kt_state&TS_CARR_ON, 'C');
	putf((long)tp->kt_state&TS_BUSY, 'B');
	putf((long)tp->kt_state&TS_ASLEEP, 'A');
	putf((long)tp->kt_state&TS_XCLUDE, 'X');
	putf((long)tp->kt_state&TS_TTSTOP, 'S');
	putf((long)tp->kt_state&TS_HUPCLS, 'H');
	putf((long)tp->kt_state&TS_TBLOCK, 'b');
	putf((long)tp->kt_state&TS_RCOLL, 'r');
	putf((long)tp->kt_state&TS_WCOLL, 'w');
	putf((long)tp->kt_state&TS_ASYNC, 'a');
	printf("%6d\n", (int)tp->kt_pgrp);
}

void
dofile(void)
{
	static const int mib[5] = { CTL_KERN, KERN_FILE, KERN_FILE_BYFILE, 0,
	    sizeof(struct kinfo_file) };
	struct kinfo_file *xfile, *fp;
	size_t len;
	int nf;
	static const char *dtypes[] = { "???", "inode", "socket", "pipe" };

	xfile = fetchrecords("files", (int *)mib, 5, &len);
	if (len % sizeof(*xfile) != 0) {
		fprintf(stderr, "pstat: files: %u bytes is not a whole "
		    "number of %u-byte records\n", (unsigned)len,
		    (unsigned)sizeof(*xfile));
		exit(1);
	}
	nf = (int)(len / sizeof(*xfile));
	putcount(nf, capacitylimit(CAPACITY_FILE), "open files");
	printf("%-8s%-9s%5s%5s%10s  %s\n", "TYPE", "FLG", "CNT", "MSG", "INO",
	    "OFFSET");
	for (fp = xfile; fp < &xfile[nf]; fp++) {
		if (fp->kf_type <= DTYPE_PIPE)
			printf("%-8.8s", dtypes[fp->kf_type]);
		else
			printf("%-8d", (int)fp->kf_type);
		putf((long)fp->kf_flag&FREAD, 'R');
		putf((long)fp->kf_flag&FWRITE, 'W');
		putf((long)fp->kf_flag&FAPPEND, 'A');
		putf((long)fp->kf_flag&FSHLOCK, 'S');
		putf((long)fp->kf_flag&FEXLOCK, 'X');
		putf((long)fp->kf_flag&FASYNC, 'I');
		putf((long)fp->kf_flag&FNONBLOCK, 'n');
		putf((long)fp->kf_flag&FMARK, 'm');
		putf((long)fp->kf_flag&FDEFER, 'd');
		printf("  %3d", (int)fp->kf_count);
		printf("  %3d", (int)fp->kf_msgcount);
		if (fp->kf_valid & KF_VALID_INODE)
			printf("  %8u", (unsigned)fp->kf_inum);
		else
			printf("  %8s", "-");
		if (fp->kf_offset < 0)
			printf("  0x%lx\n", (long)fp->kf_offset);
		else
			printf("  %ld\n", (long)fp->kf_offset);
	}
	free(xfile);
}

/*
 * VM_SWAPMAP lists the free runs of swap in ascending order and ends with
 * an entry of size 0; VM_NSWAP is the swap size. Both count DEV_BSIZE
 * blocks, so used swap is nswap less the sum of the free runs.
 */
void
doswap(void)
{
	static const int mapmib[2] = { CTL_VM, VM_SWAPMAP };
	static const int nswapmib[2] = { CTL_VM, VM_NSWAP };
	struct mapent *swp;
	size_t len;
	unsigned long freeblocks, usedblocks;
	int nswap, i, num;

	swp = fetchrecords("swapmap", (int *)mapmib, 2, &len);
	num = (int)(len / sizeof(*swp));
	fetchfixed("nswap", (int *)nswapmib, 2, &nswap, sizeof(nswap));
	for (freeblocks = 0, i = 0; i < num && swp[i].m_size; i++)
		freeblocks += swp[i].m_size;
	usedblocks = (unsigned long)nswap > freeblocks ?
	    (unsigned long)nswap - freeblocks : 0;
	printf("%d/%d swapmap entries\n", i, num);
	printf("%lu kbytes swap used, %lu kbytes free\n",
	    KBYTES(usedblocks), KBYTES(freeblocks));
	free(swp);
}

/* -T: current and limit of each fixed table, from KERN_CAPACITY. */
void
dototals(void)
{
	struct kernel_capacity_stats stats;

	getcapacity(&stats);
	printf("%3d/%3d files\n", (int)stats.metric[CAPACITY_FILE].current,
	    (int)stats.metric[CAPACITY_FILE].limit);
	printf("%3d/%3d inodes\n", (int)stats.metric[CAPACITY_INODE].current,
	    (int)stats.metric[CAPACITY_INODE].limit);
	printf("%3d/%3d processes\n", (int)stats.metric[CAPACITY_PROC].current,
	    (int)stats.metric[CAPACITY_PROC].limit);
}
