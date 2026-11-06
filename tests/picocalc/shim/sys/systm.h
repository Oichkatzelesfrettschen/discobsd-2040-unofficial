/* Host stand-in for the kernel's <sys/systm.h>. */
#ifndef _SHIM_SYS_SYSTM_H_
#define _SHIM_SYS_SYSTM_H_

#include <stdio.h>
#include <string.h>

/*
 * macOS's <string.h> defines bzero and bcopy as fortified macros; the
 * kernel's argument order is what sd.c relies on, so these replace them.
 */
#undef	bzero
#undef	bcopy
#define	bzero(p, n)	memset((p), 0, (n))
#define	bcopy(s, d, n)	memcpy((d), (s), (n))
#define	splhigh()	0
#define	splx(s)		((void)(s))

extern const char *panicstr;
void timeout(void (*fn)(char *), char *arg, int ticks);

#endif
