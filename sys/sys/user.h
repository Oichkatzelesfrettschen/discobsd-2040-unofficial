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

/*
 * exec_setupstack() records the first PSARGSZ - 1 bytes of the argument
 * strings in u_psargs, each terminator a space and the field NUL-ended, so
 * the process record (KERN_PROC) reports arguments for a process wherever
 * its image lives. On RP2040 the 52 bytes fill struct user to exactly
 * UAREA_STACK_OFFSET, which sys/kern/subr_capacity.c asserts, so the field
 * costs no user-area, swap or pool byte there.
 */
#define PSARGSZ     52

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

/*
 * Fields the kernel reaches by address -- the setjmp() save areas and the
 * command and argument strings -- rather than by word loads. Placing them
 * last leaves the offsets a Thumb-1 load reaches from &u to the system-call
 * and credential fields above (U_THUMB1_WORD_REACH below).
 */
    char    u_comm[MAXCOMLEN + 1];  /* command file name */
    label_t u_qsave;                /* for non-local gotos on interrupts */
    label_t u_rsave;                /* save info when exchanging stacks */
    label_t u_ssave;                /* label variable for swapping */
    char    u_psargs[PSARGSZ];      /* argument prefix exec recorded */
    int     u_stack[1];             /* kernel stack per user
                                     * extends from u + USIZE
                                     * backward not to reach here */
};

#ifdef KERNEL
/*
 * locore.S reads u_procp as [r2, #0] when it exchanges user areas, the
 * one access to struct user by a numeric offset.
 */
_Static_assert(__builtin_offsetof(struct user, u_procp) == 0,
    "u_procp must be the first word of the user area");

#if defined(__ARM_ARCH_6M__)
/*
 * Keyed on the compiler's ARMv6-M macro, not __thumb__: host tests define
 * __thumb__ to select target code paths while compiling with 64-bit
 * pointers, where these offsets do not hold.
 *
 * A Thumb-1 (ARMv6-M) "ldr rd, [rn, #imm]" encodes a five-bit word index,
 * so it reaches offsets 0 to 124 from its base register. A field of u in
 * that window loads from the one literal that holds &u; a field beyond it
 * needs a literal or an address computation of its own. System-call
 * dispatch reads u_frame, u_arg, u_rval and u_error on every call, and
 * permission checks read the credentials, so each must stay in the window.
 */
#define U_THUMB1_WORD_REACH(member) \
    _Static_assert(__builtin_offsetof(struct user, member) <= 124, \
        #member " lies beyond the Thumb-1 word-load reach of &u")
U_THUMB1_WORD_REACH(u_frame);
U_THUMB1_WORD_REACH(u_arg[5]);
U_THUMB1_WORD_REACH(u_rval);
U_THUMB1_WORD_REACH(u_error);
U_THUMB1_WORD_REACH(u_uid);
U_THUMB1_WORD_REACH(u_svuid);
U_THUMB1_WORD_REACH(u_ruid);
U_THUMB1_WORD_REACH(u_svgid);
U_THUMB1_WORD_REACH(u_rgid);
U_THUMB1_WORD_REACH(u_groups[NGROUPS - 1]);
#undef U_THUMB1_WORD_REACH
#endif
#endif /* KERNEL */

#include <sys/errno.h>

#ifdef KERNEL
extern  struct user u, u0;

/*
 * Increment user profiling counters.
 */
void addupc (caddr_t pc, struct uprof *pbuf, int ticks);

#endif
