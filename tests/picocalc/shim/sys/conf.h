/* Host stand-in for <sys/conf.h>: the raw-device entry sd.c hands off to. */
#ifndef _SHIM_SYS_CONF_H_
#define _SHIM_SYS_CONF_H_

struct uio;
int rawrw(dev_t dev, struct uio *uio, int flag);

#ifdef SDSWAP_ENABLED
/*
 * The block device switch as sdswap_fallback() uses it: the open and size
 * entries of the fixed swap device it returns to.
 */
struct bdevsw {
	int	(*d_open)(dev_t, int, int);
	daddr_t	(*d_psize)(dev_t);
};
extern const struct bdevsw bdevsw[];
#endif

#endif
