/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/systm.h>

void
brk(void)
{
    struct a {
        int naddr;
    };
    register int newsize, d;

    /* set newsize to new data size */
    newsize = ((struct a*)u.u_arg)->naddr - u.u_procp->p_daddr;
    if (newsize < 0)
        newsize = 0;
    if (u.u_tsize + newsize + u.u_ssize > MAXMEM) {
        u.u_error = ENOMEM;
        return;
    }
    /*
     * The stack has grown down to p_saddr and grows further as the
     * process runs; data that reaches it is data the stack will
     * overwrite. u_ssize alone does not see that: it is the stack's
     * size so far, not where it ends.
     */
    if (u.u_procp->p_daddr + newsize > u.u_procp->p_saddr) {
        u.u_error = ENOMEM;
        return;
    }
    /*
     * RLIMIT_DATA bounds the bytes above the executable text when the
     * break rises; a process over a lowered limit can still shrink.
     */
    if ((size_t)newsize > u.u_dsize) {
        size_t text = u.u_procp->p_tsize;

        if (!rlimit_data_admits((size_t)newsize > text ?
            (size_t)newsize - text : 0)) {
            u.u_error = ENOMEM;
            return;
        }
    }

    u.u_procp->p_dsize = newsize;

    /* set d to (new - old) */
    d = newsize - u.u_dsize;
//printf ("brk: new size %u bytes, incremented by %d\n", newsize, d);
    if (d > 0)
        bzero ((void*) (u.u_procp->p_daddr + u.u_dsize), d);
    u.u_dsize = newsize;
    u.u_rval = u.u_procp->p_daddr + u.u_dsize;
}
