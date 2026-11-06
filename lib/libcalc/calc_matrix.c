/*
 * Small dense matrices, row-major: product, transpose, matrix times vector,
 * determinant, inverse and linear solve. Determinant, inverse and solve share
 * one LU factorization with partial pivoting in a caller-supplied work
 * matrix; a permutation array of CALC_MAT_MAX entries lives on the stack.
 * Inverse and solve also estimate the 1-norm condition number from those
 * factors and refuse a matrix whose condition number exceeds 1 / (n * eps).
 */

#include <float.h>
#include <math.h>

#include "calc_internal.h"

/* Bytes of a rows-by-cols matrix of doubles, or 0 when the size overflows. */
static size_t
mat_bytes(size_t rows, size_t cols)
{
	if (rows == 0 || cols == 0)
		return 0;
	if (rows > ((size_t)-1) / sizeof(double) / cols)
		return 0;
	return rows * cols * sizeof(double);
}

calc_status
calc_mat_mul(const double *a, size_t m, size_t n, const double *b, size_t p, double *out)
{
	size_t ab = mat_bytes(m, n), bb = mat_bytes(n, p), ob = mat_bytes(m, p);
	size_t i, j, k;
	double s;

	if (a == NULL || b == NULL || out == NULL)
		return CALC_E_ARG;
	if (ab == 0 || bb == 0 || ob == 0)
		return CALC_E_SIZE;
	if (calc__overlap(out, ob, a, ab) || calc__overlap(out, ob, b, bb))
		return CALC_E_ALIAS;
	for (i = 0; i < m; i++) {
		for (j = 0; j < p; j++) {
			s = 0.0;
			for (k = 0; k < n; k++)
				s += a[i * n + k] * b[k * p + j];
			out[i * p + j] = s;
		}
	}
	return CALC_OK;
}

calc_status
calc_mat_vec(const double *a, size_t rows, size_t cols, const double *v, double *out)
{
	size_t ab = mat_bytes(rows, cols);
	size_t i, j;
	double s;

	if (a == NULL || v == NULL || out == NULL)
		return CALC_E_ARG;
	if (ab == 0)
		return CALC_E_SIZE;
	if (calc__overlap(out, rows * sizeof(double), a, ab) ||
	    calc__overlap(out, rows * sizeof(double), v, cols * sizeof(double)))
		return CALC_E_ALIAS;
	for (i = 0; i < rows; i++) {
		s = 0.0;
		for (j = 0; j < cols; j++)
			s += a[i * cols + j] * v[j];
		out[i] = s;
	}
	return CALC_OK;
}

calc_status
calc_mat_transpose(const double *a, size_t rows, size_t cols, double *out)
{
	size_t bytes = mat_bytes(rows, cols);
	size_t i, j;
	double t;

	if (a == NULL || out == NULL)
		return CALC_E_ARG;
	if (bytes == 0)
		return CALC_E_SIZE;
	if (a == out) {
		if (rows != cols)
			return CALC_E_ALIAS;
		for (i = 0; i < rows; i++) {
			for (j = i + 1; j < cols; j++) {
				t = out[i * cols + j];
				out[i * cols + j] = out[j * cols + i];
				out[j * cols + i] = t;
			}
		}
		return CALC_OK;
	}
	if (calc__overlap(a, bytes, out, bytes))
		return CALC_E_ALIAS;
	for (i = 0; i < rows; i++)
		for (j = 0; j < cols; j++)
			out[j * rows + i] = a[i * cols + j];
	return CALC_OK;
}

/*
 * In-place LU factorization with partial pivoting of the n-by-n matrix w.
 * perm[i] receives the original index of the row now at position i, *sign
 * the parity of the row exchanges. Returns non-zero at the first column
 * whose pivot magnitude is at most limit. The strict lower triangle holds
 * the multipliers of the unit lower factor.
 */
static int
lu_factor(double *w, size_t n, size_t *perm, int *sign, double limit)
{
	size_t i, j, k, p, t;
	double best, mag, f, pivot, tmp;

	*sign = 1;
	for (i = 0; i < n; i++)
		perm[i] = i;
	for (k = 0; k < n; k++) {
		p = k;
		best = calc__fabs(w[k * n + k]);
		for (i = k + 1; i < n; i++) {
			mag = calc__fabs(w[i * n + k]);
			if (mag > best) {
				best = mag;
				p = i;
			}
		}
		if (best <= limit)
			return 1;
		if (p != k) {
			for (j = 0; j < n; j++) {
				tmp = w[k * n + j];
				w[k * n + j] = w[p * n + j];
				w[p * n + j] = tmp;
			}
			t = perm[k];
			perm[k] = perm[p];
			perm[p] = t;
			*sign = -*sign;
		}
		pivot = w[k * n + k];
		for (i = k + 1; i < n; i++) {
			f = w[i * n + k] / pivot;
			w[i * n + k] = f;
			for (j = k + 1; j < n; j++)
				w[i * n + j] -= f * w[k * n + j];
		}
	}
	return 0;
}

static double
max_abs(const double *a, size_t count)
{
	double m = 0.0, t;
	size_t i;

	for (i = 0; i < count; i++) {
		t = calc__fabs(a[i]);
		if (t > m)
			m = t;
	}
	return m;
}

/*
 * Common argument checks for the LU users; sets *bytes. A NaN or infinite
 * entry is CALC_E_DOMAIN: the pivot search compares magnitudes, and a NaN
 * compares false against every pivot candidate.
 */
static calc_status
lu_check(const double *a, size_t n, const double *work, size_t *bytes)
{
	size_t i;

	if (a == NULL || work == NULL)
		return CALC_E_ARG;
	if (n == 0 || n > CALC_MAT_MAX)
		return CALC_E_SIZE;
	*bytes = mat_bytes(n, n);
	for (i = 0; i < n * n; i++)
		if (!calc__isfinite(a[i]))
			return CALC_E_DOMAIN;
	return CALC_OK;
}

calc_status
calc_mat_det(const double *a, size_t n, double *work, double *out)
{
	size_t perm[CALC_MAT_MAX];
	size_t bytes, i;
	calc_status st = lu_check(a, n, work, &bytes);
	double mant;
	int sign, ex = 0, e;

	if (st != CALC_OK)
		return st;
	if (out == NULL)
		return CALC_E_ARG;
	if (work != a) {
		if (calc__overlap(a, bytes, work, bytes))
			return CALC_E_ALIAS;
		for (i = 0; i < n * n; i++)
			work[i] = a[i];
	}
	if (lu_factor(work, n, perm, &sign, 0.0) != 0) {
		*out = 0.0;
		return CALC_OK;
	}
	/*
	 * The product of the pivots is kept as a mantissa in (-2^n, 2^n) and an
	 * integer exponent, so a partial product outside the double range
	 * (diag(2^600, 2^600, 2^-600, 2^-600) in either order) cannot overflow or
	 * underflow before the later factors bring it back. With no partial
	 * product outside the normal range the result equals the plain product.
	 */
	mant = (double)sign;
	for (i = 0; i < n; i++) {
		mant *= calc__frexp1(work[i * n + i], &e);
		ex += e;
	}
	/* The mantissa is at least 1, so an exponent past +-2044 is an overflow or an underflow. */
	if (ex > 2044)
		ex = 2044;
	else if (ex < -2044)
		ex = -2044;
	*out = calc__ldexp(mant, ex);
	return CALC_OK;
}

/*
 * Solve with the factors in w: forward substitution through the unit lower
 * factor, back substitution through the upper factor. x and b are n by
 * nrhs; row i of the permuted right-hand side is row perm[i] of b.
 */
static void
lu_substitute(const double *w, size_t n, const size_t *perm, const double *b,
    size_t nrhs, size_t c, double *x)
{
	size_t i, k;
	double s;

	for (i = 0; i < n; i++) {
		s = b[perm[i] * nrhs + c];
		for (k = 0; k < i; k++)
			s -= w[i * n + k] * x[k * nrhs + c];
		x[i * nrhs + c] = s;
	}
	for (i = n; i-- > 0;) {
		s = x[i * nrhs + c];
		for (k = i + 1; k < n; k++)
			s -= w[i * n + k] * x[k * nrhs + c];
		x[i * nrhs + c] = s / w[i * n + i];
	}
}

/*
 * Solve A^T z = c with the factors in w, where PA = LU and row i of PA is
 * row perm[i] of A: A^T = U^T L^T P, so forward substitution through U^T,
 * back substitution through the unit L^T, then z[perm[i]] = v[i].
 */
static void
lu_substitute_transpose(const double *w, size_t n, const size_t *perm,
    const double *c, double *z)
{
	double u[CALC_MAT_MAX] = { 0.0 }, s;
	size_t i, k;

	for (i = 0; i < n; i++) {
		s = c[i];
		for (k = 0; k < i; k++)
			s -= w[k * n + i] * u[k];
		u[i] = s / w[i * n + i];
	}
	for (i = n; i-- > 0;) {
		s = u[i];
		for (k = i + 1; k < n; k++)
			s -= w[k * n + i] * u[k];
		u[i] = s;
	}
	for (i = 0; i < n; i++)
		z[perm[i]] = u[i];
}

/* Solve A x = b for one vector with the factors in w. */
static void
lu_substitute_vector(const double *w, size_t n, const size_t *perm,
    const double *b, double *x)
{
	lu_substitute(w, n, perm, b, 1, 0, x);
}

static double
sum_abs(const double *a, size_t n)
{
	double s = 0.0;
	size_t i;

	for (i = 0; i < n; i++)
		s += calc__fabs(a[i]);
	return s;
}

/*
 * Estimate of the 1-norm of A^-1 from its LU factors: Hager's iteration in
 * Higham's form (at most five solves with A and A^T, each step moving to the
 * unit vector at the largest component of the dual vector), then the
 * alternating-sign vector test, which catches the matrices that defeat the
 * iteration. The estimate is a lower bound of the true norm and is
 * typically within a factor of three of it.
 *
 * Every test vector has a 1-norm of at most 1, so each solution has a 1-norm
 * of at most |A^-1|_1 and the estimate overflows only when |A^-1|_1 does (a
 * test vector with entries up to 2 overflowed for A = DBL_MIN * I). The dual
 * vector carries the factor 1/n and the alternating vector the factor
 * 2 / (3 n), since its entries average 1.5 in magnitude. The comparison of
 * the transposed solution with its product against x is homogeneous in the
 * dual vector, so the iteration visits the same vectors as the unscaled form.
 */
static double
inverse_norm1_estimate(const double *w, size_t n, const size_t *perm)
{
	double x[CALC_MAT_MAX] = { 0.0 }, y[CALC_MAT_MAX] = { 0.0 };
	double z[CALC_MAT_MAX] = { 0.0 };
	double est = 0.0, g, zmax, zx, alt;
	size_t i, j, last = n, iter;

	for (i = 0; i < n; i++)
		x[i] = 1.0 / (double)n;
	for (iter = 0; iter < 5; iter++) {
		lu_substitute_vector(w, n, perm, x, y);
		g = sum_abs(y, n);
		if (iter > 0 && !(g > est))
			break;
		est = g;
		for (i = 0; i < n; i++)
			z[i] = y[i] < 0.0 ? -1.0 / (double)n : 1.0 / (double)n;
		lu_substitute_transpose(w, n, perm, z, y);
		j = 0;
		zmax = calc__fabs(y[0]);
		zx = 0.0;
		for (i = 0; i < n; i++) {
			if (calc__fabs(y[i]) > zmax) {
				zmax = calc__fabs(y[i]);
				j = i;
			}
			zx += y[i] * x[i];
		}
		if (!(zmax > zx) || j == last)
			break;
		for (i = 0; i < n; i++)
			x[i] = 0.0;
		x[j] = 1.0;
		last = j;
	}
	for (i = 0; i < n; i++) {
		alt = n > 1 ? 1.0 + (double)i / (double)(n - 1) : 1.0;
		x[i] = ((i & 1) != 0 ? -alt : alt) * (2.0 / (3.0 * (double)n));
	}
	lu_substitute_vector(w, n, perm, x, y);
	g = sum_abs(y, n);
	if (g > est)
		est = g;
	return est;
}

/* Largest absolute column sum of the n-by-n matrix a. */
static double
norm1(const double *a, size_t n)
{
	double best = 0.0, s;
	size_t i, j;

	for (j = 0; j < n; j++) {
		s = 0.0;
		for (i = 0; i < n; i++)
			s += calc__fabs(a[i * n + j]);
		if (s > best)
			best = s;
	}
	return best;
}

/*
 * norm1(a) / 2^*e. A column sum overflows for entries near DBL_MAX while the
 * condition number stays small, so when the plain norm is not finite the
 * sums are taken over the entries divided by 2^e, e the exponent of the
 * largest entry, which is exact; *e is 0 otherwise.
 */
static double
norm1_scaled(const double *a, size_t n, int *e)
{
	double best = norm1(a, n), m = 0.0, s;
	size_t i, j;

	*e = 0;
	if (calc__isfinite(best))
		return best;
	for (i = 0; i < n * n; i++)
		if (calc__fabs(a[i]) > m)
			m = calc__fabs(a[i]);
	if (!calc__isfinite(m))
		return best;
	(void)calc__frexp1(m, e);
	best = 0.0;
	for (j = 0; j < n; j++) {
		s = 0.0;
		for (i = 0; i < n; i++)
			s += calc__ldexp(calc__fabs(a[i * n + j]), -*e);
		if (s > best)
			best = s;
	}
	return best;
}

/*
 * 1-norm condition estimate of a from its factors in w. Non-zero when it
 * exceeds 1 / (n * eps), when it is not finite and when it is NaN: the
 * largest relative error the inverse can carry then reaches one.
 */
static int
ill_conditioned(const double *a, const double *w, size_t n, const size_t *perm,
    double *cond)
{
	int e;
	double c = norm1_scaled(a, n, &e) * inverse_norm1_estimate(w, n, perm);

	if (e != 0)
		c = calc__ldexp(c, e);

	if (cond != NULL)
		*cond = c;
	return !(c * (double)n * DBL_EPSILON <= 1.0);
}

calc_status
calc_mat_solve(const double *a, size_t n, const double *b, size_t nrhs,
    double *work, double *x)
{
	size_t perm[CALC_MAT_MAX];
	size_t bytes, rbytes, i, c;
	calc_status st = lu_check(a, n, work, &bytes);
	int sign;

	if (st != CALC_OK)
		return st;
	if (b == NULL || x == NULL)
		return CALC_E_ARG;
	rbytes = mat_bytes(n, nrhs);
	if (rbytes == 0)
		return CALC_E_SIZE;
	if (calc__overlap(a, bytes, work, bytes) || calc__overlap(a, bytes, x, rbytes) ||
	    calc__overlap(b, rbytes, x, rbytes) || calc__overlap(work, bytes, b, rbytes) ||
	    calc__overlap(work, bytes, x, rbytes))
		return CALC_E_ALIAS;
	for (i = 0; i < n * n; i++)
		work[i] = a[i];
	if (lu_factor(work, n, perm, &sign,
	    (double)n * DBL_EPSILON * max_abs(a, n * n)) != 0)
		return CALC_E_SINGULAR;
	if (ill_conditioned(a, work, n, perm, NULL))
		return CALC_E_ILLCOND;
	for (c = 0; c < nrhs; c++)
		lu_substitute(work, n, perm, b, nrhs, c, x);
	return CALC_OK;
}

calc_status
calc_mat_inverse(const double *a, size_t n, double *work, double *out)
{
	size_t perm[CALC_MAT_MAX];
	size_t bytes, i, j, k;
	calc_status st = lu_check(a, n, work, &bytes);
	double s;
	int sign;

	if (st != CALC_OK)
		return st;
	if (out == NULL)
		return CALC_E_ARG;
	if (calc__overlap(a, bytes, work, bytes) || calc__overlap(a, bytes, out, bytes) ||
	    calc__overlap(work, bytes, out, bytes))
		return CALC_E_ALIAS;
	for (i = 0; i < n * n; i++)
		work[i] = a[i];
	if (lu_factor(work, n, perm, &sign,
	    (double)n * DBL_EPSILON * max_abs(a, n * n)) != 0)
		return CALC_E_SINGULAR;
	if (ill_conditioned(a, work, n, perm, NULL))
		return CALC_E_ILLCOND;
	/* Column j of the inverse solves A x = e_j; row i of P e_j is 1 where perm[i] == j. */
	for (j = 0; j < n; j++) {
		for (i = 0; i < n; i++) {
			s = perm[i] == j ? 1.0 : 0.0;
			for (k = 0; k < i; k++)
				s -= work[i * n + k] * out[k * n + j];
			out[i * n + j] = s;
		}
		for (i = n; i-- > 0;) {
			s = out[i * n + j];
			for (k = i + 1; k < n; k++)
				s -= work[i * n + k] * out[k * n + j];
			out[i * n + j] = s / work[i * n + i];
		}
	}
	return CALC_OK;
}

calc_status
calc_mat_cond(const double *a, size_t n, double *work, double *cond)
{
	size_t perm[CALC_MAT_MAX];
	size_t bytes, i;
	calc_status st = lu_check(a, n, work, &bytes);
	int sign;

	if (st != CALC_OK)
		return st;
	if (cond == NULL)
		return CALC_E_ARG;
	if (calc__overlap(a, bytes, work, bytes))
		return CALC_E_ALIAS;
	for (i = 0; i < n * n; i++)
		work[i] = a[i];
	if (lu_factor(work, n, perm, &sign,
	    (double)n * DBL_EPSILON * max_abs(a, n * n)) != 0)
		return CALC_E_SINGULAR;
	(void)ill_conditioned(a, work, n, perm, cond);
	return CALC_OK;
}
