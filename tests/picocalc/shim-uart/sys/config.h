/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_CONFIG_H_
#define _SHIM_UART_CONFIG_H_
struct conf_device {
	int	dev_unit;
};
struct driver {
	const char	*d_name;
	int		(*d_init)(const struct conf_device *);
};
#endif
