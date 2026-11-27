/*
 * Host stand-in: sleep() and wakeup() for picopcm.c, supplied by its test;
 * the other drivers use zero declarations from <sys/proc.h>.
 */
#ifndef _SHIM_SYS_PROC_H_
#define _SHIM_SYS_PROC_H_

#define	PZERO	25
#define	PRIBIO	20

int tsleep(caddr_t chan, int pri, int ticks);
void sleep(caddr_t chan, int pri);
void wakeup(caddr_t chan);

#endif
