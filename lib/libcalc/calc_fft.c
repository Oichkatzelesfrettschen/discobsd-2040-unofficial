/*
 * In-place iterative radix-2 decimation-in-time FFT with bit-reversal
 * permutation. Twiddle factors come from a caller-supplied table of n / 2
 * entries, or are computed per stage from sin and cos when none is given.
 */

#include <math.h>

#include "calc_internal.h"

/* n is a power of two and within CALC_FFT_MAX. */
static calc_status
fft_check(size_t n)
{
	if (n == 0 || (n & (n - 1)) != 0)
		return CALC_E_SIZE;
	if (n > CALC_FFT_MAX)
		return CALC_E_RANGE;
	return CALC_OK;
}

calc_status
calc_fft_twiddles(calc_cplx *tw, size_t n)
{
	calc_status st = fft_check(n);
	double s, c;
	size_t k;

	if (st != CALC_OK)
		return st;
	if (tw == NULL)
		return CALC_E_ARG;
	for (k = 0; k < n / 2; k++) {
		calc__sincos(-CALC_TWO_PI * (double)k / (double)n, &s, &c);
		tw[k].re = c;
		tw[k].im = s;
	}
	return CALC_OK;
}

static void
bit_reverse(calc_cplx *x, size_t n)
{
	size_t i, j = 0, bit;
	calc_cplx t;

	for (i = 1; i < n; i++) {
		bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			t = x[i];
			x[i] = x[j];
			x[j] = t;
		}
	}
}

calc_status
calc_fft(calc_cplx *x, size_t n, int inverse, const calc_cplx *twiddle)
{
	calc_status st = fft_check(n);
	size_t half, len, i, j, step;
	double wr, wi, s, c, tr, ti, scale;

	if (st != CALC_OK)
		return st;
	if (x == NULL)
		return CALC_E_ARG;
	if (inverse)
		for (i = 0; i < n; i++)
			x[i].im = -x[i].im;
	bit_reverse(x, n);
	for (len = 2; len <= n; len <<= 1) {
		half = len >> 1;
		step = n / len;
		for (j = 0; j < half; j++) {
			if (twiddle != NULL) {
				wr = twiddle[j * step].re;
				wi = twiddle[j * step].im;
			} else {
				calc__sincos(-CALC_TWO_PI * (double)j / (double)len, &s, &c);
				wr = c;
				wi = s;
			}
			for (i = j; i < n; i += len) {
				tr = x[i + half].re * wr - x[i + half].im * wi;
				ti = x[i + half].re * wi + x[i + half].im * wr;
				x[i + half].re = x[i].re - tr;
				x[i + half].im = x[i].im - ti;
				x[i].re += tr;
				x[i].im += ti;
			}
		}
	}
	if (inverse) {
		scale = 1.0 / (double)n;
		for (i = 0; i < n; i++) {
			x[i].re *= scale;
			x[i].im = -x[i].im * scale;
		}
	}
	return CALC_OK;
}

calc_status
calc_fft_magnitude(const calc_cplx *x, size_t n, double *out)
{
	size_t i;

	if (x == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	for (i = 0; i < n; i++)
		out[i] = calc__hypot(x[i].re, x[i].im);
	return CALC_OK;
}

calc_status
calc_fft_phase(const calc_cplx *x, size_t n, double *out)
{
	size_t i;

	if (x == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	for (i = 0; i < n; i++)
		out[i] = calc__atan2(x[i].im, x[i].re);
	return CALC_OK;
}
