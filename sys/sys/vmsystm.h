/*
 * Miscellaneous virtual memory subsystem variables and structures.
 *
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *  @(#)vmsystm.h   7.2.1 (2.11BSD GTE) 1/15/95
 */

/*
 * Fork/vfork accounting.
 */
struct  forkstat
{
    long    cntfork;
    long    cntvfork;
    long    sizfork;
    long    sizvfork;
};

#if defined(KERNEL) && defined(UCB_METER)
extern size_t freemem;        /* remaining clicks of free memory */

extern u_short avefree;        /* moving average of remaining free clicks */
extern u_short avefree30;      /* 30 sec (avefree is 5 sec) moving average */

/*
 * writable copies of tunables
 */
extern int maxslp;         /* max sleep time before very swappable */

extern struct forkstat forkstat;
#endif

#ifdef KERNEL
struct proc;

struct swap_exchange_state {
    struct proc *owner;
    size_t base;
    size_t span;
};

#define SWAP_IMAGE_EMPTY       0
#define SWAP_IMAGE_RESERVED    1
#define SWAP_IMAGE_COPIED      2
#define SWAP_IMAGE_COMMITTED   3
#define SWAP_IMAGE_FLASH       0
#define SWAP_IMAGE_RAM         1

/* The owner holds backing from reservation through copy and commit or cancel. */
struct swap_image_reservation {
    struct proc *owner;
    size_t addr[3];
    size_t span;
    size_t data_blocks;
    size_t stack_blocks;
    size_t text;
    size_t data;
    size_t stack;
    int tier;
    int state;
};

size_t swap_image_span(struct proc *);
int swap_exchange_switch(struct swap_image_reservation *, struct proc *,
    struct proc *, int);
void swap_exchange_restore(const struct swap_exchange_state *);
int swap_exchange_resize(struct proc *, size_t, size_t, size_t);
void swap_exchange_release(struct proc *);
int swap_image_reserve(struct swap_image_reservation *, struct proc *,
    struct proc *, u_int, u_int);
void swap_image_copy_out(struct swap_image_reservation *, struct proc *);
void swap_image_commit(struct swap_image_reservation *, struct proc *);
void swap_image_cancel(struct swap_image_reservation *);
void swap_image_release(struct proc *);
#endif
