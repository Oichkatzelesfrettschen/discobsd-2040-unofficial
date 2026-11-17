#ifndef _SHIM_SYS_SYSTM_H_
#define _SHIM_SYS_SYSTM_H_
#include <stdio.h>
void panic(const char *);
extern size_t swapnext;
void swap_cursor_publish(size_t);
void bcopy(const void *, void *, size_t);
void bzero(void *, size_t);
#define MAXMEM (192 * 1024)
/*
 * The host build defines no SDSWAP_ENABLED, so swap_lost reads 0 as it does
 * in sys/sys/systm.h for a kernel without a removable swap device.
 */
#define swap_lost 0
#endif
