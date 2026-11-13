/*
 * Host stand-in for the kernel's <machine/dump.h>: the shared format, with
 * the kernel's writer interface that sd.c and rp2040/dump.c use.
 */
#ifndef _SHIM_MACHINE_DUMP_H_
#define _SHIM_MACHINE_DUMP_H_

#ifdef KERNEL
#include <rp2040/include/dump.h>
#else
#define	KERNEL
#include <rp2040/include/dump.h>
#undef	KERNEL
#endif

#endif
