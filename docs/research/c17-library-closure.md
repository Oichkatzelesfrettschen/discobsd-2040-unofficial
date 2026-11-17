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

## Correctly rounded conversion

`strtof`, `wcstof` and `wcstod` share `lib/libc/stdlib/strtofp.c`. It parses
the whole C17 subject sequence (decimal, hexadecimal, `inf`/`infinity`,
`nan` with an optional character sequence) through a character accessor, so
the wide functions read in place with exact end positions. Hexadecimal input
is exact binary and is rounded once. For decimal input `D * 10**k`, a
candidate from double arithmetic is corrected by exact comparison with the
neighboring midpoints, `D * 5**k * 2**(k - e)` against `A`, in 16-bit limbs
because ARMv6-M has no 32 x 32 -> 64-bit multiply. Only 800 significant
digits enter `D`; a nonzero digit beyond them appends a final 1, which keeps
every comparison's sign because no binary64 midpoint has more than 767
significant digits. The member is 2,930 bytes of text with no data; its
deepest frame is about 1.45 KB.

The first `strtof`, which narrowed the inherited `strtod`, rounded
`"2.426210865987558043e+37"` one unit wrong; random strings had not come
near enough to a midpoint to show it, so the gate now also converts decimal
expansions of float midpoints cut to 9 to 25 digits. newlib's own `strtof`
gets that input wrong in the same way.

## Checked against NetBSD

`check-libc-c17-netbsd` (`NETBSD_SRC` naming a src-netbsd checkout) compiles
NetBSD's own libm, wide-string, gdtoa `strtof` and division members against
NetBSD's headers, for x86-64, i386 and Cortex-M0+, and links them ahead of the
hosting libraries as the oracle (`tests/libc_contracts/netbsd_oracle.py`). On
the qemu-user variant that replaces newlib, whose `fma`, `fmaf`, `strtof` and
`tgamma` are not accurate enough to be one. NetBSD's division members run the
tree's own fixture in place of the tree's and pass it, which, the result being
unique, means they agree on every input it draws. NetBSD's locale,
collation, multibyte conversion and classification functions read its
citrus locale database and rune tables, which a host or newlib program
cannot load, so those families have no NetBSD oracle.

At revision `894c17be5467`, and again at `1e8e727e461b` (the witnesses of
the withheld members reproduce there), NetBSD's members disagree with C17 in
five places. The tree's members are right in each, glibc agrees with the tree,
and the cases below were checked against exact arithmetic where stated:

| NetBSD member | Defect | Extent |
| --- | --- | --- |
| `s_rintf.c` | fdlibm's guard bit `0x100000 >> 21` is 0 at exponent 21, so `.75` rounds as a tie | exactly 2,097,152 of all 2**32 floats; `rintf(3659852.75f)` gives 3659852 |
| `s_rint.c` | the same at exponent 18 (`0x20000 >> 18`), for doubles whose low word is zero | exactly 262,144 doubles, `n + 0.75` with `n` even; `rint(-490468.75)` gives -490468 |
| `s_logb.c`, `s_logbf.c` | every subnormal returns -1022 or -126, the IEEE 754-1985 function, where C17 7.12.6.11p2 normalizes it | all 2**53 - 2 subnormal doubles and 2**24 - 2 floats; `logb(0x1p-1074)` gives -1022 |
| `s_fma.c` | a result just below a power of two can round up to the power | 408,909 of 2,000,000 such draws and none of 2,000,000 general ones; `fma(-2.5, 0.1, 0x1p52)` gives 0x1p52, 2**-56 from the midpoint on the other side |

The oracle withholds those members, with a witness each that fails the build
once NetBSD's defect stops reproducing, and the gate substitutes each one for
the tree's own member and requires the fixture to reject it by name. That
calibrates the fixture against real defects of the class it exists to catch.
The `rint` one also showed the fixture's blind spot: uniformly drawn
encodings almost never have a zero low word, so it now draws short dyadic
values as well. And the `logb` substitution found a defect in the tree:
`lib/libm/fabs.c`, inherited and newly built into `libm.a` by this work,
returned `-0.0` for `-0.0`; it now clears the sign bit and is a tested member.

## Provenance

No NetBSD, FreeBSD or fdlibm file is imported: every added `lib/libm`,
`lib/libc/wchar`, `lib/libc/gen` and `lib/libc/string` member is written for this
port and carries the GizmoBSD header, which is why `tools/netbsd-imports.manifest`
and `NOTICE` list none of them. The NetBSD sources named above are read from
`NETBSD_SRC` at gate time and stay outside the tree. The one published table is
the `tgamma` series, which takes its coefficients from Abramowitz and Stegun
6.1.34 (named in `lib/libm/tgamma.c`). A member later derived from an upstream
file enters the manifest with its upstream header intact.

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
- **A correctly rounded `strtod`.** The inherited `strtod` accepts no
  infinity, NaN or hexadecimal form, which C17 7.22.1.3 requires, and over
  2,000,000 random decimal strings of at most 17 digits it differs from the
  correctly rounded result for 344,874 (17%), where 7.22.1.3p9 recommends
  correct rounding up to `DECIMAL_DIG` digits. `strtofp.c` converts both
  formats correctly; moving `strtod` onto it replaces 692 bytes of text with
  2,966 and adds up to 1.45 KB of stack in its two shipped consumers, `trek`
  and `primes`, and in the native archives, which awaits the owner's resource
  decision.
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
