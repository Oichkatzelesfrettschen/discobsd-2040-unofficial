/*
 * libbn implementation; see bn.h for the interface and its conventions.
 *
 * A BIGNUM stores its magnitude as little-endian 16-bit limbs. The width
 * follows from the Cortex-M0+ (ARMv6-M): MULS yields only the low 32 bits
 * of a product and there is no hardware divide, so a 16x16 product and a
 * 32/16 quotient each fit one native 32-bit operation, while a 32-bit limb
 * would need 64-bit intermediates through libgcc (__aeabi_lmul and
 * __aeabi_uldivmod) in every inner loop. All limb arithmetic is carried out
 * in uint32_t; a promoted 16-bit operand is always cast before a multiply,
 * since a uint16_t product can overflow int.
 *
 * Algorithms: schoolbook addition, subtraction and multiplication; Knuth
 * volume 2, section 4.3.1 Algorithm D for division by a divisor of two or
 * more limbs; short division for one limb; left-to-right square and
 * multiply for exponentiation; decimal conversion by repeated division by
 * 10000, the largest power of ten below the limb base.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bn.h"

_Static_assert(sizeof(BN_ULONG) == 4, "BN_ULONG is a 32-bit word");
_Static_assert(INT_MAX >= 0x7fffffff, "int holds a bit count of 2^31");

typedef uint16_t limb_t;

#define LIMB_BITS 16
#define LIMB_MASK 0xFFFFu
/* Largest limb count whose bit length still fits an int. */
#define BN_MAXLIMBS ((size_t)(INT_MAX / LIMB_BITS) - 2)
/* Spare limbs added when storage grows, so a digit-at-a-time loop such as
 * dc's number reader reallocates once per several digits. */
#define BN_SLACK 4

struct bignum_st {
	limb_t *d;	/* d[0..top-1], least significant first */
	size_t top;	/* limbs in use; d[top-1] != 0, and top == 0 is zero */
	size_t dmax;	/* limbs allocated */
	int neg;	/* 1 only when top != 0 */
};

struct bignum_ctx {
	limb_t *buf;	/* scratch reused by BN_mul and BN_div */
	size_t cap;	/* limbs in buf */
};

/* ---- storage ---------------------------------------------------------- */

static size_t
trim(const limb_t *d, size_t n)
{
	while (n > 0 && d[n - 1] == 0)
		n--;
	return n;
}

/* Set the length and sign of r from limbs already stored in r->d. */
static void
settop(BIGNUM *r, size_t n, int neg)
{
	r->top = trim(r->d, n);
	r->neg = (r->top != 0 && neg != 0);
}

static int
expand(BIGNUM *a, size_t n)
{
	limb_t *p;
	size_t m;

	if (n <= a->dmax)
		return 1;
	if (n > BN_MAXLIMBS)
		return 0;
	m = n + BN_SLACK;
	if (m > BN_MAXLIMBS)
		m = BN_MAXLIMBS;
	p = realloc(a->d, m * sizeof(*p));
	if (p == NULL)
		return 0;
	a->d = p;
	a->dmax = m;
	return 1;
}

static limb_t *
scratch_get(BN_CTX *ctx, size_t n)
{
	limb_t *p;

	if (ctx == NULL)
		return malloc(n * sizeof(limb_t));
	if (n > ctx->cap) {
		p = realloc(ctx->buf, n * sizeof(limb_t));
		if (p == NULL)
			return NULL;
		ctx->buf = p;
		ctx->cap = n;
	}
	return ctx->buf;
}

static void
scratch_put(BN_CTX *ctx, limb_t *p)
{
	if (ctx == NULL)
		free(p);
}

BN_CTX *
BN_CTX_new(void)
{
	BN_CTX *c;

	c = malloc(sizeof(*c));
	if (c != NULL) {
		c->buf = NULL;
		c->cap = 0;
	}
	return c;
}

void
BN_CTX_free(BN_CTX *c)
{
	if (c != NULL) {
		free(c->buf);
		free(c);
	}
}

BIGNUM *
BN_new(void)
{
	BIGNUM *a;

	a = malloc(sizeof(*a));
	if (a != NULL) {
		a->d = NULL;
		a->top = 0;
		a->dmax = 0;
		a->neg = 0;
	}
	return a;
}

void
BN_free(BIGNUM *a)
{
	if (a != NULL) {
		free(a->d);
		free(a);
	}
}

BIGNUM *
BN_copy(BIGNUM *to, const BIGNUM *from)
{
	if (to == from)
		return to;
	if (!expand(to, from->top))
		return NULL;
	if (from->top != 0)
		memcpy(to->d, from->d, from->top * sizeof(limb_t));
	to->top = from->top;
	to->neg = from->neg;
	return to;
}

BIGNUM *
BN_dup(const BIGNUM *from)
{
	BIGNUM *a;

	a = BN_new();
	if (a != NULL && BN_copy(a, from) == NULL) {
		BN_free(a);
		a = NULL;
	}
	return a;
}

/* ---- words, sign, comparison ------------------------------------------ */

int
BN_set_word(BIGNUM *a, BN_ULONG w)
{
	if (!expand(a, 2))
		return 0;
	a->d[0] = (limb_t)(w & LIMB_MASK);
	a->d[1] = (limb_t)(w >> LIMB_BITS);
	settop(a, 2, 0);
	return 1;
}

int
BN_one(BIGNUM *a)
{
	return BN_set_word(a, 1);
}

BN_ULONG
BN_get_word(const BIGNUM *a)
{
	if (a->top > 2)
		return (BN_ULONG)-1;
	if (a->top == 2)
		return ((BN_ULONG)a->d[1] << LIMB_BITS) | a->d[0];
	if (a->top == 1)
		return a->d[0];
	return 0;
}

int
BN_is_zero(const BIGNUM *a)
{
	return a->top == 0;
}

int
BN_is_one(const BIGNUM *a)
{
	return a->neg == 0 && a->top == 1 && a->d[0] == 1;
}

int
BN_is_negative(const BIGNUM *a)
{
	return a->neg;
}

void
BN_set_negative(BIGNUM *a, int n)
{
	a->neg = (a->top != 0 && n != 0);
}

/* Compare normalized magnitudes. */
static int
mcmp(const limb_t *a, size_t na, const limb_t *b, size_t nb)
{
	if (na != nb)
		return na > nb ? 1 : -1;
	while (na-- > 0) {
		if (a[na] != b[na])
			return a[na] > b[na] ? 1 : -1;
	}
	return 0;
}

int
BN_ucmp(const BIGNUM *a, const BIGNUM *b)
{
	return mcmp(a->d, a->top, b->d, b->top);
}

int
BN_cmp(const BIGNUM *a, const BIGNUM *b)
{
	int c;

	if (a->neg != b->neg)
		return a->neg ? -1 : 1;
	c = mcmp(a->d, a->top, b->d, b->top);
	return a->neg ? -c : c;
}

/* ---- bits ------------------------------------------------------------- */

static unsigned
bitlen(uint32_t x)
{
	unsigned n = 0;

	while (x != 0) {
		n++;
		x >>= 1;
	}
	return n;
}

int
BN_num_bits(const BIGNUM *a)
{
	if (a->top == 0)
		return 0;
	return (int)((a->top - 1) * LIMB_BITS + bitlen(a->d[a->top - 1]));
}

int
BN_is_bit_set(const BIGNUM *a, int n)
{
	size_t w;

	if (n < 0)
		return 0;
	w = (size_t)n / LIMB_BITS;
	if (w >= a->top)
		return 0;
	return (a->d[w] >> ((unsigned)n % LIMB_BITS)) & 1;
}

/* Keep the low n bits of the magnitude. Fails for n < 0 and for a value
 * with fewer than n bits, the manual page's "already shorter than n". */
int
BN_mask_bits(BIGNUM *a, int n)
{
	size_t w;
	unsigned b;

	if (n < 0 || BN_num_bits(a) < n)
		return 0;
	w = (size_t)n / LIMB_BITS;
	b = (unsigned)n % LIMB_BITS;
	if (b != 0) {
		a->d[w] &= (limb_t)((1u << b) - 1u);
		w++;
	}
	settop(a, w, a->neg);
	return 1;
}

/* r = a >> n on the magnitude, sign kept. Reads run ahead of writes, so r
 * may be a. */
int
BN_rshift(BIGNUM *r, const BIGNUM *a, int n)
{
	size_t ls, i, nt;
	unsigned bs;
	int neg = a->neg;

	if (n < 0)
		return 0;
	ls = (size_t)n / LIMB_BITS;
	bs = (unsigned)n % LIMB_BITS;
	if (ls >= a->top) {
		r->top = 0;
		r->neg = 0;
		return 1;
	}
	nt = a->top - ls;
	if (!expand(r, nt))
		return 0;
	for (i = 0; i < nt; i++) {
		uint32_t lo = a->d[i + ls];
		uint32_t hi = (i + ls + 1 < a->top) ? a->d[i + ls + 1] : 0;

		r->d[i] = (limb_t)(((hi << LIMB_BITS) | lo) >> bs);
	}
	settop(r, nt, neg);
	return 1;
}

int
BN_rshift1(BIGNUM *r, const BIGNUM *a)
{
	return BN_rshift(r, a, 1);
}

/* ---- addition and subtraction ----------------------------------------- */

/* r = a + b for na >= nb; r has room for na + 1 limbs and may be a or b. */
static size_t
madd(limb_t *r, const limb_t *a, size_t na, const limb_t *b, size_t nb)
{
	uint32_t c = 0, t;
	size_t i;

	for (i = 0; i < nb; i++) {
		t = (uint32_t)a[i] + b[i] + c;
		r[i] = (limb_t)t;
		c = t >> LIMB_BITS;
	}
	for (; i < na; i++) {
		t = (uint32_t)a[i] + c;
		r[i] = (limb_t)t;
		c = t >> LIMB_BITS;
	}
	r[na] = (limb_t)c;
	return na + 1;
}

/* r = a - b for a >= b as magnitudes; r may be a or b. */
static size_t
msub(limb_t *r, const limb_t *a, size_t na, const limb_t *b, size_t nb)
{
	uint32_t borrow = 0, t;
	size_t i;

	for (i = 0; i < nb; i++) {
		t = (uint32_t)a[i] - b[i] - borrow;
		r[i] = (limb_t)t;
		borrow = t >> 31;
	}
	for (; i < na; i++) {
		t = (uint32_t)a[i] - borrow;
		r[i] = (limb_t)t;
		borrow = t >> 31;
	}
	return na;
}

/* r = a + b with explicit signs. The caller has expanded r to
 * max(an, bn) + 1 limbs before taking the operand pointers, because the
 * expansion may move the storage of an operand that is r itself. */
static void
addsub(BIGNUM *r, const limb_t *ad, size_t an, int aneg,
    const limb_t *bd, size_t bn, int bneg)
{
	size_t n;
	int c;

	if (aneg == bneg) {
		if (an >= bn)
			n = madd(r->d, ad, an, bd, bn);
		else
			n = madd(r->d, bd, bn, ad, an);
		settop(r, n, aneg);
		return;
	}
	c = mcmp(ad, an, bd, bn);
	if (c == 0) {
		r->top = 0;
		r->neg = 0;
	} else if (c > 0) {
		n = msub(r->d, ad, an, bd, bn);
		settop(r, n, aneg);
	} else {
		n = msub(r->d, bd, bn, ad, an);
		settop(r, n, bneg);
	}
}

int
BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
	size_t n = a->top > b->top ? a->top : b->top;

	if (!expand(r, n + 1))
		return 0;
	addsub(r, a->d, a->top, a->neg, b->d, b->top, b->neg);
	return 1;
}

int
BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
	size_t n = a->top > b->top ? a->top : b->top;

	if (!expand(r, n + 1))
		return 0;
	addsub(r, a->d, a->top, a->neg, b->d, b->top, !b->neg);
	return 1;
}

/* Split a word into the limbs of its magnitude; returns the limb count. */
static size_t
wordlimbs(limb_t wd[2], BN_ULONG w)
{
	wd[0] = (limb_t)(w & LIMB_MASK);
	wd[1] = (limb_t)(w >> LIMB_BITS);
	return trim(wd, 2);
}

int
BN_add_word(BIGNUM *a, BN_ULONG w)
{
	limb_t wd[2];
	size_t wn = wordlimbs(wd, w);
	size_t n = a->top > wn ? a->top : wn;

	if (!expand(a, n + 1))
		return 0;
	addsub(a, a->d, a->top, a->neg, wd, wn, 0);
	return 1;
}

/* ---- multiplication --------------------------------------------------- */

/* r = a * b, nb <= na, r has na + nb limbs and aliases neither operand. */
static void
mmul(limb_t *r, const limb_t *a, size_t na, const limb_t *b, size_t nb)
{
	size_t i, j;

	memset(r, 0, (na + nb) * sizeof(*r));
	for (i = 0; i < nb; i++) {
		uint32_t bi = b[i], carry = 0, t;

		if (bi == 0)
			continue;
		for (j = 0; j < na; j++) {
			t = (uint32_t)a[j] * bi + r[i + j] + carry;
			r[i + j] = (limb_t)t;
			carry = t >> LIMB_BITS;
		}
		r[i + na] = (limb_t)carry;
	}
}

/* r = a * b with explicit signs. A product whose destination shares
 * storage with an operand is formed in scratch first. */
static int
mulraw(BIGNUM *r, const limb_t *ad, size_t an, int aneg,
    const limb_t *bd, size_t bn, int bneg, BN_CTX *ctx)
{
	const limb_t *t;
	limb_t *s;
	size_t n, tn;
	int neg = (aneg != bneg);

	if (an == 0 || bn == 0) {
		r->top = 0;
		r->neg = 0;
		return 1;
	}
	if (an < bn) {
		t = ad; ad = bd; bd = t;
		tn = an; an = bn; bn = tn;
	}
	n = an + bn;
	if (n > BN_MAXLIMBS)
		return 0;
	if (r->d != NULL && (r->d == ad || r->d == bd)) {
		s = scratch_get(ctx, n);
		if (s == NULL)
			return 0;
		mmul(s, ad, an, bd, bn);
		if (!expand(r, n)) {
			scratch_put(ctx, s);
			return 0;
		}
		memcpy(r->d, s, n * sizeof(*s));
		scratch_put(ctx, s);
	} else {
		if (!expand(r, n))
			return 0;
		mmul(r->d, ad, an, bd, bn);
	}
	settop(r, n, neg);
	return 1;
}

int
BN_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx)
{
	return mulraw(r, a->d, a->top, a->neg, b->d, b->top, b->neg, ctx);
}

int
BN_mul_word(BIGNUM *a, BN_ULONG w)
{
	limb_t wd[2];
	size_t wn, i;
	uint32_t carry = 0, t;

	if (w == 0 || a->top == 0) {
		a->top = 0;
		a->neg = 0;
		return 1;
	}
	if (w > LIMB_MASK) {
		wn = wordlimbs(wd, w);
		return mulraw(a, a->d, a->top, a->neg, wd, wn, 0, NULL);
	}
	if (!expand(a, a->top + 1))
		return 0;
	for (i = 0; i < a->top; i++) {
		t = (uint32_t)a->d[i] * w + carry;
		a->d[i] = (limb_t)t;
		carry = t >> LIMB_BITS;
	}
	a->d[a->top] = (limb_t)carry;
	settop(a, a->top + 1, a->neg);
	return 1;
}

/* ---- division --------------------------------------------------------- */

/* q = a / w and the remainder, for w in 1..0xFFFF; q may be a. */
static uint32_t
mdiv1(limb_t *q, const limb_t *a, size_t na, uint32_t w)
{
	uint32_t rem = 0, cur;
	size_t i;

	for (i = na; i-- > 0;) {
		cur = (rem << LIMB_BITS) | a[i];
		q[i] = (limb_t)(cur / w);
		rem = cur % w;
	}
	return rem;
}

/*
 * Algorithm D. d has nd >= 2 limbs with d[nd-1] != 0, a has na >= nd limbs.
 * u holds na + 1 limbs and v holds nd limbs of working storage. The
 * quotient fills q[0..na-nd] and the remainder fills rem[0..nd-1].
 */
static void
mdivrem(limb_t *q, limb_t *rem, limb_t *u, limb_t *v,
    const limb_t *a, size_t na, const limb_t *d, size_t nd)
{
	unsigned s = LIMB_BITS - bitlen(d[nd - 1]);
	size_t i, j;
	uint32_t num, qh, rh, mulc, brw, sub, ui;

	/* Normalize so the divisor's top limb has its high bit set. */
	for (i = nd - 1; i > 0; i--)
		v[i] = (limb_t)((((uint32_t)d[i] << s) |
		    ((uint32_t)d[i - 1] >> (LIMB_BITS - s))) & LIMB_MASK);
	v[0] = (limb_t)(((uint32_t)d[0] << s) & LIMB_MASK);
	u[na] = (limb_t)((uint32_t)a[na - 1] >> (LIMB_BITS - s));
	for (i = na - 1; i > 0; i--)
		u[i] = (limb_t)((((uint32_t)a[i] << s) |
		    ((uint32_t)a[i - 1] >> (LIMB_BITS - s))) & LIMB_MASK);
	u[0] = (limb_t)(((uint32_t)a[0] << s) & LIMB_MASK);

	for (j = na - nd + 1; j-- > 0;) {
		/* Estimate the quotient limb from the top two limbs, then
		 * tighten it against the divisor's second limb. */
		num = ((uint32_t)u[j + nd] << LIMB_BITS) | u[j + nd - 1];
		qh = num / v[nd - 1];
		rh = num % v[nd - 1];
		while (qh > LIMB_MASK ||
		    qh * v[nd - 2] > ((rh << LIMB_BITS) | u[j + nd - 2])) {
			qh--;
			rh += v[nd - 1];
			if (rh > LIMB_MASK)
				break;
		}

		/* u[j..j+nd] -= qh * v */
		mulc = 0;
		brw = 0;
		for (i = 0; i < nd; i++) {
			num = qh * v[i] + mulc;
			mulc = num >> LIMB_BITS;
			sub = (num & LIMB_MASK) + brw;
			ui = u[i + j];
			u[i + j] = (limb_t)(ui - sub);
			brw = ui < sub;
		}
		sub = mulc + brw;
		ui = u[j + nd];
		u[j + nd] = (limb_t)(ui - sub);
		brw = ui < sub;

		if (brw) {
			/* The estimate was one too large: add the divisor
			 * back once. */
			uint32_t c = 0, t;

			qh--;
			for (i = 0; i < nd; i++) {
				t = (uint32_t)u[i + j] + v[i] + c;
				u[i + j] = (limb_t)t;
				c = t >> LIMB_BITS;
			}
			u[j + nd] = (limb_t)(u[j + nd] + c);
		}
		q[j] = (limb_t)qh;
	}

	/* The remainder is the low nd limbs of u, shifted back down. */
	for (i = 0; i < nd; i++)
		rem[i] = (limb_t)((((uint32_t)u[i] >> s) |
		    ((uint32_t)u[i + 1] << (LIMB_BITS - s))) & LIMB_MASK);
}

/*
 * dv = a / d and rem = a % d, truncated toward zero: rem takes the sign of
 * a and dv the product of the signs. Either destination may be NULL or
 * alias a or d, so every input is consumed into scratch before a
 * destination is expanded or written. d is given as raw limbs so
 * BN_div_word can pass a word.
 */
static int
divide(BIGNUM *dv, BIGNUM *rem, const BIGNUM *a,
    const limb_t *dd, size_t dn, int dneg, BN_CTX *ctx)
{
	size_t an = a->top, qn, rn;
	int aneg = a->neg, qneg = (aneg != dneg);
	limb_t *s, *u, *v, *q, *r;

	if (dn == 0 || (dv != NULL && dv == rem))
		return 0;

	if (mcmp(a->d, an, dd, dn) < 0) {
		/* |a| < |d|: quotient 0, remainder a. */
		if (rem != NULL && BN_copy(rem, a) == NULL)
			return 0;
		if (dv != NULL) {
			dv->top = 0;
			dv->neg = 0;
		}
		return 1;
	}

	qn = an - dn + 1;
	rn = dn;
	s = scratch_get(ctx, (an + 1) + dn + qn + rn);
	if (s == NULL)
		return 0;
	u = s;
	v = u + (an + 1);
	q = v + dn;
	r = q + qn;

	if (dn == 1)
		r[0] = (limb_t)mdiv1(q, a->d, an, dd[0]);
	else
		mdivrem(q, r, u, v, a->d, an, dd, dn);

	if ((dv != NULL && !expand(dv, qn)) ||
	    (rem != NULL && !expand(rem, rn))) {
		scratch_put(ctx, s);
		return 0;
	}
	if (dv != NULL) {
		memcpy(dv->d, q, qn * sizeof(*q));
		settop(dv, qn, qneg);
	}
	if (rem != NULL) {
		memcpy(rem->d, r, rn * sizeof(*r));
		settop(rem, rn, aneg);
	}
	scratch_put(ctx, s);
	return 1;
}

int
BN_div(BIGNUM *dv, BIGNUM *rem, const BIGNUM *a, const BIGNUM *d, BN_CTX *ctx)
{
	return divide(dv, rem, a, d->d, d->top, d->neg, ctx);
}

BN_ULONG
BN_div_word(BIGNUM *a, BN_ULONG w)
{
	limb_t wd[2], rb[2];
	BIGNUM rem;
	size_t wn;

	if (w == 0)
		return (BN_ULONG)-1;
	if (a->top == 0)
		return 0;
	if (w <= LIMB_MASK) {
		uint32_t r = mdiv1(a->d, a->d, a->top, w);

		settop(a, a->top, a->neg);
		return r;
	}
	/* The remainder is below w, so two limbs hold it and the stack
	 * BIGNUM never grows. */
	wn = wordlimbs(wd, w);
	rem.d = rb;
	rem.top = 0;
	rem.dmax = 2;
	rem.neg = 0;
	if (!divide(a, &rem, a, wd, wn, 0, NULL))
		return (BN_ULONG)-1;
	return BN_get_word(&rem);
}

/* ---- exponentiation --------------------------------------------------- */

/* r = a^p for p >= 0, scanning p from its top bit: square, then multiply
 * by a when the bit is set. The power accumulates in a temporary so r may
 * be a or p. */
int
BN_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p, BN_CTX *ctx)
{
	BIGNUM *t;
	BIGNUM tmp;
	int i;

	if (p->neg)
		return 0;
	if (p->top == 0)
		return BN_one(r);
	t = BN_dup(a);
	if (t == NULL)
		return 0;
	for (i = BN_num_bits(p) - 2; i >= 0; i--) {
		if (!BN_mul(t, t, t, ctx) ||
		    (BN_is_bit_set(p, i) && !BN_mul(t, t, a, ctx))) {
			BN_free(t);
			return 0;
		}
	}
	/* Hand t's storage to r. */
	tmp = *r;
	*r = *t;
	*t = tmp;
	BN_free(t);
	return 1;
}

/* ---- text conversion -------------------------------------------------- */

/* Decimal digits come from repeated division of a copy of the magnitude by
 * 10000, four digits per pass. At most bits/13 + 2 passes are needed
 * because 10000 exceeds 2^13. */
char *
BN_bn2dec(const BIGNUM *a)
{
	size_t bits, passes, size, n, pos;
	limb_t *tmp;
	char *buf;
	uint32_t rem;
	int i;

	if (a->top == 0) {
		buf = malloc(2);
		if (buf != NULL)
			memcpy(buf, "0", 2);
		return buf;
	}
	bits = (size_t)BN_num_bits(a);
	passes = bits / 13 + 2;
	size = passes * 4 + 2;
	buf = malloc(size);
	tmp = malloc(a->top * sizeof(*tmp));
	if (buf == NULL || tmp == NULL) {
		free(buf);
		free(tmp);
		return NULL;
	}
	memcpy(tmp, a->d, a->top * sizeof(*tmp));
	n = a->top;
	pos = size - 1;
	buf[pos] = '\0';
	while (n > 0) {
		rem = mdiv1(tmp, tmp, n, 10000);
		n = trim(tmp, n);
		for (i = 0; i < 4; i++) {
			buf[--pos] = (char)('0' + rem % 10);
			rem /= 10;
		}
	}
	free(tmp);
	while (buf[pos] == '0')
		pos++;
	if (a->neg)
		buf[--pos] = '-';
	memmove(buf, buf + pos, size - pos);
	return buf;
}

/* Uppercase hexadecimal, two digits per significant byte, so a value whose
 * top nibble is zero keeps a leading '0' (dc strips leading zeros). */
char *
BN_bn2hex(const BIGNUM *a)
{
	static const char digits[] = "0123456789ABCDEF";
	size_t nbytes, k, len = 0;
	char *buf;

	if (a->top == 0) {
		buf = malloc(2);
		if (buf != NULL)
			memcpy(buf, "0", 2);
		return buf;
	}
	nbytes = (size_t)BN_num_bytes(a);
	buf = malloc(nbytes * 2 + 2);
	if (buf == NULL)
		return NULL;
	if (a->neg)
		buf[len++] = '-';
	for (k = nbytes; k-- > 0;) {
		unsigned byte = ((unsigned)a->d[k / 2] >> ((unsigned)(k % 2) * 8u)) & 0xFFu;

		buf[len++] = digits[byte >> 4];
		buf[len++] = digits[byte & 0xF];
	}
	buf[len] = '\0';
	return buf;
}
