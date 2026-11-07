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
/*
 * The spl calls go to the interrupt-mask model in regmodel.h, which counts
 * nesting and records the level at which the card-detect pin is sampled.
 */
extern int shim_splraise(void);
extern void shim_splx(int s);
#define	splhigh()	shim_splraise()
#define	splbio()	shim_splraise()
#define	splx(s)		shim_splx(s)

extern const char *panicstr;
void timeout(void (*fn)(char *), char *arg, int ticks);

#endif
