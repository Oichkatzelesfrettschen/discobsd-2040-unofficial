/*
 * Host gate for sys/kern/subr_prf.c, the kernel's own printf.
 *
 * Every console line and every panic message goes through this formatter,
 * which makes a defect here expensive twice over: a bad conversion in a
 * panic path faults while already faulting. It is also the one file in
 * sys/kern that cannot be tested at the host's natural width. prf() carries
 * its own argument walk,
 *
 *     #define va_arg(ap,type) *(type*) (void*) (ap++)
 *
 * over a u_int *, so it reads one four-byte slot per argument and advances by
 * one. printf() feeds it "&fmt + 1", the address just past its first
 * parameter. Both hold exactly where a pointer, a long and an int are four
 * bytes and arguments sit on the stack, which is the target and which is also
 * what "cc -m32" produces on an x86-64 host. At the host's own width a %s
 * would read four bytes of an eight-byte pointer and the walk would
 * desynchronise from there, so this gate builds through check-kernel-ilp32
 * rather than check-kernel.
 *
 * Nothing here is a virtual machine or a container. The binary is an
 * ordinary 32-bit userspace program running on the same kernel as the rest
 * of the tier; what it needs from the platform is the width of a pointer,
 * not a booted operating system.
 *
 * The Makefile moves printf and panic aside for this gate the way the tier
 * moves malloc aside elsewhere, because libc owns both names in a host
 * binary. In this file printf() and panic() are the kernel's.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/syslog.h>

/* The kernel globals subr_prf.c reaches for. */
struct user u;
struct tty cnttys[1];
dev_t rootdev = 0x0001;

/* What the console and the terminal received since the last reset. */
#define CAPTURE 512
static char console[CAPTURE];
static unsigned console_len;
static char terminal[CAPTURE];
static unsigned terminal_len;

/* What boot() was asked to do, since panic() ends there. */
static int boot_calls;
static dev_t boot_dev;
static int boot_howto;

static void
capture(char *buf, unsigned *len, int c)
{
	if (*len < CAPTURE - 1) {
		buf[*len] = (char)c;
		(*len)++;
		buf[*len] = '\0';
	}
}

/* The console driver. This is the gate's observation point. */
void
cnputc(char c)
{
	capture(console, &console_len, c);
}

/* The terminal path, which uprintf and tprintf reach instead. */
int
ttyoutput(int c, struct tty *tp)
{
	(void)tp;
	capture(terminal, &terminal_len, c);
	return 0;
}

void
ttstart(struct tty *tp)
{
	(void)tp;
}

/* Room in the terminal's output queue; the gate says yes. */
int
ttycheckoutq(struct tty *tp, int wait)
{
	(void)tp;
	(void)wait;
	return 1;
}

/*
 * panic() ends in boot(), which never returns on a board. Here it records
 * what it was asked and returns, so the gate can read the flags back; panic
 * has nothing left to do by then.
 */
void
boot(dev_t dev, int howto)
{
	boot_calls++;
	boot_dev = dev;
	boot_howto = howto;
}

static void
reset(void)
{
	bzero((caddr_t)&u, sizeof u);
	bzero((caddr_t)cnttys, sizeof cnttys);
	console[0] = '\0';
	console_len = 0;
	terminal[0] = '\0';
	terminal_len = 0;
	boot_calls = 0;
	boot_dev = 0;
	boot_howto = 0;
	panicstr = NULL;
	hk_reset_output();
}

/* Run one format through the kernel's printf and compare the console. */
static void
expect(const char *file, int line, const char *want, const char *what)
{
	hk_checks++;
	if (!hk_streq(console, want)) {
		hk_note("  %s gave \"%s\", expected \"%s\"", what, console,
		    want);
		hk_fail(file, line, what);
	}
}

#define EXPECT(want, what)	expect(__FILE__, __LINE__, want, what)

/*
 * The widths this formatter is asked for. A four-byte slot per argument is
 * the contract, so an int, a pointer and a long all have to come back whole.
 */
static void
integer_conversions(void)
{
	reset();
	printf("plain text");
	EXPECT("plain text", "literal text");

	reset();
	printf("%d %d %d", 0, 42, -42);
	EXPECT("0 42 -42", "signed decimal");

	reset();
	printf("%u", (u_int)4294967295U);
	EXPECT("4294967295", "unsigned decimal at the top of the range");

	reset();
	printf("%o %x %X", 8, 255, 255);
	EXPECT("10 ff FF", "octal and both hexadecimal cases");

	reset();
	printf("%c%c", 'o', 'k');
	EXPECT("ok", "characters");

	reset();
	printf("[%s]", "text");
	EXPECT("[text]", "a string, which is a pointer through a four-byte slot");

	reset();
	printf("%d%%", 50);
	EXPECT("50%", "a literal percent");

	/* A long takes one slot here, the same as an int. */
	reset();
	printf("%ld", 123456L);
	EXPECT("123456", "long decimal");
}

/*
 * Width, precision and the flags that modify them. These are where a
 * formatter quietly truncates or overruns, and where the kernel's differs
 * from libc's often enough to be worth stating.
 */
static void
padding_and_flags(void)
{
	reset();
	printf("[%5d]", 42);
	EXPECT("[   42]", "right adjusted to a width");

	reset();
	printf("[%-5d]", 42);
	EXPECT("[42   ]", "left adjusted");

	reset();
	printf("[%05d]", 42);
	EXPECT("[00042]", "zero filled");

	reset();
	printf("[%5s]", "ab");
	EXPECT("[   ab]", "a string to a width");

	reset();
	printf("[%-5s]", "ab");
	EXPECT("[ab   ]", "a string left adjusted");

	reset();
	printf("[%.2s]", "abcdef");
	EXPECT("[ab]", "a string cut to a precision");

	reset();
	printf("[%#x %#o]", 255, 8);
	EXPECT("[0xff 010]", "the alternate form");

	reset();
	printf("[%*d]", 6, 42);
	EXPECT("[    42]", "a width taken from the arguments");

	/* A width wider than the digit buffer must not be taken literally. */
	reset();
	printf("[%.99d]", 7);
	HK_CHECK(console_len < CAPTURE - 1);
	HK_CHECK(hk_contains(console, "7"));
}

/*
 * %b decodes a register into its named bits, which is what a driver uses to
 * report a status word. The base comes first as a control character, then
 * each bit's number and name.
 */
static void
bit_field_decoding(void)
{
	reset();
	printf("reg=%b", 3, "\10\2BITTWO\1BITONE");
	EXPECT("reg=3<BITTWO,BITONE>", "a decoded register");

	reset();
	printf("reg=%b", 0, "\10\2BITTWO\1BITONE");
	EXPECT("reg=0", "a register with no bits set names none");
}

/*
 * Where the output goes. printf reaches the console; uprintf reaches the
 * calling process's terminal and nothing else, which is what keeps a
 * per-process complaint off the console.
 */
static void
output_routing(void)
{
	reset();
	printf("console only");
	HK_CHECK(hk_streq(console, "console only"));
	HK_CHECK(terminal_len == 0);

	/* uprintf with no controlling terminal writes nowhere. */
	reset();
	u.u_ttyp = NULL;
	uprintf("dropped");
	HK_CHECK(console_len == 0);
	HK_CHECK(terminal_len == 0);

	/* With one, it reaches the terminal and not the console. */
	reset();
	cnttys[0].t_state = TS_CARR_ON | TS_ISOPEN;
	u.u_ttyp = &cnttys[0];
	uprintf("to the user");
	HK_CHECK(hk_streq(terminal, "to the user"));
	HK_CHECK(console_len == 0);

	/* A newline reaches a terminal as a carriage return and a newline,
	   because the line discipline is not in the path here. */
	reset();
	cnttys[0].t_state = TS_CARR_ON | TS_ISOPEN;
	u.u_ttyp = &cnttys[0];
	uprintf("a\n");
	HK_CHECK(hk_streq(terminal, "a\r\n"));
}

/*
 * panic prints its message and ends in boot(). A second panic while the
 * first is still recorded adds RB_NOSYNC, because syncing the disks from a
 * panic is what turns one panic into a recursive pair.
 */
static void
panic_path(void)
{
	reset();
	panic("out of mbufs");
	HK_CHECK(hk_streq(console, "panic: out of mbufs\n"));
	HK_CHECK(panicstr != NULL);
	HK_CHECK(hk_streq(panicstr, "out of mbufs"));
	HK_CHECK(boot_calls == 1);
	HK_CHECK(boot_dev == rootdev);
	HK_CHECK(boot_howto == (RB_HALT | RB_DUMP));

	/* The second one keeps the first message and refuses to sync. */
	panic("and again");
	HK_CHECK(hk_streq(panicstr, "out of mbufs"));
	HK_CHECK(boot_calls == 2);
	HK_CHECK(boot_howto == (RB_HALT | RB_DUMP | RB_NOSYNC));
	HK_CHECK(hk_contains(console, "panic: and again"));
}

int
main(void)
{
	integer_conversions();
	padding_and_flags();
	bit_field_decoding();
	output_routing();
	panic_path();
	return hk_verdict("subr_prf");
}
