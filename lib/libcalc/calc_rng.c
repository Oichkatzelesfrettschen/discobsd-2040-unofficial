/*
 * MT19937 (Matsumoto and Nishimura) with caller-owned state, the unit
 * deviates built on it and a normal deviate by the Marsaglia polar method.
 */

#include <math.h>

#include "calc_internal.h"

#define MT_N		624u
#define MT_M		397u
#define MT_MATRIX_A	0x9908b0dfu
#define MT_UPPER	0x80000000u
#define MT_LOWER	0x7fffffffu

void
calc_rng_seed(calc_rng *r, uint32_t seed)
{
	unsigned i;

	r->mt[0] = seed;
	for (i = 1; i < MT_N; i++)
		r->mt[i] = 1812433253u * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + i;
	r->index = MT_N;
	r->spare = 0.0;
	r->have_spare = 0;
}

static void
twist(calc_rng *r)
{
	uint32_t y;
	unsigned k;

	for (k = 0; k < MT_N; k++) {
		y = (r->mt[k] & MT_UPPER) | (r->mt[(k + 1) % MT_N] & MT_LOWER);
		r->mt[k] = r->mt[(k + MT_M) % MT_N] ^ (y >> 1) ^ ((y & 1u) ? MT_MATRIX_A : 0u);
	}
	r->index = 0;
}

uint32_t
calc_rng_u32(calc_rng *r)
{
	uint32_t y;

	if (r->index >= MT_N)
		twist(r);
	y = r->mt[r->index++];
	y ^= y >> 11;
	y ^= (y << 7) & 0x9d2c5680u;
	y ^= (y << 15) & 0xefc60000u;
	y ^= y >> 18;
	return y;
}

double
calc_rng_unit(calc_rng *r)
{
	return (double)calc_rng_u32(r) / 4294967296.0;
}

double
calc_rng_unit53(calc_rng *r)
{
	uint32_t hi = calc_rng_u32(r) >> 5;
	uint32_t lo = calc_rng_u32(r) >> 6;

	return ((double)hi * 67108864.0 + (double)lo) / 9007199254740992.0;
}

double
calc_rng_normal(calc_rng *r, double mean, double sd)
{
	double u, v, s, f;

	if (r->have_spare) {
		r->have_spare = 0;
		return mean + sd * r->spare;
	}
	do {
		u = 2.0 * calc_rng_unit53(r) - 1.0;
		v = 2.0 * calc_rng_unit53(r) - 1.0;
		s = u * u + v * v;
	} while (s >= 1.0 || s == 0.0);
	f = sqrt(-2.0 * calc__log(s) / s);
	r->spare = v * f;
	r->have_spare = 1;
	return mean + sd * (u * f);
}
