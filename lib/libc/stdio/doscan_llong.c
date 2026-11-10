/*
 * 64-bit integer conversion for _doscan, split from doscan.c so that only
 * a program that scans a long long or an intmax_t carries it. See the note
 * at __doscan_ll in doscan.c: on rp2040 share/mk/sys.mk forces this member
 * only for a program declaring SCANF_LLONG=yes, and doscan.c makes an ll, j
 * or q integer directive a matching failure while the weak reference stays
 * unresolved.
 *
 * The subject sequence, the single character of lookahead and the
 * saturation at the destination limit are those of convint() in doscan.c;
 * only the accumulator is wider.
 */

#include <stdio.h>
#include <limits.h>

#include "doscan.h"

/*
 * The magnitude accumulates in 16-bit limbs, least significant first, so
 * that each step is one 32-bit multiply of a limb by the base plus a
 * carry. This target has no 64-bit multiply instruction and the board's
 * libc carries no libgcc to supply one.
 */
#define LIMBS		4
#define LIMB_BITS	16
#define LIMB_MASK	0xffffU

/*
 * Read the next character of the field, or EOF once the width is spent or
 * input ended. Only input ending sets eof, which separates a complete
 * conversion cut short by its width from an input failure.
 */
static int
l_get (struct scanstate *s, int *width)
{
	int c;

	if (*width <= 0 || s->eof)
		return EOF;
	c = getc (s->fp);
	if (c == EOF) {
		s->eof = 1;
		return EOF;
	}
	s->nread++;
	(*width)--;
	return c;
}

static void
l_unget (struct scanstate *s, int c)
{
	if (c != EOF && ungetc (c, s->fp) != EOF)
		s->nread--;
}

static int
l_isspace (int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
	    c == '\f' || c == '\r';
}

/* Value of c as a digit in the given base, or -1 when c is not one. */
static int
l_digitval (int c, int base)
{
	int v;

	if (c >= '0' && c <= '9')
		v = c - '0';
	else if (c >= 'a' && c <= 'f')
		v = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		v = c - 'A' + 10;
	else
		return -1;
	return v < base ? v : -1;
}

/*
 * Multiply the limbs by the base and add a digit. A carry out of the top
 * limb is a magnitude above ULLONG_MAX and is the return value.
 */
static int
l_muladd (unsigned int *limbs, unsigned int base, unsigned int digit)
{
	unsigned int carry, t;
	int i;

	carry = digit;
	for (i = 0; i < LIMBS; i++) {
		t = limbs[i] * base + carry;
		limbs[i] = t & LIMB_MASK;
		carry = t >> LIMB_BITS;
	}
	return carry != 0;
}

/*
 * Convert one integer directive whose destination is 64 bits wide.
 * flags_base carries the directive's flags below LL_BASE_SHIFT and its
 * base above. Leading white space is skipped without charging the field
 * width.
 */
int
__doscan_ll (struct scanstate *s, int width, int flags_base, va_list *ap)
{
	unsigned int limbs[LIMBS];
	unsigned long long acc, limit;
	int c, v, i, base, negate, ndigits, saturated;

	base = flags_base >> LL_BASE_SHIFT;
	if (base == LL_BASE_COUNT) {
		*va_arg (*ap, long long *) = s->nread;
		return CONV_OK;
	}

	/* INT_MAX stands for no width, so the skip spends nothing of it. */
	i = INT_MAX;
	do {
		c = l_get (s, &i);
	} while (c != EOF && l_isspace (c));
	if (c == EOF)
		return CONV_INPUT;
	l_unget (s, c);

	for (i = 0; i < LIMBS; i++)
		limbs[i] = 0;
	negate = 0;
	ndigits = 0;
	saturated = 0;

	c = l_get (s, &width);
	if (c == EOF)
		return s->eof ? CONV_INPUT : CONV_FAIL;

	if (c == '+' || c == '-') {
		negate = (c == '-');
		c = l_get (s, &width);
		if (c == EOF)
			return CONV_FAIL;
	}

	if ((base == 0 || base == 16) && c == '0') {
		/*
		 * The leading zero is a digit in its own right, so a
		 * number that ends there still converts.
		 */
		ndigits = 1;
		base = (base == 0) ? 8 : 16;
		c = l_get (s, &width);
		if (c == 'x' || c == 'X') {
			int d = l_get (s, &width);

			if (d != EOF && l_digitval (d, 16) >= 0) {
				base = 16;
				ndigits = 0;
				c = d;
			} else {
				l_unget (s, d);
				c = EOF;
			}
		}
	} else if (base == 0) {
		base = 10;
	}

	while (c != EOF) {
		v = l_digitval (c, base);
		if (v < 0) {
			l_unget (s, c);
			break;
		}
		if (! saturated)
			saturated = l_muladd (limbs, (unsigned int) base,
			    (unsigned int) v);
		ndigits++;
		c = l_get (s, &width);
	}

	if (ndigits == 0)
		return CONV_FAIL;

	/*
	 * The negative signed magnitude is one greater than the positive
	 * maximum; unsigned arithmetic avoids negating LLONG_MIN.
	 */
	if (! (flags_base & F_SIGNED))
		limit = ULLONG_MAX;
	else if (negate)
		limit = (unsigned long long) LLONG_MAX + 1ULL;
	else
		limit = LLONG_MAX;

	acc = ((unsigned long long) limbs[3] << 48) |
	    ((unsigned long long) limbs[2] << 32) |
	    ((unsigned long long) limbs[1] << 16) |
	    (unsigned long long) limbs[0];
	if (saturated || acc > limit) {
		acc = limit;
		if (negate && (flags_base & F_SIGNED))
			acc = 0ULL - acc;
	} else if (negate) {
		acc = 0ULL - acc;
	}

	if (flags_base & F_SUPPRESS)
		return CONV_OK;
	/*
	 * C17 7.21.6.2p11 gives ll, j and q no meaning before p. The
	 * destination is then still a pointer to a pointer, so the store
	 * keeps to its width.
	 */
	if (flags_base & F_POINTER)
		*va_arg (*ap, void **) = (void *) (unsigned long) acc;
	else
		*va_arg (*ap, unsigned long long *) = acc;
	return CONV_OK;
}
