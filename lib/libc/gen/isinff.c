/*
 * Written by Serge Vakulenko <serge@vak.ru>.
 *
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 */
#include <math.h>

/*
 * isinff(x) returns 1 is x is inf, -1 if x is -inf, else 0;
 * no branching!
 */
int isinff (float x)
{
        union {
                long s32;
                float f32;
        } u;
        long v;

        u.f32 = x;
	v = (u.s32 & 0x7fffffff) ^ 0x7f800000;
	return ~((v | -v) >> 31) & (u.s32 >> 30);
}

#if defined(__SIZEOF_DOUBLE__) && defined(__SIZEOF_FLOAT__) && \
    __SIZEOF_DOUBLE__ == __SIZEOF_FLOAT__
int isinf (double x) __attribute__((alias ("isinff")));
#else
int isinf (double x)
{
	union {
		double f64;
		unsigned long long u64;
	} value;
	unsigned long long high_word, low_word, magnitude_difference;

	value.f64 = x;
	high_word = value.u64 >> 32;
	low_word = value.u64 & 0xffffffffULL;
	magnitude_difference = low_word |
	    ((high_word & 0x7fffffffULL) ^ 0x7ff00000ULL);
	if (magnitude_difference != 0)
		return 0;
	return (high_word & 0x80000000ULL) != 0 ? -1 : 1;
}
#endif
