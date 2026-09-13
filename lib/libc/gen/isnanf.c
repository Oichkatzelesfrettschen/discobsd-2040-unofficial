/*
 * Written by Serge Vakulenko <serge@vak.ru>.
 *
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 */
#include <math.h>

/*
 * isnan(x) returns 1 is x is nan, else 0;
 * no branching!
 */
int isnanf (float x)
{
        union {
                long s32;
                float f32;
        } u;
        unsigned long ul;

        u.f32 = x;
	ul = 0x7f800000 - (u.s32 & 0x7fffffff);
	return ul >> 31;
}

#if defined(__SIZEOF_DOUBLE__) && defined(__SIZEOF_FLOAT__) && \
    __SIZEOF_DOUBLE__ == __SIZEOF_FLOAT__
int isnan (double x) __attribute__((alias ("isnanf")));
#else
int isnan (double x)
{
	union {
		double f64;
		unsigned long long u64;
	} value;
	unsigned long long high_word, low_word, fraction;

	value.f64 = x;
	high_word = value.u64 >> 32;
	low_word = value.u64 & 0xffffffffULL;
	fraction = (high_word & 0xfffffULL) | low_word;
	return (high_word & 0x7ff00000ULL) == 0x7ff00000ULL && fraction != 0;
}
#endif
