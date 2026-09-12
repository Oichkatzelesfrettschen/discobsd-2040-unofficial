/*
 * fptest: bit-exact verification of the RP2040 bootrom float path.
 *
 * lib/libc/arm/gen/rom_float.c routes __aeabi_fadd/fsub/fmul/fdiv and the
 * double forms through the bootrom float library (see
 * sys/arch/rp2040/doc/research/float-libs.md). This exercises each wrapped
 * operation and compares the result bit pattern against an independently
 * computed expected value, rather than an absolute-error tolerance whose
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
 */
#include <stdio.h>
#include <string.h>

typedef unsigned int u32;
typedef unsigned long long u64;

enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV };

struct scase { u32 a, b; int op; u32 expected; };
struct dcase { u64 a, b; int op; u64 expected; };

static const struct scase singles[] = {
#include "corpus_single.h"
};
static const struct dcase doubles[] = {
#include "corpus_double.h"
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
	int i, n, fails = 0;
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

	if (fails == 0) {
		printf("FPTEST OK\n");
		return 0;
	}
	printf("FPTEST FAILED: %d mismatch(es)\n", fails);
	return 1;
}
