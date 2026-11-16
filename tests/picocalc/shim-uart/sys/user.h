/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_USER_H_
#define _SHIM_UART_USER_H_
struct shim_user {
	unsigned short	u_uid;
};
extern struct shim_user u;
#endif
