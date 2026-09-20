/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * CTL_VM identifiers
 */
#define VM_METER    1       /* struct vmmeter */
#define VM_LOADAVG  2       /* struct loadavg */
#define VM_SWAPMAP  3       /* struct mapent _swapmap[] */
                    /* 4 was VM_COREMAP, which needs a core resource map */
#define VM_NSWAP    5       /* int, swap space in DEV_BSIZE blocks */
#define VM_MAXID    6       /* number of valid vm ids */

/*
 * One entry per id below VM_MAXID, retired ids included: sbin/sysctl walks
 * this table to that bound, so a table shorter than the bound is read past
 * its end.
 */
#ifndef KERNEL
#define CTL_VM_NAMES { \
    { 0, 0 }, \
    { "vmmeter", CTLTYPE_STRUCT }, \
    { "loadavg", CTLTYPE_STRUCT }, \
    { "swapmap", CTLTYPE_STRUCT }, \
    { 0, 0 }, \
    { "nswap", CTLTYPE_INT }, \
}
#endif
