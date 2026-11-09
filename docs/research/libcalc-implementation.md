# libcalc: implementation of the clean-room mathematics specification

Status: implementer deliverable for `docs/research/picomite-maths-clean-room-spec.md`. The library lives in
`lib/libcalc`, the acceptance gate in `tests/libcalc` (`check-libcalc`). `lib/Makefile` builds the library
once a program links it (see "Follow-ups"). The reviewed revision adds matrix conditioning, a scaled quaternion inverse, a numeric
contract for the board's ROM float routines, a qemu-arm Thumb-1 run and execution receipts; "Revision after
review" lists each change.

## Provenance statement

The implementation was written from the specification alone. The files read were:

- `docs/research/picomite-maths-clean-room-spec.md` (all of it);
- repository conventions: `AGENTS.md`, `lib/Makefile`, `lib/libbn/Makefile`, `tests/libbn/Makefile`,
  `tests/libbn/check_mutants.sh`, `tests/libbn/check_object.sh`, `share/mk/sys.mk`, `share/mk/warnings.mk`,
  `tools/compiler-probe.sh`, `games/keen/Makefile`, the head of `games/keen/keen.c`, and the license header
  of `sys/arch/rp2040/include/scb.h`;
- the tree's own C library, to learn which functions exist and how accurate they are: `include/math.h` and
  `lib/libm/{Makefile,exp,log,sqrt,sin,sinh,tanh,atan,asin,pow,hypot}.c`.

No PicoMite or MMBasic source, no file under the downloaded-source scratch directory, no vendor PicoCalc
repository and no PicoMite manual was opened, searched or fetched. Algorithms come from textbooks and
standards named in the specification (Rocksoft CRC model, RFC 4648, MT19937 paper, C99 Annex G) and from
standard numerical analysis (Smith division, Neumaier summation, LU with partial pivoting, Stirling series,
Lentz continued fraction, Marsaglia polar method, Cody-Waite argument reduction, radix-2 FFT).
The files carry no copyright line and no license block: by the owner's ruling they inherit the repository
`LICENSE` (BSD 3-Clause). Each file opens with a plain comment that states its purpose.

## What was implemented

Public interface: `lib/libcalc/mathematics.h` (the specification's sketch, refined). Sources are split by
area so that an archive link pulls only the objects a program references.

| Object | Contents |
|---|---|
| `calc_util.c` | `calc__exp`, `calc__log`, `calc__log1p`, scaled `calc__hypot`, `calc__atan2`, `calc__sincos` (private helpers, `calc_internal.h`) |
| `calc_scalar.c` | angle units, `calc_atan3`, `calc_sinh/cosh/tanh`, `calc_log10`, `calc_asinh/acosh/atanh`, `calc_round` |
| `calc_stats.c` | sum (Neumaier), mean, median (heap sort of a caller scratch), variance, stddev, min, max with position, correlation, chi-square statistic and exact tail probability, level crossing |
| `calc_vector.c` | dot, overflow-safe magnitude, normalize, cross3, 2-D rotation (double and `int32_t`) |
| `calc_matrix.c` | multiply, matrix times vector, transpose, determinant, inverse, linear solve (one LU with partial pivoting), 1-norm condition estimate (Hager) |
| `calc_complex.c` | `calc_cplx` arithmetic (Smith division, power), all 15 elementary functions, polar, abs, arg, phase, conj, proj |
| `calc_quat.c` | Hamilton product, conjugate, true inverse (scaled by the largest component), from vector, from axis and angle, from Euler angles, rotate |
| `calc_fft.c` | in-place radix-2 FFT with optional twiddle table, inverse with 1/n, magnitude, phase |
| `calc_interp.c` | `calc_lerp`, `calc_rescale` (WINDOW), `calc_interp_table` |
| `calc_elem.c` | element-wise add, subtract, multiply, divide, `int64_t` arithmetic and bit operations, shift, scale, power, blend (INTERPOLATE) |
| `calc_crc.c` | generic Rocksoft-model CRC for widths 1 to 64 and 11 presets |
| `calc_rng.c` | MT19937 with caller-owned state, unit deviates, polar normal deviate |
| `calc_base64.c` | RFC 4648 encode and strict decode |
| `calc_pid.c` | discrete PID update and parameter validation |

The defects the specification lists as not to be copied are absent and each has a test: the CRC reflect-out
flag is its own parameter and the final XOR follows reflection for every width; the 12-bit register is masked;
OR and XOR on integer lists perform OR and XOR; SCALE multiplies in double and rounds only on store;
`calc_quat_from_axis_angle` converts the angle once and normalizes the axis; MAX and MIN start from the first
element; chi-square has no table limit and the probability is exact; base64 decode rejects bad input; the
determinant and inverse use elimination, not cofactor recursion; the random stream is the standard MT19937.

## Decisions where the specification was ambiguous

1. **File layout.** `mathematics.h` is the public header as named. The single `MATHEMATICS.c` is replaced by
   the per-area objects above because a static archive links whole objects, and a calculator that needs
   `calc_crc` should not pull in the complex functions.
2. **`calc_rng` carries the polar-method cache.** The specification asks for a cached second normal deviate
   and a caller-owned state; the sketch lacked the field, so `spare` and `have_spare` were added.
   `calc_rng_seed` clears the cache. A state that was never seeded produces tempered zeros.
3. **FFT table.** `calc_fft_twiddles(tw, n)` fills the n/2 table and `calc_fft` takes it as `const`. Size
   errors: a length that is zero or not a power of two is `CALC_E_SIZE`; a power of two above `CALC_FFT_MAX`
   is `CALC_E_RANGE`, matching the two error classes the specification names.
4. **Chi-square probability.** The specification names a Lanczos log-gamma. The short six-term
   Lanczos table in common use has a relative error near 2e-10, which can exceed the 1e-8 percent
   tolerance, and a longer table is a coefficient list to retype from memory. `log_gamma` instead shifts the argument to at least 15
   and applies the Stirling series through a^-11 (remainder below 1e-16). The series and the Lentz fraction
   stop at 10000 terms instead of the specification's "about 200" because a degrees-of-freedom value of a few
   thousand needs more; non-convergence is `CALC_E_RANGE`.
5. **Chi-square table errors.** Fewer than two rows or columns is `CALC_E_SIZE` (degrees of freedom below 1);
   a negative count, or a row or column that sums to zero (zero expected count), is `CALC_E_DOMAIN`. The
   expected-count loop recomputes column totals, which costs O(rows^2 * cols) and needs no buffer.
6. **`atanh(+-1)`.** The sketch says NaN outside the open interval; `calc_atanh` returns +-infinity at +-1
   (the C99 and IEEE result) and NaN beyond. The complex `atanh` at +-1 is `CALC_E_DOMAIN`.
7. **`calc_atan3`.** A result that rounds to one full turn (a tiny negative angle) is returned as +0, so the
   range stays half open. The origin returns +0 without calling `atan2`.
8. **`calc_log10`.** Exact powers of ten up to 10^22 return exact integers (the plain `log * log10(e)`
   product is not exact for 1e15, 1e-15, 1e-4, 1e-2 and 1e-1).
9. **Matrix aliasing and the singular rule.** `calc_mat_det` accepts `work == a` (the factors replace the
   input) and otherwise rejects overlap. Inverse and solve reject every overlap among `a`, `work`, `out`, `b`
   and `x`. The inverse does not run Gauss-Jordan on the output: it factors `work` with partial pivoting
   first, applies the specification's threshold `n * DBL_EPSILON * max|a|` to the pivots (identical pivots
   to Gauss-Jordan), and writes `out` only after the factorization passed, so the "output untouched on
   singular" requirement holds. The determinant tests for an exactly zero pivot, as specified, and returns
   exactly 0. Two checks follow the specification's threshold; see decisions 21 and 22.
10. **Division by zero in lists.** The specification mandates a domain status for integer division and for
    complex division. `calc_vec_div` also reports `CALC_E_DOMAIN` for a zero divisor (either sign of zero)
    before writing anything, instead of producing infinities.
11. **Element-wise aliasing.** An output may equal an input pointer exactly; a shifted overlap is
    `CALC_E_ALIAS`. `calc_blend` rejects any overlap, as specified.
12. **Operand length mismatch.** Every API takes one length, so the specification's "unequal lengths" and
    "inner dimensions differ" cases cannot be expressed. The tests cover the reachable error paths (zero
    length, overlapping buffers) instead.
13. **CRC.** One per-bit recurrence serves every width, so widths 3, 5 and 7 work; `calc_crc32` is CRC-32/
    ISO-HDLC, the form `zlib.crc32` matches. The "command default" parameter sets (CRC-12 with polynomial
    0x80D, and so on) are test vectors, not presets. A parameter wider than the width, a width outside 1..64
    or a flag other than 0 or 1 is `CALC_E_ARG`; `data == NULL` with `len == 0` is the empty message.
14. **Base64.** Encode writes no terminator. Decode skips space, tab, CR and LF, accepts non-zero unused
    bits in the final group (RFC 4648 allows decoders to), validates the whole input before writing, and
    reports a short buffer as `CALC_E_SIZE`.
15. **PID.** `calc_pid` is a struct whose field order and names differ from the original 14-element array,
    which belongs to the interpreter binding. `calc_pid_init` returns `CALC_E_ARG` for a period below 0.001
    (the original's INIT rule), a `tau` that is zero or negative, an inverted limit pair or a non-finite gain.
    The derivative recurrence is the trapezoidal form of the specification's section 3.12 (decision 25).
16. **Complex.** One `calc_c_arg` serves both the C_ARG and C_CARG spellings; the calculator command should
    document that the manual says `C_ARG` and the original dispatches `C_CARG`. A non-finite operand passes
    through without a range check. Overflow of finite operands is `CALC_E_RANGE`. Magnitudes beyond 1e150
    switch the inverse circular functions to their leading asymptotic terms.
17. **Normal deviate.** A negative `sd` mirrors the deviate; no status is returned. The polar method draws
    from `calc_rng_unit53`.
18. **`calc_rotate2d`.** Each output array must equal its own input or be disjoint from it, and the crossed
    pairs and the two outputs must be disjoint. The `int32_t` variant is `calc_rotate2d_i32`; on
    `CALC_E_RANGE` the outputs are unspecified.
19. **Median.** `scratch` must not overlap the input (`CALC_E_ALIAS`); NaN input gives an unspecified order
    but no out-of-bounds access (the heap sort compares with strict less-than).
20. **Power.** An exponent is integral when it is finite and equals its floor, whatever its size. Integral
    exponents up to 2^31 in magnitude use binary exponentiation (exact for small integers, at most 32
    squarings); larger integral exponents use `exp(e * log|x|)` with the sign of a negative base taken from
    the parity of the exponent (every finite double of magnitude 2^53 or more is even). Other exponents use
    `exp(e * log(x))`; exponent 0.5 uses `sqrt`. The former cut at 1e9 made `(-1)^1000000001` and
    `(-1)^1e300` report `CALC_E_DOMAIN`.

21. **Conditioning (beyond the specification).** The pivot threshold accepts matrices whose inverse carries
    no correct digit: the 12-by-12 Hilbert matrix returned `CALC_OK` with `max|A * inv - I|` 0.0625, and
    `[[1, 1], [1, 1 + 1e-15]]` returned an entry near 9e14. `calc_mat_inverse` and `calc_mat_solve` now
    estimate the 1-norm condition number from the LU factors (Hager's iteration in Higham's form with the
    alternating-vector test, at most five solves with A and A^T, O(n^2) each) and return the new status
    `CALC_E_ILLCOND` when it exceeds `1 / (n * DBL_EPSILON)`, leaving the output untouched. The status is
    appended to the enum, so the existing values do not move. The estimate is a lower bound of the true
    condition number and typically within a factor of three, so the refusal point can sit below the true
    threshold by that factor. `calc_mat_cond(a, n, work, &cond)` returns the estimate with `CALC_OK` for any
    non-singular finite matrix, so a front end can show the digits left. The exact 1-norm condition numbers
    in the test come from rational arithmetic: Hilbert 4, 6, 8 and 10 are 28375, 29070279, 33872791095 and
    35357439251992 (accepted, the last with about two digits left), Hilbert 12 is 4.1e16 (refused).
    A NaN or infinite entry is `CALC_E_DOMAIN` for the determinant, inverse, solve and condition routines.
    Before, an infinite entry reported `CALC_E_SINGULAR` (the threshold became infinite) and a NaN entry
    reported `CALC_OK` with NaN output, because `mag > best` is false for NaN.
22. **Quaternion inverse.** The squared norm is formed after dividing by the largest component magnitude, so
    it lies in [1, 4]; the components of the inverse are `conj(q / s) / |q / s|^2 / s`, divided in two
    steps. `(1e-170, 0, 0, 0)` used to return `CALC_E_DOMAIN` (the square underflowed) and `(1e200, 0, 0, 0)`
    returned `CALC_OK` with w = 0 (the square overflowed); both now give the exact inverse, and the tests
    cover scales from 1e-200 to 1e200. A NaN or infinite component is `CALC_E_DOMAIN`; a quaternion whose
    inverse leaves the double range (magnitude below 1 / DBL_MAX, which needs a subnormal) is
    `CALC_E_RANGE`; the output is unwritten on both.
23. **Numeric contract.** `mathematics.h` states what the RP2040 boot ROM float routines change (subnormal
    operands and results flushed to zero, NaN results returned as infinities, comparisons unchanged) and
    which functions depend on IEEE subnormal or NaN behavior. The statement follows the datasheet and
    `sys/arch/rp2040/doc/research/audit-findings.md`; no libcalc check has run on the ROM, so board
    execution under the hardware opt-in is the measurement that confirms or revises it. The prediction to
    falsify: the NaN-propagation and subnormal-range checks fail on the board and every finite
    normal-range check passes. The new quaternion range check (`1e-310`) is such a subnormal check.
24. **NULL required pointers.** `calc_stddev` wrote through its output before checking it. A sweep in
    `calc_test.c` calls every status-returning entry point once per required pointer with that pointer NULL
    and requires `CALC_E_ARG`; the sweep catches SIGSEGV and SIGBUS with `sigaction` and `sigsetjmp`, so a
    deleted guard is a `FAIL` line and not a crash. The qemu-arm run defines `CALC_TEST_NO_SIGNALS`
    (bare-metal newlib has no `sigaction`) and runs the same calls without recovery. `check_null_sweep.sh`
    holds the sweep to the status-returning entry points declared in `mathematics.h`, so a new function
    without a sweep entry fails the `check-null-sweep` target.
25. **PID derivative.** The derivative acts on the measurement, D(s) = -Kd s / (tau s + 1) M(s). The bilinear
    transform gives d[n] = (-2 Kd (m[n] - m[n-1]) + (2 tau - T) d[n-1]) / (2 tau + T), whose pole is
    (2 tau - T) / (2 tau + T). The shipped code and the specification carried the feedback term with the
    sign of the input term, which put the pole at the negative of that value: with tau much larger than T
    the derivative rang with alternating sign, and a ramp of slope r settled at -Kd r T / (2 tau) instead of
    -Kd r. The specification (section 3.12) now records the legacy form and the corrected form as a
    Required divergence, the first vector of section 5.14 is regenerated, and `tau <= 0` is `CALC_E_ARG`
    (at tau = 0 the corrected form alternates between plus and minus 2 Kd / T and the old one accumulates).
    Two properties independent of any stored vector gate the recurrence: a ramp settles at -Kd r, and a held
    step decays monotonically with one sign for tau >= T / 2.
26. **Range of intermediates.** The promise
    in `mathematics.h` is that a finite normal input with a normal exact result gives the documented value.
    Eleven routines broke it by overflowing or underflowing an intermediate. Each fix keeps the plain
    expression on the common path and reruns on a scaled copy only when the plain result left the normal
    range, using the helpers of `calc_internal.h` (`calc__pow2`, `calc__ldexp`, `calc__frexp1`), which build a
    power of two from the IEEE exponent field. A power of two is exact on the host and on the ROM provider,
    and no helper forms a subnormal factor.
    - Statistics (`calc_stats.c`): `calc_mean`, `calc_variance`, `calc_stddev`, `calc_correl` and
      `calc_chi_square` rerun on the list scaled by 2^(1 - e), e being the exponent of the largest magnitude,
      when the plain result is non-finite (mean, variance, chi-square), outside [DBL_MIN, DBL_MAX]
      (standard deviation: a deviation inside the range can belong to a variance outside it) or when a
      variance of the correlation is outside that range. The rerun skips lists with a NaN, an infinity or
      nothing above the subnormal range. The even-length median uses `lo + (hi - lo) / 2` when `lo + hi`
      overflows. Chi-square always forms `rowtot * (coltot / total)` and `d * (d / e)`: the products of two
      totals and of two deviations underflow into the subnormal range near 1e-160 and overflow near 1e154.
    - `calc_mat_det` keeps the product of the pivots as a mantissa in (-2^n, 2^n) and an integer exponent,
      always (not only on overflow): the result equals the plain product bit for bit while no partial
      product leaves the normal range, and a partial product that underflowed into the subnormal range no
      longer costs bits. `diag(2^600, 2^600, 2^-600, 2^-600)` gives 1 in every order.
    - The condition estimate scales its test vectors so that each has 1-norm at most 1: the dual vector
      entries are +-1/n and the alternating vector is multiplied by 2 / (3 n). `DBL_MIN * I` gave an
      infinite estimate and `CALC_E_ILLCOND`. A matrix whose inverse has a 1-norm beyond DBL_MAX still
      has an infinite estimate (entries near 1e-300 with a condition number of 1e8 give `inf` from
      `calc_mat_cond` and `CALC_E_ILLCOND` from `calc_mat_inverse`); that residual is not fixed.
    - `calc_normalize`, `calc_quat_from_vector` and `calc_quat_from_axis_angle` share `calc__direction`. When
      the norm exceeds DBL_MAX the quotient is formed from operands scaled by 2^-32, which returns the
      quotient the unbounded range would. `calc_quat_from_vector` of `(DBL_MAX, DBL_MAX, DBL_MAX)` returns
      the exact unit direction and a length of +infinity with `CALC_OK`.
    - `catanh_` (serving `atanh` and `atan`) evaluates the real part at |x| and copies the sign of x,
      since `4x/d` rounds to -1 at x = -1; within 2^-500 of a pole it uses half the difference of two
      logarithms of moduli. `calc__log1p` returns `log(x)` above 2^53.
    - `calc_rescale` and `calc_interp_table` form the ratio from halved differences when the range of the
      inputs exceeds DBL_MAX.
27. **Contract narrowing.** The raw bilinear forms (`calc_dot`, `calc_cross3`, `calc_mat_mul`,
    `calc_mat_vec`, `calc_rotate2d`, `calc_rotate2d_i32`, `calc_lerp`, `calc_blend`, `calc_quat_mul`,
    `calc_quat_rotate`) evaluate the formula the header states. `calc_dot` of `(1e200, -1e200, 1)` with
    `(1e200, 1e200, 1)` is NaN where the exact result is 1. Scaling each call costs a pass over the data per
    call, so `mathematics.h` now limits the range promise for these routines to inputs whose documented
    intermediate products and sums are finite.

## Findings about the tree's `lib/libm`

Method: the `lib/libm` sources were compiled for the host (`-std=gnu89 -fno-builtin`, `errno` renamed) and
compared with `long double` results from the host C library at 200000 random points per range
(`tests/libcalc` rebuilds the same objects for its tree-libm run). Observations:

- `sin` and `cos` lose absolute accuracy in proportion to the argument: maximum absolute error 1.1e-15 for
  |x| <= 7, 1.2e-14 for |x| <= 100, 1.5e-13 for |x| <= 1000 and 1.2e-11 for |x| <= 1e5.
- `acos` is wrong for negative arguments: it computes `acos(|x|)` (for example `acos(-0.5)` returns
  1.0471975511965976 where 2.0943951023931955 is correct).
- `atan2(0, 0)` returns pi/2, and `atan2(-0.0, x)` ignores the sign of a negative zero.
- `exp` above about 770 returns the finite `HUGE_VAL` (3.4e38 in the tree's `math.h`), not infinity.
- Measured relative errors: `exp` 3.2e-15 on [-30, 30] and 4.9e-14 on [-700, 700]; `log` 3.5e-15 on
  [0.01, 100]; `sqrt` 1.7e-16; `atan2` 4.5e-16; `sinh`, `cosh`, `tanh` below 3e-15 on [-25, 25].

Consequence: libcalc computes `sin`/`cos` itself (`calc__sincos`: three-part reduction by pi/2, valid below
1e6 radians, then Taylor polynomials, 1.6e-16 absolute over |x| <= 1e5), builds the hyperbolics, `log1p`
and the inverse hyperbolics on `exp` and `log`, wraps `atan2`, `exp` and `log` for the special cases above,
and never calls `acos`, `asin`, `sinh`, `cosh`, `tanh`, `pow` or `hypot` from libm. It still depends on
`exp`, `log`, `sqrt`, `atan2` and `floor`. No tolerance in the tests was loosened: the run linked against the
tree libm meets the specification's 1e-12 everywhere (table below).

## Test evidence

Commands (the caller exports `PYTHON`, as for every Python-backed gate in the tree):

    cd lib/libcalc   && bmake MACHINE=rp2040 all host size
    cd tests/libcalc && bmake MACHINE=rp2040 check check-cross check-qemu

`check` builds `calc_test.c` against the library sources with
`-std=c17 -pedantic-errors -Wall -Wextra -Werror -Wshadow -Wconversion -Wold-style-definition -O1` and runs
it four ways; each run executes 14645 checks and passes. `check-qemu` links the same test with newlib as a
Thumb-1 program and runs it under qemu-arm user-mode emulation:

| Variant | Evidence class | Result |
|---|---|---|
| native (x86-64, host libm) | host execution | 14645 checks pass |
| `-m32 -msse2 -mfpmath=sse` (ILP32) | host execution at the target's integer widths | 14645 pass |
| AddressSanitizer plus UndefinedBehaviorSanitizer, `-fno-sanitize-recover=all` | host execution | 14645 pass |
| tree `lib/libm` sources linked ahead of the host libm | host execution against the tree's libm | 14645 pass |
| `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft`, newlib, qemu-arm 11.1.3 | instruction execution under emulation with libgcc soft-float (IEEE) | 11788 pass |

The qemu run uses `tests/libbn/qemu_support.c` for the Linux system calls newlib needs and defines
`CALC_TEST_NO_HOST_COMPLEX`: the 15-function comparison with the C library's `<complex.h>` drops out,
because newlib's `catan`, `casinh`, `cacosh` and `catanh` disagree with C99 Annex G on the branch cuts and at
signed zeros (23 differences, all in that grid; the Annex G table of `complex_vectors.h` still runs and
passes). `readelf -A` of the binary reads `Tag_CPU_arch: v6S-M` and `Tag_THUMB_ISA_use: Thumb-1`. This
executes the generated Thumb-1 code and the 32-bit ABI; libgcc's soft-float is IEEE, so it neither
exercises the boot ROM routines (flushed subnormals, NaN as infinity) nor measures silicon. The qemu skip
path stays an `@echo SKIP` line, like the `libbn` one; the width-based execution inventory has no class for
an emulator run.

Execution receipts. The ILP32, sanitizer and tree-libm variants and the mutant run go through
`tools/test-execution.mk` (`libcalc.native`, `libcalc.ilp32`, `libcalc.sanitized`, `libcalc.treelibm`,
`libcalc.negative`), listed in `tools/test-execution-inventory.json` and reached by the root `Makefile`
target `check-libcalc` from `check-ilp32-execution-recipes`. With `REQUIRE_ILP32=yes` a missing ILP32
runtime is an `ERROR` receipt and a failing make, where the former `@echo SKIP` exited 0 (checked with
`ILP32_OK=no`: `SKIP` without the variable, `ERROR` with it). `bmake check-test-execution` passes (42
tests).

Achieved accuracy against the host `long double` library, reported by the test (`INFO accuracy` line):
with the host libm, `sincos` 1.6e-16 absolute, hyperbolics 3e-16, `log10` 1.6e-16, inverse hyperbolics
2.8e-16 relative, `atan3` 8e-16 absolute; with the tree libm, `sincos` 1.6e-16, hyperbolics 3.2e-15, `log10`
7.1e-16, inverse hyperbolics 1.3e-15, `atan3` 8.3e-16. The test asserts bounds of 1e-15 (sincos), 1e-14 (atan3)
and 1e-13 (the others).

Vector coverage: every vector of sections 5.1 to 5.14 appears in `calc_test.c` with the specification's
tolerance (`TOL` 1e-12; FFT 1e-9; chi-square probability 1e-8 percent). Section 5.7 is generated into
`complex_vectors.h` by `gen_complex_vectors.py`, which parses the specification and cross-checks each of its
72 expected values against `cmath` before writing; the 15 elementary functions are also compared on a
15-by-64 grid of points (including both signs of zero) with the host `<complex.h>` (host variants only).
Checks marked "extra" in the test add: branch cuts, poles and overflow, inverse-function round trips,
near-axis accuracy, a second crossing pass in both directions, FFT against a direct DFT at every power of
two from 4 to 1024 (n = 4, 8, ..., 1024) with and without the twiddle table, matrix conditioning
(Hilbert 4 to 12, the nearly parallel pair, scale invariance, non-finite entries), quaternion inverses from
1e-200 to 1e200, CRC widths below 8 and CRC-64/XZ (check values 0x4, 0x19, 0x75 and 0x995DC9BBDF1939FA,
confirmed by an independent bitwise Python model), integer list wraparound, alias and size errors for every
routine family. AES (section 5.15) and sensor fusion (section 5.16) vectors are not used (deferred).

FFT tolerance. The section 5.11 vectors keep the specification's 1e-9. The extra comparison with a direct
DFT asserts, per bin, `4 * DBL_EPSILON * log2(n) * sqrt(n) * |x|_2`: Higham's forward-error bound for a
radix-2 FFT (`|yhat - y|_2 <= log2(n) * eta * |y|_2` with `|y|_2 = sqrt(n) * |x|_2` by Parseval and
eta about 4 `DBL_EPSILON` for a twiddle error of one `DBL_EPSILON`), applied to one bin because a bin cannot
exceed the norm. The reference sum is formed from a library-computed table indexed by `(k * j) mod n`.
Measured errors sit near 2 percent of the bound (3.1e-14 against 5.3e-12 at n = 1024). The previous
`1e-9 * max(peak, 1)` was about five orders looser and the loop ran n = 2, 8, 32, 128, 512, so 1024 and the
table path above 4 and 8 points went unchecked despite this record. A twiddle table scaled by a relative
1e-10 now fails the check by three to four orders of magnitude (mutant `fft_twiddle_tenth_digit`).

Mutants (`calc_mutate.py`, `check_mutants.sh`): the unmutated sources pass, then 55 single-fault copies must
each make `calc_test` exit 1 with a `FAIL` line. All 55 are rejected (`calc mutants: 55 of 55 rejected`;
the 34 of the first round are listed next and the 21 of the range review after them;
the 28 listed next, then six added for conditioning, non-finite matrices, the quaternion scaling and the
twiddle table):
LU without pivoting, determinant sign dropped, inverse threshold reduced to exact zero, wrong FFT bit
reversal, inverse FFT without 1/n, CRC reflect-out read from the reflect-in flag, CRC final XOR before
reflection, unmasked CRC register, wrong MT19937 seed multiplier, wrong tempering shift, wrong Smith
denominator, unscaled large-argument complex square root, inverse cosine on the wrong side of the cut,
wrong Hamilton product term, yaw not negated, even median taking one middle element, sample variance
dividing by n, uncompensated summation, early-stopping gamma series, wrong crossing plateau neighbor,
unscaled magnitude, wrong ATAN3 wrap, wrong sine/cosine quadrant sign, inexact powers of ten, base64
invalid characters read as zero, OR performing AND, wrong rescale offset and PID derivative sign; then
inverse without the condition check, solve without it, a condition estimate that never leaves its first
iterate, non-finite entries reaching the factorization, an unscaled quaternion inverse and a twiddle table
wrong in the tenth digit. The `inverse_threshold_zero` anchor moved with the new check.
The 21 mutants of the range review restore the former expression of one fix each: the PID feedback sign
(`pid_feedback_negated`; `pid_derivative_sign` now drops the input sign) and a zero `tau`; the missing
`calc_stddev` output guard, which the NULL sweep reports as a fault; the unscaled condition-estimate vectors;
the plain determinant product; the atanh real part at x instead of |x|, without the log-difference branch
near a pole, and `calc__log1p` without its large-argument branch; the mean, median, variance and standard
deviation without their overflow reruns, the standard deviation without the underflow rerun, the
correlation without the overflow rerun and without the underflow rerun, the chi-square with the product
order and without the total rerun; normalization without the scaled direction; the power with the 1e9
integer cut and without the parity sign; and rescaling and table interpolation without halved
differences. `check-mutants` runs them with the rest. The NULL sweep is held to the header by
`check_null_sweep.sh` (64 entry points), calibrated by a copy of the test without the `calc_stddev` entry. The
powers-of-ten mutant survived the first test version (the specification's vectors do not expose the
inexact cases), which led to the exact-power checks. Separately, the first run of the near-axis and
large-magnitude complex checks found two real defects in `calc_complex.c`, both fixed: an unscaled operand
in the large-argument square root, and cancellation in `s + x - 1` that has to be evaluated as
`s + (x - 1)` so the small difference A - x keeps its digits.

Lint: `shellcheck -S error` on both scripts, `ruff check`, `ruff format`, `flake8` and `mypy --strict` on both
Python files, all clean. Not run: `clang-tidy` (the tree's lib gates do not require it).

## Target size (Cortex-M0+, soft float)

Built with the tree's flags (`bmake MACHINE=rp2040 size` in `lib/libcalc`: `arm-none-eabi-gcc` 16.2.0,
`-Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -nostdinc -std=c17 -pedantic-errors` plus the tree's
`-Wall -Wextra -Werror`). Bytes of `arm-none-eabi-size`; constants (CRC presets, polynomial coefficients)
are in text:

| Object | text | data | bss |
|---|---|---|---|
| `calc_base64.o` | 685 | 0 | 0 |
| `calc_complex.o` | 4928 | 0 | 0 |
| `calc_crc.o` | 868 | 0 | 0 |
| `calc_elem.o` | 2556 | 0 | 0 |
| `calc_fft.o` | 868 | 0 | 0 |
| `calc_interp.o` | 932 | 0 | 0 |
| `calc_matrix.o` | 4208 | 0 | 0 |
| `calc_pid.o` | 692 | 0 | 0 |
| `calc_quat.o` | 1912 | 0 | 0 |
| `calc_rng.o` | 576 | 0 | 0 |
| `calc_scalar.o` | 2140 | 0 | 0 |
| `calc_stats.o` | 4940 | 0 | 0 |
| `calc_util.o` | 1544 | 0 | 0 |
| `calc_vector.o` | 1704 | 0 | 0 |
| **total** | **28553** | **0** | **0** |

The range review added 2036 bytes of text (26517 to 28553, 7.7 percent), measured by `arm-none-eabi-size` on
the objects before and after (`arm-none-eabi-gcc` 16.2.0, `-Os`): `calc_stats.o` +1184, `calc_interp.o` +280,
`calc_matrix.o` +200, `calc_elem.o` +112, `calc_complex.o` +112, `calc_vector.o` +212, `calc_util.o` +32,
`calc_quat.o` -92, `calc_pid.o` -4. The statistics dominate because five functions gained a scaled rerun
with its soft-float loops. The common path of each routine runs the same operations as before, apart from
the determinant, which always multiplies mantissas, and the quaternion constructors, which share
`calc__direction`.

Largest function frame (`-fstack-usage`): 560 bytes (`inverse_norm1_estimate`, four 12-entry vectors plus
the inlined substitutions); `calc_mat_inverse` adds 176 (the 12-entry permutation array included), so the
inverse path peaks near 740 bytes, and the largest frame outside the matrix object is 256 bytes
(`calc_quat_from_euler`). The linked objects reference only `atan2`, `cos`,
`exp`, `floor`, `log`, `sin`, `sqrt`, `memcpy`, `memset` and compiler-runtime `__aeabi_*` helpers; `check-cross` links
the objects with `ld -r`, lists the undefined symbols against that allow-list, and rejects a calibration
object that calls `malloc`. The 28.6 KB total (26.5 KB before the range fixes, 24.6 KB before the
conditioning estimate and the scaled quaternion inverse) lies far below the 192 KB process window; a program that links
only the statistics or the CRC pulls in one or two objects plus `calc_util.o`.

Not run: execution on the ISA of silicon, and execution through the boot ROM float routines. The qemu-arm run
above executes the Thumb-1 code with libgcc's IEEE soft-float. An earlier attempt with the rdimon semihosting
specs in `qemu-system-arm -M mps2-an385` produced no console output and was dropped in favor of user-mode
emulation. Soft-float cycle counts were not measured.

## Deferred

- **AES-128 (ECB, CBC, CTR).** Cryptography is outside a calculator, and an unreviewed cipher implementation
  is a liability; the specification itself says to use a vetted library if ever needed.
- **Sensor fusion (Madgwick and Mahony filters).** The board has no inertial input source, and the filters
  are hardware-bound.
- **PID channel scheduler** (INIT, START, STOP, interrupt binding) and the 14-element array form: they belong
  to the interpreter. The update formula is implemented and tested.
- **Display helpers** (V_PRINT, M_PRINT) and the **delegated array commands** (SET, ADD, SLICE, INSERT):
  front-end and list-type concerns, as the specification states.
- **Real-input FFT convenience wrappers** (load zero imaginary parts, copy real parts out): thin callers.

## Revision after review

Each change answers a finding in `docs/research/calculator-and-games-review.md`.

- Licensing: the "Copyright (c) 2026 DiscoBSD" and ISC headers are removed from every file under `lib/libcalc`,
  `tests/libcalc`, `lib/libbn` and `tests/libbn`; the files inherit the repository `LICENSE`.
- Execution receipts: see "Test evidence"; the root `Makefile` gains `check-libcalc` and `check-libbn`, both in
  `check-ilp32-execution-recipes`, and the inventory gains nine variants (five for libcalc, four for libbn)
  with their edges.
- FFT check: n = 4 to 1024 by doubling, both twiddle paths, derived tolerance.
- Quaternion inverse: scaled norm, non-finite and range statuses.
- Matrix: `CALC_E_ILLCOND`, `calc_mat_cond`, `CALC_E_DOMAIN` for non-finite entries.
- Numeric contract in `mathematics.h` for the ROM float routines; qemu-arm Thumb-1 run.
- Spelling: "centre" became "center" in `calc_stats.c`.
- `tests/libbn/check_mutants.sh` bounds each mutant at 120 seconds, as the libcalc runner does.

## Revision after the second review

The range, PID and NULL-pointer rows of the round 2 adjudication (`docs/research/calculator-and-games-review-round2.md`
on the calculator-and-games branch) are implemented as decisions 20 and 24 to 27. The check count of
`calc_test` rose from 13511 to 14645 on the host variants and from 10654 to 11788 under qemu-arm. The new
checks are: the NULL sweep (64 entry points, one call per required pointer); the exact scale relation at
k = -1000, -500, 500 and 1000 for mean, median, standard deviation, variance (k = +-500, the result of
k = +-1000 is not a normal double), correlation (each list scaled alone and both inversely), chi-square,
normalization, magnitude, the quaternion constructors, the condition estimate, the determinant with one row
scaled, rescaling and table interpolation; the determinant of every ordering of diagonals with a
representable exact determinant; the extreme-range cases (lists of 1e308, squares near 1e154 and 1e-160, tables of
1e300 and 1e-160, vectors of `DBL_MAX`, `DBL_MIN * I`); power at exponents 2^31 + 1, 2^52 + 1, 2^53 and
1e300 for bases -1, -2, -0.5, 2, 0.5, 1 and 0; the PID ramp and held-step properties; and 28 pole-approach
rows (atanh at +-1 and atan at +-i with offsets of 1e-10, 1e-153 and 1e-160 on either side of each pole, and
at 1 - 1e-10), generated by `gen_complex_vectors.py` from mpmath at 60 digits. The mpmath values agree with
the reference values of the review (atanh(-1 + 1e-10 i) = -11.8594990552502 + 0.785398163422448 i). The new
test run against the sources before the range fixes reports 281 failed checks on 79 distinct lines, among them the
`calc_stddev` call with a NULL output, which the sweep recovers from as a fault.

Not changed: the condition estimate is infinite for a matrix whose inverse 1-norm exceeds DBL_MAX (decision
26), and the raw bilinear forms keep their narrowed promise (decision 27).
