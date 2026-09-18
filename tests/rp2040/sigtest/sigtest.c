/*
 * sigtest: on-device oracle for signal delivery on the RP2040 port.
 *
 * sendsig (sys/arch/rp2040/rp2040/sig_machdep.c) builds a sigframe below
 * the interrupted stack pointer and the exception return writes an
 * eight-word hardware frame at the same address. This program checks
 * the four consequences a wrong layout produces:
 *
 *   1. the signal mask after a caught signal equals the mask before it
 *      (a frame that overlaps sc_mask restores the trampoline address);
 *   2. a system call interrupted by the signal returns -1 with EINTR
 *      (an overlap on sc_r0 returns the handler address instead);
 *   3. the handler runs on an eight-byte aligned stack (AAPCS 5.2.1.2);
 *   4. the sigcontext the handler receives carries the interrupted pc,
 *      sp and a Thumb xPSR (bit 24 set), and callee-saved registers
 *      survive the round trip.
 *
 * Prints one line per check and "SIGTEST OK" when every check passes;
 * exit status is the number of failures.
 */
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static volatile int handled;
static volatile unsigned handler_sp;
static volatile unsigned sc_pc, sc_sp, sc_psr;

static void
handler(int sig, int code, struct sigcontext *scp)
{
	register unsigned sp __asm__("sp");

	(void)code;
	handler_sp = sp;
	sc_pc = scp->sc_pc;
	sc_sp = scp->sc_sp;
	sc_psr = scp->sc_psr;
	handled = sig;
}

static int
report(const char *what, int ok, unsigned a, unsigned b)
{
	printf("%-28s %s (%#x %#x)\n", what, ok ? "ok" : "FAIL", a, b);
	return ok ? 0 : 1;
}

int
main(void)
{
	struct sigaction sa;
	int fails = 0, mask_before, mask_after, r;
	register unsigned keep7 __asm__("r7");
	unsigned marker = 0x5a5a1234u;

	/*
	 * The kernel calls the handler with the BSD three arguments while
	 * sa_handler is declared with one; the cast passes through void * so
	 * the compiler does not compare the two function types.
	 */
	sa.sa_handler = (sig_t)(void *)handler;
	sa.sa_mask = 0;
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, 0) < 0) {
		perror("sigaction");
		return 99;
	}

	mask_before = sigblock(0);
	keep7 = marker;
	alarm(1);
	errno = 0;
	r = pause();
	fails += report("pause returns -1", r == -1, (unsigned)r, 0);
	fails += report("errno is EINTR", errno == EINTR, errno, EINTR);
	fails += report("handler ran", handled == SIGALRM, handled, SIGALRM);
	mask_after = sigblock(0);
	fails += report("mask preserved", mask_after == mask_before,
	    mask_after, mask_before);
	fails += report("handler sp 8-aligned", (handler_sp & 7) == 0,
	    handler_sp, 0);
	fails += report("sc_psr is Thumb", (sc_psr & (1u << 24)) != 0,
	    sc_psr, 0);
	fails += report("sc_sp above handler sp", sc_sp > handler_sp,
	    sc_sp, handler_sp);
	fails += report("sc_pc in text", sc_pc != 0 && sc_pc != (unsigned)handler,
	    sc_pc, (unsigned)handler);
	fails += report("r7 survives signal", keep7 == marker, keep7, marker);
	if (fails == 0)
		printf("SIGTEST OK\n");
	return fails;
}
