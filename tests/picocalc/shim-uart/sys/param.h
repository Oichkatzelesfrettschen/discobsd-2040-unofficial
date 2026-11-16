/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_PARAM_H_
#define _SHIM_UART_PARAM_H_

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#undef minor
#undef makedev
#define	minor(dev)	((dev) & 0xff)
#define	makedev(x, y)	((dev_t)(((x) << 8) | (y)))
#ifndef __unused
#define	__unused	__attribute__((unused))
#endif
#define	LED_TTY		0x08
void	led_control(int mask, int on);

#endif
