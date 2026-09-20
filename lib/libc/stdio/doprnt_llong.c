/*
 * 64-bit digit conversion for _doprnt, split from doprnt.c so that only a
 * program that prints a long long carries it. See the note at __doprnt_ll
 * in doprnt.c: share/mk/sys.mk forces this member on rp2040 for a program
 * declaring PRINTF_LLONG=yes, and doprnt.c prints a question mark for a
 * value above the word while the weak reference stays unresolved.
 */

#include <stdio.h>
#include <stdarg.h>

static unsigned char
mkhex (unsigned char ch)
{
	ch &= 15;
	if (ch > 9)
		return ch + 'a' - 10;
	return ch + '0';
}

/*
 * Divide a 64-bit value by a base of at most 16 in place and return the
 * remainder, in 32-bit steps: the high word, then the low word in two
 * halves, each step a 32-bit division whose dividend is the previous
 * remainder shifted in. This target has no 64-bit divide instruction and
 * the board's libc carries no libgcc to supply one.
 */
static unsigned
udiv64_small (unsigned long long *vp, unsigned base)
{
	/* 32-bit words by name, so the low half stays a half on a 64-bit host too */
	unsigned int hi = (unsigned int)(*vp >> 32);
	unsigned int lo = (unsigned int) *vp;
	unsigned int qhi, q1, q0, r, t;

	qhi = hi / base;
	r = hi % base;
	t = (r << 16) | (lo >> 16);
	q1 = t / base;
	r = t % base;
	t = (r << 16) | (lo & 0xffffU);
	q0 = t / base;
	r = t % base;
	*vp = ((unsigned long long) qhi << 32) | (q1 << 16) | q0;
	return (unsigned) r;
}

unsigned char *
__doprnt_ll (va_list *app, int issigned, unsigned char base, int width,
	unsigned char *nbuf, int *lenp, unsigned char *negp,
	unsigned char *nonzerop)
{
	unsigned long long v;
	unsigned char *p;

	if (issigned) {
		long long ll = va_arg (*app, long long);

		if (ll < 0) {
			*negp = '-';
			v = 0ULL - (unsigned long long) ll;
		} else
			v = (unsigned long long) ll;
	} else
		v = va_arg (*app, unsigned long long);
	*nonzerop = (v != 0);	/* before the digits, which a precision pads */

	p = nbuf;
	*p = 0;
	/* C17 7.21.6.1p8: a zero with an explicit zero precision has no digits. */
	if (v == 0 && width == 0) {
		if (lenp)
			*lenp = 0;
		return (p);
	}
	for (;;) {
		*++p = mkhex ((unsigned char) udiv64_small (&v, base));
		if (--width > 0)
			continue;
		if (! v)
			break;
	}
	if (lenp)
		*lenp = (int)(p - nbuf);
	return (p);
}
