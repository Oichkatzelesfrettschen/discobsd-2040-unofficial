/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * preempt: on-device oracle for the asynchronous system trap, the return
 * to a process through userret from a clock tick rather than from a system
 * call (sys/arch/rp2040/rp2040/systick.c, syscall.c). Every case runs a
 * loop that makes no system call, so only the clock tick can deliver its
 * signal or switch it out. tools/renode/check-preempt.py runs each case in
 * its own boot.
 *
 *   alarm   alarm(1), then loop: SIGALRM's default action ends the
 *           process, so the shell sees status 142.
 *   share   two looping children; the parent sleeps 3 s, which needs the
 *           tick to switch a child out, then signals both. Each child's
 *           handler records its count; a child that never ran before the
 *           signal reports zero.
 *   spin    loop until the interrupt character arrives (status 130).
 *   regs    two processes hold sentinels in r1-r12 and lr
 *           (preempt_regs.S), one with sp 8-byte aligned and one at 4
 *           modulo 8, across 5 ms SIGALRM handlers that overwrite r8-r12,
 *           until 200 handlers ran.
 *   divide  two processes divide their own operands on the SIO divider
 *           while their 3 ms handlers divide other operands, and the
 *           parent wakes every 2 ms to force switches between them; every
 *           quotient and remainder is compared with a shift-and-subtract
 *           division.
 *   divclean  two processes each leave their own operands and results in
 *           the SIO divider with DIRTY clear (the quotient read) and loop
 *           reading them back while a third process forces switches every
 *           2 ms; neither may ever read the other's values or lose its own.
 *   execdiv execs this program from a sequence that left the SIO divider
 *           DIRTY with known operands (preempt_regs.S); the new image,
 *           "execdiv-child", must find neither operand in the divider,
 *           which exec clears with the rest of the process state.
 *   altstack  a SIGUSR1 handler runs on an alternate stack of exactly
 *           MINSIGSTKSZ bytes whose base is 7 bytes past an eight-byte
 *           boundary, the worst rounding sendsig() applies to the top;
 *           the signal frame must stay inside it, so the guard bytes
 *           below the base keep their fill and the handler's sp lies
 *           within the stack.
 *
 * Each case prints "PREEMPT <case> OK" or a FAIL line; the exit status is
 * 0 only on OK.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	REGS_HITS	200
#define	DIVIDE_HITS	150
#define	DIVIDE_CAP	2000000UL

int	preempt_regs(int misalign);
int	preempt_exec_dirty(const char *, char **);
int	sigaltstack(const struct sigaltstack *, struct sigaltstack *);
volatile int preempt_done;
unsigned preempt_saved_sp;

static volatile unsigned long count;
static volatile unsigned long count_at_signal;
static volatile int hits;
static volatile int handler_bad;
static volatile unsigned clobber_sink;

static void
catch(int sig, void (*handler)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if (sigaction(sig, &sa, NULL) < 0) {
		perror("sigaction");
		exit(2);
	}
}

static void
interval(long usec)
{
	struct itimerval it;

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = usec;
	it.it_value = it.it_interval;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		perror("setitimer");
		exit(2);
	}
}

/* Restoring division, independent of the SIO divider. */
static unsigned
soft_udiv(unsigned a, unsigned b, unsigned *rem)
{
	unsigned q = 0, r = 0;
	int bit;

	for (bit = 31; bit >= 0; bit--) {
		r = (r << 1) | ((a >> bit) & 1);
		if (r >= b) {
			r -= b;
			q |= 1U << bit;
		}
	}
	*rem = r;
	return q;
}

static void __attribute__((noreturn))
spin_forever(const char *what)
{
	printf("PREEMPT %s start\n", what);
	fflush(stdout);
	for (;;)
		count++;
}

static void
share_handler(int sig)
{
	(void)sig;
	count_at_signal = count;
	preempt_done = 1;
}

static int
share_child(int n)
{
	catch(SIGUSR1, share_handler);
	while (!preempt_done)
		count++;
	printf("PREEMPT share child %d count %lu\n", n, count_at_signal);
	return count_at_signal != 0 ? 0 : 1;
}

static int
reap(pid_t pid, const char *what)
{
	int status;

	if (waitpid(pid, &status, 0) != pid) {
		printf("FAIL %s: waitpid\n", what);
		return 1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("FAIL %s: status %#x\n", what, status);
		return 1;
	}
	return 0;
}

static int
case_share(void)
{
	pid_t child[2];
	int i, fails = 0;

	fflush(stdout);
	for (i = 0; i < 2; i++) {
		child[i] = fork();
		if (child[i] < 0) {
			perror("fork");
			return 2;
		}
		if (child[i] == 0)
			_exit(share_child(i));
	}
	sleep(3);
	printf("PREEMPT share parent woke\n");
	fflush(stdout);
	for (i = 0; i < 2; i++)
		kill(child[i], SIGUSR1);
	for (i = 0; i < 2; i++)
		fails += reap(child[i], "share child");
	return fails;
}

/*
 * The handler overwrites r8-r12, which the interrupted loop holds
 * sentinels in: r8-r11 come back through the handler's own prologue and
 * epilogue, r12 only through the signal frame.
 */
static void
regs_handler(int sig)
{
	unsigned junk = 0xdeadbeefU ^ (unsigned)sig;

	__asm volatile (
	"	mov	r8, %0\n"
	"	mov	r9, %0\n"
	"	mov	r10, %0\n"
	"	mov	r11, %0\n"
	"	mov	ip, %0\n"
	    : : "l" (junk) : "r8", "r9", "r10", "r11", "ip");
	clobber_sink = junk / ((unsigned)hits + 3U);
	if (++hits >= REGS_HITS)
		preempt_done = 1;
}

static int
regs_run(int misalign)
{
	int r;

	catch(SIGALRM, regs_handler);
	interval(5000);
	r = preempt_regs(misalign);
	interval(0);
	if (r == 0) {
		printf("PREEMPT regs sp%%8=%d handlers %d ok\n",
		    misalign ? 4 : 0, hits);
		return 0;
	}
	if (r == 99)
		printf("FAIL regs sp%%8=%d: iteration cap, %d handlers ran\n",
		    misalign ? 4 : 0, hits);
	else
		printf("FAIL regs sp%%8=%d: register %d changed\n",
		    misalign ? 4 : 0, r == 14 ? 14 : r);
	return 1;
}

static int
case_regs(void)
{
	pid_t child;
	int fails;

	fflush(stdout);
	child = fork();
	if (child < 0) {
		perror("fork");
		return 2;
	}
	if (child == 0)
		_exit(regs_run(1));
	fails = regs_run(0);
	fails += reap(child, "regs child");
	return fails;
}

static unsigned handler_seed;

static void
divide_handler(int sig)
{
	unsigned a, b, q, r, eq, er;
	int i;

	(void)sig;
	for (i = 0; i < 4; i++) {
		handler_seed = handler_seed * 1103515245U + 12345U;
		a = handler_seed;
		b = (handler_seed >> 9) | 1U;
		q = a / b;
		r = a % b;
		eq = soft_udiv(a, b, &er);
		if (q != eq || r != er)
			handler_bad++;
	}
	if (++hits >= DIVIDE_HITS)
		preempt_done = 1;
}

static int
divide_run(unsigned seed)
{
	volatile unsigned a, b;
	unsigned q, r, eq, er, x = seed;
	unsigned long bad = 0, iter = 0;

	handler_seed = seed ^ 0x9e3779b9U;
	catch(SIGALRM, divide_handler);
	interval(3000);
	while (!preempt_done && iter < DIVIDE_CAP) {
		x = x * 1664525U + 1013904223U;
		a = x;
		b = (x >> (7 + (iter & 7))) | 1U;
		q = a / b;
		r = a % b;
		eq = soft_udiv(a, b, &er);
		if (q != eq || r != er)
			bad++;
		iter++;
	}
	interval(0);
	printf("PREEMPT divide seed %#x iterations %lu handlers %d "
	    "wrong %lu handler-wrong %d\n", seed, iter, hits, bad, handler_bad);
	return (preempt_done && bad == 0 && handler_bad == 0) ? 0 : 1;
}

static int
case_divide(void)
{
	pid_t child[2];
	struct timeval tv;
	int i, fails = 0, status;
	pid_t done;

	fflush(stdout);
	for (i = 0; i < 2; i++) {
		child[i] = fork();
		if (child[i] < 0) {
			perror("fork");
			return 2;
		}
		if (child[i] == 0)
			_exit(divide_run(i ? 0x2545f491U : 0x6a09e667U));
	}
	for (i = 0; i < 2; ) {
		tv.tv_sec = 0;
		tv.tv_usec = 2000;
		(void)select(0, NULL, NULL, NULL, &tv);
		done = waitpid(-1, &status, WNOHANG);
		if (done <= 0)
			continue;
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			printf("FAIL divide child %d: status %#x\n", done, status);
			fails++;
		}
		i++;
	}
	return fails;
}

#define	ALT_GUARD	71	/* guard bytes below the base; 71 % 8 == 7 */
#define	ALT_FILL	0xa5

static volatile unsigned alt_sp;
static volatile int alt_hits;

static void
alt_handler(int sig)
{
	register unsigned sp __asm__("sp");

	(void)sig;
	alt_sp = sp;
	alt_hits++;
}

/*
 * The stack lives in this frame, on the process stack: syscall() refuses a
 * system call whose sp lies inside the data segment, sigtramp's sigreturn
 * included, so a handler on a stack in .bss or the heap dies at its return
 * whatever the frame size.
 */
static int
case_altstack(void)
{
	unsigned char alt_area[ALT_GUARD + MINSIGSTKSZ + 8]
	    __attribute__((aligned(8)));
	volatile unsigned char *guard_bytes = alt_area;
	struct sigaltstack ss;
	unsigned base = (unsigned)alt_area + ALT_GUARD;
	int i, fails = 0, guard = 0;

	memset(alt_area, ALT_FILL, sizeof(alt_area));
	ss.ss_base = (char *)base;
	ss.ss_size = MINSIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		perror("sigaltstack");
		return 2;
	}
	{
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = alt_handler;
		sa.sa_flags = SA_ONSTACK;
		if (sigaction(SIGUSR1, &sa, NULL) < 0) {
			perror("sigaction");
			return 2;
		}
	}
	kill(getpid(), SIGUSR1);
	for (i = 0; i < ALT_GUARD; i++)
		if (guard_bytes[i] != ALT_FILL)
			guard++;
	printf("PREEMPT altstack size %d base %#x handler sp %#x "
	    "guard bytes written %d\n", MINSIGSTKSZ, base, alt_sp, guard);
	if (alt_hits != 1) {
		printf("FAIL altstack: handler ran %d times\n", alt_hits);
		fails++;
	}
	if (alt_sp < base || alt_sp >= base + MINSIGSTKSZ) {
		printf("FAIL altstack: handler sp outside the alternate stack\n");
		fails++;
	}
	if (guard != 0) {
		printf("FAIL altstack: the signal frame reached below ss_base\n");
		fails++;
	}
	return fails;
}

/* About 3 s of loop each in Renode: several schedcpu switches apiece. */
#define	DIVCLEAN_ITERATIONS	15000000UL

static volatile unsigned *const divreg = (volatile unsigned *)0xd0000060U;

/* One division with DIRTY left clear: the quotient read ends the sequence. */
static void
divclean_set(unsigned a, unsigned b)
{
	divreg[0] = a;
	divreg[1] = b;
	__asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop");
	(void)divreg[5];
	(void)divreg[4];
}

static int
divclean_run(const char *role, unsigned a, unsigned b, unsigned oa,
    unsigned ob)
{
	unsigned q, r, eq, er;
	unsigned long i, bad = 0, foreign = 0;

	eq = soft_udiv(a, b, &er);
	divclean_set(a, b);
	for (i = 0; i < DIVCLEAN_ITERATIONS; i++) {
		unsigned d0 = divreg[0], d1 = divreg[1];

		r = divreg[5];
		q = divreg[4];
		if (d0 != a || d1 != b || q != eq || r != er) {
			bad++;
			if (d0 == oa || d1 == ob)
				foreign++;
		}
	}
	printf("PREEMPT divclean %s changed %lu foreign %lu of %lu reads\n",
	    role, bad, foreign, DIVCLEAN_ITERATIONS);
	return bad != 0;
}

static int
case_divclean(void)
{
	pid_t child[2];
	struct timeval tv;
	int i, fails = 0;

	fflush(stdout);
	child[0] = fork();
	if (child[0] < 0) {
		perror("fork");
		return 2;
	}
	if (child[0] == 0) {
		for (i = 0; i < 1500; i++) {
			tv.tv_sec = 0;
			tv.tv_usec = 2000;
			(void)select(0, NULL, NULL, NULL, &tv);
		}
		_exit(0);
	}
	child[1] = fork();
	if (child[1] < 0) {
		perror("fork");
		return 2;
	}
	if (child[1] == 0)
		_exit(divclean_run("B", 0x00c0ffeeU, 0x77U, 0x0badf00dU, 0x1234U));
	fails += divclean_run("A", 0x0badf00dU, 0x1234U, 0x00c0ffeeU, 0x77U);
	fails += reap(child[1], "divclean B");
	fails += reap(child[0], "divclean waker");
	return fails;
}

static int
case_execdiv_child(void)
{
	volatile unsigned *div = (volatile unsigned *)0xd0000060U;
	unsigned dividend = div[0], divisor = div[1];

	printf("PREEMPT execdiv dividend %#x divisor %#x\n", dividend, divisor);
	if (dividend == 0x12345678U || divisor == 0x9abcU) {
		printf("FAIL execdiv: the old image's divider operands survived exec\n");
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int fails;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc != 2) {
		fprintf(stderr, "usage: preempt alarm|share|spin|regs|divide|altstack|execdiv|divclean\n");
		return 2;
	}
	if (strcmp(argv[1], "alarm") == 0) {
		alarm(1);
		spin_forever("alarm");
	}
	if (strcmp(argv[1], "spin") == 0)
		spin_forever("spin");
	if (strcmp(argv[1], "share") == 0)
		fails = case_share();
	else if (strcmp(argv[1], "regs") == 0)
		fails = case_regs();
	else if (strcmp(argv[1], "divide") == 0)
		fails = case_divide();
	else if (strcmp(argv[1], "altstack") == 0)
		fails = case_altstack();
	else if (strcmp(argv[1], "divclean") == 0)
		fails = case_divclean();
	else if (strcmp(argv[1], "execdiv") == 0) {
		static char *child_argv[] = { "/tmp/preempt", "execdiv-child", NULL };

		fflush(stdout);
		preempt_exec_dirty(child_argv[0], child_argv);
		printf("FAIL execdiv: execv failed\n");
		return 1;
	} else if (strcmp(argv[1], "execdiv-child") == 0) {
		fails = case_execdiv_child();
		argv[1] = "execdiv";
	}
	else {
		fprintf(stderr, "preempt: unknown case %s\n", argv[1]);
		return 2;
	}
	if (fails == 0)
		printf("PREEMPT %s OK\n", argv[1]);
	return fails ? 1 : 0;
}
