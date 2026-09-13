#ifndef _SHIM_SYS_SYSTM_H_
#define _SHIM_SYS_SYSTM_H_
#include <stdio.h>
void panic(const char *);
extern char runin, runout;
void wakeup(caddr_t);
void sleep(caddr_t, int);
extern char __user_data_start[], __user_data_end[];
#define MAXMEM (144 * 1024)
#endif
