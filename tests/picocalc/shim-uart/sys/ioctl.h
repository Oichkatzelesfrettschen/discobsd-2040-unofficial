/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_IOCTL_H_
#define _SHIM_UART_IOCTL_H_
struct winsize {
	unsigned short	ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define	ECHO	0x0008
#define	XTABS	0x0c00
#define	CRMOD	0x0010
#define	CRTBS	0x00010000
#define	CRTERA	0x00040000
#define	CTLECH	0x00100000
#define	CRTKIL	0x00400000
#define	B115200	17
#define	TIOCSBRK	0x2000747bu	/* _IO('t', 123) */
#define	TIOCCBRK	0x2000747au	/* _IO('t', 122) */
#endif
