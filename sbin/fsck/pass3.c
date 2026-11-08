/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include "fsck.h"

void
pass3()
{
    register DINODE *dp;
    struct inodesc idesc;
    ino_t inumber, orphan, hops;

    bzero((char *)&idesc, sizeof(struct inodesc));
    idesc.id_type = DATA;
    for (inumber = ROOTINO; inumber <= lastino; inumber++) {
        /*
         * Climb ".." from inumber and reconnect the directory where the
         * climb ends: the topmost unreached directory, the directory whose
         * ".." names inumber (the climb has gone round a cycle), or the
         * directory reached after imax steps (a chain that leads into a
         * cycle not holding inumber). A directory other than inumber need
         * not lead to inumber by any entry, and the ascending loop has
         * passed inumber already, so inumber climbs again while it is still
         * DSTATE. Each turn that reconnects a directory moves one inode
         * from DSTATE to DFOUND or DCLEAR, so the turns are finite.
         */
        while (getstate(inumber) == DSTATE) {
            pathp = pathname;
            *pathp++ = '?';
            *pathp = '\0';
            idesc.id_func = findino;
            idesc.id_name = "..";
            idesc.id_parent = inumber;
            hops = 0;
            do {
                orphan = idesc.id_parent;
                if (orphan < ROOTINO || orphan > imax)
                    break;
                dp = ginode(orphan);
                idesc.id_parent = 0;
                idesc.id_number = orphan;
                (void)ckinode(dp, &idesc);
                if (idesc.id_parent == 0)
                    break;
            } while (getstate(idesc.id_parent) == DSTATE &&
                idesc.id_parent != inumber && ++hops <= imax);
            if (linkup(orphan, idesc.id_parent) != 1)
                break;
            idesc.id_func = pass2check;
            idesc.id_number = lfdir;
            descend(&idesc, orphan);
        }
    }
}
