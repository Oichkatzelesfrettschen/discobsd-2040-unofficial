/*
 * fptest: bit-exact verification of the RP2040 bootrom float path.
 *
 * The lib/libc/arm/gen/rom_float_*.S members provide
 * __aeabi_fadd/fsub/fmul/fdiv, the double forms, and __aeabi_i2d through the
 * bootrom float library (see sys/arch/rp2040/doc/research/float-libs.md). This
 * exercises each operation and compares the result bit pattern against an
 * independently computed expected value, rather than an absolute-error tolerance whose
 * own subtraction would run the code under test. The expected values are
 * IEEE-754 round-to-nearest-even results computed on the host (see
 * gencorpus in the Makefile comment), which the bootrom reproduces exactly
 * for finite normal operands (datasheet 2.8.3.2.1). Signed zero is covered
 * because the comparison is on bits: +0.0 (0x00000000) and -0.0
 * (0x80000000) differ. Subnormal, NaN, infinity and rounding-boundary
 * coverage beyond these cases needs on-device characterization to lock the
 * bootrom-contract expected bits and is deferred (float-libs.md).
 *
 * Operands are copied into volatile locals before each operation so the
 * compiler emits a run-time __aeabi call instead of constant-folding the
 * table entry at build time, which would bypass the code under test.
 *
 * The last corpus checks the shipped difftime (lib/libc/gen/difftime.c),
 * which is the only libc member whose result depends on both helpers at
 * once: it converts each time_t endpoint through __aeabi_i2d and subtracts
 * through __aeabi_dsub. Its endpoints span the signed 32-bit range, where
 * the difference exceeds time_t, so an implementation that subtracts before
 * converting returns a wrapped result these bit patterns reject.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed int i32;

enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV };

struct scase { u32 a, b; int op; u32 expected; };
struct dcase { u64 a, b; int op; u64 expected; };
struct i2dcase { i32 input; u64 expected; };
struct dtcase { time_t end, beginning; u64 expected; };

static const struct scase singles[] = {
#include "corpus_single.h"
};
static const struct dcase doubles[] = {
#include "corpus_double.h"
};
static const struct i2dcase integers[] = {
	{ 0, 0x0000000000000000ULL },
	{ 1, 0x3ff0000000000000ULL },
	{ -1, 0xbff0000000000000ULL },
	{ (-2147483647 - 1), 0xc1e0000000000000ULL },
	{ 2147483647, 0x41dfffffffc00000ULL },
	{ 8388607, 0x415fffffc0000000ULL },
	{ 8388608, 0x4160000000000000ULL },
	{ 8388609, 0x4160000020000000ULL },
	{ 16777215, 0x416fffffe0000000ULL },
	{ 16777216, 0x4170000000000000ULL },
	{ 16777217, 0x4170000010000000ULL },
	{ 1073741824, 0x41d0000000000000ULL },
	{ -1073741824, 0xc1d0000000000000ULL },
};

/*
 * The last three endpoints differ by more than time_t holds: 2^31 and
 * 2^32-1 in both directions. Subtracting before converting wraps to -2^31
 * and to -1, whose bit patterns are 0xc1e0000000000000 and
 * 0xbff0000000000000, so each case names its own falsifier.
 */
static const struct dtcase difftimes[] = {
	{ 0, 0, 0x0000000000000000ULL },			/* +0.0 */
	{ 1, 0, 0x3ff0000000000000ULL },			/* +1.0 */
	{ 0, 1, 0xbff0000000000000ULL },			/* -1.0 */
	{ 0, (-2147483647L - 1), 0x41e0000000000000ULL },	/* 2^31 */
	{ 2147483647L, (-2147483647L - 1),
	    0x41efffffffe00000ULL },				/* 2^32-1 */
	{ (-2147483647L - 1), 2147483647L,
	    0xc1efffffffe00000ULL },				/* -(2^32-1) */
};

static float
f_from(u32 bits)
{
	float f;
	memcpy(&f, &bits, sizeof f);
	return f;
}
static u32
f_bits(float f)
{
	u32 bits;
	memcpy(&bits, &f, sizeof bits);
	return bits;
}
static double
d_from(u64 bits)
{
	double d;
	memcpy(&d, &bits, sizeof d);
	return d;
}
static u64
d_bits(double d)
{
	u64 bits;
	memcpy(&bits, &d, sizeof bits);
	return bits;
}

int
main(void)
{
	int i, n, pass, fails = 0;
	const char *opn[] = { "add", "sub", "mul", "div" };

	n = sizeof(singles) / sizeof(singles[0]);
	for (i = 0; i < n; i++) {
		volatile float a = f_from(singles[i].a);
		volatile float b = f_from(singles[i].b);
		float r = 0;
		u32 got;

		switch (singles[i].op) {
		case OP_ADD: r = a + b; break;
		case OP_SUB: r = a - b; break;
		case OP_MUL: r = a * b; break;
		case OP_DIV: r = a / b; break;
		}
		got = f_bits(r);
		if (got != singles[i].expected) {
			fails++;
			printf("single[%d] %s: a=%08x b=%08x got=%08x want=%08x\n",
			    i, opn[singles[i].op], singles[i].a, singles[i].b,
			    got, singles[i].expected);
		}
	}

	n = sizeof(doubles) / sizeof(doubles[0]);
	for (i = 0; i < n; i++) {
		volatile double a = d_from(doubles[i].a);
		volatile double b = d_from(doubles[i].b);
		double r = 0;
		u64 got;

		switch (doubles[i].op) {
		case OP_ADD: r = a + b; break;
		case OP_SUB: r = a - b; break;
		case OP_MUL: r = a * b; break;
		case OP_DIV: r = a / b; break;
		}
		got = d_bits(r);
		if (got != doubles[i].expected) {
			fails++;
			printf("double[%d] %s: a=%016llx b=%016llx got=%016llx want=%016llx\n",
			    i, opn[doubles[i].op], doubles[i].a, doubles[i].b,
			    got, doubles[i].expected);
		}
	}

	/* The first pass resolves the helper cell; the second uses its cache. */
	n = sizeof(integers) / sizeof(integers[0]);
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < n; i++) {
			volatile i32 input = integers[i].input;
			double result = (double)input;
			u64 got = d_bits(result);

			if (got != integers[i].expected) {
				fails++;
				printf("i2d[%d:%d]: input=%08x got=%016llx want=%016llx\n",
				    pass, i, (u32)integers[i].input, got,
				    integers[i].expected);
			}
		}
	}

	/*
	 * The loops above have resolved both helper cells, so these passes
	 * decide difftime's own arithmetic rather than the resolver.
	 */
	n = sizeof(difftimes) / sizeof(difftimes[0]);
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < n; i++) {
			volatile time_t end = difftimes[i].end;
			volatile time_t beginning = difftimes[i].beginning;
			u64 got = d_bits(difftime(end, beginning));

			if (got != difftimes[i].expected) {
				fails++;
				printf("difftime[%d:%d]: %ld,%ld got=%016llx want=%016llx\n",
				    pass, i, (long)difftimes[i].end,
				    (long)difftimes[i].beginning, got,
				    difftimes[i].expected);
			}
		}
	}

	if (fails == 0) {
		printf("FPTEST OK\n");
		return 0;
	}
	printf("FPTEST FAILED: %d mismatch(es)\n", fails);
	return 1;
}
