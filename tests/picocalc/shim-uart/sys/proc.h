/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_PROC_H_
#define _SHIM_UART_PROC_H_
void	timeout(void (*fun)(caddr_t), caddr_t arg, int t);
void	untimeout(void (*fun)(caddr_t), caddr_t arg);
#endif
