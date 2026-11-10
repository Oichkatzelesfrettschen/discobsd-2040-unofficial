/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/stdint.h>
#include <machine/limits.h>

static void
setthetime (register struct timeval *tv)
{
    int s;

    if (! suser())
        return;
#ifdef  NOTNOW
/*
 * If the system is secure, we do not allow the time to be set to an
 * earlier value.  The time may be slowed (using adjtime) but not set back.
 *
 * NOTE:  Can not do this until ntpd is updated to deal with the coarse (50, 60
 *    hz) clocks.  Ntpd wants to adjust time system clock a few microseconds
 *    at a time (which gets rounded to 0 in adjtime below). If that fails
 *    ntpd uses settimeofday to step the time backwards which obviously
 *    will fail if the next 'if' is enabled - all that does is fill up the
 *    logfiles with "can't set time" messages and the time keeps drifting.
*/
    if (securelevel > 0 && timercmp(tv, &time, <)) {
        u.u_error = EPERM;  /* XXX */
        return;
    }
#endif
/* WHAT DO WE DO ABOUT PENDING REAL-TIME TIMEOUTS??? */
    boottime.tv_sec += tv->tv_sec - time.tv_sec;
    s = splhigh();
    time = *tv;
    lbolt = time.tv_usec / usechz;
    splx(s);
#ifdef  notyet
    /*
     * if you have a time of day board, use it here
     */
    resettodr();
#endif
}

/*
 * Time of day and interval timer support.
 *
 * These routines provide the kernel entry points to get and set
 * the time-of-day.
 */
void
gettimeofday()
{
    register struct a {
        struct  timeval *tp;
        struct  timezone *tzp;
    } *uap = (struct a *)u.u_arg;
    struct timeval atv;
    int s;
    register u_int  ms;

    if (uap->tp) {
        /*
         * We don't resolve the milliseconds on every clock tick; it's
         * easier to do it here.  Long casts are out of paranoia.
         */
        s = splhigh();
        atv = time;
        ms = lbolt;
        splx(s);
        atv.tv_usec = (long)ms * usechz;
        u.u_error = copyout ((caddr_t) &atv, (caddr_t) uap->tp,
            sizeof(atv));
        if (u.u_error)
            return;
    }
    if (uap->tzp)
        u.u_error = copyout ((caddr_t) &tz, (caddr_t) uap->tzp,
            sizeof (tz));
}

void
settimeofday()
{
    register struct a {
        struct  timeval *tv;
        struct  timezone *tzp;
    } *uap = (struct a *)u.u_arg;
    struct timeval atv;
    struct timezone atz;

    if (uap->tv) {
        u.u_error = copyin((caddr_t)uap->tv, (caddr_t)&atv,
            sizeof (struct timeval));
        if (u.u_error)
            return;
        setthetime(&atv);
        if (u.u_error)
            return;
    }
    if (uap->tzp && suser()) {
        u.u_error = copyin((caddr_t)uap->tzp, (caddr_t)&atz,
            sizeof (atz));
        if (u.u_error == 0)
            tz = atz;
    }
}

void
adjtime()
{
    register struct a {
        struct timeval *delta;
        struct timeval *olddelta;
    } *uap = (struct a *)u.u_arg;
    struct timeval atv;
    register int s;
    int64_t seconds;
    long whole;
    int adjust, ticks, slew;

    if (!suser())
        return;
    u.u_error = copyin((caddr_t)uap->delta, (caddr_t)&atv,
        sizeof (struct timeval));
    if (u.u_error)
        return;
    /*
     * The delta is seconds whole seconds and ticks ticks, |ticks| < hz.
     * Splitting the microseconds at one second first keeps every product
     * and quotient in 32 bits for any hz up to 0x7fff, so the kernel links
     * no 64-bit multiply or divide for this call; the sums of seconds alone
     * are 64 bits wide.
     */
    whole = atv.tv_usec / 1000000L;
    ticks = (int)((atv.tv_usec % 1000000L) / usechz);
    seconds = (int64_t)atv.tv_sec + whole;
    adjust = 0;
    slew = seconds >= -0x8001 && seconds <= 0x8001;
    if (slew) {
        adjust = (int)seconds * hz + ticks;
        slew = adjust <= 0x7fff && adjust >= -0x8000;
    }
    /* Large tick corrections step the clock; smaller corrections slew. */
    if (!slew) {
        s = splclock();
        seconds += time.tv_sec;
        ticks += lbolt;
        if (ticks < 0) {
            ticks += hz;
            seconds--;
        } else if (ticks >= hz) {
            ticks -= hz;
            seconds++;
        }
        if (seconds < LONG_MIN || seconds > LONG_MAX) {
            splx(s);
            u.u_error = EINVAL;
            return;
        }
        time.tv_sec = seconds;
        lbolt = ticks;
        time.tv_usec = ticks * usechz;
        adjdelta = 0;
        splx(s);
        if (!uap->olddelta)
            return;
        atv.tv_sec = atv.tv_usec = 0;
    } else {
        if (!uap->olddelta) {
            adjdelta = adjust;
            return;
        }
        atv.tv_sec = adjdelta / hz;
        atv.tv_usec = (adjdelta % hz) * usechz;
        adjdelta = adjust;
    }
    u.u_error = copyout ((caddr_t) &atv, (caddr_t) uap->olddelta,
        sizeof (struct timeval));
}

/*
 * Round tv up to whole ticks.  Returns the seconds and stores the ticks
 * past them, below hz, in *ticks.
 */
static long
tvticks(struct timeval *tv, int *ticks)
{
    long sec = tv->tv_sec;
    int t = (tv->tv_usec + usechz - 1) / usechz;

    if (t >= hz) {
        t -= hz;
        sec++;
    }
    *ticks = t;
    return sec;
}

void
getitimer()
{
    register struct a {
        u_int   which;
        struct  itimerval *itv;
    } *uap = (struct a *)u.u_arg;
    struct itimerval aitv;
    register int s;

    if (uap->which > ITIMER_PROF) {
        u.u_error = EINVAL;
        return;
    }
    s = splclock();
    if (uap->which == ITIMER_REAL) {
        register struct proc *p = u.u_procp;
        long sec = 0;
        int ticks = 0;

        aitv.it_interval.tv_sec = p->p_realtimer.it_interval;
        aitv.it_interval.tv_usec = p->p_realitick * usechz;
        if ((p->p_realtimer.it_value | p->p_realtick) != 0) {
            sec = p->p_realtimer.it_value - monosec;
            ticks = p->p_realtick - monotick;
            if (ticks < 0) {
                ticks += hz;
                sec--;
            }
            /* Due, with the expiry scan still pending: one tick left. */
            if (sec < 0 || (sec == 0 && ticks == 0)) {
                sec = 0;
                ticks = 1;
            }
        }
        aitv.it_value.tv_sec = sec;
        aitv.it_value.tv_usec = ticks * usechz;
    } else {
        register struct k_itimerval *t = &u.u_timer[uap->which - 1];

        aitv.it_interval.tv_sec = t->it_interval / hz;
        aitv.it_interval.tv_usec = (t->it_interval % hz) * usechz;
        aitv.it_value.tv_sec = t->it_value / hz;
        aitv.it_value.tv_usec = (t->it_value % hz) * usechz;
    }
    splx(s);
    u.u_error = copyout ((caddr_t)&aitv, (caddr_t)uap->itv,
        sizeof (struct itimerval));
}

void
setitimer()
{
    register struct a {
        u_int   which;
        struct  itimerval *itv, *oitv;
    } *uap = (struct a *)u.u_arg;
    struct itimerval aitv;
    register struct itimerval *aitvp;
    register struct timeval *tv;
    register int n;
    long tick_limit, vsec, isec;
    int vticks, iticks;
    int s;

    if (uap->which > ITIMER_PROF) {
        u.u_error = EINVAL;
        return;
    }
    aitvp = uap->itv;
    if (uap->oitv) {
        uap->itv = uap->oitv;
        getitimer();
        if (u.u_error)
            return;
    }
    if (aitvp == 0)
        return;
    u.u_error = copyin((caddr_t)aitvp, (caddr_t)&aitv,
        sizeof (struct itimerval));
    if (u.u_error)
        return;
    tick_limit = LONG_MAX;
    if (uap->which != ITIMER_REAL)
        tick_limit /= hz;
    tv = &aitv.it_interval;
    n = 2;
    do {
        if (itimerfix(tv) || tv->tv_sec > tick_limit ||
            (tv->tv_usec && tv->tv_sec == tick_limit))
            goto bad;
        tv = &aitv.it_value;
    } while (--n);
    vsec = tvticks(&aitv.it_value, &vticks);
    isec = tvticks(&aitv.it_interval, &iticks);
    s = splclock();
    if (uap->which == ITIMER_REAL) {
        register struct proc *p = u.u_procp;

        p->p_realtimer.it_interval = isec;
        p->p_realitick = iticks;
        if ((vsec | vticks) == 0) {
            p->p_realtimer.it_value = 0;
            p->p_realtick = 0;
        } else {
            vticks += monotick;
            vsec += monosec;
            if (vticks >= hz) {
                vticks -= hz;
                vsec++;
            }
            p->p_realtimer.it_value = vsec;
            p->p_realtick = vticks;
            if (vsec < realsec || (vsec == realsec && vticks < realtick)) {
                realsec = vsec;
                realtick = vticks;
            }
        }
    } else {
        register struct k_itimerval *t = &u.u_timer[uap->which - 1];

        t->it_value = vsec * hz + vticks;
        t->it_interval = isec * hz + iticks;
    }
    splx(s);
    return;
bad:
    u.u_error = EINVAL;
}

/*
 * Check that a proposed value to load into the .it_value or
 * .it_interval part of an interval timer is acceptable.  A caller that
 * counts ticks rounds a nonzero value up to one tick (see tvticks()).
 */
int
itimerfix(struct timeval *tv)
{
    if (tv->tv_sec < 0 || tv->tv_sec > 100000000L ||
        tv->tv_usec < 0 || tv->tv_usec >= 1000000L)
        return (EINVAL);
    return (0);
}

#ifdef NOT_CURRENTLY_IN_USE
/*
 * Decrement an interval timer by a specified number
 * of microseconds, which must be less than a second,
 * i.e. < 1000000.  If the timer expires, then reload
 * it.  In this case, carry over (usec - old value) to
 * reducint the value reloaded into the timer so that
 * the timer does not drift.  This routine assumes
 * that it is called in a context where the timers
 * on which it is operating cannot change in value.
 */
itimerdecr(itp, usec)
    register struct itimerval *itp;
    int usec;
{

    if (itp->it_value.tv_usec < usec) {
        if (itp->it_value.tv_sec == 0) {
            /* expired, and already in next interval */
            usec -= itp->it_value.tv_usec;
            goto expire;
        }
        itp->it_value.tv_usec += 1000000L;
        itp->it_value.tv_sec--;
    }
    itp->it_value.tv_usec -= usec;
    usec = 0;
    if (timerisset(&itp->it_value))
        return (1);
    /* expired, exactly at end of interval */
expire:
    if (timerisset(&itp->it_interval)) {
        itp->it_value = itp->it_interval;
        itp->it_value.tv_usec -= usec;
        if (itp->it_value.tv_usec < 0) {
            itp->it_value.tv_usec += 1000000L;
            itp->it_value.tv_sec--;
        }
    } else
        itp->it_value.tv_usec = 0;      /* sec is already 0 */
    return (0);
}
#endif /* NOT_CURRENTLY_IN_USE */

static void
tvfix(struct timeval *t1)
{
    if (t1->tv_usec < 0) {
        t1->tv_sec--;
        t1->tv_usec += 1000000L;
    }
    if (t1->tv_usec >= 1000000L) {
        t1->tv_sec++;
        t1->tv_usec -= 1000000L;
    }
}

/*
 * Add and subtract routines for timevals.
 * N.B.: subtract routine doesn't deal with
 * results which are before the beginning,
 * it just gets very confused in this case.
 * Caveat emptor.
 */
void
timevaladd(struct timeval *t1, struct timeval *t2)
{
    t1->tv_sec += t2->tv_sec;
    t1->tv_usec += t2->tv_usec;
    tvfix(t1);
}

#ifdef NOT_CURRENTLY_IN_USE
void
timevalsub(t1, t2)
    struct timeval *t1, *t2;
{
    t1->tv_sec -= t2->tv_sec;
    t1->tv_usec -= t2->tv_usec;
    tvfix(t1);
}
#endif
