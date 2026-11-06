/*
 * divtest: on-device oracle for the AEABI integer division helpers of
 * lib/libc/arm/gen/aeabi_div.S, which run on the SIO hardware divider
 * (RP2040 datasheet 2.3.1.5).
 *
 * Three checks, each printed on its own line:
 *
 *   1. corpus: edge operands (zero, one, powers of two and their
 *      neighbors, INT_MIN, INT_MAX, all-ones) divided with the C operators,
 *      which compile to the helpers, against a restoring division written
 *      here with no division operator. A divisor of zero and INT_MIN / -1
 *      are undefined in C and stay with the qemu-arm test
 *      (tests/rp2040/divider_helpers), which calls the helpers directly.
 *   2. collision: a loop divides continuously while SIGALRM fires every
 *      10 ms and its handler divides other operands. The kernel switches
 *      processes and delivers signals only on the return from a system
 *      call, so the loop issues one per pass of 32 divisions; a changed
 *      quotient or remainder falsifies the claim that no kernel path runs
 *      inside a divide sequence. The 100 Hz SysTick lands inside sequences
 *      throughout and runs no divider code.
 *   3. wait: the same store, store, branch-padding, load sequence with 0 to
 *      6 padding branches (two cycles each) against the quotient the loop
 *      computed independently. The shipped helpers pad with four branches;
 *      the line "wait: shipped padding" must report no wrong results, and
 *      the smallest error-free padding shows the margin to the 8-cycle
 *      contract.
 *
 * Prints "DIVTEST OK" when every check passes; the exit status is the
 * number of failed checks.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

typedef unsigned int u32;

#define SHIPPED_PADDING	4
#define MAX_PADDING	6

/* Restoring division; no operator the compiler lowers to a helper. */
static void
oracle_udivmod(u32 n, u32 d, u32 *quot, u32 *rem)
{
	u32 q = 0;
	u32 r = 0;
	int bit;

	for (bit = 31; bit >= 0; bit--) {
		r = (r << 1) | ((n >> bit) & 1u);
		if (r >= d) {
			r -= d;
			q |= 1u << bit;
		}
	}
	*quot = q;
	*rem = r;
}

static void
oracle_sdivmod(int n, int d, int *quot, int *rem)
{
	u32 mn = n < 0 ? 0u - (u32)n : (u32)n;
	u32 md = d < 0 ? 0u - (u32)d : (u32)d;
	u32 q;
	u32 r;

	oracle_udivmod(mn, md, &q, &r);
	*quot = (n < 0) != (d < 0) ? (int)(0u - q) : (int)q;
	*rem = n < 0 ? (int)(0u - r) : (int)r;
}

static u32 edges[200];
static int edge_count;

static void
add_edge(u32 value)
{
	int i;

	for (i = 0; i < edge_count; i++)
		if (edges[i] == value)
			return;
	if (edge_count < (int)(sizeof(edges) / sizeof(edges[0])))
		edges[edge_count++] = value;
}

static void
build_edges(void)
{
	static const u32 fixed[] = {
		0u, 1u, 2u, 3u, 7u, 10u, 100u, 12345u, 65535u, 65536u,
		1000000007u, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffffffu,
		0x55555555u, 0xaaaaaaaau, 0xdeadbeefu,
	};
	unsigned int i;
	unsigned int p;

	for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
		add_edge(fixed[i]);
		add_edge(0u - fixed[i]);
	}
	for (p = 0; p < 32u; p++) {
		add_edge(1u << p);
		add_edge((1u << p) - 1u);
		add_edge((1u << p) + 1u);
		add_edge(0u - (1u << p));
	}
}

static int
check_corpus(void)
{
	volatile u32 va;
	volatile u32 vb;
	int bad = 0;
	int i;
	int j;

	build_edges();
	for (i = 0; i < edge_count; i++) {
		for (j = 0; j < edge_count; j++) {
			u32 a = edges[i];
			u32 b = edges[j];
			u32 uq;
			u32 ur;
			int sq;
			int sr;
			int wq;
			int wr;
			u32 gq;
			u32 gr;

			if (b == 0u)
				continue;
			va = a;
			vb = b;
			oracle_udivmod(a, b, &uq, &ur);
			gq = va / vb;
			gr = va % vb;
			if (gq != uq || gr != ur)
				bad++;
			if ((int)a == (-2147483647 - 1) && (int)b == -1)
				continue;
			oracle_sdivmod((int)a, (int)b, &sq, &sr);
			wq = (int)va / (int)vb;
			wr = (int)va % (int)vb;
			if (wq != sq || wr != sr)
				bad++;
		}
	}
	printf("corpus: %d values, %d wrong\n", edge_count, bad);
	return bad != 0;
}

static volatile u32 handler_ticks;
static volatile u32 handler_bad;

static void
on_alarm(int sig, int code, struct sigcontext *scp)
{
	volatile u32 a = 999999937u;
	volatile u32 b = 13u;
	volatile int sa = -2000000011;
	volatile int sb = 97;

	(void)sig;
	(void)code;
	(void)scp;
	if (a / b != 76923072u || a % b != 1u)
		handler_bad++;
	if (sa / sb != -20618556 || sa % sb != -79)
		handler_bad++;
	handler_ticks++;
}

static int
check_collision(void)
{
	struct sigaction sa;
	struct itimerval it;
	volatile u32 va;
	volatile u32 vb;
	u32 i;
	u32 wrong = 0;
	u32 lcg = 12345u;

	/* The kernel passes the BSD three arguments; see sigtest.c. */
	sa.sa_handler = (sig_t)(void *)on_alarm;
	sa.sa_mask = 0;
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, 0) != 0) {
		printf("collision: sigaction failed\n");
		return 1;
	}
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 10000;
	it.it_value = it.it_interval;
	if (setitimer(ITIMER_REAL, &it, NULL) != 0) {
		printf("collision: setitimer failed\n");
		return 1;
	}
	for (i = 0; i < 3000000u && handler_ticks < 100u; i++) {
		u32 q;
		u32 r;

		lcg = lcg * 1664525u + 1013904223u;
		va = lcg;
		vb = (lcg >> 20) | 1u;
		q = va / vb;
		r = va % vb;
		if (r >= vb || q * vb + r != va)
			wrong++;
		if ((i & 31u) == 0u)
			(void)getpid();
	}
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = 0;
	it.it_interval = it.it_value;
	(void)setitimer(ITIMER_REAL, &it, NULL);
	printf("collision: %u divisions, %u alarms, %u wrong, %u wrong in handler\n",
	    (unsigned)i, (unsigned)handler_ticks, (unsigned)wrong,
	    (unsigned)handler_bad);
	return wrong != 0u || handler_bad != 0u || handler_ticks == 0u;
}

#define SIO_BASE	0xd0000000u

/*
 * One divider sequence with the given number of padding branches between
 * the divisor store and the quotient load, as a separate function per count
 * so the padding is straight-line code.
 */
#define PROBE(k, pad) \
static u32 \
probe##k(u32 a, u32 b) \
{ \
	register u32 sio __asm__("r3") = SIO_BASE; \
	register u32 ra __asm__("r0") = a; \
	register u32 rb __asm__("r1") = b; \
	u32 q; \
	__asm__ volatile( \
	    "str %1, [%2, #0x60]\n\t" \
	    "str %3, [%2, #0x64]\n\t" \
	    pad \
	    "ldr %0, [%2, #0x70]\n\t" \
	    : "=&l"(q) : "l"(ra), "l"(sio), "l"(rb) : "memory"); \
	return q; \
}

#define P1 "b 1f\n1:\n\t"
PROBE(0, "")
PROBE(1, P1)
PROBE(2, P1 P1)
PROBE(3, P1 P1 P1)
PROBE(4, P1 P1 P1 P1)
PROBE(5, P1 P1 P1 P1 P1)
PROBE(6, P1 P1 P1 P1 P1 P1)

static u32 (*const probes[MAX_PADDING + 1])(u32, u32) = {
	probe0, probe1, probe2, probe3, probe4, probe5, probe6,
};

static int
check_wait(void)
{
	u32 wrong[MAX_PADDING + 1];
	u32 lcg = 99u;
	int k;
	int first_good = -1;
	u32 i;

	for (k = 0; k <= MAX_PADDING; k++)
		wrong[k] = 0;
	for (i = 0; i < 2000u; i++) {
		u32 a;
		u32 b;
		u32 q;
		u32 r;

		lcg = lcg * 1664525u + 1013904223u;
		a = lcg;
		lcg = lcg * 1664525u + 1013904223u;
		b = (lcg >> (lcg & 31u)) | 1u;
		oracle_udivmod(a, b, &q, &r);
		for (k = 0; k <= MAX_PADDING; k++)
			if (probes[k](a, b) != q)
				wrong[k]++;
	}
	for (k = 0; k <= MAX_PADDING; k++) {
		printf("wait: %d padding branches (%d cycles): %u wrong of 2000\n",
		    k, 2 * k, (unsigned)wrong[k]);
		if (first_good < 0 && wrong[k] == 0u)
			first_good = k;
	}
	printf("wait: smallest error-free padding %d, shipped padding %d\n",
	    first_good, SHIPPED_PADDING);
	return wrong[SHIPPED_PADDING] != 0u;
}

int
main(void)
{
	int fails = 0;

	fails += check_corpus();
	fails += check_collision();
	fails += check_wait();
	if (fails == 0)
		printf("DIVTEST OK\n");
	else
		printf("DIVTEST FAIL: %d checks failed\n", fails);
	return fails;
}
