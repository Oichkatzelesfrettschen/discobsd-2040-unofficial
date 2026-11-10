/*
 * lib/libc/stdlib/imaxdiv.c, imaxabs.c, strtoimax.c and strtoumax.c under
 * db_ names.  The / and % operators of the hosting compiler are the oracle
 * for the division, and the hosting library's strtoll and strtoull for the
 * conversions, which the wrappers reach through the tree's own members.
 */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

imaxdiv_t db_imaxdiv(intmax_t, intmax_t);
intmax_t db_imaxabs(intmax_t);
intmax_t db_strtoimax(const char *, char **, int);
uintmax_t db_strtoumax(const char *, char **, int);

static unsigned long checks;
static unsigned long failures;

static void
expect(int condition, const char *contract)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "inttypes test: %s\n", contract);
	}
}

static void
divide(intmax_t numerator, intmax_t denominator)
{
	imaxdiv_t result;

	/* The two undefined cases of C17 6.5.5p5 and p6 have no oracle. */
	if (denominator == 0 ||
	    (numerator == INTMAX_MIN && denominator == -1))
		return;
	result = db_imaxdiv(numerator, denominator);
	checks++;
	if (result.quot != numerator / denominator ||
	    result.rem != numerator % denominator) {
		failures++;
		fprintf(stderr, "imaxdiv(%lld, %lld): tree %lld rem %lld, "
		    "oracle %lld rem %lld\n", (long long)numerator,
		    (long long)denominator, (long long)result.quot,
		    (long long)result.rem, (long long)(numerator / denominator),
		    (long long)(numerator % denominator));
	}
}

/* Every pair of the values around zero, each word boundary and each limit. */
static void
boundary_division(void)
{
	static const intmax_t anchors[] = {
		0, 1, 2, 3, 7, 10, 255, 256, 65535, 65536, 65537,
		INT32_MAX - 1, INT32_MAX, (intmax_t)INT32_MAX + 1,
		(intmax_t)UINT32_MAX - 1, UINT32_MAX,
		(intmax_t)UINT32_MAX + 1, (intmax_t)UINT32_MAX + 2,
		INT64_C(0x0000ffffffffffff), INT64_C(0x0001000000000000),
		INT64_C(0x3fffffffffffffff), INT64_C(0x4000000000000000),
		INTMAX_MAX - 1, INTMAX_MAX
	};
	size_t left, right, count = sizeof(anchors) / sizeof(anchors[0]);

	for (left = 0; left < count; left++) {
		for (right = 0; right < count; right++) {
			divide(anchors[left], anchors[right]);
			divide(-anchors[left], anchors[right]);
			divide(anchors[left], -anchors[right]);
			divide(-anchors[left], -anchors[right]);
		}
		divide(INTMAX_MIN, anchors[left]);
		divide(INTMAX_MIN, -anchors[left]);
		divide(anchors[left], INTMAX_MIN);
		divide(-anchors[left], INTMAX_MIN);
	}
	divide(INTMAX_MIN, INTMAX_MIN);
}

/* The state stays in 32 bits, so every host walks the same corpus. */
static unsigned long
step(unsigned long state)
{
	return (state * 1103515245UL + 12345UL) & 0xffffffffUL;
}

/*
 * Operands of every bit length against each other, so the quotient ranges
 * from zero to the full width.
 */
static void
generated_division(void)
{
	unsigned long state = 88172645UL;
	uintmax_t numerator, denominator;
	int round;

	for (round = 0; round < 40000; round++) {
		state = step(state);
		numerator = state;
		state = step(state);
		numerator = numerator << 32 | state;
		state = step(state);
		denominator = state;
		state = step(state);
		denominator = denominator << 32 | state;
		state = step(state);
		numerator >>= (state >> 8) % 64;
		denominator >>= (state >> 16) % 64;
		divide((intmax_t)numerator, (intmax_t)denominator);
	}
}

static void
wrappers(void)
{
	static const char *const inputs[] = {
		"0", "-1", "  +42x", "9223372036854775807",
		"9223372036854775808", "-9223372036854775808",
		"-9223372036854775809", "18446744073709551615",
		"18446744073709551616", "0x7fffffffffffffff", "zz", ""
	};
	static const int bases[] = { 0, 10, 16, 36 };
	char *tree_end, *host_end;
	size_t input, base;
	intmax_t host_signed, tree_signed;
	uintmax_t host_unsigned, tree_unsigned;
	int host_errno;

	for (input = 0; input < sizeof(inputs) / sizeof(inputs[0]); input++) {
		for (base = 0; base < sizeof(bases) / sizeof(bases[0]); base++) {
			errno = 0;
			host_signed = strtoll(inputs[input], &host_end,
			    bases[base]);
			/* No digits: errno is the tree's contract, as in strtoll_test.c. */
			host_errno = host_end == inputs[input] ? 0 : errno;
			errno = 0;
			tree_signed = db_strtoimax(inputs[input], &tree_end,
			    bases[base]);
			expect(tree_signed == host_signed &&
			    tree_end == host_end && errno == host_errno,
			    "strtoimax matches the long long conversion");
			errno = 0;
			host_unsigned = strtoull(inputs[input], &host_end,
			    bases[base]);
			/* No digits: errno is the tree's contract, as in strtoll_test.c. */
			host_errno = host_end == inputs[input] ? 0 : errno;
			errno = 0;
			tree_unsigned = db_strtoumax(inputs[input], &tree_end,
			    bases[base]);
			expect(tree_unsigned == host_unsigned &&
			    tree_end == host_end && errno == host_errno,
			    "strtoumax matches the unsigned long long conversion");
		}
	}
	expect(db_imaxabs(0) == 0 && db_imaxabs(5) == 5 && db_imaxabs(-5) == 5,
	    "imaxabs of small values");
	expect(db_imaxabs(INTMAX_MAX) == INTMAX_MAX &&
	    db_imaxabs(-INTMAX_MAX) == INTMAX_MAX &&
	    db_imaxabs(INT64_C(-4294967296)) == INT64_C(4294967296),
	    "imaxabs above the word");
}

int
main(void)
{
	boundary_division();
	generated_division();
	wrappers();
	if (failures != 0) {
		fprintf(stderr, "inttypes test: %lu of %lu checks failed\n",
		    failures, checks);
		return 1;
	}
	printf("inttypes test: %lu checks passed\n", checks);
	return 0;
}
