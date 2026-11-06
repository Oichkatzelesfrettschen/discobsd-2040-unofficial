/*
 * Host stand-in for the kernel's <sys/param.h>, enough for the PicoCalc
 * drivers to compile on the build machine. The kernel's own disk.h and buf.h
 * are used unchanged through the sibling shims.
 */
#ifndef _SHIM_SYS_PARAM_H_
#define _SHIM_SYS_PARAM_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

typedef int		daddr_t;

#undef minor
#define	minor(dev)	((dev) & 0xff)
#define	DEV_BSIZE	1024
#define	DEV_BSHIFT	10
#define	btod(x)		(((x) + DEV_BSIZE - 1) >> DEV_BSHIFT)
#define	HZ		1000
#define	NODEV		((dev_t)-1)
/* Register access goes to the model in tests/picocalc/regmodel.h. */
extern volatile unsigned *shim_reg(unsigned long addr);
#define	PC_REG32(a)	(*shim_reg((unsigned long)(a)))
#define	__unused	__attribute__((unused))
#define	IFCHR		0020000
#define	IFBLK		0060000

#endif
