/*
 * Host stand-in: sleep() and wakeup() for picopcm.c, supplied by its test;
 * the other drivers need nothing from <sys/proc.h>.
 */
#ifndef _SHIM_SYS_PROC_H_
#define _SHIM_SYS_PROC_H_

#define	PZERO	25
#define	PRIBIO	20

void sleep(caddr_t chan, int pri);
void wakeup(caddr_t chan);

#endif
