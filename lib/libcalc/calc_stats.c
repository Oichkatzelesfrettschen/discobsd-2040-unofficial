/*
 * Statistics on a flat list of doubles: compensated sum, mean, median,
 * variance, extremes, correlation, chi-square and the level-crossing
 * detector.
 */

#include <float.h>
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

/*
 * Element i of a, multiplied by the power of two s when on is non-zero. The
 * plain pass (on == 0) forms no product, so the common path costs no more
 * than an unscaled loop.
 */
#define ELEM(a, i, on, s)	((on) ? (a)[i] * (s) : (a)[i])

static double
list_sum(const double *a, size_t n, int on, double s)
{
	acc_t acc = { 0.0, 0.0 };
	size_t i;

	for (i = 0; i < n; i++)
		acc_add(&acc, ELEM(a, i, on, s));
	return acc_value(&acc);
}

/*
 * Exponent of the largest magnitude in the n doubles of a, for a rerun on
 * the list scaled by 2^(1 - *e), which puts the largest entry in [2, 4).
 * The scaled sums and squares then stay finite. Returns 0 for a list that
 * holds a NaN, an infinity, or nothing above the subnormal range: the plain
 * result stands there.
 */
static int
scale_of(const double *a, size_t n, int *e)
{
	union { double d; uint64_t u; } v;
	uint32_t field, top = 0;
	size_t i;

	/* The largest exponent field is that of the largest magnitude; 0x7ff marks a NaN or an infinity. */
	for (i = 0; i < n; i++) {
		v.d = a[i];
		field = (uint32_t)(v.u >> 52) & 0x7ffu;
		if (field > top)
			top = field;
	}
	if (top == 0 || top == 0x7ffu)
		return 0;
	*e = (int)top - 1023;
	return 1;
}

/* Undo scale_of's scaling on a result that carries it p times. */
static double
scale_back(double v, int e, int p)
{
	while (p-- > 0)
		v = calc__ldexp(v, e - 1);
	return v;
}

calc_status
calc_sum(const double *a, size_t n, double *out)
{
	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	*out = list_sum(a, n, 0, 1.0);
	return CALC_OK;
}

calc_status
calc_mean(const double *a, size_t n, double *out)
{
	double m;
	int e;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	m = list_sum(a, n, 0, 1.0) / (double)n;
	if (!calc__isfinite(m) && scale_of(a, n, &e))
		m = scale_back(list_sum(a, n, 1, calc__pow2(1 - e)) / (double)n, e, 1);
	*out = m;
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
	double lo, hi, m;
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
	if (n % 2 == 1) {
		*out = scratch[n / 2];
		return CALC_OK;
	}
	lo = scratch[n / 2 - 1];
	hi = scratch[n / 2];
	m = 0.5 * (lo + hi);
	/* Operands of one sign whose sum overflows: lo + (hi - lo) / 2 stays inside. */
	if (!calc__isfinite(m) && calc__isfinite(hi - lo))
		m = lo + 0.5 * (hi - lo);
	*out = m;
	return CALC_OK;
}

/* Variance of the list, or of the list scaled by the power of two s when on. */
static double
variance_of(const double *a, size_t n, int sample, int on, double s)
{
	acc_t acc = { 0.0, 0.0 };
	double mean, d;
	size_t i;

	mean = list_sum(a, n, on, s) / (double)n;
	for (i = 0; i < n; i++) {
		d = ELEM(a, i, on, s) - mean;
		acc_add(&acc, d * d);
	}
	return acc_value(&acc) / (double)(sample ? n - 1 : n);
}

/* Non-zero when x is zero, subnormal, infinite or NaN: the squares left the normal range. */
static int
outside_normal(double x)
{
	return !(x >= DBL_MIN && x <= DBL_MAX);
}

/*
 * Variance of the list. When it is below lowest, above DBL_MAX or NaN and the
 * list is finite, the variance of the list scaled by 2^(1 - *e) replaces it
 * and the function returns 1: the caller scales it back by e.
 */
static int
variance_rerun(const double *a, size_t n, int sample, double lowest, double *v, int *e)
{
	*v = variance_of(a, n, sample, 0, 1.0);
	if (*v >= lowest && *v <= DBL_MAX)
		return 0;
	if (!scale_of(a, n, e))
		return 0;
	*v = variance_of(a, n, sample, 1, calc__pow2(1 - *e));
	return 1;
}

calc_status
calc_variance(const double *a, size_t n, int sample, double *out)
{
	double v;
	int e;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0 || (sample && n < 2))
		return CALC_E_SIZE;
	if (variance_rerun(a, n, sample, 0.0, &v, &e))
		v = scale_back(v, e, 2);
	*out = v;
	return CALC_OK;
}

/*
 * The standard deviation is taken from the scaled variance directly: a
 * deviation inside the double range can belong to a variance outside it.
 */
calc_status
calc_stddev(const double *a, size_t n, int sample, double *out)
{
	double v;
	int e;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0 || (sample && n < 2))
		return CALC_E_SIZE;
	if (variance_rerun(a, n, sample, DBL_MIN, &v, &e))
		*out = scale_back(sqrt(v), e, 1);
	else
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

/*
 * Centered sums of products for the lists a and b, each multiplied by its own
 * power of two (sa, sb) when on.
 */
static void
correl_sums(const double *a, const double *b, size_t n, int on, double sa, double sb,
    double *cxy, double *vx, double *vy)
{
	acc_t sxy = { 0.0, 0.0 }, sxx = { 0.0, 0.0 }, syy = { 0.0, 0.0 };
	double ma, mb, dx, dy;
	size_t i;

	ma = list_sum(a, n, on, sa) / (double)n;
	mb = list_sum(b, n, on, sb) / (double)n;
	for (i = 0; i < n; i++) {
		dx = ELEM(a, i, on, sa) - ma;
		dy = ELEM(b, i, on, sb) - mb;
		acc_add(&sxy, dx * dy);
		acc_add(&sxx, dx * dx);
		acc_add(&syy, dy * dy);
	}
	*cxy = acc_value(&sxy);
	*vx = acc_value(&sxx);
	*vy = acc_value(&syy);
}

calc_status
calc_correl(const double *a, const double *b, size_t n, double *out)
{
	double cxy, vx, vy, r;
	int ea, eb;

	if (a == NULL || b == NULL || out == NULL)
		return CALC_E_ARG;
	if (n == 0)
		return CALC_E_SIZE;
	correl_sums(a, b, n, 0, 1.0, 1.0, &cxy, &vx, &vy);
	/*
	 * A variance that overflowed, or underflowed to zero or below the normal
	 * range, means the squares left the double range (the cross sum is bounded
	 * by the larger variance). The coefficient does not depend on the scale of
	 * either list, so a rerun with each list scaled on its own needs no
	 * rescaling.
	 */
	if ((outside_normal(vx) || outside_normal(vy)) &&
	    scale_of(a, n, &ea) && scale_of(b, n, &eb))
		correl_sums(a, b, n, 1, calc__pow2(1 - ea), calc__pow2(1 - eb), &cxy, &vx, &vy);
	if (vx == 0.0 || vy == 0.0)
		return CALC_E_DOMAIN;
	r = cxy / (sqrt(vx) * sqrt(vy));
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

/* Sum of count entries of a, from index first in steps of step, scaled by s when on. */
static double
strided_sum(const double *a, size_t first, size_t step, size_t count, int on, double s)
{
	double t = 0.0;
	size_t i;

	for (i = 0; i < count; i++)
		t += ELEM(a, first + i * step, on, s);
	return t;
}

/*
 * Pearson statistic of the table, or of the table scaled by the power of two
 * s when on. The expected count is rowtot * (coltot / total) and each term
 * d * (d / e): the ratios are scale-free, so no product of two totals or of
 * two deviations is formed.
 */
static double
chi_stat(const double *obs, size_t rows, size_t cols, int on, double s)
{
	acc_t grand = { 0.0, 0.0 }, stat = { 0.0, 0.0 };
	double rowtot, total, e, d;
	size_t i, j;

	for (i = 0; i < rows; i++)
		acc_add(&grand, strided_sum(obs, i * cols, 1, cols, on, s));
	total = acc_value(&grand);
	for (i = 0; i < rows; i++) {
		rowtot = strided_sum(obs, i * cols, 1, cols, on, s);
		for (j = 0; j < cols; j++) {
			e = rowtot * (strided_sum(obs, j, cols, rows, on, s) / total);
			d = ELEM(obs, i * cols + j, on, s) - e;
			acc_add(&stat, d * (d / e));
		}
	}
	return acc_value(&stat);
}

calc_status
calc_chi_square(const double *obs, size_t rows, size_t cols, double *chi, size_t *df)
{
	double x;
	size_t i, j;
	int e;

	if (obs == NULL || chi == NULL || df == NULL)
		return CALC_E_ARG;
	if (rows < 2 || cols < 2)
		return CALC_E_SIZE;
	for (i = 0; i < rows * cols; i++)
		if (!(obs[i] >= 0.0))
			return CALC_E_DOMAIN;
	for (i = 0; i < rows; i++)
		if (strided_sum(obs, i * cols, 1, cols, 0, 1.0) == 0.0)
			return CALC_E_DOMAIN;
	for (j = 0; j < cols; j++)
		if (strided_sum(obs, j, cols, rows, 0, 1.0) == 0.0)
			return CALC_E_DOMAIN;
	x = chi_stat(obs, rows, cols, 0, 1.0);
	/* A grand total above the double range makes every expected count 0. */
	if (!calc__isfinite(x) && scale_of(obs, rows * cols, &e))
		x = scale_back(chi_stat(obs, rows, cols, 1, calc__pow2(1 - e)), e, 1);
	*chi = x;
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
