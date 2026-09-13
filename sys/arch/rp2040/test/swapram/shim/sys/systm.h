#ifndef _SHIM_SYS_SYSTM_H_
#define _SHIM_SYS_SYSTM_H_
#include <stdio.h>
void panic(const char *);
extern char runin, runout;
void wakeup(caddr_t);
void sleep(caddr_t, int);
#endif
