/*
 * Production interval timers: ITIMER_REAL counted in clock ticks against the
 * monotonic clock, and ITIMER_VIRTUAL and ITIMER_PROF stored in ticks.
 *
 * The gate links sys/kern/kern_clock.c, kern_time.c and kern_fork.c, calls
 * hardclock() once per tick over a small allproc, and records every
 * psignal() with the tick it arrived on. The tick count of a signal is
 * monosec * hz + monotick after the hardclock() call that delivered it, and a
 * setitimer() call lands inside the tick that count names, at an unknown
 * fraction of it. A wait of N ticks is therefore long enough only when the
 * signal arrives at least N + 1 ticks after the call.
 */
#include "hostkern.h"
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/inode.h>
#include <sys/vm.h>

struct user u;
int hz = 1000, usechz = 1000;
struct timezone tz;
struct proc proc[NPROC];
struct proc *allproc, *freeproc, *zombproc, *qs;
struct proc *pidhash[PIDHSZ];
struct forkstat forkstat;
struct inode root_inode;

#define NSIG_LOG 1024
static struct {
    struct proc *proc;
    int signal;
    long tick;
} sigs[NSIG_LOG];
static unsigned nsigs;

static long
ticknow(void)
{
    return monosec * hz + monotick;
}

int suser(void) { return 1; }
int copyin(const caddr_t src, caddr_t dst, u_int n)
{ bcopy(src, dst, n); return 0; }
int copyout(const caddr_t src, caddr_t dst, u_int n)
{ bcopy(src, dst, n); return 0; }
void psignal(struct proc *p, int signal)
{
    if (nsigs < NSIG_LOG) {
        sigs[nsigs].proc = p;
        sigs[nsigs].signal = signal;
        sigs[nsigs].tick = ticknow();
    }
    nsigs++;
}
int setpri(struct proc *p) { (void)p; return 0; }
void addupc(caddr_t pc, struct uprof *prof, int ticks)
{ (void)pc; (void)prof; (void)ticks; }
void swapout(struct proc *p, int freecore, size_t odata, size_t ostack)
{ (void)p; (void)freecore; (void)odata; (void)ostack; }
/* The vfork branch of newproc() is outside this gate. */
void wakeup(caddr_t chan) { (void)chan; HK_CHECK(0); }
void sleep(caddr_t chan, int priority) { (void)chan; (void)priority; HK_CHECK(0); }
int hk_fork_setjmp(label_t *env) { (void)env; return 0; }

void gate_settimeofday(void);

/* The system call argument shapes that setitimer() and getitimer() read. */
struct set_args { u_int which; struct itimerval *value, *old; };
struct get_args { u_int which; struct itimerval *value; };

static void
reset(void)
{
    unsigned i;

    bzero(&u, sizeof u);
    bzero(proc, sizeof proc);
    for (i = 0; i < 4; i++) {
        proc[i].p_stat = SRUN;
        proc[i].p_pid = 100 + i;
        proc[i].p_nxt = i < 3 ? &proc[i + 1] : NULL;
    }
    allproc = &proc[0];
    noproc = 1;
    u.u_procp = &proc[0];
    nsigs = 0;
    monosec = 0;
    monotick = 0;
    realsec = LONG_MAX;
    realtick = 0;
    hk_ipl = 0;
}

static void
tick(unsigned n)
{
    while (n--)
        hardclock(0, 1);
}

/* setitimer() for p, in milliseconds. */
static int
set_timer(struct proc *p, u_int which, long value_ms, long interval_ms)
{
    struct set_args *args = (void *)u.u_arg;
    struct itimerval itv;

    itv.it_value.tv_sec = value_ms / 1000;
    itv.it_value.tv_usec = value_ms % 1000 * 1000;
    itv.it_interval.tv_sec = interval_ms / 1000;
    itv.it_interval.tv_usec = interval_ms % 1000 * 1000;
    u.u_procp = p;
    args->which = which;
    args->value = &itv;
    args->old = NULL;
    u.u_error = 0;
    setitimer();
    u.u_procp = &proc[0];
    return u.u_error;
}

static int
set_timeval(struct proc *p, u_int which, struct itimerval *itv)
{
    struct set_args *args = (void *)u.u_arg;

    u.u_procp = p;
    args->which = which;
    args->value = itv;
    args->old = NULL;
    u.u_error = 0;
    setitimer();
    u.u_procp = &proc[0];
    return u.u_error;
}

static struct itimerval
get_timer(struct proc *p, u_int which)
{
    struct get_args *args = (void *)u.u_arg;
    struct itimerval itv;

    bzero(&itv, sizeof itv);
    u.u_procp = p;
    args->which = which;
    args->value = &itv;
    u.u_error = 0;
    getitimer();
    u.u_procp = &proc[0];
    HK_CHECK(u.u_error == 0);
    return itv;
}

/* A remaining value in microseconds. */
static long
usec_of(struct timeval tv)
{
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

static unsigned
signals_for(struct proc *p)
{
    unsigned i, n = 0;

    for (i = 0; i < nsigs && i < NSIG_LOG; i++)
        if (sigs[i].proc == p && sigs[i].signal == SIGALRM)
            n++;
    return n;
}

static int
armed(struct proc *p)
{
    return (p->p_realtimer.it_value | p->p_realtick) != 0;
}

/*
 * A one-shot timer signals once, on the tick the model predicts, from every
 * phase of the second, and a shorter wait than asked never occurs.
 */
static void
one_shot(void)
{
    static const long waits_ms[] = { 1, 10, 500, 999, 1000, 2500 };
    static const int phases[] = { 0, 1, 500, 998, 999 };
    unsigned w, f;

    for (w = 0; w < sizeof waits_ms / sizeof waits_ms[0]; w++)
        for (f = 0; f < sizeof phases / sizeof phases[0]; f++) {
            long start, wait = waits_ms[w];
            unsigned raises;

            reset();
            monosec = 7;
            monotick = phases[f];
            start = ticknow();
            HK_CHECK(set_timer(&proc[1], ITIMER_REAL, wait, 0) == 0);
            HK_CHECK(armed(&proc[1]));
            /*
             * Only the signalling tick enters softclock (one splhigh) and
             * the expiry scan (one splclock); an earlier scan would add more.
             */
            raises = hk_ipl_raises;
            tick(wait + 1);
            HK_CHECK(hk_ipl_raises - raises == 2);
            HK_CHECK(signals_for(&proc[1]) == 1);
            HK_CHECK(nsigs == 1);
            if (nsigs >= 1) {
                HK_CHECK(sigs[0].proc == &proc[1]);
                HK_CHECK(sigs[0].tick == start + wait + 1);
                HK_CHECK(sigs[0].tick - start >= wait + 1);
            }
            HK_CHECK(!armed(&proc[1]));
            HK_CHECK(proc[1].p_realtimer.it_value == 0 &&
                proc[1].p_realtick == 0);
            tick(3000);
            HK_CHECK(nsigs == 1);
            HK_CHECK(realsec == LONG_MAX);
            HK_CHECK(hk_ipl == 0);
        }
}

/* Periodic timers keep their phase: no drift over many periods. */
static void
periodic(void)
{
    unsigned i;
    long start;

    /* First signal on the first tick past the deadline, 11 ticks out. */
    reset();
    monosec = 3;
    monotick = 640;
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 10, 10) == 0);
    tick(1000);
    HK_CHECK(nsigs == 99);
    for (i = 0; i < nsigs && i < 99; i++)
        HK_CHECK(sigs[i].tick == start + 11 + 10 * (long)i);
    tick(10);
    HK_CHECK(nsigs == 100 && sigs[99].tick == start + 11 + 990);

    /* A 1 ms first period leaves 100 signals inside the first 1000 ticks. */
    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 1, 10) == 0);
    tick(1000);
    HK_CHECK(nsigs == 100);
    for (i = 0; i < nsigs && i < 100; i++)
        HK_CHECK(sigs[i].tick == start + 2 + 10 * (long)i);
    HK_CHECK(armed(&proc[0]));

    /* An interval of 1 s and a fraction crosses the second boundary. */
    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 5, 1500) == 0);
    tick(6 + 1500 * 3);
    HK_CHECK(nsigs == 4);
    for (i = 0; i < nsigs && i < 4; i++)
        HK_CHECK(sigs[i].tick == start + 6 + 1500 * (long)i);
}

/* getitimer() reports the remaining time down to one tick. */
static void
readback(void)
{
    struct itimerval itv;
    long left;
    unsigned i;

    reset();
    monosec = 9;
    monotick = 998;
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 500, 250) == 0);
    itv = get_timer(&proc[0], ITIMER_REAL);
    HK_CHECK(usec_of(itv.it_value) == 500000);
    HK_CHECK(itv.it_interval.tv_sec == 0 &&
        itv.it_interval.tv_usec == 250000);
    for (i = 1; i <= 499; i++) {
        tick(1);
        itv = get_timer(&proc[0], ITIMER_REAL);
        left = usec_of(itv.it_value);
        HK_CHECK(left == (500 - (long)i) * 1000);
    }
    /* On the deadline tick the signal is one tick away, not zero. */
    tick(1);
    HK_CHECK(nsigs == 0);
    itv = get_timer(&proc[0], ITIMER_REAL);
    HK_CHECK(usec_of(itv.it_value) == 1000);
    tick(1);
    HK_CHECK(nsigs == 1);
    itv = get_timer(&proc[0], ITIMER_REAL);
    /* Periodic: the next deadline is 250 ms past the last one. */
    HK_CHECK(usec_of(itv.it_value) == 249000);

    /* Due with the scan still pending reads as one tick, not zero. */
    reset();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 20, 0) == 0);
    monoclock_advance(5000);
    itv = get_timer(&proc[0], ITIMER_REAL);
    HK_CHECK(usec_of(itv.it_value) == 1000);
    HK_CHECK(nsigs == 0);

    /* A timer that was never set, and one that was cancelled, read zero. */
    reset();
    itv = get_timer(&proc[0], ITIMER_REAL);
    HK_CHECK(usec_of(itv.it_value) == 0 && usec_of(itv.it_interval) == 0);
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 400, 30) == 0);
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 0, 30) == 0);
    itv = get_timer(&proc[0], ITIMER_REAL);
    HK_CHECK(usec_of(itv.it_value) == 0 && usec_of(itv.it_interval) == 30000);
    HK_CHECK(!armed(&proc[0]));
}

/* Re-arming replaces the deadline, earlier or later. */
static void
rearm(void)
{
    long start;

    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 500, 0) == 0);
    tick(20);
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 100, 0) == 0);
    tick(1000);
    HK_CHECK(nsigs == 1 && sigs[0].tick == start + 20 + 101);

    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 100, 0) == 0);
    tick(50);
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 400, 0) == 0);
    tick(1000);
    HK_CHECK(nsigs == 1 && sigs[0].tick == start + 50 + 401);

    /* A timer armed after the earliest scan was computed still fires. */
    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 400, 0) == 0);
    tick(401);
    HK_CHECK(nsigs == 1 && realsec == LONG_MAX);
    start = ticknow();
    HK_CHECK(set_timer(&proc[1], ITIMER_REAL, 30, 0) == 0);
    tick(100);
    HK_CHECK(nsigs == 2 && sigs[1].proc == &proc[1] &&
        sigs[1].tick == start + 31);
}

/* Cancelling the earliest timer leaves the next one on time. */
static void
cancel(void)
{
    long start;

    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 100, 0) == 0);
    HK_CHECK(set_timer(&proc[1], ITIMER_REAL, 300, 0) == 0);
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 0, 0) == 0);
    HK_CHECK(!armed(&proc[0]));
    tick(150);
    HK_CHECK(nsigs == 0);
    /* The stale earliest value cost one scan and was recomputed. */
    HK_CHECK(realsec == 0 && realtick == 300);
    tick(400);
    HK_CHECK(nsigs == 1 && sigs[0].proc == &proc[1] &&
        sigs[0].tick == start + 301);
}

/* Several processes expiring on one tick all receive SIGALRM. */
static void
simultaneous(void)
{
    long start;

    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 50, 0) == 0);
    HK_CHECK(set_timer(&proc[2], ITIMER_REAL, 50, 0) == 0);
    tick(1);
    /* A 49 ms wait one tick later reaches the same deadline. */
    HK_CHECK(set_timer(&proc[3], ITIMER_REAL, 49, 0) == 0);
    tick(100);
    HK_CHECK(nsigs == 3);
    HK_CHECK(signals_for(&proc[0]) == 1 && signals_for(&proc[2]) == 1 &&
        signals_for(&proc[3]) == 1);
    HK_CHECK(sigs[0].tick == start + 51 && sigs[1].tick == start + 51 &&
        sigs[2].tick == start + 51);
    HK_CHECK(signals_for(&proc[1]) == 0);
}

/*
 * A scan another process's deadline triggers does not fire a timer whose
 * deadline is the current tick: that one is still owed a full tick.
 */
static void
neighbor_deadline(void)
{
    long start;

    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 50, 0) == 0);
    HK_CHECK(set_timer(&proc[1], ITIMER_REAL, 51, 0) == 0);
    tick(100);
    HK_CHECK(nsigs == 2);
    HK_CHECK(nsigs == 2 && sigs[0].proc == &proc[0] &&
        sigs[0].tick == start + 51);
    HK_CHECK(nsigs == 2 && sigs[1].proc == &proc[1] &&
        sigs[1].tick == start + 52);
}

/* A child starts with all four fields clear and gets no alarm. */
static void
fork_clears(void)
{
    struct proc *child;

    reset();
    freeproc = NULL;
    allproc = &proc[0];
    proc[0].p_nxt = NULL;
    proc[3].p_nxt = NULL;
    freeproc = &proc[3];
    proc[3].p_realtimer.it_value = 99;
    proc[3].p_realtimer.it_interval = 98;
    proc[3].p_realtick = 97;
    proc[3].p_realitick = 96;
    bzero(&root_inode, sizeof root_inode);
    u.u_cdir = &root_inode;
    u.u_lastfile = -1;
    u.u_procp = &proc[0];
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 200, 40) == 0);
    child = freeproc;
    HK_CHECK(newproc(0) == 0);
    HK_CHECK(child == &proc[3] && allproc == child);
    HK_CHECK(child->p_realtimer.it_value == 0);
    HK_CHECK(child->p_realtimer.it_interval == 0);
    HK_CHECK(child->p_realtick == 0);
    HK_CHECK(child->p_realitick == 0);
    HK_CHECK(armed(&proc[0]) && proc[0].p_realtimer.it_interval == 0 &&
        proc[0].p_realitick == 40);
    tick(500);
    HK_CHECK(signals_for(child) == 0);
    HK_CHECK(signals_for(&proc[0]) > 0);
}

/* settimeofday(2) and adjtime(2) leave the deadlines where they are. */
static void
time_steps(void)
{
    struct { struct timeval *delta, *old; } *adjust = (void *)u.u_arg;
    struct timeval tv, delta;
    long start, sec_before, deadline_sec;
    int tick_before, deadline_tick;
    static const long steps[] = { -1000000, 3, 1000000 };
    unsigned i, j;

    reset();
    time.tv_sec = 5000000;
    time.tv_usec = 0;
    lbolt = 0;
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 300, 0) == 0);
    deadline_sec = proc[0].p_realtimer.it_value;
    deadline_tick = proc[0].p_realtick;
    tick(100);
    for (i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        struct { struct timeval *tv; struct timezone *tzp; } *sargs =
            (void *)u.u_arg;

        tv.tv_sec = 5000000 + steps[i];
        tv.tv_usec = 123000;
        sargs->tv = &tv;
        sargs->tzp = NULL;
        sec_before = monosec;
        tick_before = monotick;
        u.u_error = 0;
        gate_settimeofday();
        HK_CHECK(u.u_error == 0);
        HK_CHECK(time.tv_sec == tv.tv_sec);
        HK_CHECK(monosec == sec_before && monotick == tick_before);
        HK_CHECK(proc[0].p_realtimer.it_value == deadline_sec &&
            proc[0].p_realtick == deadline_tick);

        /* A step (200 s is past the slew range), then a 40 ms slew. */
        for (j = 0; j < 2; j++) {
            delta.tv_sec = j == 0 ? (steps[i] > 0 ? 200 : -200) : 0;
            delta.tv_usec = j == 0 ? 0 : 40000;
            adjust->delta = &delta;
            adjust->old = NULL;
            u.u_error = 0;
            adjtime();
            HK_CHECK(u.u_error == 0);
            HK_CHECK(monosec == sec_before && monotick == tick_before);
            HK_CHECK(proc[0].p_realtimer.it_value == deadline_sec &&
                proc[0].p_realtick == deadline_tick);
        }
    }
    tick(100);
    HK_CHECK(nsigs == 0);
    tick(101);
    HK_CHECK(nsigs == 1 && sigs[0].tick == start + 301);
}

/* Ticks the clock interrupt missed fire on the next tick. */
static void
catch_up(void)
{
    long start;
    unsigned i;

    reset();
    start = ticknow();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 100, 0) == 0);
    tick(10);
    monoclock_advance(5000);
    HK_CHECK(monosec == 5 && monotick == 10);
    HK_CHECK(nsigs == 0);
    tick(1);
    HK_CHECK(nsigs == 1 && sigs[0].tick == start + 5011);
    HK_CHECK(!armed(&proc[0]));

    /*
     * A periodic timer keeps its phase: the periods missed in the window
     * come one per tick until the deadline (10 + 10 j for period j) falls
     * behind the clock again, then they are 10 ticks apart.
     */
    reset();
    HK_CHECK(set_timer(&proc[0], ITIMER_REAL, 10, 10) == 0);
    monoclock_advance(5000);
    tick(1000);
    HK_CHECK(nsigs == 599);
    for (i = 0; i < 555; i++)
        HK_CHECK(sigs[i].tick == 5001 + (long)i);
    HK_CHECK(sigs[555].tick == 5561);
    for (i = 556; i < nsigs; i++)
        HK_CHECK(sigs[i].tick == sigs[i - 1].tick + 10);
    HK_CHECK(sigs[598].tick == 5991);

    /* Carry from the tick counter into seconds. */
    reset();
    monotick = 999;
    monoclock_advance(1);
    HK_CHECK(monosec == 1 && monotick == 0);
    monoclock_advance(2999);
    HK_CHECK(monosec == 3 && monotick == 999);
    monoclock_advance(1);
    HK_CHECK(monosec == 4 && monotick == 0);
    tick(1000);
    HK_CHECK(monosec == 5 && monotick == 0);
}

/* The largest accepted interval and value survive a round trip. */
static void
limits(void)
{
    struct itimerval itv, back;

    reset();
    monosec = 12345;
    monotick = 321;
    bzero(&itv, sizeof itv);
    itv.it_value.tv_sec = 100000000L;
    itv.it_interval.tv_sec = 100000000L;
    HK_CHECK(set_timeval(&proc[0], ITIMER_REAL, &itv) == 0);
    back = get_timer(&proc[0], ITIMER_REAL);
    HK_CHECK(back.it_interval.tv_sec == 100000000L &&
        back.it_interval.tv_usec == 0);
    HK_CHECK(usec_of(back.it_value) == 100000000L * 1000000L);

    itv.it_value.tv_sec = 0;
    itv.it_value.tv_usec = 999999;
    itv.it_interval.tv_sec = 100000000L;
    itv.it_interval.tv_usec = 999999;
    HK_CHECK(set_timeval(&proc[0], ITIMER_REAL, &itv) == 0);
    back = get_timer(&proc[0], ITIMER_REAL);
    /* 999999 us rounds up to a whole second and carries. */
    HK_CHECK(back.it_interval.tv_sec == 100000001L &&
        back.it_interval.tv_usec == 0);
    HK_CHECK(usec_of(back.it_value) == 1000000);

    itv.it_interval.tv_sec = 100000001L;
    itv.it_interval.tv_usec = 0;
    HK_CHECK(set_timeval(&proc[0], ITIMER_REAL, &itv) == EINVAL);
    itv.it_interval.tv_sec = 0;
    itv.it_value.tv_usec = 1000000;
    HK_CHECK(set_timeval(&proc[0], ITIMER_REAL, &itv) == EINVAL);
}

/* VIRTUAL and PROF hold sec * hz + ceil(usec / usechz) and read back exact. */
static void
virtual_prof(void)
{
    static const struct { long sec, usec; long ticks; } cases[] = {
        { 0, 1, 1 }, { 0, 999, 1 }, { 0, 1000, 1 }, { 0, 1001, 2 },
        { 0, 999999, 1000 }, { 1, 0, 1000 }, { 1, 1, 1001 },
        { 3, 500000, 3500 }, { 2000, 999000, 2000999 },
    };
    unsigned i, which;

    for (which = ITIMER_VIRTUAL; which <= ITIMER_PROF; which++)
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            struct itimerval itv, back;
            struct k_itimerval *t;

            reset();
            bzero(&itv, sizeof itv);
            itv.it_value.tv_sec = cases[i].sec;
            itv.it_value.tv_usec = cases[i].usec;
            itv.it_interval.tv_sec = cases[i].sec;
            itv.it_interval.tv_usec = cases[i].usec;
            HK_CHECK(set_timeval(&proc[0], which, &itv) == 0);
            t = &u.u_timer[which - 1];
            HK_CHECK(t->it_value == cases[i].ticks);
            HK_CHECK(t->it_interval == cases[i].ticks);
            back = get_timer(&proc[0], which);
            if (cases[i].usec % 1000 == 0) {
                HK_CHECK(back.it_value.tv_sec == cases[i].sec &&
                    back.it_value.tv_usec == cases[i].usec);
                HK_CHECK(back.it_interval.tv_sec == cases[i].sec &&
                    back.it_interval.tv_usec == cases[i].usec);
            }
            HK_CHECK(back.it_value.tv_sec == cases[i].ticks / hz);
            HK_CHECK(back.it_value.tv_usec ==
                cases[i].ticks % hz * usechz);
            HK_CHECK(proc[0].p_realtimer.it_value == 0 &&
                !armed(&proc[0]) && realsec == LONG_MAX);
        }
}

int
main(void)
{
    one_shot();
    periodic();
    readback();
    rearm();
    cancel();
    simultaneous();
    neighbor_deadline();
    fork_clears();
    time_steps();
    catch_up();
    limits();
    virtual_prof();
    return hk_verdict("itimer");
}
