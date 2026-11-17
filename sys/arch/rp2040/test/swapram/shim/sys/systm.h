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
 * Without SDSWAP_ENABLED swap_lost reads 0 as it does in sys/sys/systm.h for
 * a kernel without a removable swap device; the lost-device build
 * (swapram_evac_test_lost) defines the variable and its swap_with_buf()
 * sets it.
 */
#ifdef SDSWAP_ENABLED
extern char swap_lost;
#else
#define swap_lost 0
#endif
#endif
