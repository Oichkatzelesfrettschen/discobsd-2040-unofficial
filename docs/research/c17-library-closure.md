# Hosted C17 library closure

This note decomposes the request "bring libc to C17 under SUSv4 and SUSv5"
into separable obligations, measures the remaining C17 clause 7 surface, and
orders the work. [`c17-semantic-migration-plan.md`](c17-semantic-migration-plan.md)
owns language migration of existing source and
[`sus-edition-contracts.md`](sus-edition-contracts.md) owns the selected POSIX
rows; this note owns one question neither answers: which interfaces a hosted
C17 implementation requires that the tree does not yet declare or provide.

The measurement base is `b3f293a6fb0e0cce4ff3a02c24e3861a176a0660`. Claims
about other C libraries below are recalled from those projects and were not
re-read for this note; each is a design lead to verify against a pinned
revision before any text is borrowed, as
[`dpucp-2024-donor-ledger.json`](dpucp-2024-donor-ledger.json) does.

## Decomposition of the request

| Strand | Obligation | Owner | State |
| --- | --- | --- | --- |
| Standards layering | State which document requires what: ISO C17 clause 7, POSIX Issue 7, POSIX Issue 8 | `sus-edition-contracts.md`, this note | Layering below; per-row contracts continue there |
| Language dialect | Existing source compiles as GNU17, portable mechanisms as strict C17 | `c17-semantic-migration-plan.md`, `check-c17-source-denominator` | 163 paths accepted, 63 migration-open, 2,330 unreviewed |
| Library surface | Every C17 clause 7 name is declared, provided and tested, or carries a stated disposition | this note | Ledger below; first unit delivered |
| Mechanisms | Each provider states its invariant and its cost on ARMv6-M | per-unit contract notes | Catalog below |
| Donor comparison | Other libraries inform mechanism choice under a recorded license and revision | `dpucp-2024-donor-ledger.json` | Leads listed, none imported |
| Resource admission | Growth on a constrained surface has a measured vector and owner approval | growth-exception records | Unit 1 adds nothing to a shipped surface |
| Refactoring | Old-style definitions and legacy warning profiles in libc move to typed C17 | migration plan queue | `lib/libc/stdlib` still builds at `WARNLEVEL=legacy` |

"C17 compliance" is three independent claims. The *language* claim concerns
what the compiler accepts. The *library* claim concerns clause 7. The
*implementation* claim (C17 clause 4) additionally requires documented
implementation-defined behavior and correct feature macros. A passing strict
C17 compile establishes the first for the files compiled and nothing for the
other two.

## Standards layering

C17 (ISO/IEC 9899:2018) defines 29 library headers. Clause 4 paragraph 6
requires nine of them from a freestanding implementation: `<float.h>`,
`<iso646.h>`, `<limits.h>`, `<stdalign.h>`, `<stdarg.h>`, `<stdbool.h>`,
`<stddef.h>`, `<stdint.h>` and `<stdnoreturn.h>`. Three features are
conditional: an implementation may define `__STDC_NO_COMPLEX__`,
`__STDC_NO_THREADS__` or `__STDC_NO_ATOMICS__` and omit `<complex.h>`,
`<threads.h>` or `<stdatomic.h>`. No such macro exists for `<locale.h>`,
`<wchar.h>`, `<wctype.h>`, `<uchar.h>`, `<fenv.h>` or `<tgmath.h>`; a hosted
implementation provides them, though a single "C" locale with a one-byte
encoding satisfies their semantics.

POSIX defers to ISO C for every interface the two share. Issue 7
(POSIX.1-2008, 2017 edition, SUSv4) is aligned with C99. Issue 8
(POSIX.1-2024, SUSv5) is aligned with C17, so under Issue 8 the C17 library
is part of the POSIX base rather than an addition to it. Issue 8 also adopts
interfaces this tree already carries as extensions, among them `qsort_r`,
`reallocarray`, `memmem`, `strlcpy` and `strlcat`, and `quick_exit`. The
edition delta for a row is recorded in `sus-edition-contracts.md`; this
paragraph is orientation and decides no row.

The build passes `-nostdinc`, so the compiler's own copies of the
freestanding headers are unreachable and every clause 7 header is the tree's
responsibility.

## Measured surface

Each row is the result of preprocessing the named header alone with
`arm-none-eabi-gcc -std=c17 -mcpu=cortex-m0plus -mthumb -nostdinc -Iinclude
-E -dD` and searching the output for the clause 7 name. The probe list is a
sample of each header's required names, written from the standard's
synopsis; it finds absent names and does not prove a header complete.

| Header | Absent before unit 1 | Disposition |
| --- | --- | --- |
| `<iso646.h>` | whole header | **Delivered** in unit 1 |
| `<stdlib.h>` | `div`, `ldiv`, `lldiv`, `llabs`, `strtof`, `strtold` | Integer members **delivered** in unit 1; conversions in unit 3 |
| `<time.h>` | `clock`, `CLOCKS_PER_SEC`, `timespec_get`, `TIME_UTC` | Unit 2 |
| `<string.h>` | `strcoll`, `strxfrm` | Unit 4; in the "C" locale they are `strcmp` and a bounded copy |
| `<locale.h>` | whole header | Unit 4 |
| `<float.h>` | `FLT_EVAL_METHOD`, `DECIMAL_DIG`, the `*_DECIMAL_DIG`, `*_TRUE_MIN` and `*_HAS_SUBNORM` families | Unit 2; compiler-predefined values |
| `<math.h>` | classification macros, `float_t`, `double_t`, `NAN`, `math_errhandling`, and most C99 functions including every `f` and `l` variant | Units 5 and 6 |
| `<fenv.h>`, `<tgmath.h>` | whole headers | Unit 6 |
| `<wchar.h>`, `<wctype.h>`, `<uchar.h>` | whole headers | Unit 7 |
| `<complex.h>`, `<threads.h>`, `<stdatomic.h>` | whole headers | Unit 8 decides between provision and the `__STDC_NO_*__` macros |

`<assert.h>`, `<ctype.h>`, `<errno.h>`, `<inttypes.h>`, `<limits.h>`,
`<setjmp.h>`, `<signal.h>`, `<stdarg.h>`, `<stddef.h>`, `<stdint.h>` and
`<stdio.h>` showed no absent name in the sample. `lib/libm/erf.c` exists
while `<math.h>` declares neither `erf` nor `erfc`, so presence in a source
directory and presence in the interface remain separate observations.

## Mechanisms and their invariants

The catalog names, for each open unit, the property a provider must hold and
a mechanism that fits a processor with no divide instruction, no FPU and a
192 KiB process window. Each is a candidate; a unit adopts one only with its
own measured cost.

- **Truncating division.** For `d != 0` there is exactly one pair `(q, r)`
  with `n == q*d + r`, `|r| < |d|` and `r == 0` or `sign(r) == sign(n)`.
  C99 fixed `/` and `%` to that pair (6.5.5p6), so `div` needs no correction
  step; the pre-C99 BSD `div.c` carried one because C89 left the rounding
  direction of a negative quotient to the implementation. Floored division
  satisfies the first two conditions and fails the third, which is why the
  gate checks all three.
- **64-bit division.** `imaxdiv` owns a 64-step restoring divider. A faster
  candidate is schoolbook division in base 2^16 on the RP2040 SIO divider:
  each step divides a 32-bit partial remainder by a 16-bit digit, and a
  divisor wider than 16 bits needs Knuth's algorithm D normalization, which
  bounds each quotient-digit estimate to at most two corrections. It trades
  size for speed and is unmeasured here.
- **Calendar arithmetic.** Day count from a civil date reduces to closed
  form by starting the year in March, so the leap day falls last and the
  month lengths follow `(153*m + 2)/5`; the 400-year era of 146,097 days
  makes the function total over the whole `time_t` range without a loop.
  `timespec_get` needs only `gettimeofday` and a multiply by 1,000.
- **Byte search.** A word `w` contains a zero byte exactly when
  `(w - 0x01010101) & ~w & 0x80808080` is nonzero.
  [`libc-byte-search-factorization.md`](libc-byte-search-factorization.md)
  already applies it; `wmemchr` and `wcslen` in unit 7 reduce to plain word
  compares because `wchar_t` is one word.
- **Decimal to binary.** Correct rounding of `strtod` needs the decimal
  significand compared against a binary midpoint. The Eisel-Lemire method
  settles almost every input with one 64x64 to 128-bit multiply against a
  table of powers of five, and falls back to exact big-integer comparison
  otherwise. On a soft-float target the multiply is four 32x32 products.
  `strtof` must round once from the decimal value; converting through
  `double` rounds twice and is wrong for inputs near a `float` midpoint.
- **Binary to decimal.** Shortest round-trip output (`%.17g` is sufficient,
  not minimal) is produced by Ryu with a table of 128-bit multipliers or by
  Grisu with a fallback. Table size against the 989 KiB root decides which;
  `stdio-formatter-count-contract.md` owns the existing formatter.
- **Rounding functions.** `trunc`, `floor`, `ceil`, `round` and `rint`
  clear or test mantissa bits selected by the unbiased exponent and need no
  arithmetic, so they raise no spurious inexact flag and cost a few dozen
  instructions each. `copysign`, `fabs`, `signbit` and the classification
  macros are sign-and-exponent field tests.
- **Wide characters.** With `MB_CUR_MAX == 1` every multibyte conversion is
  the identity on one byte and conversion state is empty, so `mbrtowc`,
  `wcrtomb` and the `c16`/`c32` functions need no decoder. Admitting UTF-8
  later would replace that with a table-driven DFA whose accept state also
  rejects overlong and surrogate forms.
- **Sorting.** [`libc-bounded-sort-contracts.md`](libc-bounded-sort-contracts.md)
  already bounds recursion by descending into the smaller partition first,
  which caps depth at `log2(n)` frames.

## Design leads from other libraries

| Library | License class | Lead |
| --- | --- | --- |
| musl | MIT | One-locale `<locale.h>` and `<wchar.h>`; bit-field rounding functions; `timespec_get` over `clock_gettime` |
| picolibc and newlib-nano | BSD and mixed | Size-selected formatter and `strtod` variants; soft-float `<fenv.h>` whose flags are constant |
| NetBSD, OpenBSD, FreeBSD libc | BSD | Typed successors of the 4.4BSD members this tree descends from; FreeBSD msun for libm |
| PDCLib | CC0 | A complete C11 library in small separate members, useful as a per-function reading order |
| LLVM libc | Apache-2.0 with LLVM exception | Per-function unit tests and exhaustive `float` checks as oracle design |

## Ordered units

Each unit is one reviewable change with its own contract note, calibrated
gate, strict C17 header contract, import list and resource statement. A unit
that would grow the native archive, a shipped program or the root image
stops for the owner's approval of the measured vector before it merges.

1. **Integer division and `<iso646.h>`.** Delivered; contract below.
2. **Time and float constants.** `timespec_get`, `TIME_UTC`, `clock`,
   `CLOCKS_PER_SEC` and the missing `<float.h>` macros. `clock` needs a
   decision on its tick source: `times` reports `CLK_TCK=60`.
3. **`strtof` and `strtold`.** `long double` is `double` on this ABI, so
   `strtold` forwards; `strtof` needs single rounding. Depends on the
   measured accuracy of the existing `strtod`.
4. **`<locale.h>`, `strcoll`, `strxfrm`.** One "C" locale; `setlocale`
   accepts `"C"`, `"POSIX"` and `""` and rejects every other name.
5. **`<math.h>` classification and bit-level functions.** Macros,
   `float_t`, `double_t`, `NAN`, `copysign`, `trunc`, `round`, `rint`,
   `fmin`, `fmax`, `fdim`, `scalbn`, `ilogb` and their `f` variants.
6. **Transcendental completion, `<fenv.h>`, `<tgmath.h>`.** The remaining
   C99 functions; a soft-float environment with a stated flag policy.
7. **Wide and Unicode character headers.** `<wchar.h>`, `<wctype.h>` and
   `<uchar.h>` over the one-byte encoding, including wide stdio or a stated
   disposition for it.
8. **Conditional features.** Either define the three `__STDC_NO_*__` macros
   through a tree-owned mechanism under `-nostdinc`, or provide the headers.
9. **Legacy members.** Convert the remaining old-style `lib/libc/gen`
   definitions and retire `WARNLEVEL=legacy` per directory.

Units 2 through 4 are independent of each other; 5 precedes 6.

## Unit 1 contract: `div`, `ldiv`, `lldiv`, `llabs`, `<iso646.h>`

C17 7.22.6.1 and 7.22.6.2, and POSIX Issue 8
[div](https://pubs.opengroup.org/onlinepubs/9799919799/functions/div.html),
[ldiv](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ldiv.html)
and [llabs](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llabs.html),
define the quotient truncated toward zero and leave an unrepresentable
result undefined. C17 7.9 defines the eleven `<iso646.h>` macros.

| Facet | Evidence | Falsifier |
| --- | --- | --- |
| Declarations | `stdlib_div_contract.c` pins each member type through `_Generic`, each structure at two members without padding, and each prototype through an exact function-pointer initializer; `iso646_contract.c` pins each macro on operands where neighboring operators disagree | A header without `lldiv`, an `ldiv_t` with `int` members, an `xor` bound to `|` or a missing `compl` translates; all four mutants are rejected for the named reason |
| Arithmetic | `integer_division_test.c` runs 402,471 checks through volatile function pointers with built-ins disabled: the three defining conditions over every operand pair in [-181, 181] for all three widths, literal results at each width's limits and across the 32-bit word boundary, and `llabs` over small, word-crossing and limit values | A provider returning the numerator as remainder, a floored `div`, an `lldiv` returning the quotient twice or a sign-preserving `llabs` passes; all five mutants exit with their function's status and reason |
| Oracle independence | The grid evaluates `q*d + r`, a bound and a sign; it never applies `/` or `%`, so provider and oracle share no divide | The floored mutant passes, which would show the sign condition unexercised |
| Imports | `check-imports` requires `div.o` and `ldiv.o` to import `__aeabi_idivmod` alone, `lldiv.o` to import `imaxdiv` alone and `llabs.o` to import nothing | An object imports `__aeabi_ldivmod` or another compiler runtime routine libc does not carry |
| Undefined domain | A zero denominator and `MIN / -1` appear in no table; the sanitizer variant would abort on either | A fixture input reaches undefined arithmetic |

`div` and `ldiv` compile to one `__aeabi_idivmod` call, which the RP2040
provider in `lib/libc/arm/gen/aeabi_div.S` serves from one run of the SIO
divider. `lldiv` forwards to `imaxdiv`, the tree's only 64-bit divider.

Observed at `-std=c17 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft` with
arm-none-eabi-gcc 13.2.1: `div.o` and `ldiv.o` 20 text bytes each, `lldiv.o`
42, `llabs.o` 24, no data or BSS. Host execution used gcc 13.3.0 at LP64,
`-m32` and under address and undefined-behavior sanitizers; Thumb-1
execution used qemu-arm 8.2.2 with `-cpu cortex-m0` over newlib support. No
board ran this code.

### Resource boundary and residuals

The four members join the cross ELF archive only. `lib/libc_aout/libc/Makefile`
and `distrib/rp2040/boardlibc-members` list their members explicitly and are
unchanged, so no shipped program, native archive or root image contains the
new code and no growth exception is requested. Consequently:

- A program compiled on the board cannot link `div`, `ldiv`, `lldiv` or
  `llabs`. Admitting them to the native archives needs the a.out omission
  controls, a Smaller C fixture for the two members that compiler can see,
  and an approved growth vector; `libc-absolute-value-resource-exception.md`
  is the model.
- `<stdlib.h>` shows Smaller C the `div_t` and `ldiv_t` declarations and
  hides `lldiv_t`, `lldiv` and `llabs` behind `__SMALLER_C__` with the other
  `long long` interfaces. Smaller C's acceptance of the two structure-return
  prototypes is unmeasured in this unit.
- The new rows in `tools/c17/source-denominator.json` stand at `unreviewed`;
  this note is the evidence a reviewer would cite to accept them.

Replay, with caller-selected `PYTHON`:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-libc-integer-division check-c17-headers-cross
"$PYTHON" tests/libc_contracts/integer_division.py . arm-none-eabi-gcc --arm
```
