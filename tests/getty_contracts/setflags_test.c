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

#define GETTYTAB_DESCRIPTOR 31
#define GETTYTAB_BUFFER_SIZE 512

static const char *gettytab_fixture;
static size_t gettytab_offset;
static char entry_buffer[GETTYTAB_BUFFER_SIZE];
static char entry_strings[GETTYTAB_BUFFER_SIZE];
static char default_buffer[GETTYTAB_BUFFER_SIZE];
static char default_strings[GETTYTAB_BUFFER_SIZE];

int
test_open(const char *path, int flags, ...)
{
	(void)flags;
	if (strcmp(path, "/etc/gettytab") != 0)
		return -1;
	gettytab_offset = 0;
	return GETTYTAB_DESCRIPTOR;
}

ssize_t
test_read(int descriptor, void *buffer, size_t capacity)
{
	size_t remaining;
	size_t transferred;

	if (descriptor != GETTYTAB_DESCRIPTOR)
		return -1;
	remaining = strlen(gettytab_fixture) - gettytab_offset;
	transferred = remaining < capacity ? remaining : capacity;
	memcpy(buffer, gettytab_fixture + gettytab_offset, transferred);
	gettytab_offset += transferred;
	return (ssize_t)transferred;
}

int
test_close(int descriptor)
{
	return descriptor == GETTYTAB_DESCRIPTOR ? 0 : -1;
}

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

static void
reset_capabilities(void)
{
	struct gettyflags *f;
	struct gettynums *n;
	struct gettystrs *s;

	for (f = gettyflags; f->field != 0; f++) {
		f->value = 0;
		f->set = 0;
		f->defalt = 0;
	}
	for (n = gettynums; n->field != 0; n++) {
		n->value = 0;
		n->set = 0;
		n->defalt = 0;
	}
	for (s = gettystrs; s->field != 0; s++) {
		s->value = 0;
		s->defalt = 0;
	}
}

/* Public entries pass through getent(), getflag(), getnum() and getstr(). */
static void
parse_entry(const char *fixture)
{
	reset_capabilities();
	gettytab_fixture = fixture;
	gettable("entry", entry_buffer, entry_strings);
	gendefaults();
	setdefaults();
}

/* The shipped default contains ap; a named entry can replace that parity. */
static void
shipped_default_then(const char *fixture)
{
	reset_capabilities();
	gettytab_fixture = fixture;
	gettable("default", default_buffer, default_strings);
	gendefaults();
	gettable("entry", entry_buffer, entry_strings);
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

	parse_entry("entry:::\n");
	f = setflags(2);
	check((f & PASS8) == 0 && (f & (ANYP|ODDP|EVENP)) == 0 && (f & XTABS) != 0,
	    "getty contract: an empty entry derives parity-free, tab-expanding modes");
	check((setflags(1) & (RAW|CBREAK)) == RAW,
	    "getty contract: an omitted rw does not select RAW for the read mode");
	check((f & ECHO) != 0, "getty contract: an omitted ec does not leave echo on");

	parse_entry("entry:np:\n");
	f = setflags(2);
	check((f & PASS8) != 0, "getty contract: np does not select PASS8 on the final mode");
	check((setflags(0) & PASS8) == 0, "getty contract: PASS8 leaks into the message-writing mode");

	shipped_default_then("default:ap:\nentry:op:\n");
	check((setflags(0) & (ANYP|ODDP|EVENP)) == ODDP, "getty contract: op does not select odd parity");
	shipped_default_then("default:ap:\nentry:ep:\n");
	check((setflags(0) & (ANYP|ODDP|EVENP)) == EVENP, "getty contract: ep does not select even parity");
	parse_entry("entry:ap:op:\n");
	check((setflags(0) & (ANYP|ODDP|EVENP)) == ANYP, "getty contract: ap does not win over op");

	parse_entry("entry:ht:\n");
	check((setflags(0) & XTABS) == 0, "getty contract: ht leaves tab expansion on");
	parse_entry("entry:hf:\n");
	check((setflags(0) & RTSCTS) != 0, "getty contract: hf does not select RTS/CTS");
	parse_entry("entry:ec:\n");
	check((setflags(2) & ECHO) == 0, "getty contract: an explicit ec does not turn echo off");
	parse_entry("entry:rw:\n");
	check((setflags(1) & (RAW|CBREAK)) == CBREAK,
	    "getty contract: an explicit rw does not select CBREAK");

	parse_entry("entry:f0#4660:\n");
	check(setflags(0) == 0x1234, "getty contract: an explicit f0 does not override the derivation");

	/* The split and the application: the local half reaches TIOCLSET with PASS8 in it. */
	parse_entry("entry:np:\n");
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
