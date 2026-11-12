/*
 * Host gate for sys/kern/subr_prf.c, the kernel's own printf.
 *
 * Every console line and every panic message goes through this formatter,
 * which makes a defect here expensive twice over: a bad conversion in a
 * panic path faults while already faulting. The gate uses ILP32 so the
 * formatted integer and pointer widths match the target. The compiler's
 * variadic argument interface supplies each argument to prf().
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

#ifdef LOG_ENABLED
static void capture(char *buffer, unsigned *length, int character);
static char log_buffer[CAPTURE];
static unsigned log_length;
static int log_open;
static unsigned log_wakeups;

int
logwrt(char *buffer, int length, int unit)
{
	int index;

	HK_CHECK(unit == 0);
	for (index = 0; index < length; index++)
		capture(log_buffer, &log_length, buffer[index]);
	return length;
}

int
logisopen(int unit)
{
	HK_CHECK(unit == 0);
	return log_open;
}

void
logwakeup(int unit)
{
	HK_CHECK(unit == 0);
	log_wakeups++;
}
#endif

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
 * panic() ends in boot(), which sys/systm.h declares __dead. Here it records
 * what it was asked and leaves through hk_escape(), so the gate reads the
 * flags back from the hk_catch() frame that ran the panic.
 */
void
boot(dev_t dev, int howto)
{
	boot_calls++;
	boot_dev = dev;
	boot_howto = howto;
	hk_escape();
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
#ifdef LOG_ENABLED
	log_buffer[0] = '\0';
	log_length = 0;
	log_open = 0;
	log_wakeups = 0;
#endif
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
 * The formatter must preserve the value of each argument at target widths.
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
	printf("%d %s %u %x %ld", -7, "mixed", 19U, 42U, 123456L);
	EXPECT("-7 mixed 19 2a 123456", "mixed variadic arguments");

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
	EXPECT("[text]", "a string argument");

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
 *
 * The format(printf) attribute on printf() in sys/sys/systm.h checks calls
 * against ISO C, where %b is C23's binary conversion taking one argument, so
 * the register-name argument draws a format diagnostic. The gate drives
 * prf()'s implementation directly with format checking off for this
 * function; kernel sources may not use %b (check-kernel-printf-formats).
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
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
#pragma GCC diagnostic pop

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

	reset();
	cnttys[0].t_state = TS_CARR_ON | TS_ISOPEN;
	u.u_ttyp = &cnttys[0];
	uprintf("%d %s", 42, "terminal");
	HK_CHECK(hk_streq(terminal, "42 terminal"));

	reset();
	cnttys[0].t_state = TS_CARR_ON | TS_ISOPEN;
	tprintf(&cnttys[0], "%u %s", 23U, "message");
	HK_CHECK(hk_streq(terminal, "23 message"));
	HK_CHECK(console_len == 0);

	reset();
	log(LOG_INFO, "%d %s", 7, "event");
	HK_CHECK(hk_streq(console, "7 event"));
#ifdef LOG_ENABLED
	HK_CHECK(hk_streq(log_buffer, "<6>7 event"));
	HK_CHECK(log_wakeups == 1);

	reset();
	log_open = 1;
	log(LOG_INFO, "%d %s", 8, "reader");
	HK_CHECK(hk_streq(log_buffer, "<6>8 reader"));
	HK_CHECK(console_len == 0);
	HK_CHECK(log_wakeups == 1);
#endif

	/* A newline reaches a terminal as a carriage return and a newline,
	   because the line discipline is not in the path here. */
	reset();
	cnttys[0].t_state = TS_CARR_ON | TS_ISOPEN;
	u.u_ttyp = &cnttys[0];
	uprintf("a\n");
	HK_CHECK(hk_streq(terminal, "a\r\n"));
}

static void
first_panic(void)
{
	panic("out of mbufs");
}

static void
second_panic(void)
{
	panic("and again");
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
	HK_CHECK(hk_catch(first_panic));
	HK_CHECK(hk_streq(console, "panic: out of mbufs\n"));
	HK_CHECK(panicstr != NULL);
	HK_CHECK(hk_streq(panicstr, "out of mbufs"));
	HK_CHECK(boot_calls == 1);
	HK_CHECK(boot_dev == rootdev);
	HK_CHECK(boot_howto == (RB_HALT | RB_DUMP));

	/* The second one keeps the first message and refuses to sync. */
	HK_CHECK(hk_catch(second_panic));
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
