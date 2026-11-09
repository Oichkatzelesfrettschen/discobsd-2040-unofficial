# Clean-room specification of the PicoMite MATHS module behavior

Status: analyst deliverable for the clean-room reimplementation of the MATH command and MATH() function
family found in PicoMite MMBasic. Prepared 2026-11-05. The implementer reads this document and nothing
else from the original; the reviewer compares the implementation against the original afterwards.

Evidence class: static reading of the original source (the original was never compiled or executed),
cross-read against the PicoMite User Manual chapters for the MATH command and function. Every test vector
in section 5 was computed independently of the original with Python 3.14.9, numpy 2.5.4, scipy 1.18.1,
mpmath 1.4.1, the standard library (`math`, `cmath`, `statistics`, `fractions`, `zlib`, `binascii`,
`base64`) and the OpenSSL command-line tool. Where this document states what the original "does", the
statement comes from reading and has not been confirmed by running it; the legacy-versus-required split
in each item records every place where the reading found a defect or an undocumented quirk.

## 1. Provenance and license analysis

### 1.1 Artifact identity

| Item | Value |
|---|---|
| File | `MATHS.c` in the PicoMite source directory of the PicoCalc firmware archive (`PicoCalc/Code/pico_multi_booter/picomite/`) |
| Size | 114025 bytes, 3226 lines |
| SHA-256 | `80add18770e6b3af8db10796b4a5ce93292609145aabf7e8f9e184b8acfbe306` |
| Companion header | `MATHS.h`, 7056 bytes, SHA-256 `c3f2c03e51f54a76593e68904c9a2bb94c704e2a1f9bcba25527fbfbe5bc3ecc` |
| Authors named | Geoff Graham, Peter Mather (PicoMite MMBasic) |
| Also read for context | `Functions.c` (the plain trig functions and the angle-unit variables), `Commands.c` (the array fill, add, slice and insert commands that the MATH dispatcher forwards to), `MMBasic.c` (the float-to-integer conversion), the manual text |

### 1.2 License text carried by the file

The file header (identical in `MATHS.h`) reads, with the copyright-holder placeholders exactly as printed
by the authors:

```
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
   on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
   by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
   without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, ... (standard BSD disclaimer of warranty and liability)
```

Further provenance inside or beside the file:

- A Mersenne Twister block credited in a comment to Evan Sultanik, who in turn cites the Matsumoto and
  Nishimura 1998 paper.
- A CRC block under an MIT License notice naming Rob Tillaart (2021-2024). The MIT notice requires its own
  copyright and permission text to travel with copies of that block.
- The AES wrapper calls an external `aes.c`/`aes.h` pair (a separate file in the same directory, not part of
  the 3226 lines analyzed). Its license was not analyzed here.
- The iterative radix-2 FFT routine has no attribution in the file. Its structure (power-of-two check by
  level count, half-length twiddle table, bit-reversal swap, three nested loops) matches a widely
  circulated third-party reference implementation. Attribution is unverified; the reviewer treats it as
  third-party code of unknown license.

### 1.3 Why clauses 3 and 4 block import into a BSD-3/ISC tree

The tree's own license is the BSD 3-Clause text, and new port files carry the ISC permission notice (see
the repository `NOTICE`). Both grant redistribution on conditions that stop at: keep the notice in source,
reproduce it in binaries, no endorsement. The file adds two obligations that neither license has.

1. Clause 3 requires that the name "MMBasic" be used when the interpreter is mentioned in documentation and
   promotional material, and that the original copyright message be displayed on the console at startup. A
   calculator program that contains no interpreter has nothing to name, and a startup banner demanded by an
   imported file would bind every downstream boot console and every redistributor of the tree. The
   obligation travels with the code, so every recipient of the combined work would receive terms beyond BSD-3
   or ISC.
2. Clause 4 is the advertising clause. It obliges every advertising material that mentions features or use of
   the software to carry an acknowledgement. The University of California rescinded the identical clause from
   its own license in 1999 because it is unworkable for aggregated distributions; the tree's BSD-3 text
   omits it. The clause as written here also names an unfilled `<copyright holder>` placeholder, so even the
   required wording cannot be determined from the file.

Clause 5 (no endorsement) matches BSD-3 and raises no conflict. The result: no line of `MATHS.c` or
`MATHS.h` can be imported, adapted line by line, or translated. Ideas, algorithms, mathematical identities,
published constants (standard CRC polynomials, the Mersenne Twister tempering masks), interface behavior and
numeric results are facts and carry no such obligation. The CRC block's MIT notice and the unattributed FFT
are additional reasons to keep the reviewer's comparison strict. This analysis is an engineering judgment,
not legal advice; the repository owner decides any residual question.

### 1.4 Clean-room protocol in force

- Analyst: the session that wrote this document. It read the original, and it wrote this specification in its
  own words, structure and names. It copies no code, no comment text, no identifier beyond the published
  interface names that an interoperable behavior must keep, and no constant table.
- Implementer: a separate session or person. It reads this specification only, plus public standards and
  permissively licensed references (IEEE 754, C17, the Rocksoft CRC model, RFC 4648, FIPS 197, NIST SP
  800-38A, the Matsumoto-Nishimura Mersenne Twister paper, textbooks). It never opens `MATHS.c`, `MATHS.h`,
  or any other file of the PicoMite tree, and never searches for MMBasic source text. It records every
  source it consults. When a behavior is unclear it asks the analyst for a revised specification rather than
  reading the original.
- Reviewer: a third party, distinct from both. It holds the original and the finished implementation, runs the
  mechanical checks of section 6, and issues a written verdict covering shared identifiers, shared constants
  tables, shared string literals, shared code shapes and comment overlap. A finding is resolved by the
  implementer rewriting the offending region from this specification, never by editing toward the original.
- Directional rule: information flows analyst to implementer only through this document. The reviewer's
  hits are reported as "region X matches the checklist" without quoting original code.

## 2. Conventions used throughout

Legacy means the behavior the original exhibits as read. Required means the behavior the new implementation
must have. They coincide unless the item says otherwise. Where legacy contains an apparent defect, the
required behavior is the corrected one and the defect is recorded so the reviewer can confirm the
implementation did not inherit it.

- Angle unit. The original keeps a process-wide angle unit, radians by default, degrees when the user selects
  them. In degree mode an angle input is divided by 180/pi (57.2957795130823229) before use and an angle
  output is multiplied by that factor. The new API takes the unit as an explicit argument. Hyperbolic
  functions, complex-argument trigonometry and FFT phase take no unit.
- Number types. The original works on 64-bit floats (double) and 64-bit signed integers; arrays are one or
  both. Converting a float to an integer rounds half away from zero, is exact truncation above 2^52 in
  magnitude, and is a range error outside the signed 64-bit range. The new API works on `double` and, where
  the item says so, `int64_t`.
- Array layout. The original declares a two-dimensional matrix as (columns, rows) with the first index
  varying fastest in memory, which equals row-major storage of a rows-by-columns matrix. The new API uses
  explicit `rows` and `cols` with row-major storage. All vectors in section 5 give matrices as rows.
- Index base. The original honors a user-selected index base (0 or 1) for indices it reports. The new API
  reports zero-based indices; the caller adds a base.
- Error classes. The original raises a text error and aborts the statement. The new API returns a status
  code from this set: size (length or dimension mismatch, empty input, buffer too small), domain (argument
  outside the function's mathematical domain), range (result or argument outside a representable or
  supported range), singular (matrix numerically singular), alias (output overlaps an input where
  forbidden), argument (option value out of range), invalid input (malformed encoded data).
- Tolerance. Unless an item states otherwise, floating-point results match the expected value to a relative
  error of 1e-12, or an absolute error of 1e-12 when the expected magnitude is below 1. Integer, bit and
  byte results match exactly. The accuracy of the in-tree `lib/libm` was not measured here; if it cannot meet
  1e-12 for the hyperbolic, trigonometric and logarithmic items, the implementer measures it, reports the
  achieved bound and loosens only those items, naming the bound in the test.

## 3. Inventory of behavior

Each item below is an independent specification. Headings use the original interface name where the
interface name is a fact that tests must refer to. Items marked first/second/last give the priority for
the calculator (section 4). Algorithm complexity uses n for vector length, m, n, p for matrix dimensions.

Scope summary. The file implements 97 distinct commands and functions, specified below: 5 scalar, 30
complex, 10 statistics, 6 vector, 4 matrix, 6 quaternion, 13 array and seeding commands, 1 uniform random
function, 4 FFT forms, 4 CRC widths, 2 base64 directions, 4 PID forms, 6 AES mode-and-direction pairs and
2 sensor-fusion filters. Four commands that the dispatcher forwards elsewhere (fill, add, slice, insert) and
two printing helpers are named but not specified. The original has no normal-distribution generator, no
linear-system solver, no variance function, no table interpolation (its INTERPOLATE blends two arrays), no
3-D rotation-matrix code (rotation is 2-D point rotation and quaternion rotation), and no command that uses
its 64-bit CRC routine. The specification adds, as marked extensions, a linear solver, variance, a normal
deviate, table interpolation, the 64-bit CRC and the true quaternion inverse, because a calculator needs them.

### 3.1 Scalar, trigonometric and hyperbolic functions

#### ATAN3 -- two-argument arctangent mapped to a full turn
- Inputs: two reals, y first and x second (the manual heading prints them in the opposite order; the
  original evaluates y first).
- Output: the angle of the point (x, y) measured counterclockwise from the positive x axis, in the range
  [0, 2 pi) radians, expressed in the active angle unit (0 to 360 in degree mode).
- Numeric semantics: compute the usual two-argument arctangent in (-pi, pi]; add 2 pi to a negative result;
  scale by the unit factor. A result of negative zero stays negative zero (legacy); required: return +0.
  ATAN3(0, 0) returns 0.
- Errors: none beyond wrong argument count.
- Algorithm class: libm `atan2` plus a branch. O(1).
- Priority: first.

#### SINH, COSH, TANH -- hyperbolic functions
- Input: one real (not an angle; no unit conversion). Output: the hyperbolic sine, cosine, tangent.
- Edge cases: overflow of sinh/cosh returns an infinity in legacy; required: range status when a finite
  argument gives an infinite result. tanh saturates to +-1 without error.
- Algorithm class: libm calls. O(1). Priority: first.

#### LOG10 -- common logarithm with explicit domain errors
- Input: one real. Output: the base-10 logarithm.
- Errors: an argument equal to zero raises a division-by-zero style error; a negative argument raises a
  negative-argument error. Both map to the domain status. NaN passes through unchanged.
- Algorithm class: libm `log10`. O(1). Priority: first.

### 3.2 Complex numbers

Legacy representation: a complex value is a pair of 32-bit floats packed into one 64-bit integer (real part
in the low half), so every operation runs in single precision and returns a result rounded to float. Required:
a struct of two doubles; every result in this subsection is computed in double and the legacy rounding is not
reproduced. The C library the tree provides has no complex header and no inverse hyperbolic functions, so
the implementer writes these from the identities in a textbook; all functions use principal values and the
branch cuts of C99 Annex G.

Items, all first priority for the calculator unless stated. Complexity O(1) each.

#### Complex construction and parts
- CPLX(re, im): the pair (re, im).
- POLAR(radius, angle): radius*cos(theta) + i*radius*sin(theta) with theta the angle converted from the
  active unit to radians.
- REAL(z), IMAG(z): the components.
- MOD(z): sqrt(re^2 + im^2) (use a scaled formula such as hypot to avoid overflow; legacy squares in double).
- PHASE(z): atan2(im, re), converted to the active unit.
- CARG(z): atan2(im, re) in radians always. The manual names this function `C_ARG`; the original dispatches
  the keyword `C_CARG`. Provide one function with radians output and report the name mismatch in the
  documentation of the calculator command.

#### Complex arithmetic
- ADD, SUB, MUL: componentwise sum, difference and the usual product (a+bi)(c+di).
- DIV: the quotient; legacy follows IEEE rules for a zero divisor (infinities or NaN). Required: domain
  status when the divisor is exactly (0,0). Use a scaled algorithm (Smith's method) to avoid spurious
  overflow.
- POW(a, b): exp(b * log(a)) with the principal logarithm. For a = 0: b = 0 gives 1, real part of b above
  zero gives 0, anything else gives the domain status.
- CONJ: negate the imaginary part. PROJ: identity for finite z; for any infinite component the result is
  (+inf, +-0) with the zero carrying the sign of the imaginary part.
- ABS: the modulus. Legacy returns it packed as a complex number with zero imaginary part (so the caller
  reads it with the real-part function); required: return a plain real.

#### Complex elementary functions
EXP, LOG (principal), SQRT (principal, nonnegative real part), SIN, COS, TAN, ASIN, ACOS, ATAN, SINH, COSH,
TANH, ASINH, ACOSH, ATANH. Arguments of the circular functions are in radians regardless of the angle unit.
Derive them from exp, log and sqrt with formulas that stay accurate near the real axis (for example
asin(z) = -i log(iz + sqrt(1 - z^2)) needs care when |z| is large or near a cut). The manual also lists
bitwise complex AND, OR, XOR; the analyzed file contains no such functions, so they are not specified and not
required.

### 3.3 Statistics and data analysis

All statistics accept an array of integers or floats of any dimensionality; the new API takes a flat
`double` array and a length. A length of zero is a size error.

#### SUM
- Sum of all elements, accumulated left to right in double (legacy). Required result matches the exact sum
  to the common tolerance; a compensated (Neumaier) accumulation is permitted and recommended because the
  target has soft-float cost but the sums are short. O(n).
- Priority: first.

#### MEAN
- SUM divided by n. O(n). Priority: first.

#### MEDIAN
- Sort a copy of the data in ascending order; return the middle element for odd n, and the average of the two
  middle elements for even n. The caller's data is unchanged. Legacy sorts with a gap-halving shell sort on a
  temporary copy. Required: any O(n log n) or O(n^2) in-place sort of a caller-provided scratch buffer; NaN
  input is undefined. Priority: first.

#### SD -- sample standard deviation
- sqrt(sum((x - mean)^2) / (n - 1)) using a two-pass computation. For n = 1 legacy computes 0/0 and returns
  NaN; required: size status for n < 2. A population variant and a variance are not in the original; the
  required API adds `variance` with a flag selecting the divisor n-1 or n, and `stddev` as its square root.
  Priority: first.

#### MAX and MIN with optional position
- Largest and smallest value. When the data is one-dimensional the caller may ask for the position of the
  extreme; the first occurrence wins on ties (strict comparison). Legacy starts from sentinels of +-3.0e38,
  so an array whose values all lie beyond the sentinel returns the sentinel and leaves the position unset.
  Required: initialize from the first element; no sentinel. Position is reported zero-based. O(n).
  Priority: first.

#### CORREL -- Pearson correlation coefficient
- r = sum(dx*dy) / sqrt(sum(dx^2) * sum(dy^2)) with dx, dy deviations from each array's mean. The two arrays
  must have equal length (size status otherwise). Zero variance in either array gives NaN or infinity in
  legacy; required: domain status. O(n). Priority: first.

#### CHI and CHI_P -- Pearson chi-square statistic and its probability
- Input: a two-dimensional table of observed counts (rows by columns, any numeric type).
- CHI: statistic = sum over cells of (O - E)^2 / E with E = (row total)(column total)/(grand total).
  A zero expected count gives NaN in legacy; required: domain status.
- Degrees of freedom: (rows - 1)(columns - 1). Legacy derives it from the declared array bounds and is
  correct only for zero-based declarations (it is off for one-based declarations), and it indexes a 50-row
  lookup table, so more than 50 degrees of freedom reads past the table. Required: no limit other than
  df >= 1.
- CHI_P: the probability, in percent, that a chi-square variable with that many degrees of freedom exceeds
  the statistic. Legacy returns a coarse bracket taken from a printed table of 15 probability columns and
  gives the percent of the nearest tabulated column (upper bound of the bracket when the statistic exceeds
  the 10 percent critical value, lower bound when it falls below). Required: the exact upper-tail
  probability 100 * Q(df/2, chi/2), with Q the regularized upper incomplete gamma function, computed by a
  series for chi/2 < df/2 + 1 and a continued fraction otherwise (Numerical Recipes style Lentz algorithm),
  using a Lanczos log-gamma. Tolerance: absolute 1e-8 in percent units. The legacy table is not to be
  reproduced.
- Complexity: O(rows*cols) plus a bounded iteration count (at most about 200 terms). Priority: second.

#### CROSSING -- first level crossing in a sampled signal
- Inputs: a one-dimensional array a of n samples; optional level L (default 0); optional direction d, +1
  rising or -1 falling (default +1; 0 is an argument error).
- Output: a zero-based position, or -1 when none is found. Legacy reports the raw zero-based storage
  position without adding the index base.
- Rule, first pass (scan i from 0 while i <= n-4, return on the first match): for d = +1 a match needs
  a[i] < L, a[i+2] > L and a[i] <= a[i+1] <= a[i+2], and the result is i+1. For d = -1 the mirror image:
  a[i] > L, a[i+2] < L, a[i] >= a[i+1] >= a[i+2], result i+1. The last sample is never examined.
- Rule, second pass (only when the first pass found nothing; i from 0 while i <= n-6): for d = +1 a match
  needs a[i+1] <= L, a[i+3] >= L, a[i+1] <= a[i+2] <= a[i+3], a[i] < a[i+2] and a[i+4] > a[i+2], result
  i+2. For d = -1 mirrored: a[i+1] >= L, a[i+3] <= L, a[i+1] >= a[i+2] >= a[i+3], a[i] > a[i+2],
  a[i+4] < a[i+2], result i+2.
- The monotonic-triple condition rejects single-sample noise spikes; this is the purpose of the rule and the
  reason it is specified exactly rather than simplified. Arrays shorter than 4 samples return -1.
- Complexity O(n). Priority: last.

### 3.4 Vector operations

#### MAGNITUDE
- Euclidean norm of a vector of any length. Legacy sums squares in double and takes the square root, which
  overflows for components near 1e154. Required: scale by the largest absolute component (or use a
  hypot-style accumulation) so that [1e200, 1e200] returns 1.414...e200. O(n). Priority: first.

#### DOTPRODUCT
- Sum of products of two equal-length vectors; unequal lengths are a size error. O(n). Priority: first.

#### V_NORMALISE (unit vector)
- Divide each component by the magnitude. Zero vector: legacy produces NaN; required: domain status. Input
  and output may be the same buffer in the new API. O(n). Priority: first.

#### V_CROSS
- Cross product of two 3-vectors (a2b3 - a3b2, a3b1 - a1b3, a1b2 - a2b1 in one-based component names); any
  other length is a size error. The output may alias an input (copy the operands first). O(1). Priority:
  first.

#### V_MULT -- matrix times vector
- A matrix with `rows` rows and `cols` columns times a vector of length `cols`, giving a vector of length
  `rows`: out[i] = sum over j of A[i][j] * v[j]. Length mismatch is a size error. The output must not
  overlap the matrix or vector (alias status; legacy raises "destination same as source"). O(rows*cols).
  Priority: second.

#### V_ROTATE -- rotate 2-D points about a center
- Inputs: center (x0, y0), angle, arrays xin and yin of equal length, arrays xout and yout of the same length.
- Rule: with c = cos(theta), s = sin(theta), dx = x - x0, dy = y - y0: x' = dx*c - dy*s + x0 and
  y' = dy*c + dx*s + y0. A positive angle turns counterclockwise when y points up (clockwise on a screen
  whose y axis points down). Input and output arrays may be the same.
- Output storage: float arrays receive the exact value; integer arrays receive the value rounded half away
  from zero. The new API produces doubles; an `int` convenience wrapper rounds with `round()`.
- Errors: unequal lengths are a size error. O(n). Priority: second.

#### V_PRINT and M_PRINT
- Display helpers that print a vector on one line (comma separated) or a matrix one row per line using the
  interpreter's number formatting; integer vectors may print in hexadecimal. They carry no numeric
  semantics and are not specified; the calculator front end owns formatting.

### 3.5 Matrix operations

The original stores matrices as column-first arrays; the required API uses row-major.

#### M_TRANSPOSE
- Output is the transpose of a rows-by-cols matrix, a cols-by-rows matrix. Output dimensions must match
  (size status). In-place transposition is allowed only for square matrices in the new API. O(rows*cols).
  Priority: first.

#### M_MULT
- Product of an m-by-n matrix and an n-by-p matrix into an m-by-p matrix. Inner dimension mismatch or wrong
  output size is a size error. The output must not overlap either input. Accumulation in double, order
  of summation over the inner index ascending. O(m*n*p). Priority: first.

#### M_DETERMINANT
- Determinant of a square matrix; non-square input is a size error; a 1-by-1 matrix returns its element.
- Legacy: recursive cofactor expansion along the first row, allocating a temporary minor at every level;
  time grows factorially and memory with depth. Required: LU factorization with partial pivoting, sign
  tracking per row swap, product of the diagonal. A matrix with a zero pivot returns exactly 0. Cost
  O(n^3), working storage n*n doubles supplied by the caller (or in place). Results for integer matrices
  match the exact determinant within the common tolerance relative to the product of the row norms.
- Priority: first.

#### M_INVERSE
- Inverse of a square matrix. Legacy: adjugate divided by determinant via recursive cofactors (factorial
  time), with the singular test `determinant == 0.0` evaluated exactly, which accepts near-singular
  matrices and returns garbage. The output must not be the input array.
- Required: Gauss-Jordan elimination with partial pivoting on an augmented working copy. A pivot whose
  magnitude is at most n * 2.220446049250313e-16 * (largest absolute entry of the original matrix) is
  declared singular (singular status, output untouched). Cost O(n^3); working storage is one n*n copy of the
  input supplied by the caller, with the identity built in the output matrix, and a pivot index array of
  fixed maximum size on the stack. Priority: first.

#### Solve (extension, absent from the original)
- Solve A x = b for one or more right-hand sides. Required: LU with partial pivoting and the same singular
  threshold as the inverse. A calculator needs it more than the inverse, and it is numerically better than
  forming the inverse. Priority: first.

### 3.6 Quaternions

A legacy quaternion is a five-element array (w, x, y, z, tag). The tag carries the original magnitude of a
vector that was converted to a quaternion; it plays no role in rotation. The new API uses a four-field
struct and returns a magnitude separately. All items O(1). Priority: second for all.

#### Q_MULT
- Hamilton product of two quaternions: w = w1w2 - x1x2 - y1y2 - z1z2; x = w1x2 + x1w2 + y1z2 - z1y2;
  y = w1y2 - x1z2 + y1w2 + z1x2; z = w1z2 + x1y2 - y1x2 + z1w2. Legacy sets the output tag to the product of
  the two tags.

#### Q_INVERT
- Negate x, y and z; keep w. This is the conjugate, which equals the multiplicative inverse only for a unit
  quaternion; legacy does not divide by the squared norm. Required: provide `conjugate` with this behavior and
  `inverse` that divides by the squared norm (domain status for the zero quaternion).

#### Q_VECTOR
- From a vector (x, y, z): w = 0, the xyz part is the unit vector, and the tag (separate output in the new
  API) is the magnitude. A zero vector gives NaN in legacy; required: domain status.

#### Q_CREATE -- quaternion from axis and angle
- From an angle theta and an axis (x, y, z): the unit quaternion (cos(theta/2), axis_unit * sin(theta/2)).
- Legacy defects: the cosine term omits the angle-unit conversion that the sine term applies, so the result
  is not a unit rotation in degree mode; and the axis is not normalized before use, then the whole
  quaternion is normalized, which changes the effective angle for a non-unit axis. Required: convert the
  angle once, normalize the axis first (zero axis is a domain error), build the quaternion from the
  converted half angle. For a unit axis in radian mode legacy and required agree.

#### Q_EULER -- quaternion from yaw, pitch, roll
- Inputs in the active angle unit. Required mapping (matches the legacy output): define three single-axis
  rotation quaternions: Qx(pitch) = (cos(pitch/2), sin(pitch/2), 0, 0), Qy(-yaw) = (cos(-yaw/2), 0,
  sin(-yaw/2), 0), Qz(roll) = (cos(roll/2), 0, 0, sin(roll/2)). The result is the Hamilton product
  Qz(roll) * Qy(-yaw) * Qx(pitch). Yaw enters negated and acts about the y axis, roll acts about the z axis
  and pitch about the x axis (an unusual assignment kept for compatibility). Tag = 1.

#### Q_ROTATE
- Rotate a vector quaternion v (w = 0) by a rotation quaternion r: result = r * v * conjugate(r) (two Hamilton
  products). The tag is the product tag(r) * tag(v) * tag(r). For a unit r the result is the rotated vector
  with w = 0 up to rounding.

### 3.7 Element-wise array operations (calculator: list processing)

These work on arrays of any dimensionality; operands have equal length (size status otherwise). Legacy
requires all operands to be integer arrays or all to be float arrays except where noted. Priority: last,
because a calculator list type usually already offers them; specify only if lists are implemented.

#### C_ADD, C_SUB, C_MUL, C_DIV
- Element-wise sum, difference, product, quotient. Output may alias any input. Integer division by zero is
  undefined in legacy; required: domain status. Integer division truncates toward zero; integer arithmetic
  wraps modulo 2^64 in legacy; required: wrap, documented.

#### C_AND, C_OR, C_XOR
- Element-wise bitwise operations on 64-bit integers. For float arrays the operands convert to 64-bit
  integers, the bit operation runs, and the result converts back to float.
- Legacy defect: for integer arrays C_OR and C_XOR both perform AND. Required: the named operation.

#### SCALE
- Multiply every element by a scalar into an output array, optionally converting between integer and float
  storage. Legacy defect: when the output is an integer array and the scalar was supplied as a float, the
  scalar is first rounded to an integer, so 0.5 becomes 1 (round half away from zero). Required: multiply in
  double, then convert to integer only when storing (round half away from zero, range status above
  the int64 range). A scale of exactly 1 is a plain copy-with-conversion.

#### POWER
- Raise every element to a scalar power. Legacy: exponent equal to 1 copies; integer output rounds after
  the power. A negative base with a non-integer exponent gives NaN for float output and an undefined
  integer conversion. Required: domain status for that case. The original manual does not document this
  command.

#### INTERPOLATE -- blend two arrays
- out = (in2 - in1) * ratio + in1, element-wise, ratio a scalar (not limited to 0..1). Arrays must be
  distinct objects (alias status). Integer output rounds half away from zero. O(n). Priority: second.

#### WINDOW -- rescale to a range
- Find the minimum input_min and maximum input_max of the input, then
  out = (x - input_min) / (input_max - input_min) * (output_max - output_min) + output_min. output_min may exceed output_max, which
  inverts the data. The command can also return input_min and input_max. Legacy integer outputs truncate toward
  zero (no rounding), unlike INTERPOLATE. Constant input divides by zero in legacy; required: domain
  status. O(n). Priority: second.

#### SHIFT -- bit shift of integer arrays
- Shift each int64 by an amount in [-63, 63]; outside is an argument error. Positive: left shift on the
  unsigned reinterpretation (wraps modulo 2^64, result reinterpreted as signed). Negative: arithmetic right
  shift (sign extending) by default; an optional flag selects a logical (zero filling) right shift. Zero
  copies. O(n). Priority: last.

#### Delegated commands (SET, ADD, SLICE, INSERT) and RANDOMIZE
- SET (fill with a value), ADD (add a scalar), SLICE (copy one row, column or line of a multidimensional
  array into a vector) and INSERT (the reverse) are forwarded by the dispatcher to functions that live in a
  different source file. They were not specified from this file. They are trivial loops and belong to the
  calculator's list type if it has one. Priority: last.
- RANDOMIZE [n]: seed the generator with the non-negative integer n; with no argument seed with the low 32
  bits of a microsecond timer. A negative seed is an argument error. Specified with RAND below.

### 3.8 Random numbers

#### RAND -- uniform generator (Mersenne Twister)
- Legacy: a 32-bit Mersenne Twister whose 624-word state is filled by a custom linear recurrence rather than
  the published initialization; the generator seeds itself from a microsecond timer on first use; the
  output word is divided by 4294967295, so the range is the closed interval [0, 1], although the manual
  promises a half-open range.
- Required: the standard 32-bit Mersenne Twister (MT19937) of Matsumoto and Nishimura with the published
  seed routine (state[0] = seed; state[i] = 1812433253 * (state[i-1] xor (state[i-1] >> 30)) + i), and the
  published twist and tempering. The unit uniform deviate is the 32-bit output divided by 2^32 (half-open
  [0, 1)); the API also offers the 53-bit form from two outputs. Bit-for-bit compatibility with the stream
  of the original is deliberately not required: programs cannot depend on it, and reproducing it would
  require carrying a nonstandard seeding constant into the new code.
- Normal deviate (extension, absent from the original): Marsaglia polar method on two unit uniforms, caching
  the second value; algorithm named so the reviewer sees no Box-Muller code copied. Mean 0, variance 1.
- State: 624 words plus an index (about 2.5 KB) in a caller-supplied structure; no global state in the
  library. Priority: first.

### 3.9 Fast Fourier transform

Sizes: a power of two from 1 to 32768 in legacy (the check loops from 1 doubling while below 65536);
otherwise a size error. The new API sets a compile-time ceiling (recommended default 1024, maximum 4096)
for the calculator and rejects larger sizes with the range status.

#### FFT -- forward transform
- X[k] = sum over n of x[n] * exp(-2 pi i k n / N), no normalization. Legacy takes a real signal and writes
  an interleaved real/imaginary output of 2N doubles; the new API transforms an array of complex values in
  place (a real signal is loaded with zero imaginary parts).
- Algorithm class: iterative radix-2 decimation-in-time Cooley-Tukey with bit-reversal permutation and a
  table of N/2 twiddle factors. O(N log N). Working storage: the data plus an optional N/2-entry twiddle
  table supplied by the caller (when absent, compute twiddles per stage with sin and cos; slower, no
  allocation).

#### FFT INVERSE
- The inverse transform with scaling 1/N, built from the forward transform by conjugating the input,
  transforming, conjugating the result and dividing by N. Legacy returns only the real part; the new API
  returns the complex array and a helper copies the real parts. Tolerance: 1e-9 relative to the largest
  magnitude of the signal.

#### FFT MAGNITUDE and FFT PHASE
- Magnitude |X[k]| and phase atan2(im, re) of every bin of the forward transform of a real signal (all N
  bins, not only the first N/2+1). The phase is in radians and ignores the angle unit. Bins whose magnitude
  is below 1e-9 have meaningless phase; tests skip them. The bin frequency is k * sample_rate / N.
- Priority for the FFT family: second.

### 3.10 Checksums

#### CRC8, CRC12, CRC16, CRC32 -- parameterized bitwise CRC
- Inputs: the data (an array of integers or floats whose elements are 0..255, or a string), an optional
  length (1..65535; defaults to the whole array or string), and optional parameters in this order:
  polynomial, initial value (start mask), final XOR value (end mask), reflect-input flag, reflect-output
  flag. Defaults: polynomials 0x07 (CRC8), 0x80D (CRC12), 0x1021 (CRC16), 0x04C11DB7 (CRC32); initial value,
  final XOR and both flags zero. Parameter ranges: 0..255, 0..4095, 0..65535, 0..4294967295. Note that the
  defaults leave initial value and final XOR at zero, so the plain default is the unreflected,
  unconditioned form, not the Ethernet CRC-32.
- Algorithm: shift-register CRC (the Rocksoft parameterized model) processing each byte most significant
  bit first: if reflect-input, bit-reverse the byte; XOR it into the top byte of the register (for 12 bits,
  into bits 11..4); eight times shift left, XORing the polynomial when the bit shifted out was set. After the
  last byte, if reflect-output, bit-reverse the full register of the given width; then XOR the final
  value. Result is the width-bit register as an integer.
- Legacy deviations (not to be reproduced):
  - The reflect-output flag is read from the reflect-input argument position, so it cannot be set
    independently.
  - For widths 8 and 32 the final XOR is applied before the output reflection; for 12 and 16 it is applied
    after. The model above (reflect, then XOR) is required for all widths. The two orders agree when the final
    XOR is all zeros or all ones; the divergence vectors in section 5.9 show a difference otherwise.
  - The 12-bit routine never masks the register to 12 bits, so without output reflection the result carries
    stray high bits (for the ASCII digits 1 to 9 under polynomial 0x80F with zero initial value the legacy
    routine produces 0xDF5B, where the correct 12-bit CRC is 0xF5B). Required: mask to 12 bits.
  - Elements above 255 are rejected, negative elements are not.
- A 64-bit routine (polynomial constants include 0x42F0E1EBA9EA3693) and a 16-bit CCITT convenience routine
  exist in the file but are not reachable from any command; the required API includes the 64-bit form because
  the generic routine covers every width up to 64 with one code path.
- Complexity O(8*length) shifts, or O(length) with a 256-entry table (optional; the table is 1 KB for 8-bit
  to 2 KB for 16-bit; bitwise is acceptable). Priority: first for 8, 16, 32; second for 12 and 64.

### 3.11 Encoding

#### BASE64 ENCODE and DECODE
- Encode: standard RFC 4648 alphabet, '=' padding, no line breaks. Output length 4*ceil(n/3). Returns the
  number of characters produced. Output to a string is limited to 255 characters in legacy.
- Decode: the inverse; returns the byte count. Legacy: any character outside the alphabet is treated as the
  value zero without error, a trailing group of fewer than 4 characters is silently ignored, padding is
  recognized by position. Required: invalid-input status for characters outside the alphabet (ASCII
  whitespace may be skipped), for a length that is not a multiple of 4, and for misplaced padding.
- Capacity: output buffer too small is a size status. O(n). Priority: second (last for a calculator).

### 3.12 Specialized items to exclude from the calculator

#### PID controller (INIT, START, STOP commands and the PID function)
- The controller is a discrete PID with a trapezoidal integral and a band-limited derivative on the
  measurement. Per update with setpoint sp and measurement m:
  - e = sp - m; the proportional term is Kp*e.
  - integrator += 0.5 * Ki * T * (e + previous_e); clamp to [integrator_min, integrator_max].
  - Legacy: differentiator = -(2*Kd*(m - previous_m) + (2*tau - T)*differentiator) / (2*tau + T). The
    feedback term carries the minus sign of the input term, which puts the pole at z = -(2*tau - T)/(2*tau + T)
    and makes the filtered derivative alternate in sign for tau above T/2.
    Required (divergence from legacy): differentiator = (-2*Kd*(m - previous_m) + (2*tau - T)*differentiator) /
    (2*tau + T), the trapezoidal (Tustin) form of D(s) = -Kd*s/(tau*s + 1)*M(s) under
    s = (2/T)*(1 - 1/z)/(1 + 1/z). The pole is z = (2*tau - T)/(2*tau + T), the image of s = -1/tau: positive for
    tau >= T/2, inside the unit circle for tau > 0. A measurement ramp of slope r settles at
    differentiator = -Kd*r, and a held step decays monotonically for tau >= T/2.
  - Required (divergence from legacy): tau <= 0 is an invalid parameter. At tau = 0 neither form is a usable
    derivative: legacy accumulates and the Tustin form alternates between plus and minus 2*Kd/T.
  - output = proportional + integrator + differentiator, clamped to [out_min, out_max].
  - store e and m for the next call.
- The 14-element float parameter array is, in order: Kp, Ki, Kd, tau, out_min, out_max, integrator_min,
  integrator_max, T (seconds), integrator, previous_e, differentiator, previous_m, last output. INIT binds the
  array and a BASIC interrupt routine to a channel 1..8 and rejects a size other than 14 or T below
  0.001. START arms a timer; STOP clears the channel entirely. The function form returns the output for a
  configured and started channel.
- Unit inconsistency in the original: the first interrupt is armed at T*1000 microseconds while subsequent
  intervals use T*1000000 microseconds; this cannot be resolved from the analyzed file.
- Calculator relevance: none. The update formula is trivial to specify and test (section 5.14); the channel
  scheduler depends on the interpreter's interrupt machinery and is not part of the library.

#### AES128 (ENCRYPT and DECRYPT in ECB, CBC and CTR modes)
- Note: a wrapper around the external `aes.c`; the cipher itself is not in the analyzed file.
- Behavior: 16-byte key; input a multiple of 16 bytes (no padding in any mode, CTR included); elements
  0..255. ENCRYPT in CBC and CTR generates a 16-byte IV, prepends it to the output (output length n+16),
  and allows an optional caller IV to override it; DECRYPT in those modes consumes the first 16 bytes as
  the IV. ECB has no IV. Legacy generates the IV with the C library `rand()` low byte on the RP2040, which is
  not cryptographically secure. Output to a string is limited to 255 bytes.
- Calculator relevance: none. If ever needed, use a vetted permissive library; do not hand-write the
  cipher. Test vectors in section 5.15 are the NIST ones.

#### SENSORFUSION MADGWICK and MAHONY (attitude estimation)
- Two published inertial-measurement algorithms (Madgwick's gradient-descent filter and Mahony's
  complementary filter), each updating a persistent orientation quaternion (initially (1,0,0,0)) from
  accelerometer, gyroscope (rad/s) and magnetometer samples, with the elapsed time since the previous call
  (milliseconds from a hardware tick, capped at one second) as the integration step, then writing roll,
  pitch, yaw in radians via the standard formulas roll = atan2(2(wx + yz), 1 - 2(x^2 + y^2)),
  pitch = asin(clamp(2(wy - zx), -1, 1)), yaw = atan2(2(wz + xy), 1 - 2(y^2 + z^2)). A zero-length
  accelerometer or magnetometer vector makes the update return without change.
- Defaults: Madgwick gain 0.5; Mahony proportional gain 10, integral gain 0.
- Known defect in the Mahony routine as read: after the first quaternion component is updated the remaining
  components are integrated using the updated value rather than the previous one.
- Calculator relevance: none; hardware-bound. Only invariant tests are given (section 5.16).

## 4. Calculator relevance and the proposed C library

### 4.1 Target constraints

The RP2040 is a Cortex-M0+ with no FPU. Every `double` operation runs through software floating point, so
the library keeps loops short, avoids transcendental calls inside inner loops where an incremental update
works (FFT twiddles, polynomial evaluation), and never allocates in a hot path. One flat 192 KB process
window holds text, data, bss and stack, and the 53-by-32 text display drives screen-sized lists, not
megabyte arrays. The library therefore takes every buffer from the caller, uses stack arrays bounded by
compile-time maxima (`CALC_MAT_MAX` default 12, `CALC_FFT_MAX` default 1024), and returns a status code instead
of aborting. Execution-time numbers for soft-float operations were not measured for this specification.

### 4.2 Implement first

1. Scalar functions: ATAN3, SINH, COSH, TANH, LOG10, plus the inverse hyperbolic functions the tree's `libm`
   lacks (needed by the complex inverse functions).
2. Statistics on a list: sum, mean, median, sample and population variance and standard deviation, minimum
   and maximum with position, correlation.
3. Vector basics: dot product, magnitude, normalize, 3-D cross product.
4. Small matrices (up to `CALC_MAT_MAX`): transpose, multiply, determinant, inverse, linear solve, matrix
   times vector.
5. Complex arithmetic and elementary functions in double precision.
6. CRC and base conversion helpers for the programmer mode: the generic parameterized CRC with the catalog
   presets of section 5.9.
7. Random numbers: seeded uniform generator and the normal deviate.

### 4.3 Implement second

FFT up to the compile-time ceiling, chi-square statistic and exact probability, linear interpolation and
range rescaling, 2-D point rotation, quaternions, base64, crossing detector.

### 4.4 Do not implement (interpreter-bound or hardware-bound)

PID channels and their timer interrupts, AES (a vetted external library covers it if ever wanted), the sensor
fusion filters, and every piece of argument parsing: the original's array-variable lookup, index-base
handling, temporary-memory allocator, string-as-array tricks (the first byte of an MMBasic string holds its
length), constant-variable checks, and the error-abort mechanism have no counterpart in a plain C library.
Display helpers (V_PRINT, M_PRINT) belong to the front end. The delegated array commands (SET, ADD, SLICE,
INSERT) belong to the calculator's list type if it has one.

### 4.5 Proposed API (`mathematics.h`, implemented in `MATHEMATICS.c`)

Location: a new `lib/libcalc` directory (preferred, so that `usr.bin/calc` and other front ends link one
library) or `usr.bin/calc`. Language C17, `-Wall -Wextra -Werror` clean, no dynamic allocation, no global
mutable state, `static const` tables only. New files carry the project's ISC header per `NOTICE`. The sketch
below is a design for the implementer to refine; names are original to this specification.

```c
#ifndef MATHEMATICS_H
#define MATHEMATICS_H

#include <stddef.h>
#include <stdint.h>

#ifndef CALC_MAT_MAX
#define CALC_MAT_MAX 12u          /* largest matrix side for det/inverse/solve */
#endif
#ifndef CALC_FFT_MAX
#define CALC_FFT_MAX 1024u        /* largest transform length (power of two) */
#endif

typedef enum {
    CALC_OK = 0,
    CALC_E_SIZE,        /* empty input, length mismatch, buffer too small, side above maximum */
    CALC_E_DOMAIN,      /* argument outside the mathematical domain */
    CALC_E_RANGE,       /* result or argument outside the supported range */
    CALC_E_SINGULAR,    /* matrix numerically singular */
    CALC_E_ALIAS,       /* output overlaps an input where forbidden */
    CALC_E_ARG,         /* option value out of range */
    CALC_E_INVALID      /* malformed encoded input */
} calc_status;

typedef enum { CALC_RADIANS = 0, CALC_DEGREES = 1 } calc_angle_unit;

typedef struct { double re, im; } calc_cplx;
typedef struct { double w, x, y, z; } calc_quat;

/* --- scalar --- */
double      calc_angle_to_rad(calc_angle_unit u, double a);
double      calc_angle_from_rad(calc_angle_unit u, double r);
double      calc_atan3(double y, double x, calc_angle_unit u);   /* [0, one turn) */
calc_status calc_sinh(double x, double *out);
calc_status calc_cosh(double x, double *out);
calc_status calc_tanh(double x, double *out);
calc_status calc_log10(double x, double *out);
double      calc_asinh(double x);
double      calc_acosh(double x);        /* NaN below 1 */
double      calc_atanh(double x);        /* NaN outside (-1, 1) */

/* --- statistics on a flat list of n doubles --- */
calc_status calc_sum(const double *a, size_t n, double *out);
calc_status calc_mean(const double *a, size_t n, double *out);
calc_status calc_median(const double *a, size_t n, double *scratch /* n doubles */, double *out);
calc_status calc_variance(const double *a, size_t n, int sample, double *out);
calc_status calc_stddev(const double *a, size_t n, int sample, double *out);
calc_status calc_min(const double *a, size_t n, double *out, size_t *index /* may be NULL */);
calc_status calc_max(const double *a, size_t n, double *out, size_t *index /* may be NULL */);
calc_status calc_correl(const double *a, const double *b, size_t n, double *out);
calc_status calc_chi_square(const double *obs, size_t rows, size_t cols,
                            double *chi, size_t *df);
calc_status calc_chi_square_p(double chi, size_t df, double *percent);
calc_status calc_crossing(const double *a, size_t n, double level, int direction,
                          long *index /* -1 when none */);

/* --- vectors --- */
calc_status calc_dot(const double *a, const double *b, size_t n, double *out);
calc_status calc_magnitude(const double *a, size_t n, double *out);
calc_status calc_normalize(const double *a, size_t n, double *out);   /* out may equal a */
calc_status calc_cross3(const double a[3], const double b[3], double out[3]);
calc_status calc_rotate2d(double cx, double cy, double angle, calc_angle_unit u,
                          const double *xin, const double *yin, size_t n,
                          double *xout, double *yout);            /* in may equal out */

/* --- matrices, row-major, side <= CALC_MAT_MAX --- */
calc_status calc_mat_mul(const double *a, size_t m, size_t n,
                         const double *b, size_t p, double *out);   /* out must not alias */
calc_status calc_mat_vec(const double *a, size_t rows, size_t cols,
                         const double *v, double *out);
calc_status calc_mat_transpose(const double *a, size_t rows, size_t cols, double *out);
calc_status calc_mat_det(const double *a, size_t n, double *work /* n*n */, double *out);
calc_status calc_mat_inverse(const double *a, size_t n, double *work /* n*n */, double *out);
calc_status calc_mat_solve(const double *a, size_t n, const double *b, size_t nrhs,
                           double *work /* n*n */, double *x);

/* --- complex --- */
calc_cplx   calc_c_make(double re, double im);
calc_cplx   calc_c_polar(double radius, double angle, calc_angle_unit u);
double      calc_c_abs(calc_cplx z);
double      calc_c_arg(calc_cplx z);                                /* radians */
double      calc_c_phase(calc_cplx z, calc_angle_unit u);
calc_cplx   calc_c_conj(calc_cplx z);
calc_cplx   calc_c_proj(calc_cplx z);
calc_status calc_c_add(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_sub(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_mul(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_div(calc_cplx a, calc_cplx b, calc_cplx *out);
calc_status calc_c_pow(calc_cplx a, calc_cplx b, calc_cplx *out);
typedef enum {
    CALC_C_EXP = 1, CALC_C_LOG, CALC_C_SQRT, CALC_C_SIN, CALC_C_COS, CALC_C_TAN,
    CALC_C_ASIN, CALC_C_ACOS, CALC_C_ATAN, CALC_C_SINH, CALC_C_COSH, CALC_C_TANH,
    CALC_C_ASINH, CALC_C_ACOSH, CALC_C_ATANH
} calc_cfn;
calc_status calc_c_func(calc_cfn fn, calc_cplx z, calc_cplx *out);

/* --- quaternions (the magnitude tag of the original is returned separately) --- */
calc_quat   calc_quat_mul(calc_quat a, calc_quat b);
calc_quat   calc_quat_conj(calc_quat q);
calc_status calc_quat_inverse(calc_quat q, calc_quat *out);
calc_status calc_quat_from_vector(double x, double y, double z, calc_quat *q, double *magnitude);
calc_status calc_quat_from_axis_angle(double ax, double ay, double az, double angle,
                                      calc_angle_unit u, calc_quat *q);
calc_quat   calc_quat_from_euler(double yaw, double pitch, double roll, calc_angle_unit u);
calc_quat   calc_quat_rotate(calc_quat r, calc_quat v);           /* r * v * conj(r) */

/* --- FFT: in place, n a power of two, n <= CALC_FFT_MAX --- */
calc_status calc_fft(calc_cplx *x, size_t n, int inverse, calc_cplx *twiddle /* n/2 or NULL */);
calc_status calc_fft_magnitude(const calc_cplx *x, size_t n, double *out);
calc_status calc_fft_phase(const calc_cplx *x, size_t n, double *out);

/* --- interpolation and scaling --- */
double      calc_lerp(double a, double b, double t);
calc_status calc_rescale(const double *in, size_t n, double output_min, double output_max,
                         double *out, double *input_min, double *input_max);
calc_status calc_interp_table(const double *xs, const double *ys, size_t n, double x, double *out);
                                  /* xs strictly ascending; linear; DOMAIN outside [xs[0], xs[n-1]] */

/* --- CRC (Rocksoft model, any width 1..64) --- */
typedef struct {
    uint8_t  width;
    uint64_t poly, init, xorout;
    uint8_t  refin, refout;
} calc_crc_spec;
extern const calc_crc_spec calc_crc8, calc_crc8_maxim, calc_crc12_dect, calc_crc12_umts,
    calc_crc16_arc, calc_crc16_ccitt_false, calc_crc16_xmodem, calc_crc32, calc_crc32c,
    calc_crc32_bzip2, calc_crc64_ecma;
calc_status calc_crc(const calc_crc_spec *spec, const uint8_t *data, size_t len, uint64_t *out);

/* --- random numbers: caller-owned state --- */
typedef struct { uint32_t mt[624]; unsigned index; } calc_rng;
void        calc_rng_seed(calc_rng *r, uint32_t seed);
uint32_t    calc_rng_u32(calc_rng *r);
double      calc_rng_unit(calc_rng *r);          /* [0, 1) from one 32-bit word */
double      calc_rng_unit53(calc_rng *r);        /* [0, 1) from two words */
double      calc_rng_normal(calc_rng *r, double mean, double sd); /* polar method */

/* --- base64 --- */
calc_status calc_b64_encode(const uint8_t *in, size_t n, char *out, size_t cap, size_t *written);
calc_status calc_b64_decode(const char *in, size_t n, uint8_t *out, size_t cap, size_t *written);

#endif
```

Implementation notes:

- Matrix routines work on a caller-owned work buffer of n*n doubles and fixed-size permutation arrays of
  `CALC_MAT_MAX` entries on the stack; they return `CALC_E_SIZE` for a side above the maximum.
- Complex functions are written against `calc_cplx`; no `<complex.h>` is assumed. `calc_c_func` takes the
  enumerated function code declared next to it.
- The FFT needs no allocation: twiddle factors come from the optional caller table or from per-stage `sin`
  and `cos`. A real-input convenience wrapper that loads zero imaginary parts is a thin caller.
- Statuses never abort; the calculator front end maps each status to its own message text. The original's
  message wording is not reproduced.
- The reviewer's similarity checklist (section 6) applies to `MATHEMATICS.c` and `mathematics.h` and to the
  tests.

## 5. Acceptance test vectors

All vectors were computed independently of the original; section 1.4's protocol applies. Rounded to 15
significant digits. Tolerances follow section 2 unless stated. "Error class" lines require the named status
(an implementation may return a stricter status); they test the corrected behavior, not the legacy behavior.

### 5.1 ATAN3 (y, x); radians and degrees
```
atan3(y=1, x=1): radians 0.785398163397448 ; degrees 45
atan3(y=-1, x=1): radians 5.49778714378214 ; degrees 315
atan3(y=-1, x=-1): radians 3.92699081698724 ; degrees 225
atan3(y=0, x=-1): radians 3.14159265358979 ; degrees 180
atan3(y=-1, x=0): radians 4.71238898038469 ; degrees 270
atan3(y=0.5, x=2): radians 0.244978663126864 ; degrees 14.0362434679265
atan3(y=3, x=0): radians 1.5707963267949 ; degrees 90
```

### 5.2 SINH, COSH, TANH
```
x=0: sinh 0 ; cosh 1 ; tanh 0
x=1: sinh 1.1752011936438 ; cosh 1.54308063481524 ; tanh 0.761594155955765
x=-2.5: sinh -6.05020448103979 ; cosh 6.13228947966369 ; tanh -0.98661429815143
x=0.5: sinh 0.521095305493747 ; cosh 1.12762596520638 ; tanh 0.46211715726001
x=20: sinh 242582597.704895 ; cosh 242582597.704895 ; tanh 1
```

### 5.3 LOG10
```
log10(1000) = 3
log10(0.001) = -3
log10(2) = 0.301029995663981
log10(1) = 0
log10(0) -> error class: zero argument
log10(-1) -> error class: negative argument
```

### 5.4 Statistics: SUM, MEAN, MEDIAN, SD (sample), variance
```
A = [2, 4, 4, 4, 5, 5, 7, 9]: sum 40 ; mean 5 ; median 4.5 ; sample variance 4.57142857142857 ; sample sd 2.1380899352994 ; population variance 4
B = [1, 2, 3, 4, 5, 6]: sum 21 ; mean 3.5 ; median 3.5 ; sample variance 3.5 ; sample sd 1.87082869338697 ; population variance 2.91666666666667
C = [3, 1, 2]: sum 6 ; mean 2 ; median 2 ; sample variance 1 ; sample sd 1 ; population variance 0.666666666666667
D = [-1.5, 0, 2.25, 8]: sum 8.75 ; mean 2.1875 ; median 1.125 ; sample variance 17.390625 ; sample sd 4.17020682940307 ; population variance 13.04296875
E = [10]: sum 10 ; mean 10 ; median 10 ; sample variance and sample sd -> error class: fewer than two values (legacy returns NaN)
```
Minimum, maximum with position:
```
a = [3, 9, 2, 9, 2]: max 9 at zero-based index 1 ; min 2 at zero-based index 2 (first occurrence wins)
a = [5, -1, -1, 7]: max 7 at zero-based index 3 ; min -1 at zero-based index 1 (first occurrence wins)
a = [4.5]: max 4.5 at zero-based index 0 ; min 4.5 at zero-based index 0 (first occurrence wins)
```
Correlation:
```
x=[1, 2, 3, 4, 5], y=[2, 4, 5, 4, 5]: r = 0.774596669241483
x=[1, 2, 3, 4, 5], y=[5, 4, 3, 2, 1]: r = -1
x=[1, 2, 3, 4], y=[2, 4, 6, 8]: r = 1
x=[0.5, 1.5, -2, 4], y=[3, -1, 0.25, 2]: r = 0.244549555993055
x=[1,1,1], y=[1,2,3]: zero variance -> error class: domain (legacy returns NaN)
```
Chi-square statistic, degrees of freedom and probability (percent). Tolerance for the probability: absolute
1e-8 in percent units. Table rows are listed top to bottom:
```
observed rows [[12, 5]; [7, 9]]: chi-square 2.43057551968156 ; df 1 ; p-value in percent 11.8989205532145
observed rows [[20, 15, 10]; [30, 35, 40]]: chi-square 4.76190476190476 ; df 2 ; p-value in percent 9.246247606292
observed rows [[10, 20, 30]; [20, 20, 20]; [30, 20, 10]]: chi-square 20 ; df 4 ; p-value in percent 0.0499399227387334
p-value in percent for chi-square 3.84145882069412 with df 1: 4.99999999999999
p-value in percent for chi-square 9.48772903678115 with df 4: 5.00000000000001
p-value in percent for chi-square 0.5 with df 2: 77.8800783071405
p-value in percent for chi-square 20 with df 3: 0.0169742435552826
p-value in percent for chi-square 0 with df 5: 100
```
Crossing (the vectors follow the two-pass rule of section 3.3; they come from an independent model of that
rule, not from running the original):
```
a=[0, 1, 2, 3, 4], level 2.5, direction 1: index 2
a=[0, 0, 5, 5], level 2.5, direction 1: index 1
a=[4, 3, 2, 1, 0], level 2.5, direction -1: index 1
a=[0, 1, 2, 3], level 10, direction 1: index -1
a=[1, 2], level 0, direction 1: index -1
a=[0, 1, 0, 1, 2, 3, 4, 5], level 3.5, direction 1: index 5
a=[-2, -1, 1, 2], level 0, direction 1: index 1
```

### 5.5 Vectors: dot, magnitude, normalize, cross, matrix times vector
```
dot([1, 2, 3], [4, 5, 6]) = 32
dot([1, 0, -1, 2], [3, 3, 3, 0.5]) = 1
dot([0.5, 0.25], [8, -4]) = 3
magnitude([3, 4]) = 5
magnitude([1, 2, 2]) = 3
magnitude([0, 0, 0]) = 0
magnitude([1e+200, 1e+200]) = 1.4142135623731e+200
normalize([3, 4]) = [0.6, 0.8]
normalize([1, 2, 2]) = [0.333333333333333, 0.666666666666667, 0.666666666666667]
normalize([0, 5, 0, 0]) = [0, 1, 0, 0]
normalize([0,0,0]) -> error class: zero vector (legacy returns NaN entries)
cross([1, 0, 0], [0, 1, 0]) = [0, 0, 1]
cross([1, 2, 3], [4, 5, 6]) = [-3, 6, -3]
cross([2, -1, 0.5], [0, 3, 1]) = [-2.5, -2, 6]
matrix (rows) [[1, 2, 3]; [4, 5, 6]] times vector [1, 0, -1] = [-2, -2]
matrix (rows) [[2, 0]; [0, 3]; [1, 1]] times vector [5, 7] = [10, 21, 12]
matrix (rows) [[0.5]] times vector [4] = [2]
```
Rotation of points. Expected floating values are exact to 1e-12 absolute (for example the 6.1e-17 below is
zero within tolerance):
```
rotate points [(1, 0)] about (0,0) by 90 degrees (counterclockwise with y up): [('6.12323399573677e-17', '1')]
rotate points [(2, 1)] about (1,1) by 180 degrees (counterclockwise with y up): [('0', '1')]
rotate points [(3, 0), (0, 3)] about (0,0) by 30 degrees (counterclockwise with y up): [('2.59807621135332', '1.5'), ('-1.5', '2.59807621135332')]
integer output, points (10,0),(0,10),(-10,0) about (0,0) by 45 degrees, rounded half away from zero: [(7, 7), (-7, 7), (-7, -7)]
```

### 5.6 Matrices: transpose, multiply, determinant, inverse, solve
Integer-valued determinants and inverses are computed exactly with rational arithmetic and then converted,
so the stated numbers are the true values:
```
transpose([[1, 2, 3]; [4, 5, 6]]) = [[1, 4]; [2, 5]; [3, 6]]
transpose([[1, 2]; [3, 4]; [5, 6]]) = [[1, 3, 5]; [2, 4, 6]]
transpose([[7]]) = [[7]]
multiply [[1, 2, 3]; [4, 5, 6]] by [[7, 8]; [9, 10]; [11, 12]] = [[58, 64]; [139, 154]]
multiply [[1, 0]; [0, 1]] by [[5, 6]; [7, 8]] = [[5, 6]; [7, 8]]
multiply [[2, 3]] by [[4]; [5]] = [[23]]
multiply [[1, 2]; [3, 4]] by [[0, 1]; [1, 0]] = [[2, 1]; [4, 3]]
multiply a 2x3 by a 2x3 -> error class: inner dimensions differ
```
```
det([[2, 0]; [0, 3]]) = 6
det([[1, 2]; [3, 4]]) = -2
det([[6, 1, 1]; [4, -2, 5]; [2, 8, 7]]) = -306
det([[4, -2, 1, 3]; [3, 6, -4, 2]; [2, 1, 8, -5]; [1, -3, 2, 9]]) = 2180
det([[1, 2]; [2, 4]]) = 0 (singular; exact zero)
det([[0, 1]; [1, 0]]) = -1
det of [[5]] = 5 ; det of a non-square matrix -> error class: size
```
```
inverse([[4, 7]; [2, 6]]) = [[0.6, -0.7]; [-0.2, 0.4]]
inverse([[1, 2, 3]; [0, 1, 4]; [5, 6, 0]]) = [[-24, 18, 5]; [20, -15, -4]; [-5, 4, 1]]
inverse([[2, -1, 0]; [-1, 2, -1]; [0, -1, 2]]) = [[0.75, 0.5, 0.25]; [0.5, 1, 0.5]; [0.25, 0.5, 0.75]]
inverse([[2, 0, 0]; [0, 4, 0]; [0, 0, 8]]) = [[0.5, 0, 0]; [0, 0.25, 0]; [0, 0, 0.125]]
inverse([[0, 1]; [1, 0]]) = [[0, 1]; [1, 0]]
inverse([[1,2],[2,4]]) -> error class: singular matrix
inverse of a non-square matrix -> error class: size
```
```
solve A x = b with A = [[2, 1, -1]; [-3, -1, 2]; [-2, 1, 2]], b = [8, -11, -3]: x = [2, 3, -1]
solve A x = b with A = [[4, -2, 1, 3]; [3, 6, -4, 2]; [2, 1, 8, -5]; [1, -3, 2, 9]], b = [1, 2, 3, 4]: x = [0.00917431192660551, 0.571100917431193, 0.612385321100917, 0.497706422018349]
solve A x = b with A = [[3, 0]; [0, 5]], b = [6, 10]: x = [2, 2]
solve with A = [[1,2],[2,4]], b = [1,2] -> error class: singular matrix
```

### 5.7 Complex numbers
Unary functions at three points each, results computed with `cmath` (double precision, principal values):
```
exp: (1, 2) -> (-1.13120438375681, 2.47172667200482) ; (-0.5, 0.25) -> (0.587675090343923, 0.150058086622163) ; (3, -4) -> (-13.1287830814622, 15.200784463068)
log: (1, 2) -> (0.80471895621705, 1.10714871779409) ; (-0.5, 0.25) -> (-0.58157540490284, 2.67794504458899) ; (3, -4) -> (1.6094379124341, -0.927295218001612)
sqrt: (1, 2) -> (1.27201964951407, 0.786151377757423) ; (-0.5, 0.25) -> (0.171780374861256, 0.727673345112677) ; (3, -4) -> (2, -1)
sin: (1, 2) -> (3.16577851321617, 1.95960104142161) ; (-0.5, 0.25) -> (-0.494485780933195, 0.221688164149575) ; (3, -4) -> (3.85373803791938, 27.0168132580039)
cos: (1, 2) -> (2.03272300701967, -3.0518977991518) ; (-0.5, 0.25) -> (0.905150150559607, 0.121108796043812) ; (3, -4) -> (-27.0349456030742, 3.85115333481178)
tan: (1, 2) -> (0.0338128260798967, 1.01479361614663) ; (-0.5, 0.25) -> (-0.504500702698564, 0.312420692502589) ; (3, -4) -> (-0.000187346204629478, -0.999355987381473)
asin: (1, 2) -> (0.427078586392476, 1.528570919481) ; (-0.5, 0.25) -> (-0.501608853275501, 0.281396056245293) ; (3, -4) -> (0.633983865639177, -2.30550903124348)
acos: (1, 2) -> (1.14371774040242, -1.528570919481) ; (-0.5, 0.25) -> (2.0724051800704, -0.281396056245293) ; (3, -4) -> (0.93681246115572, 2.30550903124348)
atan: (1, 2) -> (1.33897252229449, 0.402359478108525) ; (-0.5, 0.25) -> (-0.484254490329966, 0.200586618131234) ; (3, -4) -> (1.44830699523146, -0.158997191679999)
sinh: (1, 2) -> (-0.489056259041294, 1.40311925062204) ; (-0.5, 0.25) -> (-0.504895714387995, 0.278979128350262) ; (3, -4) -> (-6.548120040911, 7.61923172032141)
cosh: (1, 2) -> (-0.64214812471552, 1.06860742138278) ; (-0.5, 0.25) -> (1.09257080473192, -0.128921041728098) ; (3, -4) -> (-6.58066304055116, 7.58155274274654)
tanh: (1, 2) -> (1.16673625724092, -0.243458201185725) ; (-0.5, 0.25) -> (-0.485487281024135, 0.19805544995135) ; (3, -4) -> (1.00070953606723, -0.00490825806749606)
asinh: (1, 2) -> (1.46935174436819, 1.06344002357775) ; (-0.5, 0.25) -> (-0.492675683420771, 0.224328452634667) ; (3, -4) -> (2.29991404087927, -0.917616853351479)
acosh: (1, 2) -> (1.528570919481, 1.14371774040242) ; (-0.5, 0.25) -> (0.281396056245293, 2.0724051800704) ; (3, -4) -> (2.30550903124348, -0.93681246115572)
atanh: (1, 2) -> (0.173286795139986, 1.17809724509617) ; (-0.5, 0.25) -> (-0.500370000052531, 0.314398143207716) ; (3, -4) -> (0.117500907311434, -1.40992104959658)
conj: (1, 2) -> (1, -2) ; (-0.5, 0.25) -> (-0.5, -0.25) ; (3, -4) -> (3, 4)
abs (modulus): (1, 2) -> 2.23606797749979 ; (-0.5, 0.25) -> 0.559016994374947 ; (3, -4) -> 5
arg (radians, always): (1, 2) -> 1.10714871779409 ; (-0.5, 0.25) -> 2.67794504458899 ; (3, -4) -> -0.927295218001612
phase (angle unit applies; degrees shown): (1, 2) -> 63.434948822922 ; (-0.5, 0.25) -> 153.434948822922 ; (3, -4) -> -53.130102354156
real part / imaginary part: (1, 2) -> 1 / 2 ; (-0.5, 0.25) -> -0.5 / 0.25 ; (3, -4) -> 3 / -4
```
Binary functions, construction and projection:
```
add: (1, 2) , (3, -1) -> (4, 1) ; (-0.5, 0.25) , (2, 2) -> (1.5, 2.25) ; (3, -4) , (0.5, 0) -> (3.5, -4)
sub: (1, 2) , (3, -1) -> (-2, 3) ; (-0.5, 0.25) , (2, 2) -> (-2.5, -1.75) ; (3, -4) , (0.5, 0) -> (2.5, -4)
mul: (1, 2) , (3, -1) -> (5, 5) ; (-0.5, 0.25) , (2, 2) -> (-1.5, -0.5) ; (3, -4) , (0.5, 0) -> (1.5, -2)
div: (1, 2) , (3, -1) -> (0.1, 0.7) ; (-0.5, 0.25) , (2, 2) -> (-0.0625, 0.1875) ; (3, -4) , (0.5, 0) -> (6, -8)
pow: (1, 2) , (3, -1) -> (-27.4363819916061, 19.7893103650107) ; (-0.5, 0.25) , (2, 2) -> (-0.000732491068154499, -0.00128036214283212) ; (3, -4) , (0.5, 0) -> (2, -1)
div by zero: (1,2) / (0,0) -> error class: division by zero (legacy yields IEEE infinities)
construct polar (radius, angle): (2, 90 deg) -> (1.22464679914735e-16, 2) ; (1, pi/3 rad) -> (0.5, 0.866025403784439) ; (3, 180 deg) -> (-3, 3.67394039744206e-16)
construct (2.5, -1.5) -> (2.5, -1.5); (0,0) -> (0, 0); (-7, 1e-3) -> (-7, 0.001)
proj: (1,2) -> (1, 2); (-0.5,0.25) -> (-0.5, 0.25); (inf,-3) -> (inf, -0)
```
Tolerance: 1e-12 relative to max(1, |expected|) per component. The legacy single-precision implementation
agrees only to about 1e-6; the new code must reach the tighter bound.

### 5.8 Quaternions
```
mult [0, 1, 0, 0] * [0, 0, 1, 0] = [0, 0, 0, 1]
mult [1, 0, 0, 0] * [0.5, 0.5, 0.5, 0.5] = [0.5, 0.5, 0.5, 0.5]
mult [0.5, 0.5, 0.5, 0.5] * [0.5, -0.5, 0.5, -0.5] = [0.5, -0.5, 0.5, 0.5]
mult [1, 2, 3, 4] * [5, 6, 7, 8] = [-60, 12, 30, 24]
The magnitude tag of a product is the product of the two tags.
invert (conjugate) [1, 2, 3, 4] = [1, -2, -3, -4]
invert (conjugate) [0.5, 0.5, -0.5, 0.5] = [0.5, -0.5, 0.5, -0.5]
vector quaternion of ([3, 4, 12]): w=0, xyz = [0.230769230769231, 0.307692307692308, 0.923076923076923], magnitude tag 13
vector quaternion of ([0, 0, 2]): w=0, xyz = [0, 0, 1], magnitude tag 2
vector quaternion of ([1, -1, 1]): w=0, xyz = [0.577350269189626, -0.577350269189626, 0.577350269189626], magnitude tag 1.73205080756888
vector quaternion of (0,0,0) -> error class: zero vector
```
Creation and rotation (tolerance 1e-12 absolute; axis-angle and Euler results follow the construction in
section 3.6):
```
from axis-angle axis [0, 0, 1] angle 90 deg: [0.707106781186548, 0, 0, 0.707106781186547]
from axis-angle axis [0, 0, 2] angle 90 deg: [0.707106781186548, 0, 0, 0.707106781186547]
from axis-angle axis [1, 1, 1] angle 120 deg: [0.5, 0.5, 0.5, 0.5]
from axis-angle axis [1, 0, 0] angle 3.14159265358979 rad: [6.12323399573677e-17, 1, 0, 0]
from Euler yaw 0 pitch 0 roll 0 (rad): [1, 0, 0, 0]
from Euler yaw 0.3 pitch 0.5 roll 0.7 (rad): [0.887272187679753, 0.279443894078474, -0.052132410889548, 0.363237369728236]
from Euler yaw 90 pitch 0 roll 0 (deg): [0.707106781186548, 0, -0.707106781186547, 0]
from Euler yaw 0 pitch 30 roll 0 (deg): [0.965925826289068, 0.258819045102521, 0, 0]
from Euler yaw 0 pitch 0 roll 45 (deg): [0.923879532511287, 0, 0, 0.38268343236509]
rotate vector quaternion [0, 1, 0, 0] by rotation quaternion [0.707106781186548, 0, 0, 0.707106781186547] = [0, 2.22044604925031e-16, 1, 0]
rotate vector quaternion [0, 0, 1, 0] by rotation quaternion [6.12323399573677e-17, 1, 0, 0] = [0, 0, -1, 1.22464679914735e-16]
rotate vector quaternion [0, 0, 0, 1] by rotation quaternion [0.707106781186548, 0, 0.707106781186547, 0] = [0, 1, 0, 2.22044604925031e-16]
rotate vector quaternion [0, 1, 0, 0] by rotation quaternion [0.5, 0.5, 0.5, 0.5] = [0, 1.11022302462516e-16, 1, -1.11022302462516e-16]
```

### 5.9 CRC
Catalog parameter sets with the check value over the ASCII bytes `123456789`. The independent model
reproduced all 21 catalog check values below, the 32-bit ISO-HDLC form also matched `zlib.crc32`, and the
XMODEM and CCITT-FALSE forms matched `binascii.crc_hqx`.

| Parameter set | width | polynomial | init | final xor | reflect in | reflect out | check over ASCII 123456789 |
|---|---|---|---|---|---|---|---|
| CRC-8 | 8 | 0x7 | 0x0 | 0x0 | 0 | 0 | 0xF4 |
| CRC-8/MAXIM | 8 | 0x31 | 0x0 | 0x0 | 1 | 1 | 0xA1 |
| CRC-8/DARC | 8 | 0x39 | 0x0 | 0x0 | 1 | 1 | 0x15 |
| CRC-8/SAE-J1850 | 8 | 0x1D | 0xFF | 0xFF | 0 | 0 | 0x4B |
| CRC-8/BLUETOOTH | 8 | 0xA7 | 0x0 | 0x0 | 1 | 1 | 0x26 |
| CRC-12/DECT | 12 | 0x80F | 0x0 | 0x0 | 0 | 0 | 0xF5B |
| CRC-12/UMTS | 12 | 0x80F | 0x0 | 0x0 | 0 | 1 | 0xDAF |
| CRC-12/GSM | 12 | 0xD31 | 0x0 | 0xFFF | 0 | 0 | 0xB34 |
| CRC-12/CDMA2000 | 12 | 0xF13 | 0xFFF | 0x0 | 0 | 0 | 0xD4D |
| CRC-16/ARC | 16 | 0x8005 | 0x0 | 0x0 | 1 | 1 | 0xBB3D |
| CRC-16/MODBUS | 16 | 0x8005 | 0xFFFF | 0x0 | 1 | 1 | 0x4B37 |
| CRC-16/CCITT-FALSE | 16 | 0x1021 | 0xFFFF | 0x0 | 0 | 0 | 0x29B1 |
| CRC-16/XMODEM | 16 | 0x1021 | 0x0 | 0x0 | 0 | 0 | 0x31C3 |
| CRC-16/KERMIT | 16 | 0x1021 | 0x0 | 0x0 | 1 | 1 | 0x2189 |
| CRC-16/DNP | 16 | 0x3D65 | 0x0 | 0xFFFF | 1 | 1 | 0xEA82 |
| CRC-32/ISO-HDLC | 32 | 0x4C11DB7 | 0xFFFFFFFF | 0xFFFFFFFF | 1 | 1 | 0xCBF43926 |
| CRC-32/BZIP2 | 32 | 0x4C11DB7 | 0xFFFFFFFF | 0xFFFFFFFF | 0 | 0 | 0xFC891918 |
| CRC-32/MPEG-2 | 32 | 0x4C11DB7 | 0xFFFFFFFF | 0x0 | 0 | 0 | 0x0376E6E7 |
| CRC-32C | 32 | 0x1EDC6F41 | 0xFFFFFFFF | 0xFFFFFFFF | 1 | 1 | 0xE3069283 |
| CRC-32Q | 32 | 0x814141AB | 0x0 | 0x0 | 0 | 0 | 0x3010BF7F |
| CRC-64/ECMA-182 | 64 | 0x42F0E1EBA9EA3693 | 0x0 | 0x0 | 0 | 0 | 0x6C40DF5F0B497347 |

Further messages under eight of those parameter sets (exact match):

| Message | CRC-8 | CRC-8/MAXIM | CRC-12/DECT | CRC-12/UMTS | CRC-16/ARC | CRC-16/CCITT-FALSE | CRC-32/ISO-HDLC | CRC-32C |
|---|---|---|---|---|---|---|---|---|
| empty message | 0x00 | 0x00 | 0x000 | 0x000 | 0x0000 | 0xFFFF | 0x00000000 | 0x00000000 |
| bytes 00 01 02 03 | 0x48 | 0xD8 | 0x851 | 0x8A1 | 0xA110 | 0xE5F1 | 0x8BB98613 | 0xD9331AA3 |
| ASCII The quick brown fox jumps over the lazy dog | 0xC1 | 0x16 | 0x515 | 0xA8A | 0xFCDF | 0x8FDD | 0x414FA339 | 0x22620404 |
| single byte FF | 0xF3 | 0x35 | 0x606 | 0x606 | 0x4040 | 0xFF00 | 0xFF000000 | 0xFF000000 |

Behavior of the command defaults (no optional parameters; exact match):
```
CRC8 with all defaults (polynomial 0x7, init 0, final xor 0, no reflection) over ASCII 123456789: 0xF4
CRC8 with all defaults (polynomial 0x7, init 0, final xor 0, no reflection) over bytes 00 01 02 03: 0x48
CRC8 with all defaults (polynomial 0x7, init 0, final xor 0, no reflection) over single byte FF: 0xF3
CRC12 with all defaults (polynomial 0x80D, init 0, final xor 0, no reflection) over ASCII 123456789: 0xEFB
CRC12 with all defaults (polynomial 0x80D, init 0, final xor 0, no reflection) over bytes 00 01 02 03: 0x005
CRC12 with all defaults (polynomial 0x80D, init 0, final xor 0, no reflection) over single byte FF: 0x752
CRC16 with all defaults (polynomial 0x1021, init 0, final xor 0, no reflection) over ASCII 123456789: 0x31C3
CRC16 with all defaults (polynomial 0x1021, init 0, final xor 0, no reflection) over bytes 00 01 02 03: 0x6131
CRC16 with all defaults (polynomial 0x1021, init 0, final xor 0, no reflection) over single byte FF: 0x1EF0
CRC32 with all defaults (polynomial 0x4C11DB7, init 0, final xor 0, no reflection) over ASCII 123456789: 0x89A1897F
CRC32 with all defaults (polynomial 0x4C11DB7, init 0, final xor 0, no reflection) over bytes 00 01 02 03: 0xAC691451
CRC32 with all defaults (polynomial 0x4C11DB7, init 0, final xor 0, no reflection) over single byte FF: 0xB1F740B4
```

Divergence vectors where the legacy final-XOR-versus-reflection order differs from the required model
(the implementation must produce the "required" value):
```
width 8 poly 0x7 init 0x0 final-xor 0x55 reflect-in 0 reflect-out 1 over ASCII 123456789: required (reflect, then xor) 0x7A ; legacy 8/32-bit order (xor, then reflect) 0x85
width 32 poly 0x4C11DB7 init 0xFFFFFFFF final-xor 0x12345678 reflect-in 0 reflect-out 1 over ASCII 123456789: required (reflect, then xor) 0xF55338B8 ; legacy 8/32-bit order (xor, then reflect) 0xF90D4288
width 8 poly 0x7 init 0x0 final-xor 0xFF reflect-in 0 reflect-out 1 over ASCII 123456789: required (reflect, then xor) 0xD0 ; legacy 8/32-bit order (xor, then reflect) 0xD0
```
Legacy 12-bit defect: the unmasked legacy routine returns 0xDF5B for CRC-12/DECT over `123456789`;
the required result is 0xF5B.

### 5.10 Random numbers
Mersenne Twister reference outputs (32-bit words, standard seeding). These are exact:
```
seed 5489: outputs 1..3 = [3499211612, 581869302, 3890346734] ; output 10000 = 4123659995
seed 1: outputs 1..3 = [1791095845, 4282876139, 3093770124]
seed 0: outputs 1..3 = [2357136044, 2546248239, 3071714933]
unit mapping from a 32-bit word w: u = w / 2^32 (half-open). For w = 3499211612: u = 0.814723691903055 ; for w = 0: u = 0 ; for w = 4294967295: u = 0.999999999767169 (< 1)
```
Statistical acceptance (the exact stream is implementation-defined only for the normal deviate; the uniform
words above are exact). With the generator seeded 20260101 and 200000 draws: uniform unit values have mean
within 0.004 of 0.5 and every value in [0, 1); normal deviates (mean 0, sd 1) have sample mean within 0.012 of
0, sample standard deviation within 0.012 of 1, and between 0.67 and 0.695 of the deviates within one unit
of the mean. Each bound is at least five standard errors, so a correct implementation passes with
probability above 0.999999.

### 5.11 FFT
Tolerance 1e-9 relative to the largest magnitude in the output; phase compared only where the magnitude
exceeds 1e-9:
```
x = [1, 2, 3, 4]
    X = [(10, 0), (-2, 2), (-2, 0), (-2, -2)]
    |X| = [10, 2.82842712474619, 2, 2.82842712474619]
    arg X (radians; compare only where |X| > 1e-9) = [0, 2.35619449019234, 3.14159265358979, -2.35619449019234]
x = [1, 0, 0, 0, 0, 0, 0, 0]
    X = [(1, 0), (1, 0), (1, 0), (1, 0), (1, 0), (1, 0), (1, 0), (1, 0)]
    |X| = [1, 1, 1, 1, 1, 1, 1, 1]
    arg X (radians; compare only where |X| > 1e-9) = [0, 0, 0, 0, 0, 0, 0, 0]
x = [1, 0.707106781186548, 6.12323399573677e-17, -0.707106781186547, -1, -0.707106781186548, -1.83697019872103e-16, 0.707106781186547]
    X = [(0, 0), (4, -5.58947851566226e-16), (0, 0), (0, 0), (0, 0), (0, 0), (0, 0), (4, 5.58947851566226e-16)]
    |X| = [0, 4, 0, 0, 0, 0, 0, 4]
    arg X (radians; compare only where |X| > 1e-9) = [0, -1.39736962891556e-16, 0, 0, 0, 0, 0, 1.39736962891556e-16]
x = [1, -1, 1, -1]
    X = [(0, 0), (0, 0), (4, 0), (0, 0)]
    |X| = [0, 0, 4, 0]
    arg X (radians; compare only where |X| > 1e-9) = [0, 0, 0, 0]
x = [5]
    X = [(5, 0)]
    |X| = [5]
    arg X (radians; compare only where |X| > 1e-9) = [0]
inverse: X = FFT of [0.5, -1, 2.5, 3, 0, -2, 1, 4] (as above) returns the real signal [0.5, -1, 2.5, 3, 0, -2, 1, 4]
inverse of X = [(10,0), (-2,2), (-2,0), (-2,-2)] returns [1, 2, 3, 4]
n = 6 or n = 0 -> error class: length not a power of two
```

### 5.12 Interpolation, rescaling, scaling, power, shifts, element-wise operations
```
lerp(a=0, b=10, t=0.25) = 2.5
lerp(a=10, b=0, t=1.5) = -5
lerp(a=2, b=4, t=0) = 2
lerp(a=1, b=3, t=0.5) = 2
```
```
rescale [1, 2, 3] into [7, 3] = [7, 5, 3] (input min 1, input max 3)
rescale [0, 5, 10] into [0, 1] = [0, 0.5, 1] (input min 0, input max 10)
rescale [-4, 0, 4, 8] into [-1, 1] = [-1, -0.333333333333333, 0.333333333333333, 1] (input min -4, input max 8)
rescale of [3,3,3] -> error class: zero range (legacy divides by zero)
```
```
scale [1, 2, 3] by 2 = [2, 4, 6]
scale [4, -6, 0.5] by -0.5 = [-2, 3, -0.25]
scale [10] by 0 = [0]
power [1, 2, 3] ^ 2 = [1, 4, 9]
power [4, 9] ^ 0.5 = [2, 3]
power [2, -3] ^ 3 = [8, -27]
power [8] ^ -0.333333333333333 = [0.5]
power [-4] ^ 0.5 -> error class: domain (legacy returns NaN for float output)
```
```
shift 1 left by 3 = 8 ; shift 5 left by 62 = 4611686018427387904 ; shift 1 left by 63 = -9223372036854775808
arithmetic right shift: -16 by 2 = -4 ; 1000 by 3 = 125 ; -1 by 63 = -1
logical right shift: -16 by 2 = 4611686018427387900 ; -1 by 1 = 9223372036854775807 ; 256 by 4 = 16
shift by 0 returns the input unchanged; shift amounts outside -63..63 -> error class: argument range
```
```
add [1,2] + [3,4] = [4,6] ; subtract [10,5] - [3,7] = [7,-2] ; multiply [2,3] * [4,-5] = [8,-15] ; divide float [9,1] / [3,4] = [3, 0.25]
integer and [12,10] & [10,6] = [8, 2] ; or [12,10] | [10,6] = [14, 14] ; xor [12,10] ^ [10,6] = [6, 12]
integer divide [7,-7] / [2,2] truncates toward zero = [3,-3]; divisor zero -> error class: division by zero
operands differing in length -> error class: size
```

### 5.13 Base64 (RFC 4648 vectors; exact)
```
encode (empty) -> "" (length 0) ; decode returns 0 bytes
encode 66 -> "Zg==" (length 4) ; decode returns 1 bytes
encode 666f -> "Zm8=" (length 4) ; decode returns 2 bytes
encode 666f6f -> "Zm9v" (length 4) ; decode returns 3 bytes
encode 666f6f62 -> "Zm9vYg==" (length 8) ; decode returns 4 bytes
encode 666f6f6261 -> "Zm9vYmE=" (length 8) ; decode returns 5 bytes
encode 666f6f626172 -> "Zm9vYmFy" (length 8) ; decode returns 6 bytes
encode 000102 -> "AAEC" (length 4) ; decode returns 3 bytes
encode fffefdfc -> "//79/A==" (length 8) ; decode returns 4 bytes
decode of "Zm9v!A==" (character outside the alphabet) -> error class: invalid input (legacy maps it silently to zero bits)
decode of a length not a multiple of 4 -> error class: invalid input (legacy drops the trailing partial group)
```

### 5.14 PID update formula (tolerance 1e-12; the vectors check the formula, not the scheduler)
```
Kp 2, Ki 0.5, Kd 0.1, tau 0.02, output limits [-10, 10], integrator limits [-5, 5], T 0.1, setpoint 1; measurements [0, 0.2, 0.5, 0.9, 1] -> outputs [2.025, 1.38428571428571, 0.796377551020408, -0.122733236151604, 0.165814244064973]
Kp 100, Ki 10, Kd 0, tau 0.05, output limits [-10, 10], integrator limits [-2, 2], T 0.01, setpoint 1; measurements [0, 0, 0.1] -> outputs [10, 10, 10]
Kp 0, Ki 1, Kd 0, tau 0.05, output limits [-1, 1], integrator limits [-0.25, 0.25], T 0.5, setpoint 2; measurements [0, 0, 0, 0] -> outputs [0.25, 0.25, 0.25, 0.25]
```
The first row follows the Required recurrence of section 3.12, derived independently of the legacy expression (tau 0.02 is below T/2 = 0.05, so the pole is z = -0.43 and the response rings while it decays). Two properties hold for any parameters and need no stored vector: with Kp = Ki = 0, a measurement ramp m[n] = n*T*r settles at output = -Kd*r; with Kp = Ki = 0 and tau >= T/2, a held step in m gives a differentiator that keeps one sign and shrinks in magnitude at every update.

### 5.15 AES-128 (NIST FIPS 197 and SP 800-38A vectors produced with the OpenSSL command-line tool)
```
ECB, key 000102030405060708090a0b0c0d0e0f, one block 00112233445566778899aabbccddeeff -> 69c4e0d86a7b0430d8cdb78070b4c55a
ECB, key 2b7e151628aed2a6abf7158809cf4f3c, four blocks 6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710 -> 3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf43b1cd7f598ece23881b00e3ed0306887b0c785e27e8ad3f8223207104725dd4
CBC, key 2b7e151628aed2a6abf7158809cf4f3c, iv 000102030405060708090a0b0c0d0e0f, four blocks as above -> 7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7 ; decrypting returns the plaintext
CTR, key 2b7e151628aed2a6abf7158809cf4f3c, initial counter block f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff, four blocks as above -> 874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee ; the same operation decrypts
Wrapper framing: an encrypt operation without a caller IV returns 16 IV bytes followed by the ciphertext (length n + 16); decrypt reads the first 16 bytes as the IV. Input length not a multiple of 16 -> error class: size. Key length other than 16 -> error class: size.
```

### 5.16 Sensor fusion (invariants only; not part of the calculator)
Quaternion norm stays within 1e-12 of 1 after every update. With the correction gain set to zero, one update
of a pure rotation rate from the identity orientation with a 0.1 s step gives (tolerance 1e-12):
```
rates (gx, gy, gz) = (0, 0, 1) rad/s, dt 0.1 s, identity start, zero correction gain: quaternion [0.998752338877845, 0, 0, 0.0499376169438922], roll 0, pitch 0, yaw 0.0999167914438855 (radians)
rates (gx, gy, gz) = (1, 0, 0) rad/s, dt 0.1 s, identity start, zero correction gain: quaternion [0.998752338877845, 0.0499376169438922, 0, 0], roll 0.0999167914438855, pitch 0, yaw 0 (radians)
rates (gx, gy, gz) = (0, 1, 0) rad/s, dt 0.1 s, identity start, zero correction gain: quaternion [0.998752338877845, 0, 0.0499376169438922, 0], roll 0, pitch 0.0999167914438855, yaw 0 (radians)
```
Test inputs for the correction terms are an accelerometer sample (0, 0, 1) and a magnetometer sample
(0.3, 0, 0.4); an implementation treats a zero-length correction step as no correction rather than dividing
by zero. Madgwick with gain zero and Mahony with both gains zero reduce to the same first-order integration for the
first update; the Mahony defect noted in section 3.12 changes later components of that first update by a
second-order amount and is not exercised.

## 6. Similarity-review checklist (reviewer only; line numbers refer to the original `MATHS.c`)

The reviewer holds both texts. The implementer sees this list too, which is intentional: it names what the
new code must not contain.

### 6.1 Mechanical procedure

1. Normalize both sources (strip comments and whitespace, lower-case identifiers) and look for any run of 12 or
   more identical consecutive tokens longer than a single standard expression; every run is a finding.
2. Search the implementation, tests and comments for each identifier, constant and literal below,
   case-insensitively. Identifiers and constants in the "Distinctive" columns are findings. Items marked
   "standard" are expected to appear because the published algorithm fixes them.
3. Compare comment text: no comment sentence of the original may appear, even paraphrased by word order only.
4. Compare function decomposition and ordering for the regions in 6.4 and 6.5.
5. Record each finding as "region and rule", never as a quote of the original.

### 6.2 Distinctive identifiers (lines)

| Lines | Identifier family | Note |
|---|---|---|
| 46-49 | `PI` global, `cplx`, `fcplx` typedefs, forward declaration of the FFT command | |
| 50-102 | `chitable` | 51 rows by 15 columns, first row holds the 15 probabilities |
| 103-104 | `q`, `eInt` filter state arrays | |
| 114-171 | `g_myrand`, `tagMTRand`, `m_seedRand`, `seedRand`, `genRandLong`, `UPPER_MASK`, `LOWER_MASK`, `TEMPERING_MASK_B`, `TEMPERING_MASK_C`, static `mag` pair | the masks and 0x9908b0df are standard |
| 173-271 | `b64_chr`, `b64_int`, `b64e_size`, `b64d_size`, `b64_encode`, `b64_decode` | the size helper counts groups in a loop |
| 276-278 | `genRand` | divides by 0xffffffff |
| 283-295 | `floatshellsort` | variable names h, l, j, k |
| 297-337 | `alloc1df`, `alloc2df`, `dealloc2df` | |
| 339-354 | `Q_Mult`, `Q_Invert` | fifth element multiplied |
| 355-404 | `reverse8`, `reverse12`, `reverse16`, `reverse32`, `reverse64` | mask-and-shift ladder; 0xAA/0x55, 0xCC/0x33, 0xF0/0x0F pattern is standard |
| 430-590 | `crc8`, `crc12`, `crc16`, `crc16_CCITT`, `crc32`, `crc64`, parameter names `polynome`, `startmask`, `endmask`, `reverseIn`, `reverseOut` | MIT-licensed third-party block (Rob Tillaart); a periodic `routinechecks()` call every 256 bytes |
| 592-758 | `parseintegerarray`, `parsestringarray`, `parsenumberarray`, `parsefloatrarray`, `parsearrays`, `parseany` | interpreter-bound; nothing similar belongs in the new code |
| 759-919 | `parseAES`, `parseB64`, `returnAES` | |
| 920-927 | `farr2d`, `iarr2d` | index computation `d1*b+a` |
| 928-987 | `PIDController_Update`, struct fields `Kp`, `Ki`, `Kd`, `tau`, `limMin`, `limMax`, `limMinInt`, `limMaxInt`, `T`, `integrator`, `prevError`, `differentiator`, `prevMeasurement`, `out` (defined in `MATHS.h`) | the 14-field order is a distinctive layout |
| 989-995 | `getrnd` | |
| 1764-1800 | locals `inmin`, `inmax`, `outmin`, `outmax` (the spec uses `input_min` and so on) | low-confidence name overlap |
| 996-1866 | `cmd_math` | first-letter dispatch (S, C, V, M, Q, other) |
| 1872-1883 | `getComplex`, `retComplex` | eight-byte copy of a float complex into an integer |
| 1885-2683 | `fun_math` | |
| 2685-2741 | `reverse_bits`, `Fft_transformRadix2`, `exptable` | unattributed third-party shape: level count, half-size table, three nested loops |
| 2745-2825 | `cmd_FFT` | loop `i<65536` doubling to test power of two |
| 2826-2889 | `cmd_SensorFusion` | |
| 2895-3111 | `MadgwickQuaternionUpdate`, `MahonyQuaternionUpdate`, locals `_2q1mx`, `_2q1my`, `_2q1mz`, `_2q2mx`, `_4bx`, `_4bz`, `_2bx`, `_2bz`, `qDot1`..`qDot4`, `s1`..`s4`, `pa`, `pb`, `pc`, `ysqr`, `t0`..`t4` | |
| 3113-3225 | `transpose` (which divides by the determinant), `cofactor`, `determinant`, `m_minor`, `m_cofactor` | recursive expansion; the function named `transpose` performs the inversion step |

### 6.3 Distinctive constants and values

| Lines | Value | Rule |
|---|---|---|
| 128 | seed multiplier 6069 | must not appear; the new code uses the published 1812433253 |
| 151 | default reseed value 4357 | must not appear |
| 146, 167-169 | 0x9908b0df, 0x9d2c5680, 0xefc60000, shifts 11, 7, 15, 18 | standard MT19937; expected |
| 277 | divisor 0xffffffff | must not appear in the uniform mapping |
| 1764 | sentinels 1.5e+308 and -1.5e308 | must not appear |
| 2431, 2473 | sentinels -3.0e+38 and 3.0e+38 | must not appear |
| 50-102 | any run of the printed table values, for example the first data row beginning 0.0000397 and 0.000157 | must not appear; the new code computes probabilities |
| 2705 | `SIZE_MAX / sizeof(double complex)` guard | shape finding if present |

### 6.4 String literals (error and keyword text)

Error text in the original is short and may recur by coincidence; the reviewer flags exact matches of the
multi-word ones and any whole table of messages.

| Lines | Literal (abbreviated) |
|---|---|
| 599, 627, 657, 686 | "Cannot change a constant" |
| 609-612, 638-641, 668-671, 696-699 | "Argument % must be ..." family (integer array, string array, numerical array, floating point array, 1D/2D) |
| 709 | "Argument count" |
| 713 | "Array size mismatch" |
| 714 | "Arrays must be all integer or all floating point" |
| 774, 777, 786 | "Key must be 16 elements long", "input must be multiple of 16 elements long", "Initialisation vector must be 16 elements long" |
| 789, 806, 821, 2061 | "Key number out of bounds 0-255", "input number out of bounds 0-255", "Variable > 255" |
| 909, 915 | "Too many elements for string output" |
| 1215, 1426 | "Destination array same as source" |
| 1291-1295, 1516-1518, 1534, 1554, 1581, 1603-1607 | "Argument N must be a 3 element floating point array" and the 5 element variants |
| 1345, 2413 | "Array must be square" |
| 1346 | "Same array specified for input and output" |
| 1357 | "Determinant of array is zero" |
| 1421, 1425 | "Input array size mismatch", "Output array size mismatch" |
| 1697, 1703, 1713-1714, 1901-1902 | "Channel not initialised", "Channel not configured", "Channel not active", "Argument 2 must be a 14 element floating point array", "Invalid update rate" |
| 1831, 1841 | "Arrays must be different" |
| 2197 | "Valid are -1 and 1" |
| 2396-2397 | "Divide by zero", "Negative argument" |
| 2660-2661, 2670-2671 | "Output array too small", "Output exceeds string size" |
| 2765 | "array size must be a power of 2" |
| 2830, 2859, 2888 | "Incorrect number of parameters", "Invalid command" |
| 302, 318 | "Unable to allocate memory for 1D/2D float array..." |
| 1011-1862 (command dispatch), 1893-2680 (function dispatch) | keyword spellings: SET, SCALE, SHIFT, SLICE, SENSORFUSION, C_ADD, C_MUL, C_MULT, C_AND, C_XOR, C_OR, C_SUB, C_DIV, V_MULT, V_ROTATE, V_NORMALISE, V_CROSS, V_PRINT, M_INVERSE, M_TRANSPOSE, M_MULT, M_PRINT, Q_INVERT, Q_VECTOR, Q_EULER, Q_CREATE, Q_MULT, Q_ROTATE, AES128, PID, ADD, POWER, WINDOW, RANDOMIZE, INTERPOLATE, INSERT, FFT; function keywords such as ATAN3, CHI_P, CROSSING, DOTPRODUCT, M_DETERMINANT, MAGNITUDE, BASE64 | interface names may be reused only where the calculator command set deliberately mirrors the MMBasic names; internal identifiers must not |

### 6.5 Code shapes to compare by eye

- 1030-1050 and 1737-1757: four-way nested branch on (float or integer) input crossed with (float or integer)
  output, each branch a single-statement loop.
- 1092-1198: seven copies of the same fetch-three-arrays-then-loop block.
- 2037-2176: four copies of the CRC wrapper that differ only in width and range.
- 2296-2368: the chi-square lookup walks the table outward from the 10 percent column.
- 2745-2825: four near-identical blocks each testing power-of-two size by a doubling loop.
- 3113-3225: factorial-time minor/cofactor recursion; the new determinant and inverse use elimination, so
  any recursion on minors is a finding.

## 7. Items the analyst could not fully specify, and open questions

1. Bit-exact compatibility with the original random stream is excluded by decision, not by inability: it
   requires the nonstandard seeding constant in section 6.3, which the implementer must not carry.
2. The accuracy of the in-tree `lib/libm` (`sinh`, `cosh`, `tanh`, `atan2`, `log10`, `exp`, `log`, `pow`,
   `sin`, `cos`) was not measured. If it falls short of 1e-12, the implementer reports the measured bound and
   proposes either per-item looser tolerances or a more accurate replacement in `MATHEMATICS.c`. The tree has
   no complex header, no inverse hyperbolic functions, no `lgamma`/`erfc`-based gamma, and no `expm1`; the
   implementer builds what it needs.
3. The unit mismatch in the PID scheduler (section 3.12) cannot be resolved from the analyzed file; it is
   irrelevant to the calculator.
4. The delegated array commands (SET, ADD, SLICE, INSERT) and the AES cipher live in other files; their
   behavior comes only from the manual summary and the wrapper reading, not from their implementation.
5. The behavior of every legacy routine on NaN or infinite input was not analyzed beyond the cases stated.
   The new code defines: NaN in, NaN out for pure functions, and a domain status wherever an item says so.
6. The original was never executed. Any place where the reading may be wrong (for example the exact
   crossing rule, or the chi-square bracket selection) is covered by the required behavior of the new code
   rather than by a vector that depends on the original's behavior; the crossing vectors test the stated rule
   and should be treated as a specification choice the owner can replace with the simpler "first
   sign-change" rule if desired.
7. Whether the calculator front end wants the MMBasic keyword names, the Python/`bc`-like names, or its own is
   a product decision outside this document.
