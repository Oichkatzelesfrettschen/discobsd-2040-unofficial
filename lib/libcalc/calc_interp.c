/*
 * Linear interpolation, table lookup and range rescaling.
 */

#include "calc_internal.h"

double
calc_lerp(double a, double b, double t)
{
	return (b - a) * t + a;
}

calc_status
calc_rescale(const double *in, size_t n, double output_min, double output_max,
    double *out, double *input_min, double *input_max)
{
	double lo, hi, span, orange;
	size_t i;

	if (in == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__partial(in, out, n * sizeof(double)))
		return CALC_E_ALIAS;
	lo = hi = in[0];
	for (i = 1; i < n; i++) {
		if (in[i] < lo)
			lo = in[i];
		if (in[i] > hi)
			hi = in[i];
	}
	span = hi - lo;
	if (span == 0.0)
		return CALC_E_DOMAIN;
	orange = output_max - output_min;
	for (i = 0; i < n; i++)
		out[i] = (in[i] - lo) / span * orange + output_min;
	if (input_min != NULL)
		*input_min = lo;
	if (input_max != NULL)
		*input_max = hi;
	return CALC_OK;
}

calc_status
calc_interp_table(const double *xs, const double *ys, size_t n, double x, double *out)
{
	size_t lo, hi, mid, i;

	if (xs == NULL || ys == NULL || out == NULL)
		return CALC_E_ARG;
	if (n < 2)
		return CALC_E_SIZE;
	for (i = 1; i < n; i++)
		if (!(xs[i] > xs[i - 1]))
			return CALC_E_ARG;
	if (!(x >= xs[0] && x <= xs[n - 1]))
		return CALC_E_DOMAIN;
	/* Invariant: xs[lo] <= x <= xs[hi]. */
	lo = 0;
	hi = n - 1;
	while (hi - lo > 1) {
		mid = lo + (hi - lo) / 2;
		if (xs[mid] <= x)
			lo = mid;
		else
			hi = mid;
	}
	if (x == xs[lo]) {
		*out = ys[lo];
	} else if (x == xs[hi]) {
		*out = ys[hi];
	} else {
		*out = calc_lerp(ys[lo], ys[hi], (x - xs[lo]) / (xs[hi] - xs[lo]));
	}
	return CALC_OK;
}
