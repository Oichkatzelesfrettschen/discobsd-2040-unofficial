#!/usr/bin/env python3
"""Write one mutated copy of lib/libcalc per fault into DEST/NAME/.

usage: calc_mutate.py LIBCALC_DIRECTORY DEST_DIRECTORY

Each mutant replaces one exact text in one source file, and the replacement
must match exactly once, so a rewrite of the library that moves the site
stops this script instead of silently skipping the mutant. A fix that spans
several expressions gives OLD and NEW as lists of equal length; each pair must
match once. check_mutants.sh then builds the acceptance test against each copy
and requires it to fail.
"""

import os
import shutil
import sys

# (name, file, old text, new text, fault it models)
MUTANTS = [
    (
        "lu_no_pivoting",
        "calc_matrix.c",
        "\t\t\tif (mag > best) {",
        "\t\t\tif (0 && mag > best) {",
        "LU without partial pivoting",
    ),
    (
        "lu_sign_dropped",
        "calc_matrix.c",
        "\t\t\t*sign = -*sign;",
        "\t\t\t*sign = *sign;",
        "determinant ignores row exchanges",
    ),
    (
        "inverse_threshold_zero",
        "calc_matrix.c",
        "\tif (lu_factor(work, n, perm, &sign,\n"
        "\t    (double)n * DBL_EPSILON * max_abs(a, n * n)) != 0)\n"
        "\t\treturn CALC_E_SINGULAR;\n\tif (ill_conditioned(a, work, n, perm, NULL))\n"
        "\t\treturn CALC_E_ILLCOND;\n\t/* Column j",
        "\tif (lu_factor(work, n, perm, &sign, 0.0) != 0)\n"
        "\t\treturn CALC_E_SINGULAR;\n\tif (ill_conditioned(a, work, n, perm, NULL))\n"
        "\t\treturn CALC_E_ILLCOND;\n\t/* Column j",
        "singular test reduced to an exact zero pivot",
    ),
    (
        "inverse_condition_ignored",
        "calc_matrix.c",
        "\tif (ill_conditioned(a, work, n, perm, NULL))\n\t\treturn CALC_E_ILLCOND;\n\t/* Column j",
        "\t/* Column j",
        "inverse returns a matrix whose condition number exceeds 1 / (n eps)",
    ),
    (
        "solve_condition_ignored",
        "calc_matrix.c",
        "\tif (ill_conditioned(a, work, n, perm, NULL))\n\t\treturn CALC_E_ILLCOND;\n\tfor (c = 0;",
        "\tfor (c = 0;",
        "solve returns a solution of a matrix beyond 1 / (n eps)",
    ),
    (
        "condition_estimate_no_iteration",
        "calc_matrix.c",
        "\t\tif (!(zmax > zx) || j == last)\n\t\t\tbreak;",
        "\t\tif (!(zmax > zx) || j == last || iter == 0)\n\t\t\tbreak;",
        "norm estimate never moves to the dual vector's largest component",
    ),
    (
        "matrix_nonfinite_accepted",
        "calc_matrix.c",
        "\t\tif (!calc__isfinite(a[i]))\n\t\t\treturn CALC_E_DOMAIN;",
        "\t\tif (0)\n\t\t\treturn CALC_E_DOMAIN;",
        "NaN and infinite matrix entries reach the factorization",
    ),
    (
        "quat_inverse_unscaled",
        "calc_quat.c",
        "\ts = calc__fabs(q.w);\n",
        "\ts = 1.0;\n\t(void)calc__fabs(q.w);\n",
        "inverse forms the squared norm of the unscaled components",
    ),
    (
        "fft_twiddle_tenth_digit",
        "calc_fft.c",
        "\t\ttw[k].re = c;",
        "\t\ttw[k].re = c * (1.0 + 1e-10);",
        "twiddle table wrong in the tenth digit",
    ),
    (
        "fft_bit_reversal",
        "calc_fft.c",
        "\t\tbit = n >> 1;",
        "\t\tbit = n > 4 ? n >> 2 : n >> 1;",
        "wrong bit-reversal permutation",
    ),
    (
        "fft_inverse_scale",
        "calc_fft.c",
        "\t\tscale = 1.0 / (double)n;",
        "\t\tscale = 1.0;",
        "inverse transform without 1/n",
    ),
    (
        "crc_reflect_out_from_in",
        "calc_crc.c",
        "\tif (spec->refout)\n\t\treg = reflect(reg, w);",
        "\tif (spec->refin)\n\t\treg = reflect(reg, w);",
        "reflect-out read from the reflect-in flag",
    ),
    (
        "crc_xor_before_reflect",
        "calc_crc.c",
        "\tif (spec->refout)\n\t\treg = reflect(reg, w);\n\t*out = reg ^ spec->xorout;",
        "\treg ^= spec->xorout;\n\tif (spec->refout)\n\t\treg = reflect(reg, w);\n\t*out = reg;",
        "final XOR applied before output reflection",
    ),
    (
        "crc_register_unmasked",
        "calc_crc.c",
        "\t\t\treg = (reg << 1) & mask;",
        "\t\t\treg = reg << 1;",
        "register not masked to its width",
    ),
    ("rng_seed_multiplier", "calc_rng.c", "1812433253u", "1812433254u", "wrong seeding constant"),
    ("rng_tempering", "calc_rng.c", "\ty ^= y >> 18;", "\ty ^= y >> 17;", "wrong tempering shift"),
    (
        "complex_div_denominator",
        "calc_complex.c",
        "\t\t*den = b.re + b.im * r;",
        "\t\t*den = b.re - b.im * r;",
        "wrong Smith denominator",
    ),
    (
        "complex_sqrt_scaling",
        "calc_complex.c",
        "calc__copysign(scale * (b / (2.0 * t)), z.im));",
        "calc__copysign(scale * (z.im / (2.0 * t)), z.im));",
        "unscaled operand in the large-argument square root",
    ),
    (
        "complex_acos_sign",
        "calc_complex.c",
        "\treturn cmk(re, -calc__copysign(lg, y));",
        "\treturn cmk(re, calc__copysign(lg, y));",
        "inverse cosine imaginary part on the wrong side of the cut",
    ),
    (
        "quat_product_sign",
        "calc_quat.c",
        "a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x",
        "a.w * b.y + a.x * b.z + a.y * b.w + a.z * b.x",
        "wrong Hamilton product term",
    ),
    (
        "quat_euler_yaw",
        "calc_quat.c",
        "calc_angle_to_rad(u, -yaw)",
        "calc_angle_to_rad(u, yaw)",
        "yaw not negated",
    ),
    (
        "median_even_average",
        "calc_stats.c",
        "\tm = 0.5 * (lo + hi);",
        "\tm = hi;",
        "even-length median takes one middle element",
    ),
    (
        "variance_divisor",
        "calc_stats.c",
        "(double)(sample ? n - 1 : n)",
        "(double)(sample ? n : n)",
        "sample variance divides by n",
    ),
    (
        "sum_uncompensated",
        "calc_stats.c",
        "\t\ta->c += (a->s - t) + x;",
        "\t\ta->c += 0.0;",
        "compensation dropped from the larger-magnitude branch",
    ),
    (
        "chi_gamma_epsilon",
        "calc_stats.c",
        "#define GAMMA_EPS\t1.0e-17",
        "#define GAMMA_EPS\t1.0e-3",
        "series stops early",
    ),
    (
        "crossing_second_pass",
        "calc_stats.c",
        "\t\t\t    a[i] < a[i + 2] && a[i + 4] > a[i + 2]) {",
        "\t\t\t    a[i] < a[i + 2] && a[i + 4] < a[i + 2]) {",
        "plateau rule accepts the wrong neighbor",
    ),
    (
        "magnitude_unscaled",
        "calc_vector.c",
        "\t\tt = a[i] / mx;",
        "\t\tt = a[i];",
        "magnitude overflows near 1e200",
    ),
    (
        "atan3_wrap",
        "calc_scalar.c",
        "\t\tr += CALC_TWO_PI;",
        "\t\tr += CALC_PI;",
        "negative angles shifted by half a turn",
    ),
    (
        "sincos_quadrant",
        "calc_util.c",
        "\t\t*c = -sn;\n\t\tbreak;\n\tcase 2:",
        "\t\t*c = sn;\n\t\tbreak;\n\tcase 2:",
        "wrong sign in the second quadrant",
    ),
    (
        "log10_exact_power",
        "calc_scalar.c",
        "\t\tif (x == pow10_exact(k))\n\t\t\tv = nearest;",
        "\t\tif (x == pow10_exact(k) && k > 40)\n\t\t\tv = nearest;",
        "powers of ten no longer exact",
    ),
    (
        "b64_invalid_as_zero",
        "calc_base64.c",
        "\t\tif (v < 0)\n\t\t\treturn CALC_E_INVALID;",
        "\t\tif (v < 0)\n\t\t\tv = 0;",
        "characters outside the alphabet read as zero",
    ),
    (
        "i64_or_is_and",
        "calc_elem.c",
        "\t\tout[i] = a[i] | b[i];",
        "\t\tout[i] = a[i] & b[i];",
        "OR performs AND",
    ),
    (
        "rescale_offset",
        "calc_interp.c",
        "\t\tout[i] = span_mix(output_min, output_max,",
        "\t\tout[i] = span_mix(output_min + lo, output_max,",
        "wrong output offset",
    ),
    (
        "pid_derivative_sign",
        "calc_pid.c",
        "\tc->differentiator = (-2.0 * c->kd",
        "\tc->differentiator = (2.0 * c->kd",
        "derivative on measurement loses its sign",
    ),
    (
        "pid_feedback_negated",
        "calc_pid.c",
        "\tc->differentiator = (-2.0 * c->kd * (measurement - c->last_measurement) +",
        "\tc->differentiator = -(2.0 * c->kd * (measurement - c->last_measurement) +",
        "feedback term carries the sign of the input term, moving the pole to z = -a",
    ),
    (
        "pid_tau_zero_accepted",
        "calc_pid.c",
        "!(tau > 0.0)",
        "!(tau >= 0.0)",
        "a zero derivative time constant is accepted",
    ),
    (
        "stddev_null_output_unguarded",
        "calc_stats.c",
        "\tif (a == NULL || out == NULL)\n\t\treturn CALC_E_ARG;\n"
        "\tif (n == 0 || (sample && n < 2))\n\t\treturn CALC_E_SIZE;\n"
        "\tif (variance_rerun(a, n, sample, DBL_MIN, &v, &e))",
        "\tif (a == NULL)\n\t\treturn CALC_E_ARG;\n"
        "\tif (n == 0 || (sample && n < 2))\n\t\treturn CALC_E_SIZE;\n"
        "\tif (variance_rerun(a, n, sample, DBL_MIN, &v, &e))",
        "standard deviation writes through a NULL output",
    ),
    (
        "cond_vectors_unscaled",
        "calc_matrix.c",
        [
            "\t\t\tz[i] = y[i] < 0.0 ? -1.0 / (double)n : 1.0 / (double)n;",
            "\t\tx[i] = ((i & 1) != 0 ? -alt : alt) * (2.0 / (3.0 * (double)n));",
            "\tg = sum_abs(y, n);\n\tif (g > est)\n\t\test = g;\n\treturn est;",
        ],
        [
            "\t\t\tz[i] = y[i] < 0.0 ? -1.0 : 1.0;",
            "\t\tx[i] = (i & 1) != 0 ? -alt : alt;",
            "\tg = 2.0 * sum_abs(y, n) / (3.0 * (double)n);\n"
            "\tif (g > est)\n\t\test = g;\n\treturn est;",
        ],
        "condition-estimate test vectors with entries up to 2 overflow near DBL_MIN",
    ),
    (
        "det_plain_product",
        "calc_matrix.c",
        "\t\tmant *= calc__frexp1(work[i * n + i], &e);\n\t\tex += e;",
        "\t\tmant *= work[i * n + i];\n\t\te = 0;\n\t\tex += e;",
        "determinant multiplies the pivots directly and overflows or underflows early",
    ),
    (
        "catanh_odd_symmetry",
        "calc_complex.c",
        "\txa = calc__fabs(x);",
        "\txa = x;",
        "atanh real part evaluated at x instead of |x|: log1p(-1) near x = -1",
    ),
    (
        "catanh_log_difference",
        "calc_complex.c",
        "\tif (d < 0x1p-1000)",
        "\tif (0)",
        "atanh within 2^-500 of a pole keeps 4x/d, which overflows",
    ),
    (
        "asin_tiny_imaginary_dropped",
        "calc_complex.c",
        "\tif (ay < 1.0e-154 && x < 1.0e150) {",
        "\tif (0 && ay < 1.0e-154 && x < 1.0e150) {",
        "the inverse functions square an imaginary part below sqrt(DBL_MIN)",
    ),
    (
        "hyp_mul_sign_replaced",
        "calc_complex.c",
        "\t*sh = (0.5 * fs * e) * e;\n\tif (y < 0.0)\n\t\t*sh = -*sh;",
        "\t*sh = calc__copysign((0.5 * fs * e) * e, y);",
        "the large-argument sinh product takes the sign of y alone",
    ),
    (
        "cdiv_numerator_unscaled",
        "calc_complex.c",
        " ||\n\t    !calc__isfinite(q.re) || !calc__isfinite(q.im)) &&",
        ") &&",
        "complex division keeps a Smith numerator that overflowed",
    ),
    (
        "span_mix_unscaled",
        "calc_interp.c",
        "\tif (calc__isfinite(d) || !calc__isfinite(lo) || !calc__isfinite(hi))",
        "\tif (1)",
        "rescaling and table lookup keep an output span that overflowed",
    ),
    (
        "hyp_mul_overflow",
        "calc_complex.c",
        "\tif (ay < 709.0) {",
        "\tif (ay < 1e300) {",
        "complex sin, cos, sinh and cosh form an overflowing cosh before the factor",
    ),
    (
        "norm1_unscaled",
        "calc_matrix.c",
        "\tif (calc__isfinite(best))\n\t\treturn best;\n\tfor (i = 0; i < n * n; i++)",
        "\treturn best;\n\tfor (i = 0; i < n * n; i++)",
        "the condition estimate keeps a column sum that overflowed",
    ),
    (
        "cdiv_unscaled",
        "calc_complex.c",
        "\tif ((!calc__isfinite(den) || calc__fabs(den) < CALC_DBL_MIN ||",
        "\tif (0 && (!calc__isfinite(den) || calc__fabs(den) < CALC_DBL_MIN ||",
        "complex division keeps a denominator that overflowed or went subnormal",
    ),
    (
        "asin_large_modulus",
        "calc_complex.c",
        "\t\t*lg = CALC_LN2 + calc__log(big) + 0.5 * calc__log1p(q * q);",
        "\t\t*lg = CALC_LN2 + calc__log(calc__hypot(x, ay));\n\t\t(void)big;\n\t\t(void)q;",
        "the asymptotic inverse functions form |z|, which overflows past DBL_MAX",
    ),
    (
        "log1p_large_argument",
        "calc_util.c",
        "\tif (x > 0x1p53)\n\t\treturn calc__log(x);\n",
        "",
        "log1p of a huge argument forms log(u) * x, which overflows",
    ),
    (
        "mean_overflow_rerun",
        "calc_stats.c",
        "\tif (!calc__isfinite(m) && scale_of(a, n, &e))\n\t\tm = scale_back(list_sum",
        "\tif (0 && scale_of(a, n, &e))\n\t\tm = scale_back(list_sum",
        "mean of a list whose sum overflows",
    ),
    (
        "median_midpoint_overflow",
        "calc_stats.c",
        "\tif (!calc__isfinite(m) && calc__isfinite(hi - lo))",
        "\tif (0)",
        "even-length median adds two large values before halving",
    ),
    (
        "variance_overflow_rerun",
        "calc_stats.c",
        "\tif (*v >= lowest && *v <= DBL_MAX)\n\t\treturn 0;",
        "\tif (*v >= lowest)\n\t\treturn 0;",
        "variance and deviation of data whose squared deviations overflow",
    ),
    (
        "stddev_underflow_rerun",
        "calc_stats.c",
        "\tif (variance_rerun(a, n, sample, DBL_MIN, &v, &e))\n\t\t*out",
        "\tif (variance_rerun(a, n, sample, 0.0, &v, &e))\n\t\t*out",
        "standard deviation of a variance that underflows to zero",
    ),
    (
        "correl_underflow_rerun",
        "calc_stats.c",
        "\treturn !(x >= DBL_MIN && x <= DBL_MAX);",
        "\treturn !(x >= 0.0 && x <= DBL_MAX);",
        "correlation whose variances underflow to zero is reported as constant",
    ),
    (
        "correl_overflow_rerun",
        "calc_stats.c",
        "\treturn !(x >= DBL_MIN && x <= DBL_MAX);",
        "\treturn !(x >= DBL_MIN);",
        "correlation of lists whose squares overflow",
    ),
    (
        "chi_square_product_order",
        "calc_stats.c",
        [
            "\t\t\te = rowtot * (strided_sum(obs, j, cols, rows, on, s) / total);",
            "\t\t\tacc_add(&stat, d * (d / e));",
        ],
        [
            "\t\t\te = rowtot * strided_sum(obs, j, cols, rows, on, s) / total;",
            "\t\t\tacc_add(&stat, d * d / e);",
        ],
        "expected count formed from the product of two totals",
    ),
    (
        "chi_square_total_rerun",
        "calc_stats.c",
        "\tif (!calc__isfinite(x) && scale_of(obs, rows * cols, &e))",
        "\tif (0 && scale_of(obs, rows * cols, &e))",
        "chi-square of a table whose grand total overflows",
    ),
    (
        "normalize_overflow_direction",
        "calc_vector.c",
        "\t\tif (!calc__isfinite(len)) {\n\t\t\tlen = m * 0x1p-32 * root;",
        "\t\tif (0) {\n\t\t\tlen = m * 0x1p-32 * root;",
        "normalization divides by a norm above DBL_MAX and returns zeros",
    ),
    (
        "power_integral_limit",
        "calc_elem.c",
        "\tintegral = calc__isfinite(exponent) && exponent == floor(exponent);",
        "\tintegral = calc__isfinite(exponent) && calc__fabs(exponent) <= 1.0e9 &&\n"
        "\t    exponent == floor(exponent);",
        "exponents above 1e9 are not integers",
    ),
    (
        "power_large_parity",
        "calc_elem.c",
        "\t\tif (odd && calc__signbit(in[i]))",
        "\t\tif (0 && odd && calc__signbit(in[i]))",
        "negative base with a large odd exponent loses its sign",
    ),
    (
        "rescale_span_overflow",
        "calc_interp.c",
        "\thalve = !calc__isfinite(span) && calc__isfinite(lo) && calc__isfinite(hi);",
        "\thalve = 0;",
        "rescale divides by an input range above DBL_MAX",
    ),
    (
        "interp_span_overflow",
        "calc_interp.c",
        "fraction(x, xs[lo], xs[hi], span, !calc__isfinite(span));",
        "fraction(x, xs[lo], xs[hi], span, 0);",
        "table interpolation divides by a node spacing above DBL_MAX",
    ),
]


def main() -> None:
    src, dest = sys.argv[1], sys.argv[2]
    names = [m[0] for m in MUTANTS]
    if len(set(names)) != len(names):
        sys.exit("calc_mutate: duplicate mutant names")
    shutil.rmtree(dest, ignore_errors=True)
    for name, fname, old, new, _what in MUTANTS:
        target = os.path.join(dest, name)
        os.makedirs(target)
        for entry in sorted(os.listdir(src)):
            if entry.endswith((".c", ".h")):
                shutil.copy(os.path.join(src, entry), target)
        path = os.path.join(target, fname)
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        olds = old if isinstance(old, list) else [old]
        news = new if isinstance(new, list) else [new]
        if len(olds) != len(news):
            sys.exit("calc_mutate: %s: OLD and NEW differ in length" % name)
        for o, n in zip(olds, news):
            if text.count(o) != 1:
                sys.exit(
                    "calc_mutate: %s: %r occurs %d times in %s" % (name, o, text.count(o), fname)
                )
            text = text.replace(o, n)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
    print("calc_mutate: wrote %d mutants" % len(MUTANTS))


main()
