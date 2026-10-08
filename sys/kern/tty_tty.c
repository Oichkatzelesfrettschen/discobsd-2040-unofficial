/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *  @(#)tty_tty.c   1.2 (2.11BSD GTE) 11/29/94
 */

/*
 * Indirect driver for controlling tty.
 *
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/systm.h>

/*
 * The entry points match struct cdevsw and the prototypes in sys/systm.h.
 * syopen() receives the open mode like every d_open, and the controlling
 * terminal's driver is opened with mode 0 whatever the caller passed.
 */
int
syopen(dev_t dev __unused, int flag, int mode __unused)
{
    if (u.u_ttyp == NULL)
        return (ENXIO);
    return((*cdevsw[major(u.u_ttyd)].d_open)(u.u_ttyd, flag, 0));
}

int
syread(dev_t dev __unused, struct uio *uio, int flag)
{
    if (u.u_ttyp == NULL)
        return (ENXIO);
    return ((*cdevsw[major(u.u_ttyd)].d_read)(u.u_ttyd, uio, flag));
}

int
sywrite(dev_t dev __unused, struct uio *uio, int flag)
{
    if (u.u_ttyp == NULL)
        return (ENXIO);
    return ((*cdevsw[major(u.u_ttyd)].d_write)(u.u_ttyd, uio, flag));
}

int
syioctl(dev_t dev __unused, u_int cmd, caddr_t addr, int flag)
{
    if (cmd == TIOCNOTTY) {
        u.u_ttyp = 0;
        u.u_ttyd = 0;
        u.u_procp->p_pgrp = 0;
        return (0);
    }
    if (u.u_ttyp == NULL)
        return (ENXIO);
    return ((*cdevsw[major(u.u_ttyd)].d_ioctl)(u.u_ttyd, cmd, addr, flag));
}

int
syselect(dev_t dev __unused, int rw)
{

    if (u.u_ttyp == NULL) {
        u.u_error = ENXIO;
        return (0);
    }
    return ((*cdevsw[major(u.u_ttyd)].d_select)(u.u_ttyd, rw));
}
