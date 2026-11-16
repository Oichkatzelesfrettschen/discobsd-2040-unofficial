/*
 * Host stand-in for uart_test.c (tests/picocalc): the declarations
 * sys/arch/rp2040/dev/uart.c uses, and no more. The test supplies every
 * function declared here.
 */
#ifndef _SHIM_UART_TTY_H_
#define _SHIM_UART_TTY_H_
struct uio;
struct clist {
	int	c_cc;
};
struct tty {
	struct clist	t_outq;
	caddr_t		t_addr;
	int		t_state;
	int		t_flags;
	char		t_ispeed, t_ospeed;
	void		(*t_oproc)(struct tty *);
	struct winsize	t_winsize;
};
#define	TS_TIMEOUT	0x000001
#define	TS_ISOPEN	0x000004
#define	TS_CARR_ON	0x000008
#define	TS_BUSY		0x000010
#define	TS_TTSTOP	0x000100
#define	TS_XCLUDE	0x000400
/* stdio.h is already in (sys/param.h), so its getc stays the stdio one. */
#define	getc(q)	shim_tty_getc(q)
int	shim_tty_getc(struct clist *);
void	ttyinput(int c, struct tty *tp);
void	ttyowake(struct tty *tp);
void	ttstart(struct tty *tp);
void	ttychars(struct tty *tp);
int	ttyopen(dev_t dev, struct tty *tp);
int	ttyclose(struct tty *tp);
void	ttywflush(struct tty *tp);
int	ttread(struct tty *tp, struct uio *uio, int flag);
int	ttwrite(struct tty *tp, struct uio *uio, int flag);
int	ttioctl(struct tty *tp, u_int cmd, caddr_t addr, int flag);
int	ttyselect(struct tty *tp, int rw);
#endif
