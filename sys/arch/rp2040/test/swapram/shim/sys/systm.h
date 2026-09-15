#ifndef _SHIM_SYS_SYSTM_H_
#define _SHIM_SYS_SYSTM_H_
#include <stdio.h>
void panic(const char *);
extern char runin, runout;
extern size_t swapnext;
void swap_cursor_publish(size_t);
void wakeup(caddr_t);
void sleep(caddr_t, int);
void bcopy(const void *, void *, size_t);
extern char __user_data_start[], __user_data_end[];
#define MAXMEM (144 * 1024)
#endif
