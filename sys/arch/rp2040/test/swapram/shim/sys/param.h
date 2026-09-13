/*
 * Host stand-in for the kernel's sys/param.h: the types and constants
 * arch/rp2040/rp2040/swapram.c and kern/subr_rmap.c use, with the
 * RP2040 values from machine/machparam.h.
 */
#ifndef _SHIM_SYS_PARAM_H_
#define _SHIM_SYS_PARAM_H_
#define _DEFAULT_SOURCE 1
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#define KERNEL 1
#define SWAPRAM 1
#ifndef SWAPRAM_KB
#define SWAPRAM_KB 16
#endif
#define NPROC 25
#define DEV_BSHIFT 10
#define DEV_BSIZE (1 << DEV_BSHIFT)
#define btod(x) (((x) + DEV_BSIZE - 1) >> DEV_BSHIFT)
#define USIZE 3072
#define SSIZE 2048
#endif
