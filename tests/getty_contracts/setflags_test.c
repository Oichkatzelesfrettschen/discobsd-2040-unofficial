/*
 * Contract gate for getty's tty mode derivation and its application:
 * setflags() in libexec/getty/subr.c, the defaults that fill an omitted
 * capability, and main.c's applymode(), which divides the mode word and
 * sets both halves with TIOCSETP and TIOCLSET. Compiled from the tree
 * against the tree's headers, so the constants are the kernel's; ioctl is
 * answered here and its calls recorded, since no terminal is open.
 *
 * rw and ec are inverted capabilities: the table stores the inverse of
 * what the entry says, so an omitted rw means RAW and an explicit rw means
 * CBREAK, and an omitted ec means echo on while an explicit ec turns it
 * off. The assertions follow the entry the administrator writes.
 */

#include <sys/ioctl.h>
#include <sgtty.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "gettytab.h"
#include "extern.h"

extern struct sgttyb tmode;

/* The ioctl calls main.c makes, as the driver would see them. */
static unsigned int last_setp_req;
static short last_setp_flags;
static unsigned int last_lset_req;
static int last_lset_word;
static unsigned int ioctl_requests[2];
static int ioctl_calls;

int
test_ioctl(int fd, int req, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	(void)fd;
	if (ioctl_calls < 2)
		ioctl_requests[ioctl_calls] = (unsigned int)req;
	ioctl_calls++;
	/* The request constants are _IOW words above INT_MAX; compare them unsigned. */
	if ((unsigned int)req == (unsigned int)TIOCSETP) {
		last_setp_req = (unsigned int)req;
		last_setp_flags = ((struct sgttyb *)arg)->sg_flags;
	} else if ((unsigned int)req == (unsigned int)TIOCLSET) {
		last_lset_req = (unsigned int)req;
		last_lset_word = *(int *)arg;
	}
	return 0;
}

static int checks;
static int failures;

static void
check(int condition, const char *message)
{
	checks++;
	if (!condition) {
		failures++;
		(void)write(2, message, strlen(message));
		(void)write(2, "\n", 1);
	}
}

/* An empty gettytab entry: every capability omitted, then the defaults filled in. */
static void
fresh(void)
{
	struct gettyflags *f;
	struct gettynums *n;

	for (f = gettyflags; f->field != 0; f++) {
		f->value = 0;
		f->set = 0;
	}
	for (n = gettynums; n->field != 0; n++) {
		n->value = 0;
		n->set = 0;
	}
	gendefaults();
	setdefaults();
}

/* An explicit boolean capability, as the parser stores it: the inverse for an inverted one. */
static void
explicit(int index)
{
	gettyflags[index].value = !gettyflags[index].invrt;
	gettyflags[index].set = 1;
}

/* The shipped default contains ap; a named entry can replace that parity. */
static void
shipped_default_then(int parity_index)
{
	struct gettyflags *flag;

	fresh();
	explicit(4);	/* ap in etc/gettytab's default entry */
	gendefaults();
	for (flag = gettyflags; flag->field != 0; flag++)
		flag->set = 0;
	explicit(parity_index);
	resolveparity();
	setdefaults();
}

int
main(void)
{
	long f;
	int local;

	check(strcmp(gettyflags[2].field, "ep") == 0 && strcmp(gettyflags[3].field, "op") == 0 &&
	    strcmp(gettyflags[4].field, "ap") == 0 && strcmp(gettyflags[5].field, "ec") == 0 &&
	    strcmp(gettyflags[19].field, "hf") == 0 && strcmp(gettyflags[20].field, "np") == 0,
	    "getty contract: flag table names do not match the header's indexes");

	fresh();
	f = setflags(2);
	check((f & PASS8) == 0 && (f & (ANYP|ODDP|EVENP)) == 0 && (f & XTABS) != 0,
	    "getty contract: an empty entry derives parity-free, tab-expanding modes");
	check((setflags(1) & (RAW|CBREAK)) == RAW,
	    "getty contract: an omitted rw does not select RAW for the read mode");
	check((f & ECHO) != 0, "getty contract: an omitted ec does not leave echo on");

	fresh();
	explicit(20);	/* np */
	f = setflags(2);
	check((f & PASS8) != 0, "getty contract: np does not select PASS8 on the final mode");
	check((setflags(0) & PASS8) == 0, "getty contract: PASS8 leaks into the message-writing mode");

	shipped_default_then(3);	/* op */
	check((setflags(0) & (ANYP|ODDP|EVENP)) == ODDP, "getty contract: op does not select odd parity");
	shipped_default_then(2);	/* ep */
	check((setflags(0) & (ANYP|ODDP|EVENP)) == EVENP, "getty contract: ep does not select even parity");
	fresh();
	explicit(4);	/* ap */
	explicit(3);	/* op */
	check((setflags(0) & (ANYP|ODDP|EVENP)) == ANYP, "getty contract: ap does not win over op");

	fresh();
	explicit(0);	/* ht */
	check((setflags(0) & XTABS) == 0, "getty contract: ht leaves tab expansion on");
	fresh();
	explicit(19);	/* hf */
	check((setflags(0) & RTSCTS) != 0, "getty contract: hf does not select RTS/CTS");
	fresh();
	explicit(5);	/* ec */
	check((setflags(2) & ECHO) == 0, "getty contract: an explicit ec does not turn echo off");
	fresh();
	for (struct gettyflags *p = gettyflags; p->field != 0; p++)
		if (strcmp(p->field, "rw") == 0)
			explicit((int)(p - gettyflags));
	check((setflags(1) & (RAW|CBREAK)) == CBREAK,
	    "getty contract: an explicit rw does not select CBREAK");

	fresh();
	F0 = 0x1234;
	F0set = 1;
	check(setflags(0) == 0x1234, "getty contract: an explicit f0 does not override the derivation");

	/* The split and the application: the local half reaches TIOCLSET with PASS8 in it. */
	fresh();
	explicit(20);	/* np */
	ioctl_calls = 0;
	f = setflags(2);
	splitflags(f, &tmode, &local);
	check(tmode.sg_flags == (short)(f & 0xffff) && local == (int)(f >> 16) &&
	    (local & LPASS8) != 0,
	    "getty contract: splitflags does not divide the mode word at the local half");
	applymode(f, &tmode, CRMOD);
	check(ioctl_calls == 2 &&
	    ioctl_requests[0] == (unsigned int)TIOCSETP &&
	    ioctl_requests[1] == (unsigned int)TIOCLSET &&
	    last_setp_req == (unsigned int)TIOCSETP &&
	    last_lset_req == (unsigned int)TIOCLSET &&
	    last_setp_flags == tmode.sg_flags && (last_setp_flags & CRMOD) != 0 &&
	    last_lset_word == local &&
	    (last_lset_word & LPASS8) != 0,
	    "getty contract: main.c does not apply low then local modes through TIOCSETP and TIOCLSET");

	if (failures != 0) {
		(void)write(2, "getty contracts: fail\n", 22);
		return 1;
	}
	(void)write(1, "getty contracts: pass\n", 22);
	(void)checks;
	return 0;
}
