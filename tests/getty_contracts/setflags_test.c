/*
 * Contract gate for getty's tty mode derivation, libexec/getty/subr.c's
 * setflags(), compiled from the tree against the tree's headers so the
 * flag constants are the kernel's. The console gate, check-renode, does not
 * reach getty on the emulated board, so the behavior 2.11BSD patches 480,
 * 484, 487 and 493 add is pinned here: the renumbered flag table still
 * answers to its capability names, np selects PASS8 on the final mode word,
 * the parity capabilities select their bits, and an explicit f0/f1/f2
 * overrides the derivation.
 */

#include <sys/ioctl.h>
#include <sgtty.h>
#include <string.h>
#include <unistd.h>

#include "gettytab.h"

/* main.c's terminal state, which subr.c's character table points into. */
struct sgttyb tmode;
struct tchars tc;
struct ltchars ltc;
char hostname[32];

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
clearflags(void)
{
	struct gettyflags *f;

	for (f = gettyflags; f->field != 0; f++) {
		f->value = 0;
		f->set = 0;
	}
	for (struct gettynums *n = gettynums; n->field != 0; n++) {
		n->value = 0;
		n->set = 0;
	}
}

int
main(void)
{
	long f;

	/* The table indexes the header names must agree with the table's own names. */
	check(strcmp(gettyflags[2].field, "ep") == 0 && strcmp(gettyflags[3].field, "op") == 0 &&
	    strcmp(gettyflags[4].field, "ap") == 0 && strcmp(gettyflags[5].field, "ec") == 0 &&
	    strcmp(gettyflags[19].field, "hf") == 0 && strcmp(gettyflags[20].field, "np") == 0,
	    "getty contract: flag table names do not match the header's indexes");

	clearflags();
	f = setflags(2);
	check((f & PASS8) == 0 && (f & (ANYP|ODDP|EVENP)) == 0 && (f & XTABS) != 0,
	    "getty contract: defaults derive parity-free, tab-expanding modes");

	clearflags();
	NP = 1;
	f = setflags(2);
	check((f & PASS8) != 0, "getty contract: np does not select PASS8 on the final mode");
	f = setflags(0);
	check((f & PASS8) == 0, "getty contract: PASS8 leaks into the message-writing mode");

	clearflags();
	OP = 1;
	check((setflags(0) & (ANYP|ODDP|EVENP)) == ODDP, "getty contract: op does not select odd parity");
	clearflags();
	EP = 1;
	check((setflags(0) & (ANYP|ODDP|EVENP)) == EVENP, "getty contract: ep does not select even parity");
	clearflags();
	AP = 1;
	OP = 1;
	check((setflags(0) & (ANYP|ODDP|EVENP)) == ANYP, "getty contract: ap does not win over op");

	clearflags();
	HT = 1;
	check((setflags(0) & XTABS) == 0, "getty contract: ht leaves tab expansion on");
	clearflags();
	HF = 1;
	check((setflags(0) & RTSCTS) != 0, "getty contract: hf does not select RTS/CTS");
	clearflags();
	EC = 1;
	check((setflags(2) & ECHO) != 0, "getty contract: ec does not select ECHO on the final mode");
	check((setflags(1) & (RAW|CBREAK)) == CBREAK, "getty contract: the read mode is not CBREAK by default");

	clearflags();
	F0 = 0x1234;
	F0set = 1;
	check(setflags(0) == 0x1234, "getty contract: an explicit f0 does not override the derivation");

	/* The 8-bit local flags live above bit 16, where main.c splits them for TIOCLSET. */
	clearflags();
	NP = 1;
	f = setflags(2);
	check((int)(f >> 16) == LPASS8, "getty contract: PASS8 does not sit in the local-mode half");

	if (failures != 0) {
		(void)write(2, "getty contracts: fail\n", 22);
		return 1;
	}
	(void)write(1, "getty contracts: pass\n", 22);
	(void)checks;
	return 0;
}
