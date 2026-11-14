/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/dk.h>
#include <sys/sysctl.h>
#include <sys/time.h>

/*
 * Every statistic comes from a sysctl(3) record, so the program needs no
 * privilege: CTL_VM VM_STATS (event counters, cpu ticks, fork and name-cache
 * statistics), CTL_HW HW_DISKSTATS (one record per drive a driver claimed),
 * CTL_VM VM_METER (process and memory totals), CTL_HW HW_USERMEM (free
 * memory), CTL_KERN KERN_BOOTTIME and CTL_KERN KERN_CLOCKRATE.
 */

void    dotimes(void);
void    dosum(void);
void    doforkst(void);
void    stats(int dn);
double  stat1(int row);
void    dointr(long nintv);
void    printhdr(int sig);
void    fetch(int *mib, u_int miblen, void *buf, size_t len, const char *what);
int     fetchdisks(struct kinfo_disk *kd);
void    fetchvmstat(struct kinfo_vmstat *vs);

char    *defdrives[] = { "sd0", 0 };
struct kinfo_disk disks[DK_NDRIVE];     /* names, as first fetched */
int dr_select[DK_NDRIVE];
int dk_ndrive;
int ndrives = 0;
int pflag;
int hz;

struct {
    long    time[CPUSTATES];
    long    xfer[DK_NDRIVE];
} s, s1;

double  etime;
time_t  now, boottime;
int lines = 1;

void
printhdr(int sig)
{
    int i;

    (void)sig;
    if (pflag)
        printf("-procs- -----memory----- -swap- ");
    else
        printf("-procs- ---memory-- ");

    printf("-----disks----- ");

    if (pflag) {
        printf("-----faults----- ------cpu------\n");
        printf(" r b w    avm  tx   fre   i  o  ");
    } else {
        printf("---faults--- ----cpu----\n");
        printf(" r b w    avm   fre ");
    }

    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            printf("%.3s ", disks[i].kd_name);
    printf("  in  sy");
    if (pflag)
        printf("  tr");
    printf("  cs  us");
    if (pflag)
        printf("  ni");
    printf("  sy  id\n");
    lines = 19;
}

/*
 * Read one sysctl record of exactly len bytes into buf, or exit.
 */
void
fetch(int *mib, u_int miblen, void *buf, size_t len, const char *what)
{
    size_t size = len;

    if (sysctl(mib, miblen, buf, &size, NULL, 0) < 0) {
        fprintf(stderr, "vmstat: sysctl %s: %s\n", what,
            strerror(errno));
        exit(1);
    }
    if (size != len) {
        fprintf(stderr, "vmstat: sysctl %s: record is %u bytes, "
            "expected %u\n", what, (unsigned)size, (unsigned)len);
        exit(1);
    }
}

void
fetchvmstat(struct kinfo_vmstat *vs)
{
    int mib[2];

    mib[0] = CTL_VM;
    mib[1] = VM_STATS;
    fetch(mib, 2, vs, sizeof *vs, "vm.stats");
}

/*
 * Read the drive records into kd (room for DK_NDRIVE) and return how many
 * the kernel reports; zero on a kernel whose drivers claim no drive.
 */
int
fetchdisks(struct kinfo_disk *kd)
{
    int mib[2];
    size_t size = DK_NDRIVE * sizeof (struct kinfo_disk);

    mib[0] = CTL_HW;
    mib[1] = HW_DISKSTATS;
    if (sysctl(mib, 2, kd, &size, NULL, 0) < 0) {
        fprintf(stderr, "vmstat: sysctl hw.diskstats: %s\n",
            strerror(errno));
        exit(1);
    }
    return (int)(size / sizeof (struct kinfo_disk));
}

int
main(int argc, char **argv)
{
    int i, mib[2];
    int iter, iflag = 0;
    long nintv, t;
    char **cp;
    unsigned long divisor;
    struct kinfo_vmstat vs;
    struct kinfo_disk cur[DK_NDRIVE];
    struct vmtotal total;
    struct __timeval32 tv;      /* the kernel's layout, sys/sys/time.h */
    struct clockinfo clock;
    size_t size;
    long pfree;
    const struct kinfo_vmevents *ev;

    iter = 0;
    argc--, argv++;
    while (argc>0 && argv[0][0]=='-') {
        char *opt = *argv++;
        argc--;
        while (*++opt) switch (*opt) {

        case 't':
            dotimes();
            exit(0);

        case 'f':
            doforkst();
            exit(0);

        case 's':
            dosum();
            exit(0);

        case 'i':
            iflag++;
            break;

        case 'p':
            pflag++;
            break;

        default:
            fprintf(stderr,
                "usage: vmstat [ -fsip ] [ interval ] [ count]\n");
            exit(1);
        }
    }
    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;
    fetch(mib, 2, &tv, sizeof tv, "kern.boottime");
    boottime = (time_t)(utime32_t)tv.tv_sec;
    mib[1] = KERN_CLOCKRATE;
    size = sizeof clock;
    if (sysctl(mib, 2, &clock, &size, NULL, 0) < 0) {
        fprintf(stderr, "vmstat: sysctl kern.clockrate: %s\n",
            strerror(errno));
        exit(1);
    }
    hz = clock.hz;
    if (hz <= 0) {
        fprintf(stderr, "vmstat: clock rate %d\n", hz);
        exit(1);
    }
    dk_ndrive = fetchdisks(disks);
    time(&now);
    nintv = (long)(now - boottime);
    if (nintv <= 0 || nintv > 60L*60L*24L*365L*10L) {
        fprintf(stderr, "Time makes no sense... boot time must be wrong.\n");
        exit(1);
    }
    if (iflag) {
        dointr(nintv);
        exit(0);
    }
    /*
     * Choose drives to be displayed.  Priority
     * goes to (in order) drives supplied as arguments,
     * default drives.  If everything isn't filled
     * in and there are drives not taken care of,
     * display the first few that fit.
     */
    ndrives = 0;
    while (argc > 0 && !isdigit((unsigned char)argv[0][0])) {
        for (i = 0; i < dk_ndrive; i++) {
            if (strcmp(disks[i].kd_name, argv[0]))
                continue;
            dr_select[i] = 1;
            ndrives++;
        }
        argc--, argv++;
    }
    for (i = 0; i < dk_ndrive && ndrives < 4; i++) {
        if (dr_select[i])
            continue;
        for (cp = defdrives; *cp; cp++)
            if (strcmp(disks[i].kd_name, *cp) == 0) {
                dr_select[i] = 1;
                ndrives++;
                break;
            }
    }
    for (i = 0; i < dk_ndrive && ndrives < 4; i++) {
        if (dr_select[i])
            continue;
        dr_select[i] = 1;
        ndrives++;
    }
    if (argc > 1)
        iter = atoi(argv[1]);
    signal(SIGCONT, printhdr);
loop:
    if (--lines == 0)
        printhdr(0);
    fetchvmstat(&vs);
    for (i = 0; i < CPUSTATES; i++)
        s.time[i] = vs.vs_cp_time[i];
    t = fetchdisks(cur);
    if (t > dk_ndrive)
        t = dk_ndrive;
    for (i = 0; i < t; i++)
        s.xfer[i] = cur[i].kd_xfer;

    /*
     * The first report averages the counts since boot; later ones show the
     * kernel's rates for the last second.
     */
    ev = nintv != 1 ? &vs.vs_sum : &vs.vs_rate;
    divisor = (unsigned long)nintv;

    mib[0] = CTL_HW;
    mib[1] = HW_USERMEM;
    size = sizeof pfree;
    if (sysctl(mib, 2, &pfree, &size, NULL, 0) < 0) {
        fprintf(stderr, "vmstat: sysctl hw.usermem: %s\n",
            strerror(errno));
        exit(1);
    }
    mib[0] = CTL_VM;
    mib[1] = VM_METER;
    fetch(mib, 2, &total, sizeof total, "vm.meter");

    etime = 0;
    for (i=0; i < dk_ndrive; i++) {
        t = s.xfer[i];
        s.xfer[i] -= s1.xfer[i];
        s1.xfer[i] = t;
    }
    for (i=0; i < CPUSTATES; i++) {
        t = s.time[i];
        s.time[i] -= s1.time[i];
        s1.time[i] = t;
        etime += s.time[i];
    }
    if(etime == 0.)
        etime = 1.;

    printf("%2d%2d%2d", total.t_rq, total.t_dw, total.t_sw);
    /*
     * We don't use total.t_free because it slops around too much
     * within this kernel
     */
    printf("%7ld", total.t_avm);
    if (pflag)
        printf("%4ld",
            total.t_avm ? (total.t_avmtxt * 100) / total.t_avm : 0);
    printf("%6ld", pfree);

    if (pflag) {
        printf("%4lu%3lu ", (unsigned long)ev->ve_swpin / divisor,
            (unsigned long)ev->ve_swpout / divisor);
    }

    etime /= (float)hz;
    for (i = 0; i < dk_ndrive; i++) {
        if (dr_select[i])
            stats(i);
    }
    printf("%5lu%4lu", (unsigned long)ev->ve_intr / divisor,
        (unsigned long)ev->ve_syscall / divisor);
    if (pflag)
        printf("%4lu", (unsigned long)ev->ve_trap / divisor);
    printf("%4lu", (unsigned long)ev->ve_swtch / divisor);

    for(i=0; i<CPUSTATES; i++) {
        float f = stat1(i);
        if (! pflag && i == 0) {    /* US+NI */
            i++;
            f += stat1(i);
        }
        printf(" %3.0f", f);
    }
    printf("\n");
    fflush(stdout);
    nintv = 1;
    if (--iter &&argc > 0) {
        sleep(atoi(argv[0]));
        goto loop;
    }
    return 0;
}

void
dotimes(void)
{
    printf("page in/out/reclamation is not applicable to 2.11BSD\n");
}

void
dosum(void)
{
    struct kinfo_vmstat vs;
    unsigned long nchtotal;

    fetchvmstat(&vs);
    printf("%9lu swap ins\n", (unsigned long)vs.vs_sum.ve_swpin);
    printf("%9lu swap outs\n", (unsigned long)vs.vs_sum.ve_swpout);
    printf("%9lu kbytes swapped in\n", (unsigned long)vs.vs_sum.ve_kbin);
    printf("%9lu kbytes swapped out\n", (unsigned long)vs.vs_sum.ve_kbout);
    printf("%9lu cpu context switches\n", (unsigned long)vs.vs_sum.ve_swtch);
    printf("%9lu device interrupts\n", (unsigned long)vs.vs_sum.ve_intr);
    printf("%9lu software interrupts\n", (unsigned long)vs.vs_sum.ve_soft);
    printf("%9lu traps\n", (unsigned long)vs.vs_sum.ve_trap);
    printf("%9lu system calls\n", (unsigned long)vs.vs_sum.ve_syscall);
#define nz(x)   ((x) ? (x) : 1)
    nchtotal = (unsigned long)vs.vs_nch_goodhits + vs.vs_nch_badhits +
        vs.vs_nch_falsehits + vs.vs_nch_miss + vs.vs_nch_long;
    printf("%9lu total name lookups", nchtotal);
    printf(" (cache hits %lu%% system %lu%% per-process)\n",
        (unsigned long)vs.vs_nch_goodhits * 100 / nz(nchtotal),
        (unsigned long)vs.vs_nch_pass2 * 100 / nz(nchtotal));
    printf("%9s badhits %lu, falsehits %lu, toolong %lu\n", "",
        (unsigned long)vs.vs_nch_badhits,
        (unsigned long)vs.vs_nch_falsehits,
        (unsigned long)vs.vs_nch_long);
}

void
doforkst(void)
{
    struct kinfo_vmstat vs;

    fetchvmstat(&vs);
    if (vs.vs_cntfork != 0)
        printf("%lu forks, %lu kbytes, average=%.2f\n",
            (unsigned long)vs.vs_cntfork, (unsigned long)vs.vs_sizfork,
            (float) vs.vs_sizfork / vs.vs_cntfork);
    if (vs.vs_cntvfork != 0)
        printf("%lu vforks, %lu kbytes, average=%.2f\n",
            (unsigned long)vs.vs_cntvfork, (unsigned long)vs.vs_sizvfork,
            (float) vs.vs_sizvfork / vs.vs_cntvfork);
}

void
stats(int dn)
{
    if (dn >= dk_ndrive) {
        printf("   0");
        return;
    }
    printf("%4.0f", s.xfer[dn]/etime);
}

double
stat1(int row)
{
    double t;
    int i;

    t = 0;
    for(i=0; i<CPUSTATES; i++)
        t += s.time[i];
    if(t == 0.)
        t = 1.;
    return(s.time[row]*100./t);
}

void
dointr(long nintv)
{
    (void)nintv;
    printf("Device interrupt statistics are not applicable to 2.11BSD\n");
}
