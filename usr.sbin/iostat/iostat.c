/*
 * iostat
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/dk.h>
#include <sys/sysctl.h>
#include <sys/time.h>

/*
 * Every statistic comes from a sysctl(3) record, so the program needs no
 * privilege: CTL_VM VM_STATS (cpu ticks and terminal character counts),
 * CTL_HW HW_DISKSTATS (one record per drive a driver claimed) and
 * CTL_KERN KERN_CLOCKRATE.
 */

void    stats(int dn);
void    stat1(int o);
void    printhdr(int sig);
void    fetchstats(struct kinfo_vmstat *vs, struct kinfo_disk *kd, int *nd);

struct kinfo_disk disks[DK_NDRIVE];     /* names, as first fetched */
int dr_select[DK_NDRIVE];
int dk_ndrive;
int ndrives = 0;

struct {
    long    cp_time[CPUSTATES];
    long    dk_bytes[DK_NDRIVE];
    long    dk_xfer[DK_NDRIVE];
    long    tk_nin;
    long    tk_nout;
} s, s1;

int hz;
double  etime;
int tohdr = 1;

void
printhdr(int sig)
{
    int i;

    (void)sig;
    printf("---tty---");
    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            printf(" ---%3.3s--", disks[i].kd_name);
    printf(" ------cpu------\n");

    printf(" tin tout");
    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            printf(" kbps tps");
    printf("  us  ni  sy  id\n");
    tohdr = 19;
}

/*
 * Read the event record and the drive records, or exit; *nd receives the
 * number of drive records, zero when no driver claimed a drive.
 */
void
fetchstats(struct kinfo_vmstat *vs, struct kinfo_disk *kd, int *nd)
{
    int mib[2];
    size_t size;

    mib[0] = CTL_VM;
    mib[1] = VM_STATS;
    size = sizeof *vs;
    if (sysctl(mib, 2, vs, &size, NULL, 0) < 0) {
        fprintf(stderr, "iostat: sysctl vm.stats: %s\n", strerror(errno));
        exit(1);
    }
    if (size != sizeof *vs) {
        fprintf(stderr, "iostat: sysctl vm.stats: record is %u bytes, "
            "expected %u\n", (unsigned)size, (unsigned)sizeof *vs);
        exit(1);
    }
    mib[0] = CTL_HW;
    mib[1] = HW_DISKSTATS;
    size = DK_NDRIVE * sizeof (struct kinfo_disk);
    if (sysctl(mib, 2, kd, &size, NULL, 0) < 0) {
        fprintf(stderr, "iostat: sysctl hw.diskstats: %s\n",
            strerror(errno));
        exit(1);
    }
    *nd = (int)(size / sizeof (struct kinfo_disk));
}

int
main(int argc, char *argv[])
{
    int i, mib[2], nd;
    int iter;
    long t;
    struct kinfo_vmstat vs;
    struct kinfo_disk cur[DK_NDRIVE];
    struct clockinfo clock;
    size_t size;

    iter = 0;
    for (argc--, argv++; argc > 0 && argv[0][0] == '-'; argc--, argv++)
        ;
    mib[0] = CTL_KERN;
    mib[1] = KERN_CLOCKRATE;
    size = sizeof clock;
    if (sysctl(mib, 2, &clock, &size, NULL, 0) < 0) {
        fprintf(stderr, "iostat: sysctl kern.clockrate: %s\n",
            strerror(errno));
        exit(1);
    }
    hz = clock.hz;
    if (hz <= 0) {
        fprintf(stderr, "iostat: clock rate %d\n", hz);
        exit(1);
    }
    fetchstats(&vs, disks, &dk_ndrive);

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
        dr_select[i] = 1;
        ndrives++;
    }
    if (argc > 1)
        iter = atoi(argv[1]);
    signal(SIGCONT, printhdr);
loop:
    if (--tohdr == 0)
        printhdr(0);
    fetchstats(&vs, cur, &nd);
    if (nd > dk_ndrive)
        nd = dk_ndrive;
    for (i = 0; i < nd; i++) {
        s.dk_xfer[i] = cur[i].kd_xfer;
        s.dk_bytes[i] = cur[i].kd_bytes;
    }
    s.tk_nin = vs.vs_tk_nin;
    s.tk_nout = vs.vs_tk_nout;
    for (i = 0; i < CPUSTATES; i++)
        s.cp_time[i] = vs.vs_cp_time[i];

    for (i = 0; i < dk_ndrive; i++) {
        if (! dr_select[i])
            continue;
#define X(fld)  t = s.fld[i]; s.fld[i] -= s1.fld[i]; s1.fld[i] = t
        X(dk_xfer); X(dk_bytes);
    }
    t = s.tk_nin; s.tk_nin -= s1.tk_nin; s1.tk_nin = t;
    t = s.tk_nout; s.tk_nout -= s1.tk_nout; s1.tk_nout = t;
    etime = 0;
    for(i=0; i<CPUSTATES; i++) {
        X(cp_time);
        etime += s.cp_time[i];
    }
    if (etime == 0.0)
        etime = 1.0;
    etime /= (float) hz;
    printf("%4.0f%5.0f", s.tk_nin/etime, s.tk_nout/etime);
    for (i=0; i<dk_ndrive; i++)
        if (dr_select[i])
            stats(i);
    for (i=0; i<CPUSTATES; i++)
        stat1(i);
    printf("\n");
    fflush(stdout);
    if (--iter && argc > 0) {
        sleep(atoi(argv[0]));
        goto loop;
    }
    return 0;
}

void
stats(int dn)
{
    /* number of bytes transferred */
    printf("%5.0f", (double) s.dk_bytes[dn] / 1024 / etime);

    /* number of transfers */
    printf("%4.0f", (double) s.dk_xfer[dn] / etime);
}

void
stat1(int o)
{
    int i;
    double time;

    time = 0;
    for(i=0; i<CPUSTATES; i++)
        time += s.cp_time[i];
    if (time == 0.0)
        time = 1.0;
    printf(" %3.0f", 100.0 * s.cp_time[o] / time);
}
