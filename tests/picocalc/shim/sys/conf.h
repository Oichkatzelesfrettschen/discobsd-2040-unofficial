/* Host stand-in for <sys/conf.h>: the raw-device entry sd.c hands off to. */
#ifndef _SHIM_SYS_CONF_H_
#define _SHIM_SYS_CONF_H_

struct uio;
int rawrw(dev_t dev, struct uio *uio, int flag);

#endif
