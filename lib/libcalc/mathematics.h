/*
 * libcalc: the numerical core of the calculator.
 *
 * Every routine takes its buffers from the caller, allocates nothing, keeps
 * no global mutable state and reports failure through calc_status instead of
 * aborting. Matrices are row-major. Angles take an explicit unit. A routine
 * that returns a status leaves its output unwritten on any status other than
 * CALC_OK, except where a comment says the output is unspecified.
 *
 * An empty sample, a length mismatch, a buffer that is too small and a side
 * above CALC_MAT_MAX report CALC_E_SIZE. A required pointer that is NULL
 * reports CALC_E_ARG.
 *
 * Numeric contract
 *
 * The test suite establishes behavior on IEEE-754 binary64 hardware: round to
 * nearest even, gradual underflow, quiet NaN propagation. The RP2040 boot ROM
 * routines that serve __aeabi_dadd, dsub, dmul and ddiv on the target differ
 * in two ways (RP2040 datasheet section 2.8.3.2; sys/arch/rp2040/doc/research/
 * audit-findings.md): they flush subnormal operands and results to zero, and
 * they return an infinity where IEEE arithmetic returns a NaN. Comparisons
 * stay IEEE (the software compare of lib/libc/arm/gen), as do the sign-bit
 * operations of calc_internal.h, so a NaN that arrives as an argument is still
 * recognized by x != x, and an infinity still fails the finiteness test.
 * libcalc therefore promises, on a target with the ROM provider:
 *
 *   - A finite input of normal magnitude whose exact result is a normal
 *     double gives the documented value within the accuracy the tests state.
 *   - A NaN produced by arithmetic (0/0, inf - inf, inf * 0, a NaN summand)
 *     arrives as an infinity. The functions whose documented value is a NaN
 *     are calc_asinh, calc_acosh and calc_atanh outside their domains, the
 *     NaN passthrough of calc_sinh, calc_cosh, calc_tanh, calc_log10,
 *     calc_sum and calc_mean, and the special-value entries of calc_c_func
 *     and calc_c_*. They return an infinity or a NaN there; a caller treats
 *     a non-finite output as a failed evaluation.
 *   - A NaN argument is still rejected where a function tests its inputs by
 *     comparison: calc_quat_inverse and the matrix routines return
 *     CALC_E_DOMAIN for a NaN or infinite entry on either provider.
 *   - An input or result below DBL_MIN in magnitude is zero. The extended
 *     range of calc_magnitude, calc_normalize, calc_c_abs, the Smith
 *     division of calc_c_div, the complex square root and calc_quat_inverse
 *     holds for magnitudes down to DBL_MIN; below it the scaled intermediate
 *     values are flushed and the result is zero or a domain status.
 *
 * Functions whose documented value depends on IEEE subnormal or NaN
 * behavior, and so differ on the target, are therefore those named in the
 * second and fourth items.
 *
 * The statement follows the datasheet and the audit record. The tests have
 * run on host IEEE hardware and under qemu-arm with libgcc soft-float, which
 * is IEEE and does not exercise the ROM; board execution of calc_test under
 * the hardware opt-in is the measurement that confirms or revises it
 * (docs/research/libcalc-implementation.md).
 */

#ifndef MATHEMATICS_H
#define MATHEMATICS_H

#include <stddef.h>
#include <stdint.h>

#ifndef CALC_MAT_MAX
#define CALC_MAT_MAX 12u	/* largest matrix side for det/inverse/solve */
#endif
#ifndef CALC_FFT_MAX
#define CALC_FFT_MAX 1024u	/* largest transform length (power of two) */
#endif

typedef enum {
	CALC_OK = 0,
	CALC_E_SIZE,	/* empty input, length mismatch, buffer too small, side above maximum */
	CALC_E_DOMAIN,	/* argument outside the mathematical domain */
	CALC_E_RANGE,	/* result or argument outside the supported range */
	CALC_E_SINGULAR,	/* matrix numerically singular */
	CALC_E_ALIAS,	/* output overlaps an input where forbidden */
	CALC_E_ARG,	/* option value out of range, or a required pointer is NULL */
	CALC_E_INVALID,	/* malformed encoded input */
	CALC_E_ILLCOND	/* matrix condition number above 1 / (n * DBL_EPSILON) */
} calc_status;

typedef enum { CALC_RADIANS = 0, CALC_DEGREES = 1 } calc_angle_unit;

typedef struct { double re, im; } calc_cplx;
typedef struct { double w, x, y, z; } calc_quat;

/* --- scalar -------------------------------------------------------------- */

double calc_angle_to_rad(calc_angle_unit u, double a);
double calc_angle_from_rad(calc_angle_unit u, double r);

/*
 * Angle of the point (x, y) counterclockwise from the positive x axis, in
 * [0, one turn) in unit u. The result is +0 for the origin and for any angle
 * that rounds to a full turn.
 */
double calc_atan3(double y, double x, calc_angle_unit u);

/* CALC_E_RANGE when a finite argument gives an infinite result. NaN passes. */
calc_status calc_sinh(double x, double *out);
calc_status calc_cosh(double x, double *out);
calc_status calc_tanh(double x, double *out);	/* saturates to +-1 */

/* CALC_E_DOMAIN for x <= 0. NaN passes. Exact powers of ten give exact results. */
calc_status calc_log10(double x, double *out);

/*
 * Inverse hyperbolics. acosh is NaN below 1, atanh is NaN outside [-1, 1]
 * and +-infinity at +-1.
 */
double calc_asinh(double x);
double calc_acosh(double x);
double calc_atanh(double x);

/* Round half away from zero. Magnitudes at or above 2^52 are returned as is. */
double calc_round(double x);

/* --- statistics on a flat list of n doubles ------------------------------ */

calc_status calc_sum(const double *a, size_t n, double *out);	/* Neumaier-compensated */
calc_status calc_mean(const double *a, size_t n, double *out);

/*
 * scratch holds n doubles and must not overlap a. NaN input gives an
 * unspecified order but never touches memory outside a and scratch.
 */
calc_status calc_median(const double *a, size_t n, double *scratch, double *out);

/* sample != 0 divides by n - 1 and needs n >= 2; sample == 0 divides by n. */
calc_status calc_variance(const double *a, size_t n, int sample, double *out);
calc_status calc_stddev(const double *a, size_t n, int sample, double *out);

/* First occurrence wins on ties. index may be NULL. */
calc_status calc_min(const double *a, size_t n, double *out, size_t *index);
calc_status calc_max(const double *a, size_t n, double *out, size_t *index);

/* Pearson r. CALC_E_DOMAIN when either list has zero variance. */
calc_status calc_correl(const double *a, const double *b, size_t n, double *out);

/*
 * Pearson chi-square statistic of a rows-by-cols table of non-negative
 * counts, row-major. df = (rows - 1) * (cols - 1). rows or cols below 2
 * report CALC_E_SIZE; a negative count or an empty row or column reports
 * CALC_E_DOMAIN.
 */
calc_status calc_chi_square(const double *obs, size_t rows, size_t cols,
    double *chi, size_t *df);

/*
 * Upper-tail probability in percent, 100 * Q(df / 2, chi / 2). chi < 0 is
 * CALC_E_DOMAIN, df == 0 is CALC_E_SIZE, and a series or continued fraction
 * that does not converge is CALC_E_RANGE.
 */
calc_status calc_chi_square_p(double chi, size_t df, double *percent);

/*
 * First level crossing of the sampled signal a. direction is +1 (rising) or
 * -1 (falling); any other value is CALC_E_ARG. *index is the zero-based
 * position, or -1 when none is found or n < 4.
 */
calc_status calc_crossing(const double *a, size_t n, double level, int direction,
    long *index);

/* --- vectors ------------------------------------------------------------- */

calc_status calc_dot(const double *a, const double *b, size_t n, double *out);
calc_status calc_magnitude(const double *a, size_t n, double *out);	/* overflow-safe */
calc_status calc_normalize(const double *a, size_t n, double *out);	/* out may equal a */
calc_status calc_cross3(const double a[3], const double b[3], double out[3]);

/*
 * Rotate the points (xin[i], yin[i]) about (cx, cy). A positive angle turns
 * counterclockwise when y points up. Each output array must equal its input
 * array or be disjoint from every other array; xout and yout must be
 * disjoint.
 */
calc_status calc_rotate2d(double cx, double cy, double angle, calc_angle_unit u,
    const double *xin, const double *yin, size_t n, double *xout, double *yout);

/*
 * Integer points. Results round half away from zero. CALC_E_RANGE when a
 * rounded coordinate leaves the int32_t range; the outputs are then
 * unspecified.
 */
calc_status calc_rotate2d_i32(double cx, double cy, double angle, calc_angle_unit u,
    const int32_t *xin, const int32_t *yin, size_t n, int32_t *xout, int32_t *yout);

/* --- matrices, row-major ------------------------------------------------- */

/* out (m by p) must not overlap a or b. */
calc_status calc_mat_mul(const double *a, size_t m, size_t n,
    const double *b, size_t p, double *out);

/* out (length rows) must not overlap a or v. */
calc_status calc_mat_vec(const double *a, size_t rows, size_t cols,
    const double *v, double *out);

/* out is cols by rows. In place only for a square matrix with out == a. */
calc_status calc_mat_transpose(const double *a, size_t rows, size_t cols, double *out);

/*
 * work holds n * n doubles. work may equal a, which then holds the
 * factorization. Otherwise work must not overlap a. A zero pivot gives
 * exactly 0. A NaN or infinite entry is CALC_E_DOMAIN.
 */
calc_status calc_mat_det(const double *a, size_t n, double *work, double *out);

/*
 * LU with partial pivoting. A pivot whose magnitude is at most
 * n * DBL_EPSILON * max|a| is CALC_E_SINGULAR. A matrix whose estimated
 * 1-norm condition number (calc_mat_cond) exceeds 1 / (n * DBL_EPSILON) is
 * CALC_E_ILLCOND: the computed inverse could have no correct digit. A NaN or
 * infinite entry is CALC_E_DOMAIN. out is untouched on every status other
 * than CALC_OK. work holds n * n doubles; work, a and out must not overlap
 * one another.
 */
calc_status calc_mat_inverse(const double *a, size_t n, double *work, double *out);

/*
 * Solve A x = B for nrhs right-hand sides, B and x being n by nrhs. The
 * singular, ill-conditioned and non-finite statuses of calc_mat_inverse
 * apply, and x is untouched on them. a, b, work and x must not overlap one
 * another.
 */
calc_status calc_mat_solve(const double *a, size_t n, const double *b, size_t nrhs,
    double *work, double *x);

/*
 * Estimate of the 1-norm condition number |A|_1 * |A^-1|_1 of the n by n
 * matrix a, from its LU factors (Hager's iteration; a lower bound that is
 * typically within a factor of three). CALC_E_SINGULAR and CALC_E_DOMAIN as
 * for calc_mat_inverse; a matrix beyond 1 / (n * DBL_EPSILON) is CALC_OK
 * here, with the estimate in *cond, so a front end can report the digits it
 * has left. work holds n * n doubles and must not overlap a.
 */
calc_status calc_mat_cond(const double *a, size_t n, double *work, double *cond);

/* --- complex ------------------------------------------------------------- */

calc_cplx calc_c_make(double re, double im);
calc_cplx calc_c_polar(double radius, double angle, calc_angle_unit u);
double calc_c_abs(calc_cplx z);
double calc_c_arg(calc_cplx z);	/* radians, (-pi, pi] */
double calc_c_phase(calc_cplx z, calc_angle_unit u);
calc_cplx calc_c_conj(calc_cplx z);
calc_cplx calc_c_proj(calc_cplx z);

/* CALC_E_RANGE when finite operands give a non-finite result. */
calc_status calc_c_add(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_sub(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_mul(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_div(calc_cplx a, calc_cplx b, calc_cplx *out);	/* DOMAIN for b == 0 */
/* 0^0 = 1, 0^b = 0 for Re b > 0, every other 0^b is CALC_E_DOMAIN. */
calc_status calc_c_pow(calc_cplx a, calc_cplx b, calc_cplx *out);

typedef enum {
	CALC_C_EXP = 1, CALC_C_LOG, CALC_C_SQRT, CALC_C_SIN, CALC_C_COS, CALC_C_TAN,
	CALC_C_ASIN, CALC_C_ACOS, CALC_C_ATAN, CALC_C_SINH, CALC_C_COSH, CALC_C_TANH,
	CALC_C_ASINH, CALC_C_ACOSH, CALC_C_ATANH
} calc_cfn;

/*
 * Principal values with the branch cuts of C99 Annex G. Circular arguments
 * are radians. CALC_E_DOMAIN at the poles (log 0, atanh +-1, atan +-i);
 * CALC_E_RANGE when a finite argument gives a non-finite result; an unknown
 * code is CALC_E_ARG.
 */
calc_status calc_c_func(calc_cfn fn, calc_cplx z, calc_cplx *out);

/* --- quaternions (the magnitude tag of the original is returned separately) */

calc_quat calc_quat_mul(calc_quat a, calc_quat b);	/* Hamilton product */
calc_quat calc_quat_conj(calc_quat q);
/*
 * Conjugate over the squared norm, computed after scaling by the largest
 * component, so |q| anywhere in [1 / DBL_MAX, DBL_MAX] works. CALC_E_DOMAIN
 * for the zero quaternion and for a NaN or infinite component; CALC_E_RANGE
 * when a component of the inverse is not finite. out is unwritten otherwise.
 */
calc_status calc_quat_inverse(calc_quat q, calc_quat *out);
/* w = 0, unit xyz and the original length. DOMAIN for the zero vector. */
calc_status calc_quat_from_vector(double x, double y, double z, calc_quat *q,
    double *magnitude);
/* The axis is normalized first; a zero axis is CALC_E_DOMAIN. */
calc_status calc_quat_from_axis_angle(double ax, double ay, double az, double angle,
    calc_angle_unit u, calc_quat *q);
/* Qz(roll) * Qy(-yaw) * Qx(pitch). */
calc_quat calc_quat_from_euler(double yaw, double pitch, double roll, calc_angle_unit u);
calc_quat calc_quat_rotate(calc_quat r, calc_quat v);	/* r * v * conj(r) */

/* --- FFT: in place, n a power of two ------------------------------------- */

/*
 * Fill tw[0 .. n/2) with exp(-2 pi i k / n) for later calls with the same n.
 */
calc_status calc_fft_twiddles(calc_cplx *tw, size_t n);

/*
 * Forward transform X[k] = sum x[j] exp(-2 pi i j k / n), or with inverse != 0
 * the inverse transform scaled by 1 / n. twiddle is NULL or a table that
 * calc_fft_twiddles filled for this n. n that is not a power of two (and
 * n == 0) is CALC_E_SIZE; n above CALC_FFT_MAX is CALC_E_RANGE.
 */
calc_status calc_fft(calc_cplx *x, size_t n, int inverse, const calc_cplx *twiddle);

/* Magnitude and phase (radians, no angle unit) of all n bins. */
calc_status calc_fft_magnitude(const calc_cplx *x, size_t n, double *out);
calc_status calc_fft_phase(const calc_cplx *x, size_t n, double *out);

/* --- interpolation and scaling ------------------------------------------- */

double calc_lerp(double a, double b, double t);	/* (b - a) * t + a */

/*
 * Map the range of in onto [output_min, output_max]; output_min may exceed
 * output_max. CALC_E_DOMAIN for constant input. input_min and input_max may
 * be NULL. out may equal in.
 */
calc_status calc_rescale(const double *in, size_t n, double output_min, double output_max,
    double *out, double *input_min, double *input_max);

/*
 * Linear interpolation in a table. xs is strictly ascending (otherwise
 * CALC_E_ARG), n >= 2, and x must lie in [xs[0], xs[n-1]] (otherwise
 * CALC_E_DOMAIN). An x equal to a node returns that node's y exactly.
 */
calc_status calc_interp_table(const double *xs, const double *ys, size_t n, double x,
    double *out);

/* --- element-wise list operations ---------------------------------------- */

/*
 * out may equal an input exactly; any other overlap is CALC_E_ALIAS.
 * Division by zero is CALC_E_DOMAIN and nothing is written.
 */
calc_status calc_vec_add(const double *a, const double *b, size_t n, double *out);
calc_status calc_vec_sub(const double *a, const double *b, size_t n, double *out);
calc_status calc_vec_mul(const double *a, const double *b, size_t n, double *out);
calc_status calc_vec_div(const double *a, const double *b, size_t n, double *out);

/* Integer lists wrap modulo 2^64. INT64_MIN / -1 wraps to INT64_MIN. */
calc_status calc_i64_add(const int64_t *a, const int64_t *b, size_t n, int64_t *out);
calc_status calc_i64_sub(const int64_t *a, const int64_t *b, size_t n, int64_t *out);
calc_status calc_i64_mul(const int64_t *a, const int64_t *b, size_t n, int64_t *out);
calc_status calc_i64_div(const int64_t *a, const int64_t *b, size_t n, int64_t *out);	/* toward zero */
calc_status calc_i64_and(const int64_t *a, const int64_t *b, size_t n, int64_t *out);
calc_status calc_i64_or(const int64_t *a, const int64_t *b, size_t n, int64_t *out);
calc_status calc_i64_xor(const int64_t *a, const int64_t *b, size_t n, int64_t *out);

/*
 * Shift by amount in [-63, 63], else CALC_E_ARG. Positive shifts left
 * (wrapping); negative shifts right, arithmetic unless logical != 0.
 */
calc_status calc_i64_shift(const int64_t *a, size_t n, int amount, int logical,
    int64_t *out);

/* Multiply in double; the integer forms round half away from zero. */
calc_status calc_scale(const double *in, size_t n, double s, double *out);
calc_status calc_scale_to_i64(const double *in, size_t n, double s, int64_t *out);
calc_status calc_scale_i64(const int64_t *in, size_t n, double s, int64_t *out);

/*
 * Raise each element to a power. CALC_E_DOMAIN for a negative base with a
 * non-integer exponent and for 0 raised to a negative exponent; nothing is
 * written then.
 */
calc_status calc_power(const double *in, size_t n, double exponent, double *out);

/* out = (in2 - in1) * ratio + in1. out must not overlap either input. */
calc_status calc_blend(const double *in1, const double *in2, size_t n, double ratio,
    double *out);

/* --- CRC (Rocksoft model, any width 1..64) ------------------------------- */

typedef struct {
	uint8_t width;
	uint64_t poly, init, xorout;
	uint8_t refin, refout;
} calc_crc_spec;

extern const calc_crc_spec calc_crc8, calc_crc8_maxim, calc_crc12_dect, calc_crc12_umts,
    calc_crc16_arc, calc_crc16_ccitt_false, calc_crc16_xmodem, calc_crc32, calc_crc32c,
    calc_crc32_bzip2, calc_crc64_ecma;

/*
 * Bitwise shift-register CRC: reflect each byte if refin, shift the width-bit
 * register most significant bit first, reflect the register if refout, then
 * XOR xorout. A width outside 1..64, a parameter wider than the width or a
 * flag other than 0 or 1 is CALC_E_ARG.
 */
calc_status calc_crc(const calc_crc_spec *spec, const uint8_t *data, size_t len,
    uint64_t *out);

/* --- random numbers: caller-owned state ---------------------------------- */

typedef struct {
	uint32_t mt[624];
	unsigned index;
	double spare;		/* second Marsaglia deviate, valid when have_spare */
	unsigned have_spare;
} calc_rng;

void calc_rng_seed(calc_rng *r, uint32_t seed);	/* MT19937 init_genrand */
uint32_t calc_rng_u32(calc_rng *r);
double calc_rng_unit(calc_rng *r);	/* [0, 1) from one 32-bit word */
double calc_rng_unit53(calc_rng *r);	/* [0, 1) from two words */
double calc_rng_normal(calc_rng *r, double mean, double sd);	/* polar method */

/* --- base64 (RFC 4648, no line breaks) ----------------------------------- */

/* Writes 4 * ceil(n / 3) characters and no terminator. */
calc_status calc_b64_encode(const uint8_t *in, size_t n, char *out, size_t cap,
    size_t *written);
/*
 * ASCII whitespace is skipped. A character outside the alphabet, a length
 * that is not a multiple of four and misplaced padding are CALC_E_INVALID.
 * Nothing is written on a status other than CALC_OK.
 */
calc_status calc_b64_decode(const char *in, size_t n, uint8_t *out, size_t cap,
    size_t *written);

/* --- PID controller (discrete, trapezoidal integral) --------------------- */

typedef struct {
	double kp, ki, kd;
	double tau;		/* derivative low-pass time constant, seconds */
	double period;		/* update interval T, seconds, >= 0.001 */
	double out_lo, out_hi;
	double integ_lo, integ_hi;
	double integrator, last_error, differentiator, last_measurement, output;
} calc_pid;

/* CALC_E_ARG for period < 0.001, tau < 0, an inverted limit pair or a NaN. */
calc_status calc_pid_init(calc_pid *c, double kp, double ki, double kd, double tau,
    double period, double out_lo, double out_hi, double integ_lo, double integ_hi);
double calc_pid_update(calc_pid *c, double setpoint, double measurement);

#endif
