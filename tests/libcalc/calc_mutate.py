#!/usr/bin/env python3
"""Write one mutated copy of lib/libcalc per fault into DEST/NAME/.

usage: calc_mutate.py LIBCALC_DIRECTORY DEST_DIRECTORY

Each mutant replaces one exact text in one source file, and the replacement
must match exactly once, so a rewrite of the library that moves the site
stops this script instead of silently skipping the mutant. check_mutants.sh
then builds the acceptance test against each copy and requires it to fail.
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
        "\t\tden = b.re + b.im * r;",
        "\t\tden = b.re - b.im * r;",
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
        "\t\t*out = 0.5 * (scratch[n / 2 - 1] + scratch[n / 2]);",
        "\t\t*out = scratch[n / 2];",
        "even-length median takes one middle element",
    ),
    (
        "variance_divisor",
        "calc_stats.c",
        "(double)(sample ? n - 1 : n)",
        "(double)n",
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
        "\t\tt = a[i] / m;",
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
        "(in[i] - lo) / span * orange + output_min;",
        "(in[i] - lo) / span * orange + lo;",
        "wrong output offset",
    ),
    (
        "pid_derivative_sign",
        "calc_pid.c",
        "\tc->differentiator = -(2.0 * c->kd",
        "\tc->differentiator = (2.0 * c->kd",
        "derivative on measurement loses its sign",
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
        if text.count(old) != 1:
            sys.exit(
                "calc_mutate: %s: %r occurs %d times in %s" % (name, old, text.count(old), fname)
            )
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text.replace(old, new))
    print("calc_mutate: wrote %d mutants" % len(MUTANTS))


main()
