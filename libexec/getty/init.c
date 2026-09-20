/*
 * Getty table initializations.
 *
 * Melbourne getty.
 *
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sgtty.h>
#include "gettytab.h"

extern	struct sgttyb tmode;
extern	struct tchars tc;
extern	struct ltchars ltc;
extern	char hostname[];

struct	gettystrs gettystrs[] = {
	{ .field = "nx" },			/* next table */
	{ .field = "cl" },			/* screen clear characters */
	{ .field = "im" },			/* initial message */
	{ .field = "lm", .defalt = "login: " }, /* login message */
	{ .field = "er", .defalt = &tmode.sg_erase }, /* erase character */
	{ .field = "kl", .defalt = &tmode.sg_kill }, /* kill character */
	{ .field = "et", .defalt = &tc.t_eofc }, /* eof character (eot) */
	{ .field = "pc", .defalt = "" },	/* pad character */
	{ .field = "tt" },			/* terminal type */
	{ .field = "ev" },			/* environment */
	{ .field = "lo", .defalt = "/usr/bin/login" }, /* login program */
	{ .field = "hn", .defalt = hostname },	/* host name */
	{ .field = "he" },			/* host name edit */
	{ .field = "in", .defalt = &tc.t_intrc }, /* interrupt char */
	{ .field = "qu", .defalt = &tc.t_quitc }, /* quit char */
	{ .field = "xn", .defalt = &tc.t_startc }, /* XON (start) char */
	{ .field = "xf", .defalt = &tc.t_stopc }, /* XOFF (stop) char */
	{ .field = "bk", .defalt = &tc.t_brkc }, /* brk char (alt \n) */
	{ .field = "su", .defalt = &ltc.t_suspc }, /* suspend char */
	{ .field = "ds", .defalt = &ltc.t_dsuspc }, /* delayed suspend */
	{ .field = "rp", .defalt = &ltc.t_rprntc }, /* reprint char */
	{ .field = "fl", .defalt = &ltc.t_flushc }, /* flush output */
	{ .field = "we", .defalt = &ltc.t_werasc }, /* word erase */
	{ .field = "ln", .defalt = &ltc.t_lnextc }, /* literal next */
	{ .field = 0 }
};

struct	gettynums gettynums[] = {
	{ .field = "is" },			/* input speed */
	{ .field = "os" },			/* output speed */
	{ .field = "sp" },			/* both speeds */
	{ .field = "to" },			/* timeout */
	{ .field = "f0" },			/* output flags */
	{ .field = "f1" },			/* input flags */
	{ .field = "f2" },			/* user mode flags */
	{ .field = "pf" },			/* delay before flush at 1st prompt */
	{ .field = 0 }
};

struct	gettyflags gettyflags[] = {
	{ .field = "ht", .invrt = 0 },	/* has tabs */
	{ .field = "nl", .invrt = 1 },	/* has newline char */
	{ .field = "ep", .invrt = 0 },	/* even parity */
	{ .field = "op", .invrt = 0 },	/* odd parity */
	{ .field = "ap", .invrt = 0 },	/* any parity */
	{ .field = "ec", .invrt = 1 },	/* no echo */
	{ .field = "co", .invrt = 0 },	/* console special */
	{ .field = "cb", .invrt = 0 },	/* crt backspace */
	{ .field = "ck", .invrt = 0 },	/* crt kill */
	{ .field = "ce", .invrt = 0 },	/* crt erase */
	{ .field = "pe", .invrt = 0 },	/* printer erase */
	{ .field = "rw", .invrt = 1 },	/* do not use raw */
	{ .field = "xc", .invrt = 1 },	/* do not ^X ctl chars */
	{ .field = "ig", .invrt = 0 },	/* ignore garbage */
	{ .field = "ps", .invrt = 0 },	/* do port selector speed select */
	{ .field = "hc", .invrt = 1 },	/* do not set hangup on close */
	{ .field = "ub", .invrt = 0 },	/* unbuffered output */
	{ .field = "ab", .invrt = 0 },	/* auto-baud detect with '\r' */
	{ .field = "dx", .invrt = 0 },	/* set decctlq */
	{ .field = "hf", .invrt = 0 },	/* set hardware flow control */
	{ .field = "np", .invrt = 0 },	/* no parity (PASS8) */
	{ .field = 0 }
};
