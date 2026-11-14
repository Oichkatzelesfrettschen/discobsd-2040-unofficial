/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)cons.c	1.3 (2.11BSD GTE) 1997/4/25
 */

/*
 * Console driver.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>

#ifdef RP2040
#include <machine/board.h>
#endif

struct tty cnttys[1];

static dev_t
console_device(void)
{
#ifdef RP2040
	int major_number, minor_number;

	board_console(&major_number, &minor_number);
	return makedev(major_number, minor_number);
#else
	return makedev(CONS_MAJOR, CONS_MINOR);
#endif
}

/*
 * The terminal the console redirector forwards to: the d_ttys entry of the
 * device console_device() names, or NULL when that driver keeps none.
 * cnttys is the redirector's own cdevsw slot and carries no traffic.
 */
struct tty *
cntty(void)
{
	dev_t dev = console_device();
	struct tty *ttys = cdevsw[major(dev)].d_ttys;

	return (ttys != NULL ? &ttys[minor(dev)] : NULL);
}

int
cnopen(dev_t cn __unused, int flag, int mode)
{
	dev_t dev = console_device();

	return cdevsw[major(dev)].d_open(dev, flag, mode);
}

int
cnclose(dev_t cn __unused, int flag, int mode)
{
	dev_t dev = console_device();

	return cdevsw[major(dev)].d_close(dev, flag, mode);
}

int
cnread(dev_t cn __unused, struct uio *uio, int flag)
{
	dev_t dev = console_device();

	return cdevsw[major(dev)].d_read(dev, uio, flag);
}

int
cnwrite(dev_t cn __unused, struct uio *uio, int flag)
{
	dev_t dev = console_device();

	return cdevsw[major(dev)].d_write(dev, uio, flag);
}

int
cnselect(dev_t cn __unused, int rw)
{
	dev_t dev = console_device();

	return cdevsw[major(dev)].d_select(dev, rw);
}

int
cnioctl(dev_t cn __unused, u_int cmd, caddr_t addr, int flag)
{
	dev_t dev = console_device();

	return cdevsw[major(dev)].d_ioctl(dev, cmd, addr, flag);
}

/*
 * Put a symbol on console terminal.
 */
void
cnputc(char c)
{
	dev_t dev = console_device();

	if (cdevsw[major(dev)].r_write) {
		cdevsw[major(dev)].r_write(dev, c);
	} else {
		putc(c, &cdevsw[major(dev)].d_ttys[minor(dev)].t_outq);
		ttstart(&cdevsw[major(dev)].d_ttys[minor(dev)]);
		ttyflush(&cdevsw[major(dev)].d_ttys[minor(dev)], 0);
	}

	if (c == '\n')
		cnputc('\r');
}

/*
 * Receive a symbol from console terminal.
 */
int
cngetc(void)
{
	dev_t dev = console_device();

	if (cdevsw[major(dev)].r_read) {
		return cdevsw[major(dev)].r_read(dev);
	} else {
		return getc(&cdevsw[major(dev)].d_ttys[minor(dev)].t_rawq);
	}
}
