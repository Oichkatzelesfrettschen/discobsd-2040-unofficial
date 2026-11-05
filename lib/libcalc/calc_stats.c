/*
 * Statistics on a flat list of doubles: compensated sum, mean, median,
 * variance, extremes, correlation, chi-square and the level-crossing
 * detector.
 */

#include <math.h>

#include "calc_internal.h"

/* Neumaier-compensated accumulator. A non-finite partial sum is kept as is. */
typedef struct {
	double s, c;
} acc_t;

static void
acc_add(acc_t *a, double x)
{
	double t = a->s + x;

	if (!calc__isfinite(t)) {
		a->s = t;
		return;
	}
	if (calc__fabs(a->s) >= calc__fabs(x))
		a->c += (a->s - t) + x;
	else
		a->c += (x - t) + a->s;
	a->s = t;
}

static double
acc_value(const acc_t *a)
{
	return a->s + a->c;
}

static double
list_sum(const double *a, size_t n)
{
	acc_t acc = { 0.0, 0.0 };
	size_t i;

	for (i = 0; i < n; i++)
		acc_add(&acc, a[i]);
	return acc_value(&acc);
}

calc_status
calc_sum(const double *a, size_t n, double *out)
{
	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	*out = list_sum(a, n);
	return CALC_OK;
}

calc_status
calc_mean(const double *a, size_t n, double *out)
{
	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	*out = list_sum(a, n) / (double)n;
	return CALC_OK;
}

/* Sift the element at root down a max-heap of count elements. */
static void
sift_down(double *h, size_t root, size_t count)
{
	size_t child;
	double t;

	while ((child = 2 * root + 1) < count) {
		if (child + 1 < count && h[child] < h[child + 1])
			child++;
		if (!(h[root] < h[child]))
			return;
		t = h[root];
		h[root] = h[child];
		h[child] = t;
		root = child;
	}
}

static void
heap_sort(double *h, size_t n)
{
	size_t i;
	double t;

	for (i = n / 2; i-- > 0;)
		sift_down(h, i, n);
	for (i = n; i-- > 1;) {
		t = h[0];
		h[0] = h[i];
		h[i] = t;
		sift_down(h, 0, i);
	}
}

calc_status
calc_median(const double *a, size_t n, double *scratch, double *out)
{
	size_t i;

	if (a == NULL || scratch == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	if (calc__overlap(a, n * sizeof(double), scratch, n * sizeof(double)))
		return CALC_E_ALIAS;
	for (i = 0; i < n; i++)
		scratch[i] = a[i];
	heap_sort(scratch, n);
	if (n % 2 == 1)
		*out = scratch[n / 2];
	else
		*out = 0.5 * (scratch[n / 2 - 1] + scratch[n / 2]);
	return CALC_OK;
}

calc_status
calc_variance(const double *a, size_t n, int sample, double *out)
{
	acc_t acc = { 0.0, 0.0 };
	double mean, d;
	size_t i;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0 || (sample && n < 2))
		return CALC_E_SIZE;
	mean = list_sum(a, n) / (double)n;
	for (i = 0; i < n; i++) {
		d = a[i] - mean;
		acc_add(&acc, d * d);
	}
	*out = acc_value(&acc) / (double)(sample ? n - 1 : n);
	return CALC_OK;
}

calc_status
calc_stddev(const double *a, size_t n, int sample, double *out)
{
	double v;
	calc_status st = calc_variance(a, n, sample, &v);

	if (st != CALC_OK)
		return st;
	*out = sqrt(v);
	return CALC_OK;
}

calc_status
calc_min(const double *a, size_t n, double *out, size_t *index)
{
	size_t i, at = 0;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	for (i = 1; i < n; i++)
		if (a[i] < a[at])
			at = i;
	*out = a[at];
	if (index != NULL)
		*index = at;
	return CALC_OK;
}

calc_status
calc_max(const double *a, size_t n, double *out, size_t *index)
{
	size_t i, at = 0;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	for (i = 1; i < n; i++)
		if (a[i] > a[at])
			at = i;
	*out = a[at];
	if (index != NULL)
		*index = at;
	return CALC_OK;
}

calc_status
calc_correl(const double *a, const double *b, size_t n, double *out)
{
	acc_t sxy = { 0.0, 0.0 }, sxx = { 0.0, 0.0 }, syy = { 0.0, 0.0 };
	double ma, mb, dx, dy, vx, vy, r;
	size_t i;

	if (a == NULL || b == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	ma = list_sum(a, n) / (double)n;
	mb = list_sum(b, n) / (double)n;
	for (i = 0; i < n; i++) {
		dx = a[i] - ma;
		dy = b[i] - mb;
		acc_add(&sxy, dx * dy);
		acc_add(&sxx, dx * dx);
		acc_add(&syy, dy * dy);
	}
	vx = acc_value(&sxx);
	vy = acc_value(&syy);
	if (vx == 0.0 || vy == 0.0)
		return CALC_E_DOMAIN;
	r = acc_value(&sxy) / (sqrt(vx) * sqrt(vy));
	if (r > 1.0)
		r = 1.0;
	else if (r < -1.0)
		r = -1.0;
	*out = r;
	return CALC_OK;
}

/* --- chi-square ---------------------------------------------------------- */

/*
 * ln Gamma(a) for a > 0: Stirling's series after shifting the argument up to
 * at least 15, where the terms through a^-11 leave a relative remainder
 * below 1e-16.
 */
static double
log_gamma(double a)
{
	double prod = 1.0, inv, inv2;

	while (a < 15.0) {
		prod *= a;
		a += 1.0;
	}
	inv = 1.0 / a;
	inv2 = inv * inv;
	return (a - 0.5) * calc__log(a) - a + 0.91893853320467274178
	    + inv * (1.0 / 12.0 + inv2 * (-1.0 / 360.0 + inv2 * (1.0 / 1260.0
	    + inv2 * (-1.0 / 1680.0 + inv2 * (1.0 / 1188.0
	    + inv2 * (-691.0 / 360360.0))))))
	    - calc__log(prod);
}

#define GAMMA_ITER_MAX	10000
#define GAMMA_EPS	1.0e-17
#define GAMMA_TINY	1.0e-300

/*
 * Regularized upper incomplete gamma function Q(a, x), a > 0, x > 0: the
 * power series for P below x = a + 1, otherwise the continued fraction
 * evaluated with the modified Lentz algorithm. Returns 0 and stores Q, or
 * -1 when neither converges within GAMMA_ITER_MAX terms.
 */
static int
gamma_q(double a, double x, double *q)
{
	double prefactor = calc__exp(-x + a * calc__log(x) - log_gamma(a));
	double sum, term, ap, b, c, d, h, an, del;
	int i;

	if (x < a + 1.0) {
		ap = a;
		sum = term = 1.0 / a;
		for (i = 0; i < GAMMA_ITER_MAX; i++) {
			ap += 1.0;
			term *= x / ap;
			sum += term;
			if (calc__fabs(term) < calc__fabs(sum) * GAMMA_EPS) {
				*q = 1.0 - sum * prefactor;
				return 0;
			}
		}
		return -1;
	}
	b = x + 1.0 - a;
	c = 1.0 / GAMMA_TINY;
	d = 1.0 / b;
	h = d;
	for (i = 1; i <= GAMMA_ITER_MAX; i++) {
		an = -(double)i * ((double)i - a);
		b += 2.0;
		d = an * d + b;
		if (calc__fabs(d) < GAMMA_TINY)
			d = GAMMA_TINY;
		c = b + an / c;
		if (calc__fabs(c) < GAMMA_TINY)
			c = GAMMA_TINY;
		d = 1.0 / d;
		del = d * c;
		h *= del;
		if (calc__fabs(del - 1.0) < 1.0e-16) {
			*q = prefactor * h;
			return 0;
		}
	}
	return -1;
}

calc_status
calc_chi_square_p(double chi, size_t df, double *percent)
{
	double q;

	if (percent == NULL)
		return CALC_E_ARG;
	if (df == 0)
		return CALC_E_SIZE;
	if (calc__isnan(chi) || chi < 0.0)
		return CALC_E_DOMAIN;
	if (chi == 0.0) {
		*percent = 100.0;
		return CALC_OK;
	}
	if (calc__isinf(chi)) {
		*percent = 0.0;
		return CALC_OK;
	}
	if (gamma_q(0.5 * (double)df, 0.5 * chi, &q) != 0)
		return CALC_E_RANGE;
	if (q < 0.0)
		q = 0.0;
	if (q > 1.0)
		q = 1.0;
	*percent = 100.0 * q;
	return CALC_OK;
}

calc_status
calc_chi_square(const double *obs, size_t rows, size_t cols, double *chi, size_t *df)
{
	acc_t grand = { 0.0, 0.0 }, stat = { 0.0, 0.0 };
	double rowtot, coltot, total, e, d;
	size_t i, j, k;

	if (obs == NULL || chi == NULL || df == NULL)
		return CALC_E_ARG;
	if (rows < 2 || cols < 2)
		return CALC_E_SIZE;
	for (i = 0; i < rows; i++) {
		rowtot = 0.0;
		for (j = 0; j < cols; j++) {
			if (!(obs[i * cols + j] >= 0.0))
				return CALC_E_DOMAIN;
			rowtot += obs[i * cols + j];
		}
		if (rowtot == 0.0)
			return CALC_E_DOMAIN;
		acc_add(&grand, rowtot);
	}
	for (j = 0; j < cols; j++) {
		coltot = 0.0;
		for (i = 0; i < rows; i++)
			coltot += obs[i * cols + j];
		if (coltot == 0.0)
			return CALC_E_DOMAIN;
	}
	total = acc_value(&grand);
	for (i = 0; i < rows; i++) {
		rowtot = 0.0;
		for (j = 0; j < cols; j++)
			rowtot += obs[i * cols + j];
		for (j = 0; j < cols; j++) {
			coltot = 0.0;
			for (k = 0; k < rows; k++)
				coltot += obs[k * cols + j];
			e = rowtot * coltot / total;
			d = obs[i * cols + j] - e;
			acc_add(&stat, d * d / e);
		}
	}
	*chi = acc_value(&stat);
	*df = (rows - 1) * (cols - 1);
	return CALC_OK;
}

/* --- level crossing ------------------------------------------------------ */

/*
 * First pass: a monotonic triple that straddles the level. Second pass, for
 * a signal that touches the level on a plateau: a monotonic triple around
 * a center sample that is bracketed by one lower and one higher neighbor.
 * Both reject a single-sample spike.
 */
calc_status
calc_crossing(const double *a, size_t n, double level, int direction, long *index)
{
	size_t i;

	if (a == NULL || index == NULL)
		return CALC_E_ARG;
	if (direction != 1 && direction != -1)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	*index = -1;
	if (n < 4)
		return CALC_OK;
	for (i = 0; i + 4 <= n; i++) {
		if (direction == 1) {
			if (a[i] < level && a[i + 2] > level &&
			    a[i] <= a[i + 1] && a[i + 1] <= a[i + 2]) {
				*index = (long)(i + 1);
				return CALC_OK;
			}
		} else {
			if (a[i] > level && a[i + 2] < level &&
			    a[i] >= a[i + 1] && a[i + 1] >= a[i + 2]) {
				*index = (long)(i + 1);
				return CALC_OK;
			}
		}
	}
	for (i = 0; i + 6 <= n; i++) {
		if (direction == 1) {
			if (a[i + 1] <= level && a[i + 3] >= level &&
			    a[i + 1] <= a[i + 2] && a[i + 2] <= a[i + 3] &&
			    a[i] < a[i + 2] && a[i + 4] > a[i + 2]) {
				*index = (long)(i + 2);
				return CALC_OK;
			}
		} else {
			if (a[i + 1] >= level && a[i + 3] <= level &&
			    a[i + 1] >= a[i + 2] && a[i + 2] >= a[i + 3] &&
			    a[i] > a[i + 2] && a[i + 4] < a[i + 2]) {
				*index = (long)(i + 2);
				return CALC_OK;
			}
		}
	}
	return CALC_OK;
}
