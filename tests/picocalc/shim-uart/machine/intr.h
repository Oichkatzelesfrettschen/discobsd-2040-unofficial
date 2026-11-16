/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_INTR_H_
#define _SHIM_UART_INTR_H_
#define	IPL_TTY		4
int	spltty(void);
void	splx(int s);
void	arm_intr_set_priority(int irq, int ipl);
void	arm_intr_enable_irq(int irq);
#endif
