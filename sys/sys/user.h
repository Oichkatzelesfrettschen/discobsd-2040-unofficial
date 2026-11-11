/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifdef KERNEL
#include "dir.h"
#include "exec.h"
#include "time.h"
#include "resource.h"
#else
#include <sys/dir.h>
#include <sys/exec.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

/*
 * Per-process state that need not remain resident while the process is
 * swapped. The user block and proc structure cross-reference each other.
 */
#define MAXCOMLEN   MAXNAMLEN   /* <= MAXNAMLEN, >= sizeof(ac_comm) */

struct user {
    struct  proc *u_procp;          /* pointer to proc structure */
    struct  trapframe *u_frame;     /* address of users saved frame */

/* syscall parameters and results */
    int     u_arg[6];               /* arguments to current system call */
    int     u_rval;                 /* return value */
    int     u_error;                /* return error code */

/* 1.1 - processes and protection */
    uid_t   u_uid;                  /* effective user id */
    uid_t   u_svuid;                /* saved user id */
    uid_t   u_ruid;                 /* real user id */
    gid_t   u_svgid;                /* saved group id */
    gid_t   u_rgid;                 /* real group id */
    gid_t   u_groups[NGROUPS];      /* groups, NOGROUP terminated */

/* 1.2 - memory management */
    size_t  u_tsize;                /* text size (clicks) */
    size_t  u_dsize;                /* data size (clicks) */
    size_t  u_ssize;                /* stack size (clicks) */

/* 1.3 - signal management */
    sig_t   u_signal[NSIG];         /* disposition of signals */
    long    u_sigmask[NSIG];        /* signals to be blocked */
    long    u_sigonstack;           /* signals to take on sigstack */
    long    u_sigintr;              /* signals that interrupt syscalls */
    long    u_oldmask;              /* saved mask from before sigpause */
    int     u_code;                 /* ``code'' to trap */
    int     u_psflags;              /* Process Signal flags */
    struct  sigaltstack u_sigstk;   /* signal stack info */
    u_int   u_sigtramp;             /* pointer to trampoline code in user space */

/* 1.4 - descriptor management */
    struct  file *u_ofile[NOFILE];  /* file structures for open files */
    char    u_pofile[NOFILE];       /* per-process flags of open files */
    int u_lastfile;                 /* high-water mark of u_ofile */
#define UF_EXCLOSE  0x1             /* auto-close on exec */
#define UF_MAPPED   0x2             /* mapped from device */
    struct  inode *u_cdir;          /* current directory */
    struct  inode *u_rdir;          /* root directory of current process */
    struct  tty *u_ttyp;            /* controlling tty pointer */
    dev_t   u_ttyd;                 /* controlling tty dev */
    int     u_cmask;                /* mask for file creation */

/* 1.5 - timing and statistics */
    struct  k_rusage u_ru;          /* stats for this proc */
    struct  k_rusage u_cru;         /* sum of stats for reaped children */
    struct  k_itimerval u_timer[2]; /* profile/virtual timers */
    long    u_start;
    int     u_dupfd;                /* XXX - see kern_descrip.c/fdopen */

    struct uprof {                  /* profile arguments */
        unsigned *pr_base;          /* buffer base */
        unsigned pr_size;           /* buffer size */
        unsigned pr_off;            /* pc offset */
        unsigned pr_scale;          /* pc scaling */
    } u_prof;

/* 1.6 - resource controls */
    struct  rlimit u_rlimit[RLIM_NLIMITS];

/* namei & co. */
    struct  nameicache {            /* last successful directory search */
        off_t nc_prevoffset;        /* offset at which last entry found */
        ino_t nc_inumber;           /* inum of cached directory */
        dev_t nc_dev;               /* dev of cached directory */
    } u_ncache;

/* cold state kept beyond the Thumb-1 word-load immediate window */
    char    u_comm[MAXCOMLEN + 1];  /* command file name */
    label_t u_qsave;                /* for non-local gotos on interrupts */
    label_t u_rsave;                /* save info when exchanging stacks */
    label_t u_ssave;                /* label variable for swapping */
    int     u_stack[1];             /* kernel stack per user
                                     * extends from u + USIZE
                                     * backward not to reach here */
};

#if defined(KERNEL) && defined(RP2040)
/*
 * A Thumb-1 word load reaches 124 bytes from its base register. Keep the
 * fields used by syscall dispatch and credential checks in that window so a
 * reference through &u needs neither a literal address nor an extra add.
 */
#define U_WORD_LOAD_REACH(field) \
    _Static_assert(offsetof(struct user, field) <= 124, \
        #field " exceeds Thumb-1 word-load reach")
U_WORD_LOAD_REACH(u_procp);
U_WORD_LOAD_REACH(u_frame);
U_WORD_LOAD_REACH(u_arg);
U_WORD_LOAD_REACH(u_rval);
U_WORD_LOAD_REACH(u_error);
U_WORD_LOAD_REACH(u_uid);
U_WORD_LOAD_REACH(u_svuid);
U_WORD_LOAD_REACH(u_ruid);
U_WORD_LOAD_REACH(u_svgid);
U_WORD_LOAD_REACH(u_rgid);
U_WORD_LOAD_REACH(u_groups[NGROUPS - 1]);
#undef U_WORD_LOAD_REACH
#endif

#include <sys/errno.h>

#ifdef KERNEL
extern  struct user u, u0;

/*
 * Increment user profiling counters.
 */
void addupc (caddr_t pc, struct uprof *pbuf, int ticks);

#endif
