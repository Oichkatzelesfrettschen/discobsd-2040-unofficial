/* Host stand-in for the kernel's <sys/config.h>. */
#ifndef _SHIM_SYS_CONFIG_H_
#define _SHIM_SYS_CONFIG_H_

struct conf_device {
	int	dummy;
};

struct driver {
	const char	*name;
	int		(*probe)(struct conf_device *);
};

#endif
