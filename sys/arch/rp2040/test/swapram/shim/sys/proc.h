#ifndef _SHIM_SYS_PROC_H_
#define _SHIM_SYS_PROC_H_
/* The fields swapram.c touches, named as sys/proc.h names them. */
struct proc {
    int     p_pid;
    short   p_flag;
    size_t  p_addr, p_daddr, p_saddr, p_dsize, p_ssize;
};
extern struct proc proc[];
#define SLOAD 0x0001
#endif
