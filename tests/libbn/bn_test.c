/*
 * Host test for libbn. usage: bn_test VECTORS
 *
 * VECTORS is the output of gen_vectors.py: operations on signed hexadecimal
 * operands with results computed by Python integers. The test includes the
 * implementation file (BN_SOURCE, default "bn.c") so that
 *
 *   - operands are built straight into the limb array, independent of
 *     BN_set_word and BN_mul_word;
 *   - results are read straight out of the limb array and checked for the
 *     representation invariants (top trimmed, zero never negative, top
 *     within the allocation);
 *   - malloc, realloc and free are replaced by counting wrappers, which
 *     lets the test fail the k-th allocation of an operation and assert
 *     that the operation reports failure, leaves its destination unchanged
 *     and leaks nothing.
 *
 * Each arithmetic vector runs with the result in a fresh dirty BIGNUM and
 * with every legal aliasing of result and operands, with and without a
 * BN_CTX. A failing check prints "FAIL <group>" and the run exits 1.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long live_allocs;
static long fail_at; /* fail the fail_at-th allocation from arming; 0 = off */
static long fail_hits;

static int
should_fail(void)
{
	if (fail_at == 0)
		return 0;
	if (--fail_at == 0) {
		fail_hits++;
		return 1;
	}
	return 0;
}

static void *
t_malloc(size_t n)
{
	void *p;

	if (should_fail())
		return NULL;
	p = malloc(n);
	if (p != NULL) {
		live_allocs++;
	}
	return p;
}

static void *
t_realloc(void *old, size_t n)
{
	void *p;

	if (should_fail())
		return NULL;
	p = realloc(old, n);
	if (p != NULL) {
		if (old == NULL)
			live_allocs++;
	}
	return p;
}

static void
t_free(void *p)
{
	if (p != NULL)
		live_allocs--;
	free(p);
}

#define malloc t_malloc
#define realloc t_realloc
#define free t_free

#ifndef BN_SOURCE
#define BN_SOURCE "bn.c"
#endif
#include BN_SOURCE

#undef malloc
#undef realloc
#undef free

static long checks, failures;
static const char *group = "none";
static unsigned long lineno;

static void
fail(const char *what, const char *got, const char *want)
{
	failures++;
	if (failures <= 20)
		printf("FAIL %s: %s (vector line %lu): got %.80s want %.80s\n",
		    group, what, lineno, got, want);
}

static void
check_str(const char *what, const char *got, const char *want)
{
	checks++;
	if (strcmp(got, want) != 0)
		fail(what, got, want);
}

static void
check_long(const char *what, long got, long want)
{
	char g[32], w[32];

	checks++;
	if (got != want) {
		(void)snprintf(g, sizeof(g), "%ld", got);
		(void)snprintf(w, sizeof(w), "%ld", want);
		fail(what, g, w);
	}
}

/* ---- direct access to the representation ------------------------------ */

/* Signed lowercase hex of a BIGNUM read from its limbs; also asserts the
 * representation invariants. The result is malloc'ed with the real malloc. */
static char *
to_hex(const BIGNUM *a)
{
	size_t i, len = 0;
	char *s = malloc(a->top * 4 + 3);

	if (s == NULL)
		abort();
	if (a->top > a->dmax)
		fail("invariant top <= dmax", "violated", "holds");
	if (a->top == 0 && a->neg != 0)
		fail("invariant zero is not negative", "negative zero", "zero");
	if (a->top != 0 && a->d[a->top - 1] == 0)
		fail("invariant top limb nonzero", "leading zero limb", "trimmed");
	if (a->neg != 0 && a->neg != 1)
		fail("invariant neg is 0 or 1", "other", "0 or 1");
	if (a->top == 0) {
		strcpy(s, "0");
		return s;
	}
	if (a->neg)
		s[len++] = '-';
	len += (size_t)snprintf(s + len, 5, "%x", a->d[a->top - 1]);
	for (i = a->top - 1; i-- > 0;)
		len += (size_t)snprintf(s + len, 5, "%04x", a->d[i]);
	return s;
}

/* A BIGNUM holding the signed hex value, built limb by limb without the
 * library's arithmetic. */
static BIGNUM *
from_hex(const char *hex)
{
	BIGNUM *a = BN_new();
	int neg = 0;
	size_t digits, nl, i, k;

	if (a == NULL)
		abort();
	if (*hex == '-') {
		neg = 1;
		hex++;
	}
	digits = strlen(hex);
	nl = (digits + 3) / 4;
	if (nl != 0 && !expand(a, nl))
		abort();
	for (i = 0; i < nl; i++) {
		unsigned v = 0;

		for (k = 0; k < 4; k++) {
			size_t pos = digits - i * 4;
			unsigned dgt;
			char c;

			if (k >= pos)
				break;
			c = hex[pos - 1 - k];
			dgt = (unsigned)(isdigit((unsigned char)c) ? c - '0' : c - 'a' + 10);
			v |= dgt << (4 * k);
		}
		a->d[i] = (limb_t)v;
	}
	settop(a, nl, neg);
	return a;
}

/* A destination pre-loaded with a long nonzero value, so a function that
 * forgets to clear stale limbs or the sign is caught. */
static BIGNUM *
dirty(void)
{
	return from_hex("-deadbeefcafe0123456789abcdef0011223344556677889900aabbccddeeff");
}

static char *
cur_hex(const BIGNUM *a)
{
	return to_hex(a);
}

static void
check_bn(const char *what, const BIGNUM *got, const char *want)
{
	char *g = cur_hex(got);

	check_str(what, g, want);
	free(g);
}

/* ---- vector operations ------------------------------------------------ */

/* Free each distinct BIGNUM of the list once; entries may repeat when a
 * test aliases arguments, and may be NULL. */
static void
free_unique(BIGNUM **x, int n)
{
	int i, j;

	for (i = 0; i < n; i++) {
		for (j = 0; j < i; j++)
			if (x[j] == x[i])
				break;
		if (j == i)
			BN_free(x[i]);
	}
}

static BN_CTX *shared_ctx;

static BN_CTX *
pick_ctx(unsigned variant)
{
	return (variant & 1) ? shared_ctx : NULL;
}

static void
op_binary(const char *op, const char *sa, const char *sb, const char *want)
{
	int same = strcmp(sa, sb) == 0;
	unsigned v;

	for (v = 0; v < 6; v++) {
		BIGNUM *a = from_hex(sa), *b, *r, *all[3];
		BN_CTX *ctx = pick_ctx(v);
		int ok;

		/* Variants 4 and 5 pass one BIGNUM as both operands, which
		 * is only legal when the vector's operands are equal. */
		if (same && v >= 4)
			b = a;
		else
			b = from_hex(sb);
		if (v == 2 || (v == 4 && same))
			r = a;
		else if (v == 3)
			r = b;
		else
			r = dirty();
		if (strcmp(op, "add") == 0)
			ok = BN_add(r, a, b);
		else if (strcmp(op, "sub") == 0)
			ok = BN_sub(r, a, b);
		else
			ok = BN_mul(r, a, b, ctx);
		check_long("operation returns 1", ok, 1);
		check_bn(op, r, want);
		all[0] = a;
		all[1] = b;
		all[2] = r;
		free_unique(all, 3);
	}
}

static void
op_div(const char *sa, const char *sd, const char *sq, const char *sr)
{
	unsigned v;

	for (v = 0; v < 12; v++) {
		BIGNUM *a = from_hex(sa), *d = from_hex(sd);
		BIGNUM *dv = NULL, *rem = NULL, *all[4];
		BN_CTX *ctx = pick_ctx(v);
		int ok;

		switch (v / 2) {
		case 0: /* separate quotient and remainder */
			dv = dirty();
			rem = dirty();
			ok = BN_div(dv, rem, a, d, ctx);
			break;
		case 1: /* remainder only through BN_mod */
			rem = dirty();
			ok = BN_mod(rem, a, d, ctx);
			break;
		case 2: /* quotient only */
			dv = dirty();
			ok = BN_div(dv, NULL, a, d, ctx);
			break;
		case 3: /* quotient replaces the dividend */
			dv = a;
			rem = dirty();
			ok = BN_div(dv, rem, a, d, ctx);
			break;
		case 4: /* quotient replaces the divisor */
			dv = d;
			rem = dirty();
			ok = BN_div(dv, rem, a, d, ctx);
			break;
		default: /* remainder replaces the dividend or the divisor */
			dv = dirty();
			rem = (v & 1) ? a : d;
			ok = BN_div(dv, rem, a, d, ctx);
			break;
		}
		check_long("division returns 1", ok, 1);
		if (dv != NULL)
			check_bn("quotient", dv, sq);
		if (rem != NULL)
			check_bn("remainder", rem, sr);
		all[0] = a;
		all[1] = d;
		all[2] = dv;
		all[3] = rem;
		free_unique(all, 4);
	}
}

static void
op_divz(const char *sa)
{
	BIGNUM *a = from_hex(sa), *z = from_hex("0");
	BIGNUM *dv = dirty(), *rem = dirty();
	char *dv0 = to_hex(dv), *rem0 = to_hex(rem);

	check_long("division by zero returns 0", BN_div(dv, rem, a, z, shared_ctx), 0);
	check_bn("quotient untouched by failed division", dv, dv0);
	check_bn("remainder untouched by failed division", rem, rem0);
	check_long("modulo by zero returns 0", BN_mod(rem, a, z, NULL), 0);
	free(dv0);
	free(rem0);
	BN_free(a);
	BN_free(z);
	BN_free(dv);
	BN_free(rem);
}

static void
op_exp(const char *sa, const char *sp, const char *want)
{
	unsigned v;

	for (v = 0; v < 6; v++) {
		BIGNUM *a = from_hex(sa), *p = from_hex(sp), *r, *all[3];
		BN_CTX *ctx = pick_ctx(v);

		r = (v / 2 == 0) ? dirty() : (v / 2 == 1) ? a : p;
		check_long("exp returns 1", BN_exp(r, a, p, ctx), 1);
		check_bn("exp", r, want);
		all[0] = a;
		all[1] = p;
		all[2] = r;
		free_unique(all, 3);
	}
}

static void
op_cmp(const char *sa, const char *sb, long c, long uc)
{
	BIGNUM *a = from_hex(sa), *b = from_hex(sb);

	check_long("BN_cmp", BN_cmp(a, b), c);
	check_long("BN_cmp reversed", BN_cmp(b, a), -c);
	check_long("BN_ucmp", BN_ucmp(a, b), uc);
	BN_free(a);
	BN_free(b);
}

static void
op_bits(const char *sa, long nbits, long nbytes)
{
	BIGNUM *a = from_hex(sa);

	check_long("BN_num_bits", BN_num_bits(a), nbits);
	check_long("BN_num_bytes", BN_num_bytes(a), nbytes);
	BN_free(a);
}

static void
op_bit(const char *sa, long n, long want)
{
	BIGNUM *a = from_hex(sa);

	check_long("BN_is_bit_set", BN_is_bit_set(a, (int)n), want);
	BN_free(a);
}

static void
op_mask(const char *sa, long n, const char *want)
{
	BIGNUM *a = from_hex(sa);
	char *before = to_hex(a);
	int ok = BN_mask_bits(a, (int)n);

	if (strcmp(want, "ERR") == 0) {
		check_long("BN_mask_bits fails", ok, 0);
		check_bn("BN_mask_bits failure leaves the value", a, before);
	} else {
		check_long("BN_mask_bits succeeds", ok, 1);
		check_bn("BN_mask_bits", a, want);
	}
	free(before);
	BN_free(a);
}

static void
op_rsh(const char *sa, long n, const char *want)
{
	BIGNUM *a = from_hex(sa), *r = dirty();

	check_long("BN_rshift returns 1", BN_rshift(r, a, (int)n), 1);
	check_bn("BN_rshift", r, want);
	check_long("BN_rshift in place", BN_rshift(a, a, (int)n), 1);
	check_bn("BN_rshift r == a", a, want);
	if (n == 1) {
		BN_free(a);
		a = from_hex(sa);
		check_long("BN_rshift1 returns 1", BN_rshift1(r, a), 1);
		check_bn("BN_rshift1", r, want);
	}
	BN_free(a);
	BN_free(r);
}

static BN_ULONG
parse_word(const char *hex)
{
	return (BN_ULONG)strtoul(hex, NULL, 16);
}

static void
op_words(const char *op, char **f)
{
	if (strcmp(op, "setword") == 0) {
		BIGNUM *a = dirty();
		BN_ULONG w = parse_word(f[0]);

		check_long("BN_set_word returns 1", BN_set_word(a, w), 1);
		check_bn("BN_set_word", a, f[0]);
		check_long("BN_get_word round trip", (long)BN_get_word(a), (long)w);
		BN_free(a);
	} else if (strcmp(op, "getword") == 0) {
		BIGNUM *a = from_hex(f[0]);

		check_long("BN_get_word", (long)BN_get_word(a), (long)parse_word(f[1]));
		BN_free(a);
	} else if (strcmp(op, "addw") == 0 || strcmp(op, "mulw") == 0) {
		BIGNUM *a = from_hex(f[0]);
		BN_ULONG w = parse_word(f[1]);
		int ok = (op[0] == 'a') ? BN_add_word(a, w) : BN_mul_word(a, w);

		check_long("word operation returns 1", ok, 1);
		check_bn(op, a, f[2]);
		BN_free(a);
	} else if (strcmp(op, "divw") == 0) {
		BIGNUM *a = from_hex(f[0]);
		BN_ULONG w = parse_word(f[1]);
		BN_ULONG rem = BN_div_word(a, w);

		check_bn("BN_div_word quotient", a, f[2]);
		check_long("BN_div_word remainder", (long)rem, (long)parse_word(f[3]));
		BN_free(a);
	}
}

static void
op_text(const char *op, const char *sa, const char *want)
{
	BIGNUM *a = from_hex(sa);
	char *s = (op[0] == 'd') ? BN_bn2dec(a) : BN_bn2hex(a);

	if (s == NULL) {
		fail("conversion returned NULL", "NULL", want);
	} else {
		check_str(op, s, want);
		t_free(s);
	}
	BN_free(a);
}

static void
op_pred(const char *sa, long z, long one, long neg)
{
	BIGNUM *a = from_hex(sa);
	BIGNUM *b = BN_dup(a);

	check_long("BN_is_zero", BN_is_zero(a), z);
	check_long("BN_is_one", BN_is_one(a), one);
	check_long("BN_is_negative", BN_is_negative(a), neg);
	check_bn("BN_dup", b, sa);
	BN_set_negative(b, 1);
	check_long("BN_set_negative(1) keeps zero positive", BN_is_negative(b), z ? 0 : 1);
	BN_set_negative(b, 0);
	check_long("BN_set_negative(0)", BN_is_negative(b), 0);
	BN_free(b);
	BN_free(a);
}

static int
split(char *line, char **f, int max)
{
	int n = 0;
	char *p = strtok(line, " \t\r\n");

	while (p != NULL && n < max) {
		f[n++] = p;
		p = strtok(NULL, " \t\r\n");
	}
	return n;
}

static long
num(const char *s)
{
	return strtol(s, NULL, 10);
}

static void
run_vector(char *line)
{
	char *f[8];
	int n = split(line, f, 8);
	const char *op;

	if (n == 0 || f[0][0] == '#')
		return;
	op = f[0];
	group = op;
	if ((strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
	    strcmp(op, "mul") == 0) && n == 4)
		op_binary(op, f[1], f[2], f[3]);
	else if (strcmp(op, "div") == 0 && n == 5)
		op_div(f[1], f[2], f[3], f[4]);
	else if (strcmp(op, "divz") == 0 && n == 2)
		op_divz(f[1]);
	else if (strcmp(op, "exp") == 0 && n == 4)
		op_exp(f[1], f[2], f[3]);
	else if (strcmp(op, "cmp") == 0 && n == 5)
		op_cmp(f[1], f[2], num(f[3]), num(f[4]));
	else if (strcmp(op, "bits") == 0 && n == 4)
		op_bits(f[1], num(f[2]), num(f[3]));
	else if (strcmp(op, "bit") == 0 && n == 4)
		op_bit(f[1], num(f[2]), num(f[3]));
	else if (strcmp(op, "mask") == 0 && n == 4)
		op_mask(f[1], num(f[2]), f[3]);
	else if (strcmp(op, "rsh") == 0 && n == 4)
		op_rsh(f[1], num(f[2]), f[3]);
	else if (strcmp(op, "setword") == 0 && n == 2)
		op_words(op, f + 1);
	else if ((strcmp(op, "getword") == 0 && n == 3) ||
	    (strcmp(op, "addw") == 0 && n == 4) ||
	    (strcmp(op, "mulw") == 0 && n == 4) ||
	    (strcmp(op, "divw") == 0 && n == 5))
		op_words(op, f + 1);
	else if ((strcmp(op, "dec") == 0 || strcmp(op, "hex") == 0) && n == 3)
		op_text(op, f[1], f[2]);
	else if (strcmp(op, "pred") == 0 && n == 5)
		op_pred(f[1], num(f[2]), num(f[3]), num(f[4]));
	else
		fail("malformed or unknown vector", op, "known operation");
}

/* ---- behavior outside the vector grammar ------------------------------ */

static void
direct_checks(void)
{
	BIGNUM *a = from_hex("-5"), *p = from_hex("-3"), *r = from_hex("7");
	BIGNUM *z = from_hex("0");

	group = "direct";
	lineno = 0;

	check_long("BN_exp with a negative exponent fails", BN_exp(r, a, p, NULL), 0);
	check_bn("BN_exp failure leaves the result", r, "7");
	check_long("BN_div_word by zero", (long)BN_div_word(a, 0), (long)(BN_ULONG)-1);
	check_bn("BN_div_word by zero leaves the value", a, "-5");
	check_long("BN_div with dv == rem fails", BN_div(r, r, a, p, NULL), 0);
	BN_set_negative(z, 1);
	check_long("BN_set_negative on zero", BN_is_negative(z), 0);
	check_long("BN_rshift with negative count fails", BN_rshift(r, a, -1), 0);
	check_long("BN_mask_bits negative count fails", BN_mask_bits(a, -1), 0);
	check_long("BN_is_bit_set negative index", BN_is_bit_set(a, -1), 0);
	check_long("BN_copy to self returns self", BN_copy(a, a) == a, 1);
	BN_free(NULL);
	BN_CTX_free(NULL);
	BN_free(a);
	BN_free(p);
	BN_free(r);
	BN_free(z);
}

/* Fail the k-th allocation of an operation for k = 1, 2, ... until the
 * operation succeeds. Every failing run must return the failure value,
 * leave its destinations unchanged and release everything it took. */
typedef int (*op_fn)(BIGNUM **v, BN_CTX *ctx);

static int
f_add(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_add(v[0], v[1], v[2]);
}

static int
f_sub_alias(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_sub(v[1], v[1], v[2]);
}

static int
f_mul(BIGNUM **v, BN_CTX *ctx)
{
	return BN_mul(v[0], v[1], v[2], ctx);
}

static int
f_mul_alias(BIGNUM **v, BN_CTX *ctx)
{
	return BN_mul(v[1], v[1], v[2], ctx);
}

static int
f_div(BIGNUM **v, BN_CTX *ctx)
{
	return BN_div(v[0], v[3], v[1], v[2], ctx);
}

static int
f_div_alias(BIGNUM **v, BN_CTX *ctx)
{
	return BN_div(v[1], v[3], v[1], v[2], ctx);
}

static int
f_exp(BIGNUM **v, BN_CTX *ctx)
{
	return BN_exp(v[0], v[1], v[4], ctx);
}

static int
f_mulw(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_mul_word(v[1], 0x12345u);
}

static int
f_addw(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_add_word(v[1], 0xFFFFFFFFu);
}

static int
f_divw(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_div_word(v[1], 1000000007u) != (BN_ULONG)-1;
}

static int
f_rsh(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_rshift(v[0], v[1], 7);
}

static int
f_copy(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_copy(v[0], v[1]) != NULL;
}

static int
f_setword(BIGNUM **v, BN_CTX *ctx)
{
	(void)ctx;
	return BN_set_word(v[5], 0xFFFFFFFFu);
}

static void
inject(const char *name, op_fn fn, int use_ctx, const char *sa, const char *sb)
{
	long k;

	group = name;
	for (k = 1; k < 200; k++) {
		BIGNUM *v[6];
		char *before[6];
		BN_CTX *ctx = NULL;
		int i, ok;
		long base = live_allocs;

		v[0] = from_hex("-123456789abcdef");
		v[1] = from_hex(sa);
		v[2] = from_hex(sb);
		v[3] = from_hex("55");
		v[4] = from_hex("a");
		v[5] = from_hex("0");
		for (i = 0; i < 6; i++)
			before[i] = to_hex(v[i]);
		if (use_ctx) {
			ctx = BN_CTX_new();
			if (ctx == NULL)
				abort();
		}
		fail_at = k;
		ok = fn(v, ctx);
		fail_at = 0;
		if (!ok) {
			for (i = 0; i < 6; i++) {
				char *now = to_hex(v[i]);

				check_str("destination unchanged after allocation failure",
				    now, before[i]);
				free(now);
			}
		}
		for (i = 0; i < 6; i++) {
			free(before[i]);
			BN_free(v[i]);
		}
		BN_CTX_free(ctx);
		check_long("allocation balance after operation", live_allocs, base);
		if (ok)
			break;
	}
	check_long("fault injection reached success", k < 200, 1);
}

static void
inject_text(void)
{
	long k;
	BIGNUM *a = from_hex("-123456789abcdef0123456789abcdef0123456789");

	group = "text-alloc";
	for (k = 1; k < 20; k++) {
		char *s;
		long base = live_allocs;

		fail_at = k;
		s = BN_bn2dec(a);
		fail_at = 0;
		if (s == NULL) {
			check_long("bn2dec failure leaks nothing", live_allocs, base);
		} else {
			t_free(s);
			break;
		}
	}
	for (k = 1; k < 20; k++) {
		char *s;
		long base = live_allocs;

		fail_at = k;
		s = BN_bn2hex(a);
		fail_at = 0;
		if (s == NULL) {
			check_long("bn2hex failure leaks nothing", live_allocs, base);
		} else {
			t_free(s);
			break;
		}
	}
	for (k = 1; k < 4; k++) {
		BIGNUM *d;
		long base = live_allocs;

		fail_at = k;
		d = BN_dup(a);
		fail_at = 0;
		if (d == NULL) {
			check_long("BN_dup failure leaks nothing", live_allocs, base);
		} else {
			BN_free(d);
			break;
		}
	}
	BN_free(a);
}

static void
fault_injection(void)
{
	const char *big = "-ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"
	    "123456789abcdef0123456789abcdef0123456789abcdef";
	const char *div = "8000fffe0001ffff1234";

	inject("alloc-add", f_add, 0, big, div);
	inject("alloc-sub-alias", f_sub_alias, 0, "ffffffffffffffffffffffffffffffff", "-1");
	inject("alloc-mul", f_mul, 0, big, div);
	inject("alloc-mul-ctx", f_mul, 1, big, div);
	inject("alloc-mul-alias", f_mul_alias, 0, big, div);
	inject("alloc-mul-alias-ctx", f_mul_alias, 1, big, div);
	inject("alloc-div", f_div, 0, big, div);
	inject("alloc-div-ctx", f_div, 1, big, div);
	inject("alloc-div-alias", f_div_alias, 1, big, div);
	inject("alloc-exp", f_exp, 0, big, div);
	inject("alloc-exp-ctx", f_exp, 1, big, div);
	inject("alloc-mulw", f_mulw, 0, big, div);
	inject("alloc-mulw-wide", f_mulw, 0, "ffffffffffffffff", div);
	inject("alloc-addw", f_addw, 0, "ffffffffffffffffffffffff", div);
	inject("alloc-divw", f_divw, 0, big, div);
	inject("alloc-rsh", f_rsh, 0, big, div);
	inject("alloc-copy", f_copy, 0, big, div);
	inject("alloc-setword", f_setword, 0, big, div);
	inject_text();
}

int
main(int argc, char **argv)
{
	FILE *fp;
	char *line;
	size_t cap = 1u << 20;
	unsigned long vectors = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: bn_test VECTORS\n");
		return 2;
	}
	fp = fopen(argv[1], "r");
	line = malloc(cap);
	if (fp == NULL || line == NULL) {
		perror(argv[1]);
		return 2;
	}
	shared_ctx = BN_CTX_new();
	if (shared_ctx == NULL)
		abort();
	while (fgets(line, (int)cap, fp) != NULL) {
		lineno++;
		if (line[0] != '#')
			vectors++;
		run_vector(line);
	}
	fclose(fp);
	free(line);
	direct_checks();
	fault_injection();
	BN_CTX_free(shared_ctx);
	check_long("live allocations at exit", live_allocs, 0);
	printf("bn_test: vectors=%lu checks=%ld failures=%ld injected_failures=%ld\n",
	    vectors, checks, failures, fail_hits);
	if (failures != 0)
		return 1;
	printf("PASS libbn\n");
	return 0;
}
