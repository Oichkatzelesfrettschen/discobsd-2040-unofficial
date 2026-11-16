# Hosted C17 library closure

This note records the C17 clause 7 interfaces this tree adds for a hosted
implementation, the invariant each provider holds, the gate that tests it, and
what remains unprovided. [`c17-semantic-migration-plan.md`](c17-semantic-migration-plan.md)
owns language migration of existing source and
[`sus-edition-contracts.md`](sus-edition-contracts.md) owns the selected POSIX
rows. POSIX Issue 8 (SUSv5) aligns with C17, so these interfaces are also part
of the Issue 8 base; Issue 7 (SUSv4) aligns with C99, whose library differs
from C17's only in the C11 additions (`aligned_alloc`, `quick_exit`,
`timespec_get`, `<uchar.h>`, `<stdalign.h>`, `<stdnoreturn.h>`, and the
optional `<threads.h>` and `<stdatomic.h>`).

The measurement base is `b3f293a6fb0e0cce4ff3a02c24e3861a176a0660`. Every new
member is in the cross ELF archives only: `lib/libc_aout` and
`distrib/rp2040/boardlibc-members` list members explicitly and are unchanged,
so no shipped program, native archive or root image contains the new code, and
no board executed it.

## Provided

| Header | Interfaces | Gate |
| --- | --- | --- |
| `<iso646.h>` | the eleven operator spellings | `check-c17-headers-cross` |
| `<stdlib.h>` | `div`, `ldiv`, `lldiv`, `llabs`, `strtof`, `strtold` | `check-libc-integer-division`, `check-libc-c17-strtof` |
| `<locale.h>`, `<string.h>` | `setlocale`, `localeconv`, `struct lconv`, `LC_*`; `strcoll`, `strxfrm` | `check-libc-c17-locale` |
| `<time.h>` | `clock`, `clock_t`, `CLOCKS_PER_SEC`, `timespec_get`, `TIME_UTC` | `check-libc-c17-time` |
| `<float.h>` | `FLT_EVAL_METHOD`, `DECIMAL_DIG`, `*_DECIMAL_DIG`, `*_HAS_SUBNORM`, `*_TRUE_MIN` | `check-c17-headers-cross` |
| `<math.h>` | `float_t`, `double_t`, `NAN`, `FP_*`, `math_errhandling`, the classification and comparison macros, and every C99/C17 function in double, float and long double form | `check-libc-c17-math` |
| `<fenv.h>`, `<tgmath.h>` | the floating-point environment of a flagless implementation; type-generic selection | `check-libc-c17-math`, `check-c17-headers-cross` |
| `<wchar.h>`, `<wctype.h>`, `<uchar.h>` | wide string, conversion, numeric, classification and `char16_t`/`char32_t` functions | `check-libc-c17-wide` |

`check-c17-headers-cross` also requires each function `c17_library_contract.c`
pins to have a definition in the built `libc.a` or `libm.a`, so a declaration
without a provider fails the build tier rather than a user's link.

## Invariants and mechanisms

**Truncating division.** For `d != 0` exactly one pair `(q, r)` satisfies
`n == q*d + r`, `|r| < |d|` and `r == 0 || sign(r) == sign(n)`. C99 fixed `/`
and `%` to that pair, so `div` is the operator pair, which the compiler merges
into one `__aeabi_idivmod` call served by the RP2040 SIO divider. `lldiv`
forwards to `imaxdiv`, the one 64-bit divider. The gate checks the three
conditions, not a second division, so floored division fails it.

**One locale.** The system has the "C" locale only, which POSIX also names
"POSIX". `setlocale` accepts those two and `""` and refuses everything else, so
a caller that asks for another locale learns that it did not get it.
Collation is byte order, `strxfrm` and `wcsxfrm` are the identity bounded by
the destination size, and the `lconv` values are those 7.11.2.1p3 fixes.

**One-byte encoding.** Each byte is one character whose wide value is the byte
value (`mbtowc.c`). Every multibyte and Unicode conversion therefore handles
exactly one unit, never returns "incomplete" for nonempty input, keeps no
state (`mbstate_t` is never read), and reports `EILSEQ` only for a wide value
above `UCHAR_MAX`. Wide classification is `<ctype.h>` on the basic character
set; other wide characters are in no class.

**Bit-level math.** `trunc`, `round`, `rint`, `copysign`, `nextafter`,
`ilogb`, `scalbn` and the classification macros work on the IEEE 754 fields
(`lib/libm/fpbits.h`). With unbiased exponent `e`, the fraction holds
`52 - e` bits below the binary point: `trunc` clears them, `round` adds half a
unit first (a carry out of the fraction is the next power of two's encoding),
and `rint` compares the discarded bits with one half and breaks a tie on the
parity bit. Encodings of one sign are ordered as integers, so `nextafter` is
the encoding plus or minus one. `scalbn` shifts a subnormal result and rounds
ties to even; because the subnormal encoding continues the normal one, a
round-up out of the fraction lands on the smallest normal without a special
case. None of these performs floating arithmetic, so none rounds.

**fma.** `fma` forms the 106-bit product of the significands exactly in 128
bits from 16-bit limbs, since ARMv6-M has no 32x32->64 multiply and libc
links no 64-bit multiply routine. The addend is aligned with a sticky bit for
everything shifted out; that is sound because the unshifted operand has zero
low bits, and a shift that loses bits (over 21 places) cannot coincide with a
cancellation of more than one bit, so the sticky bit stays 50 places below the
rounding position. The sum is rounded once. `fmaf` forms the product exactly in
double, adds in double, and rounds that sum to odd using the 2Sum error term;
a value rounded to odd at 53 bits narrows to the same float as the exact sum.

**remquo.** `fmod` is exact, so reducing `|x|` modulo `8|y|` keeps the three
low quotient bits; three exact conditional subtractions bring the remainder
below `|y|` while counting them. The final comparison is `r` against
`|y| - r`, which cannot overflow and is exact where it matters (Sterbenz).

**Accuracy-preserving transcendentals.** `expm1` and `log1p` use Kahan's
correction: with `u` the rounded `exp(x)` or `1 + x`, the ratio with
`log(u)` or `u - 1` cancels the rounding of `u`. `asinh`, `acosh` and `atanh`
are rewritten through `log1p` so that small arguments and the branch point
keep their accuracy. `exp2` splits off `rint(x)` exactly and scales the
exponent field, so integral arguments are exact; `log2` takes the significand
in `[sqrt(1/2), sqrt(2))`, so powers of two give the exact exponent. `cbrt`
reduces to `[0.5, 4)` times an exact power of two and runs six Newton steps.
`tgamma` evaluates the series of `1/gamma(1 + z)` for `z` in `[-1/2, 1/2]`
and carries it to `x` with the recurrence, so no logarithm or power amplifies
its error; `lgamma` is `log|tgamma|` below 30, Stirling's series above, and
the reflection formula far below zero.

**Floating-point environment.** Arithmetic is software, rounds to nearest and
records no flags. `<fenv.h>` therefore defines no exception macro,
`FE_ALL_EXCEPT` is 0, `FE_TONEAREST` is the only direction, and the functions
succeed exactly for the requests that state can represent. `math_errhandling`
is `MATH_ERRNO`; the new members set `errno` for domain and pole errors and
overflow.

**Free aliases.** `long double` has double's format on this ABI, so every
`long double` function, `strtold`, `wcstold` and `nexttoward` are the double
functions under a second name (an `__asm__` label), costing no code. A
translation unit whose `long double` is wider sees none of them.

**Type-generic selection.** `<tgmath.h>` selects on the sum of the arguments
after giving each integer argument type double (7.25p3). Its contract caught
the first version selecting `powf` for `pow(float, int)`.

## Measured accuracy

Against glibc 2.39 on x86-64 at the fixture's 30,000 draws per member, the
largest errors in units of the last place were: `cbrt` 3, `exp2` 3,
`expm1` 27, `log1p` 23, `log2` 17, `asinh`, `acosh` and `atanh` 21 to 22,
`tgamma` 19, `lgamma` 6.8 (scaled absolute error); the float forms 0. The
`log`-based members inherit most of their error from the inherited `log()`,
whose own error reaches about 20 units. The gate bounds are 32 (4 for
`cbrt`). These are measurements of those draws, not proven bounds.

The qemu-user variant links newlib, whose `fma` rounds twice, whose `remquo`
disagrees with its own `remainder` for subnormal divisors, whose `scalbnf`
flushes the smallest subnormal to zero and whose `tgamma` misses glibc by 55
units at -24.9. Those members are held there to per-member result digests
pinned from the glibc run (`tests/libc_contracts/c17_math_digests.h`); the
digests agree bit for bit across LP64, ILP32 and Thumb-1.

## Not provided

- **Wide streams and `wcsftime`.** `fwprintf`, `fgetwc` and the rest of 7.29.2
  and 7.29.3 need stream orientation in `FILE` and wide conversions in the
  formatter. `<wchar.h>` declares none of them, so a caller fails to compile
  rather than to link.
- **`<complex.h>`, `<threads.h>`, `<stdatomic.h>`.** C17 makes all three
  conditional. The compiler predefines no `__STDC_NO_COMPLEX__`,
  `__STDC_NO_THREADS__` or `__STDC_NO_ATOMICS__` for this target, and the tree
  has no mechanism under `-nostdinc` to define them, so the absence is not yet
  announced the way the standard requires.
- **Correctly rounded `strtof`.** It narrows `strtod`'s double, which rounds
  twice and can differ in the last place near a float rounding boundary.
- **Locales beyond "C".** No other locale exists, and `setlocale` says so.

## Replay

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-c17-headers-cross check-libc-integer-division
for f in locale strtof time math wide; do bmake MACHINE=rp2040 check-libc-c17-$f; done
for f in locale strtof math wide; do
	"$PYTHON" tests/libc_contracts/c17_family.py $f . arm-none-eabi-gcc --arm
done
```
