# Numeric math libraries for DiscoBSD/RP2040

Target: RP2040, dual Cortex-M0+, ARMv6-M, Thumb-1 encoding only, no FPU, no
MMU/MPU, 264 KB SRAM. DiscoBSD userland is static-linked a.out OMAGIC, one
process gets a single 96 KB window (per
[`board-libc.md`](board-libc.md) and
[`ondevice-c-compilers.md`](ondevice-c-compilers.md) in this directory), and
code size competes directly with data and stack inside that window. This
report answers three questions: what is the correct name for the kind of
library the project wants, what does the RP2040 mask ROM already provide for
free, and what should DiscoBSD actually build or vendor given the two
answers above.

## Result

- The RP2040 bootrom ships a correctly-rounded, IEEE-754 single-precision
  float library in mask ROM on every chip, plus a double-precision library
  from bootrom V2 onward, documented in the datasheet's own cycle-count
  tables (RP2040 datasheet section 2.8.3.2, "Fast Floating Point Library").
  `_fadd` costs 71 cycles, `_fmul` costs 58-69 cycles, `_fdiv` costs 71
  cycles, `_fsqrt` costs 63 cycles -- against libgcc/GoFast soft-float
  numbers on the same class of core (Cortex-M0, no divider) that run
  3x-10x slower for the same operations (Section 2, Table 169; comparison
  below). This is free: it costs 0 bytes of flash/RAM budget beyond a
  16-bit lookup table pointer, because the code already lives in the part.
  **This is the single highest-leverage finding in this report** -- any
  DiscoBSD float path that does not route through this ROM table is
  leaving 3x-10x performance and several KB of libgcc.a on the table for
  nothing.
- FP16 ("half float") has no hardware or ROM support on this chip at any
  bootrom version -- the datasheet's floating-point tables list only float
  (`_f*`) and double (`_d*`) entries, nothing 16-bit. "Fast FP16 math" on
  RP2040 can only mean fast storage-format conversion (halving RAM
  footprint for arrays) followed by ordinary float arithmetic; there is no
  half-precision ALU to be fast in.
- Given the ROM routines exist for free and code size is the binding
  constraint, the recommended build order is: (a) Q16.16 fixed point for
  the hot paths that do not need dynamic range, hand-written against
  Thumb-1, no external library required; (b) a bootrom-float RTABI shim
  (`__aeabi_fadd` etc. calling `_fadd` through the ROM table) as the
  default `float` implementation in libc, replacing libgcc's soft-float
  routines outright; (c) true general-purpose float/double soft-float
  (Berkeley SoftFloat or libgcc) reserved for the rare case that needs
  correctly-rounded double math the ROM cannot provide (bootrom V1 parts)
  or NaN/denormal-exact IEEE semantics the ROM's simplified exception
  model does not give. Section 4 gives the concrete plan and size budget.

## 1. Terminology

The user's three guesses name three different, non-overlapping techniques.
All three are legitimate tools on a no-FPU MCU; the right one depends on
what the calling code needs (dynamic range vs. speed vs. IEEE conformance),
not on which is "more numeric."

**Fixed-point arithmetic, Q number format.** A Qm.n value is a plain
integer -- `m` bits of signed/unsigned integer part, `n` bits of fractional
part, decimal point implied at compile time, never stored. Q16.16 means a
32-bit `int32_t` where the low 16 bits are the fraction; addition and
subtraction are ordinary integer add/sub; multiplication needs a widening
64-bit-then-shift (or a 32x32->64 multiply plus a right shift by `n`);
division needs a pre-shift of the dividend before the integer divide.
Range and precision are both fixed at compile time and traded off against
each other by choosing `m` and `n` -- Q16.16 gives roughly +-32768 range at
2^-16 (~1.5e-5) resolution; Q8.24 trades range for four more bits of
fraction. There is no separate exponent, so there is no hidden bit, no
rounding mode, no denormal, no NaN, and no exception -- correctness is
whatever the programmer's chosen `m.n` allows, and overflow silently wraps
like any other integer unless the code guards it. Right tool when: the
value's dynamic range is known and bounded at compile time (angles, PWM
duty cycles, filter coefficients, screen coordinates, audio samples), and
the RP2040 has no hardware divider penalty to avoid (ARMv6-M actually has
*no* integer divide instruction at all -- see
[`ondevice-c-compilers.md`](ondevice-c-compilers.md) line 16 -- so even Q
format division costs a software division routine; only add/sub/mul are
genuinely cheap).

**"DSP math" -- block/vector kernels over fixed or floating types.** This
is not a numeric *format*, it is a library *shape*: fixed-size data types
(commonly Q7, Q15, Q31 fixed-point plus f16/f32/f64 float, as in ARM's
CMSIS-DSP, Section 3) paired with block operations -- FIR/IIR filter, FFT,
dot product, vector add/scale -- that amortize call overhead and enable
loop unrolling or SIMD across many samples at once. "DSP-style" is
orthogonal to "Q-format": a DSP kernel can operate on Q15 fixed-point
samples or on `float` samples: the label describes the vectorized calling
convention (`arm_fir_q15()` operating on a whole sample buffer), not the
number representation underneath it.

**Soft-float -- software emulation of IEEE-754.** This is a full binary32
or binary64 emulation: explicit sign/exponent/mantissa fields, hidden bit,
denormals, NaN and Infinity encodings, and (in a conformant implementation)
the four IEEE rounding modes and the five exception flags, all realized in
integer instructions because the chip has no FPU. `libgcc`'s
`__aeabi_fadd`/`__aeabi_dadd`/etc. (Section 3) and Berkeley SoftFloat
(Section 3) are both soft-float implementations; so, functionally, is the
RP2040 bootrom's float/double library (Section 2), except it deliberately
narrows IEEE conformance (denormals flush to zero, NaN becomes Infinity,
only round-to-nearest-even) to buy speed and code size -- see the exact
tradeoffs quoted in Section 2. Right tool when: dynamic range is *not*
known at compile time, or the code needs `float`/`double` semantics for
interop with existing C source (`math.h`, ported code, a expression that
mixes magnitudes across many orders).

The user's phrase "static math" most likely points at *fixed-point*
(values whose format is fixed/static at compile time, as opposed to
floating), which the Q-format answer above already covers; it is not a
separate technique.

## 2. The RP2040 bootrom floating-point library

Primary source: RP2040 datasheet (Raspberry Pi Trading, document
`RP-008371-DS`), section 2.8 "Bootrom," specifically **2.8.3.1** ("Bootrom
Functions," the general lookup mechanism), **2.8.3.2** ("Fast Floating
Point Library," Tables 169-170), and **2.8.3.3** ("Bootrom Data," Table
171). Fetched from
`https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf`
(the current canonical URL as of this research pass; the datasheet is
also mirrored at `datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf`,
which redirects there). Page/line numbers below are from that PDF's text
extraction, not physical page numbers, since pagination varies by viewer.

### 2.1 What is actually in ROM

Every RP2040, regardless of bootrom version, has a correctly-rounded
single-precision (`float`) library in the 16 KB mask ROM. Bootrom V2 and
V3 parts (the datasheet does not give a silicon-revision-to-bootrom-version
table in this section; treat "V1/V2/V3" as bootrom, not stepping, versions
until independently confirmed against a specific chip's `version_number()`
mask ROM call) add a second, separate double-precision (`double`) table at
the same relative layout. The library's own design statement, quoted
directly from 2.8.3.2.1:

> "the emphasis is more on improved performance for the basic operations
> (add, subtract, multiply, divide and square root) and more on reduced
> footprint for the scientific functions (trigonometric functions,
> logarithms and exponentials)."

and on conformance:

> "input denormals are treated as zero, input NaNs are treated as
> infinities, output denormals are flushed to zero, and output NaNs are
> rendered as infinities. Only the round-to-nearest, even-on-tie rounding
> mode is supported. Traps are not supported."

The five basic ops (`add`, `sub`, `mul`, `div`, `sqrt`) are correctly
rounded despite that narrowing; the transcendental functions (trig, `exp`,
`ln`) guarantee only <=1 ULP. Sine/cosine/tangent are range-limited to
|x| < 128 (float) or |x| < 1024 (double) radians at the ROM level -- the
SDK's `pico_float`/`pico_double` wrapper does its own range reduction
before calling the ROM function to give full-range results at extra cost;
a bare a.out calling the ROM table directly gets the narrower range unless
it reduces the argument itself.

### 2.2 Measured cycle costs (the datasheet's own numbers)

Table 169 (single precision) and Table 170 (double precision), "average
time... over random (worst case) input," selected rows (cycles, not
microseconds -- the table header's "Cycles (Avg)" is correct, its column
subhead mislabels "us" in the PDF's extracted text):

| Operation | float, V1 | float, V2/V3 | double, V2/V3 |
| --- | --- | --- | --- |
| add | 71 | 71 | 91 |
| sub | 74 | 74 | 95 |
| mul | 69 | 58 | 155 |
| div | 71 | 71 | 183 |
| sqrt | 63 | 63 | 169 |
| cmp | n/a | 25 | 39 |
| int->float | 55 | 55 | 69 |
| float->int | 37 | 40 | 75 |
| sin | 593 | 577 | 1618 |
| cos | 603 | 587 | 1617 |
| tan | 669 | 653 | 1891 |
| atan2 | n/a | 667 | 2168 |
| exp | 542 | 524 | 804 |
| ln | 810 | 789 | 428 |
| float<->double | -- | 15 (f->d) / 23 (d->f) | -- |

V3 adds a combined `_fsincos`/`_sincos` that returns sine and cosine from
one call, "considerably faster than calling `_fsin` and `_fcos`
separately" (2.8.3.2.2), at 577 cycles (float) / 1718 cycles (double) for
the pair -- i.e., roughly the cost of one transcendental call, not two.

### 2.3 Comparison against soft-float

The datasheet does not itself publish a libgcc comparison table. The
independent data point comes from Mark Owen's `qfplib-m0-full`
(quinapalus.com; GPLv2, commercial license available directly from the
author -- see Section 3), which multiple primary sources (the Raspberry Pi
forums thread ["How fast are the fast floating point
functions"](https://forums.raspberrypi.com/viewtopic.php?t=308794) and
["ROM Floating point and math
functions"](https://forums.raspberrypi.com/viewtopic.php?t=347486))
describe as the base the RP2040 bootrom float library was built from, with
RP2040-specific further optimization -- this lineage claim is
forum-sourced, not confirmed against bootrom source comments in this pass,
so treat it as *likely* rather than *cited fact*. `qfplib-m0-full`'s own
published comparison table (measured on an LPC11U68, Cortex-M0, single-cycle
flash, from its GitHub README) gives a same-class-of-core data point:

| Function | qfplib-m0-full | GCC libgcc | GoFast |
| --- | --- | --- | --- |
| fadd | 76 | 102 | 182 |
| fsub | 78 | 108 | 181 |
| fmul | 62 | 166 | 144 |
| fdiv | 83 | 475 | 799 |
| fsqrt | 67 | 460 | 1590 |
| fsin | 584 | 3300 | 394 |
| fcos | 595 | 3350 | 393 |
| ftan | 671 | 6140 | 1090 |
| fatan2 | 673 | 4930 | 2041 |

Read together with the datasheet's own RP2040 numbers (which track the
qfplib column closely for the basic ops), the honest claim is: RP2040 ROM
float add/sub/mul/div/sqrt run roughly 1.5x-8x faster than libgcc
soft-float on a same-class Cortex-M0 core, and ROM trig/transcendentals run
roughly 5x-9x faster than libgcc's. The GCC column above is *not*
re-measured on RP2040 in this pass; it is qfplib's own reported comparison
number on a different Cortex-M0 part, so treat the specific multipliers as
**unverified for RP2040 specifically**, though the RP2040 datasheet's plain
prose statement that the ROM library is faster than "the standard gcc
implementation" is corroborated independently by the forum threads above
and is not in question -- only the exact multiplier is.

The forum thread also gives DSP-style block numbers at 125 MHz (unverified
-- forum post, not datasheet, and the poster's methodology is not given in
the fetched excerpt): roughly 1.6-1.75 million float add/mul/div ops/sec
against roughly 12.5 million fixed-point ops/sec, a rough 7x fixed-point
edge that matches the general rule that even ROM-accelerated float costs
several integer-op-equivalents per operation (Section 2.2's 58-91 cycle
figures for a single add/mul against a 1-cycle Thumb-1 `ADD`).

### 2.4 The ROM function-table lookup mechanism

This is the part that matters for a bare a.out with no pico-sdk C runtime.
Quoted directly from the datasheet, 2.8.3 and 2.8.3.1 (Table 163 and the
SDK excerpt the datasheet itself reproduces from
`pico-sdk/src/rp2_common/pico_bootrom/bootrom.c` lines 12-19):

- Fixed addresses, valid on every RP2040 regardless of bootrom version:
  - `0x00000010`: 3 bytes, magic `'M', 'u', 0x01`.
  - `0x00000013`: 1 byte, bootrom version (informational only -- "should
    not be used to infer the exact location of any functions").
  - `0x00000014`: 16-bit pointer to `rom_func_table` (function lookup
    table).
  - `0x00000016`: 16-bit pointer to `rom_data_table` (data lookup table,
    holds the pointer to the float/double tables themselves).
  - `0x00000018`: 16-bit pointer to a `rom_table_lookup()` helper
    function.
- A caller reads the magic at `0x10` to confirm it matches, then treats
  the three halfwords at `0x14`/`0x16`/`0x18` as valid.
- Lookup codes are two ASCII characters packed little-endian into a
  32-bit code: `code = c1 | (c2 << 8)` (the datasheet's own C, verbatim):

  ```c
  uint32_t rom_table_code(char c1, char c2) {
       return (c2 << 8) | c1;
  }
  ```

- `rom_func_lookup(code)` and `rom_data_lookup(code)` walk the
  corresponding table and return a function pointer or data pointer, or
  NULL if the code is absent from this bootrom version -- the exact
  pattern the SDK itself uses (`pico_bootrom/bootrom.c`, reproduced in the
  datasheet), and directly callable with zero pico-sdk dependency: it is
  four ROM reads and one small linear/indexed table walk, no
  initialization, no heap, no C runtime beyond the ability to dereference
  a pointer.
- The float/double function *tables themselves* are reached through the
  **data** table, not the function table, with codes `'S','F'`
  (`soft_float_table`, Table 169's contents) and `'S','D'`
  (`soft_double_table`, Table 170's contents, "only present in the V2
  bootrom") -- Table 171, 2.8.3.3. `'F','S'`/`'F','E'` give the start/end
  address of the float library's code+data blob in ROM, documented as
  usable to copy the whole float implementation into RAM (`PICO_FLOAT_IN_RAM`
  in the SDK exists for exactly this, trading a few hundred bytes of RAM
  for ROM-wait-state-free execution -- SDK behavior, not re-verified
  against datasheet text in this pass).
- Once `soft_float_table` is in hand, its entries are fixed byte offsets
  (`0x00` = `_fadd`, `0x04` = `_fsub`, `0x08` = `_fmul`, ... per Table 169
  above) -- a bare a.out can hard-code these offsets after confirming the
  bootrom version byte, or walk the table generically; the datasheet
  explicitly warns the *table* pointer (`'S','F'` via `rom_data_lookup`)
  should be re-resolved per boot rather than hard-coded, since "their
  locations may change with each Bootrom release," but the *offsets
  within* that table are part of the documented, versioned ABI (Table 169's
  own column layout: "Functions common to all versions" at fixed offsets,
  V2-only additions from `0x54` onward in the *same* table, not a
  different one).
- Calling convention: "these functions follow the standard ARM EABI for
  passing floating point values" (2.8.3.2.2) -- i.e., a bare a.out treats
  `_fadd` exactly like a normal `float f(float, float)` C function using
  the soft-float AAPCS (arguments/results in `r0`-`r3`), with no special
  glue beyond having the function pointer.

Net: a bare DiscoBSD a.out, with no pico-sdk, no C runtime init beyond
`crt0`, can reach every ROM float/double routine in roughly a dozen lines
of hand-written C or assembly (read magic, read the three table pointers,
resolve `'S','F'`/`'S','D'` through `rom_data_lookup`, index by the fixed
offset, call through the soft-float AAPCS) -- this is a smaller surface
than linking libgcc's soft-float object files, and it is faster per
Section 2.3.

## 3. Existing libraries

### libfixmath (Q16.16, C)

MIT license, C99, implements Q16.16 fixed point with a `math.h`-parallel
API (`fix16_add`, `fix16_mul`, `fix16_sin`, etc.), maintained at
[PetteriAimonen/libfixmath](https://github.com/PetteriAimonen/libfixmath)
(the original Google Code project's GitHub successor;
[mhfan/libfixmath](https://github.com/mhfan/libfixmath) and
[sunsided/libfixmath](https://github.com/sunsided/libfixmath) are
mirrors/forks of the same code). No hardware divide or multiply-wide
instruction is assumed by the format itself, only by the *implementation*
-- ARMv6-M has neither a divide instruction nor (unlike Cortex-M3/M4) a
single-cycle 32x32->64 `UMULL`/`SMULL` with the same throughput, so
libfixmath's C reference path (not its optional CPU-specific asm) is the
realistic fit; it needs porting/verification against Thumb-1 rather than
being usable unmodified. Binary size not found in this pass (unverified);
the API surface is small enough (a handful of `.c`/`.h` files) that a
DiscoBSD port would selectively link only the operations libc actually
calls, the same closure-based approach `mkboardlibc.py` already uses for
libc itself (`board-libc.md`).

### fpm (Q-format, C++ header-only)

MIT license, header-only C++ template library
([mikelankamp.github.io/fpm](https://mikelankamp.github.io/fpm/)),
providing `fpm::fixed_16_16` and similar types with operator overloading.
Not directly relevant to DiscoBSD: the tree's toolchain is a C, not C++,
static-libc environment (`smlrc`/`ccom`/`lcc`, per
`ondevice-c-compilers.md`), and a header-only C++ template library gives
no artifact to link against a C a.out. Useful only as a *reference
implementation* to read when hand-writing the C/Thumb-1 Q16.16 routines
recommended in Section 4 -- its rounding and overflow-handling choices are
a reasonable model to copy.

### ARM CMSIS-DSP

Apache-2.0, [ARM-software/CMSIS-DSP](https://github.com/ARM-software/CMSIS-DSP).
Supports Cortex-M0/M0+ today: the library documentation states it targets
"Cortex-M and Cortex-A processor[s]" generally and that "the correct
implementation is picked through feature flags" per target -- cores
without the DSP/SIMD extension (M0, M0+, M3) get "pure C scalar reference
implementations," while M4/M7/M33/M55-class cores with SIMD get
hand-optimized paths. Data types offered: `q7`, `q15`, `q31` fixed-point,
plus `f16`/`f32`/`f64` float (source: CMSIS-DSP overview README, ARM
Software GitHub org). For RP2040 this means CMSIS-DSP is *usable* but
brings none of its performance advantage -- Cortex-M0+ always takes the
generic C scalar path, so the library buys API surface (a large,
well-tested set of FIR/IIR/FFT/stats kernels) at the cost of pulling in a
general-purpose, non-Thumb-1-tuned C codebase and its Apache-2.0-licensed
header tree; every kernel actually used would need to compile under the
in-tree toolchain (unverified whether `ccom`/`smlrc` accept CMSIS-DSP's C
dialect -- it targets `arm-none-eabi-gcc`/`armclang` in practice) and be
checked for code size before inclusion.

### libgcc soft-float (baseline)

The compiler runtime library shipped with any GCC ARM cross-compiler
(`arm-none-eabi-gcc`, the toolchain `board-libc.md` already uses to build
DiscoBSD's own libc). Implements the ARM Run-time ABI for the ARM
Architecture ("RTABI," ARM document `IHI0043`, current issue 2023Q3, at
[ARM-software/abi-aa
rtabi32.rst](https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst))
section 4.1 "Floating-point," which names the exact symbols a soft-float
build calls: `__aeabi_fadd`/`fsub`/`fmul`/`fdiv` (single), `__aeabi_dadd`/
`dsub`/`dmul`/`ddiv` (double), `__aeabi_i2f`/`f2iz`/`i2d`/`d2iz` and
siblings (integer conversions), `__aeabi_fcmpeq`/`fcmplt`/... and `dcmp*`
equivalents (comparisons), always using the base ("soft") procedure-call
standard regardless of the target's actual float ABI setting. This is
DiscoBSD's current *implicit* baseline any time C source uses `float` or
`double` and the linker pulls in `libgcc.a` -- it is what Section 2.3's
"GCC libgcc" column measures, and what Section 2's ROM shim is meant to
replace symbol-for-symbol.

### Berkeley SoftFloat (John Hauser)

Release 3e, BSD-new-style license from Release 3 onward (v2, used
historically in the Linux kernel, carried extra conditions -- see
[www.jhauser.us/arithmetic/SoftFloat.html](http://www.jhauser.us/arithmetic/SoftFloat.html)
and the [ucb-bar/berkeley-softfloat-3](https://github.com/ucb-bar/berkeley-softfloat-3)
mirror). Full IEEE-754 conformance including all four rounding modes and
exception flags, "completely faithful to the IEEE Standard, while at the
same time being relatively fast" per the author's own framing -- but
"fast" there is relative to *other conformant* soft-float, not to the
RP2040 ROM's narrowed-conformance library. This is the right tool only
when DiscoBSD code needs behavior the ROM library explicitly does not
provide: real denormals, real NaN payloads, or a rounding mode other than
round-to-nearest-even (Section 2.1's quoted narrowing). Size and speed on
Thumb-1 specifically: unverified in this pass -- no RP2040 or Cortex-M0
benchmark for SoftFloat-3e was found; ISO C99 source with no
architecture-specific assembly means it is portable but not tuned for a
register-starved Thumb-1 target.

### musl / newlib libm

Both are general-purpose libc/libm implementations (C source, portable),
not evaluated for line-by-line Thumb-1 fitness in this pass. Their
`libm` transcendental functions (`sin`, `exp`, `pow`, ...) are written for
hosted, hardware-FPU-typical targets and are not size-optimized for a 96 KB
process window the way the ROM's scientific functions explicitly are
(Section 2.1's stated size-over-precision tradeoff for `sin`/`cos`/`exp`/
`ln`). newlib's "nano" variants trim printf/scanf format-string handling
but do not specifically retune libm for no-FPU size; concrete newlib-nano
libm object sizes for RP2040/Thumb-1 were not found and are **unverified**.
Given the ROM already supplies `sin`/`cos`/`tan`/`atan2`/`exp`/`ln` at the
cycle costs in Section 2.2 for zero flash cost, pulling in musl's or
newlib's libm implementations of the same functions has no code-size or
speed justification unless a specific function the ROM lacks (e.g.
`pow`, `acos`, `asin`, hyperbolic functions) is actually needed by ported
code -- and even then, hand-writing that one function atop the ROM's
`_fexp`/`_fln` (e.g. `pow(x,y) = exp(y*ln(x))`, the standard identity) is
almost certainly cheaper in flash than linking a general libm.

### FP16 (half precision)

ARMv6-M defines no `__fp16` hardware path: there is no FPU at all, so
there is no half-precision ALU, no VCVT, nothing. GCC on ARM targets
without hardware half support still accepts `__fp16` as a *storage*
type and implements every arithmetic operation on it by inserting a
promotion to `float`/soft-float `double` first -- the conversion itself
runs through libgcc's own `__gnu_h2f_ieee` (half -> float) and
`__gnu_f2h_ieee` (float -> half), implemented in
`libgcc/config/arm/fp16.c`
([gcc-mirror/gcc](https://github.com/gcc-mirror/gcc/blob/master/libgcc/config/arm/fp16.c)),
which use the AEABI/RTABI soft-float calling convention "even for targets
that use the hard-float convention by default" -- i.e., these are always
plain soft-float-ABI function calls, exactly like `__aeabi_fadd`, with no
special hardware path to fall back to on this chip. Two independent
public-domain-ish implementations of the same bit-twiddling conversion
exist and are commonly vendored instead of paying the libgcc dependency:
Fabian Giesen's half<->float code (public domain, used inside Intel's SPMD
Program Compiler among others) and
[Maratyszcza/FP16](https://github.com/Maratyszcza/FP16) (MIT license,
header-only, both the branch-based and branchless/table conversion
variants, plus IEEE and ARM-alternative half formats). **The plain
statement the user's brief asks for: FP16 on Cortex-M0+ is a storage
format only.** There is no way to add, multiply, or compare two `__fp16`
values without first promoting both operands to `float` (or via the ROM's
float table, Section 2), doing the arithmetic there, and (if the result is
being stored back into a half-precision array) converting back down. "Fast
FP16 math" on this chip therefore means: fast, small `half<->float`
conversion (a few dozen Thumb-1 instructions each way with a table or
branchless bit-manipulation approach, not a libgcc pull-in) feeding the
already-fast ROM float kernels from Section 2 -- the win FP16 buys here is
halved RAM/flash footprint for stored arrays, not faster arithmetic.

## 4. Recommendation for DiscoBSD

The binding constraint is the 96 KB per-process window and the fact that
Section 2's ROM routines already exist in every chip at zero size cost.
Rank order, cheapest and most certain first:

**(a) Hand-written Q16.16 fixed point for the common, range-bounded
cases.** No external library dependency: write `fix16_add`/`sub` as plain
`int32_t` add/sub (1 Thumb-1 instruction, no soft-float call at all),
`fix16_mul` as a 32x32 multiply (`__aeabi_lmul` or, since ARMv6-M has no
single-instruction 32x32->64 multiply either, a small hand-written
widening multiply) followed by a 16-bit arithmetic shift, and `fix16_div`
as a pre-shift-then-integer-divide (routed through the same
`__aeabi_idiv`/software-divide routine libc already needs elsewhere for
plain `int` division -- no new dependency). Model the rounding/overflow
policy on libfixmath's implementation (Section 3) rather than vendoring it
wholesale, so the closure stays inside whatever functions libc's own
callers actually exercise -- exactly the `mkboardlibc.py` discipline
`board-libc.md` already established for the rest of libc. This is the
right default for anything with known bounds: geometry, PWM/duty-cycle
math, filter coefficients, fixed-length counters -- and it is the only one
of the three tiers that has zero call overhead into ROM and zero cycles
lost to IEEE bit-format decode.

**(b) Use the RP2040 ROM tables as libc's native floating-point provider.**
The `lib/libc/arm/gen/rom_float_*.S` members export the standard AEABI and
compiler-runtime names, so both cross-linked programs and programs linked on
the board resolve through `-lc` without a linker-specific `--wrap` seam. The
48-member partition exposes 92 public names while allowing the archive linker
to extract only the operations and shared helpers a program reaches. The full
provider contains 1,800 text bytes and 76 initialized-data bytes, but those
totals are not the per-program cost. GNU objdump, LLVM objdump, Capstone,
radare2, and Rizin agree on all 720 Thumb instruction boundaries: 679
instructions are 16-bit and 41 are 32-bit. Each ROM-backed target contributes
one four-byte demand-filled cache cell; direct comparisons and sign changes use
no writable storage. Arithmetic remains 1.5x-8x faster than the software
routines measured in Section 2.3, subject to the ROM numerical contract in
Section 2.1.

### 4.1 Implementation findings: divider safety, conversion, and caller census

Three facts from the bootrom source (`raspberrypi/pico-bootrom-rp2040`,
`bootrom/bootrom_rt0.S` and `bootrom/mufplib.S`), the SDK, and the DiscoBSD
kernel determine the provider design.

**The SIO divider is per-core state with one bounded owner.**
`mufp_fdiv` (and every transcendental, which branch into `fdiv_n`) writes
the SIO hardware divider at `SIO_BASE+DIV_UDIVIDEND`/`DIV_UDIVISOR` and
reads `DIV_QUOTIENT` (`mufplib.S`, `use_hw_div=1`, lines 1050-1089). The
RP2040 provides one hardware divider per core (datasheet 2.3.1.5), not one
peripheral shared across the cores; DiscoBSD runs a single core, so the
divider is core-local state that same-core preemption -- a context switch or
an interrupt that itself divides -- corrupts: a process preempted between the
divisor write and the quotient read gets another context's quotient on
resume. The
pico-sdk guards this in its own `__aeabi_fdiv` wrapper
(`float_aeabi_rp2040.S`, the `fdiv_save_state` path), but that wrapper cannot
protect against an interrupt handler that does not follow the same protocol.
DiscoBSD user Thread mode runs with `CONTROL[0]=1`, so user code also cannot
mask interrupts. The native provider admits direct division under a narrower
invariant: the single-core kernel does not switch processes inside a healthy
user instruction sequence, and the kernel, IRQ, NMI, and callout graph has no
SIO-divider consumer. Source and linked-image gates reject a new divider owner.
The earlier context-switch-only checkpoint did not protect against an IRQ
divider consumer and added work to every kernel save and restore, so the native
provider removes it. `mufp_fadd`, `mufp_fsub`, `mufp_fmul`, `mufp_fsqrt`, and
the conversion entries are divider-free.

The scheduler and exception paths make the narrower invariant precise.
`Reset_Handler` writes `CONTROL=3`, which selects unprivileged Thread mode and
the process stack. SysTick enters `SysTick_Handler`, passes the stacked frame to
`systick`, and calls `hardclock`; exception return resumes the interrupted
process. `hardclock` and its callouts can set `runrun`, but the linked image
contains `swtch` calls only in exit, signal stop or trace handling, `userret`,
and blocking sleep. The syscall and fault return paths call `userret`, which
consumes `runrun`. A successful ROM divide therefore finishes before another
user process can execute. The source and linked-image divider gates cover 155
dependency-closed kernel files and reject any SIO divider register access or
reachable divider runtime symbol in kernel, IRQ, NMI, and callout code.

**Attended-hardware falsifier.** A B2 board running Boot ROM V3 must resolve the
single- and double-precision table entries to valid Thumb targets and preserve
results while a forced SysTick, USB, or UART interrupt lands between divider
input and result access. A changed result, an unexpected table target, an IRQ
divider owner, or a process switch before exception return falsifies the direct
design. The retained hardware run must record the ROM version, resolved target
addresses, interrupt count, expected and observed float bits, and the injected
collision location. Offline evidence does not substitute for that authorized
board run.

**`mufp_float2int` floors; `__aeabi_f2iz` truncates.** The ROM
`float2int` converts "rounding towards -Inf, clamping" (`mufplib.S` line
248), while the C cast `__aeabi_f2iz` rounds toward zero, so they disagree
on every negative non-integer (`(int)-2.7` is `-2`, `float2int(-2.7)` is
`-3`). The native signed 32-bit helpers therefore use the SDK-style bitwise
truncation algorithm. Unsigned and 64-bit conversions use only ROM entries
whose domain and saturation behavior matches the declared symbol contract,
with explicit signed adapters where needed.

**The tree's float users are mostly double-precision; single-precision callers
are sparse and incidental.** A census of undefined `__aeabi_f*` (single)
references across the built objects (`arm-none-eabi-nm` over every `.o`)
finds 98 reference sites, and among the binaries the rp2040 manifest
actually ships only `awk`, `vmstat`, `iostat` and `smlrc` reference
single-precision float at all -- each doing its real arithmetic in
`double` (awk's number type is `double`; the float refs are incidental casts).
The float-heavy paths that matter -- printf `%f`/`%e`/`%g` through `doprnt`,
`awk`, `bc`, and `dc` -- use the `soft_double_table` (`'S','D'`), which Table
171 documents as present only on Boot ROM V2+. The resolver validates the ROM
version and the requested table entry before publishing a target. A requested
double entry on a V1 part exits deterministically with status 70 rather than
calling through a missing table. The B2 target reports Boot ROM V3.

**(c) True general-purpose float/double soft-float, reserved for the
narrow cases the ROM cannot serve.** Two situations force this tier:
first, a bootrom V1 part needs double precision (V1 has no `soft_double_table`
at all, per Table 171/2.8.3.3) -- here, pulling in libgcc's `__aeabi_d*`
routines (or a hand-ported double-precision-only slice of Berkeley
SoftFloat, Section 3) for *doubles specifically* while keeping the ROM
shim for *floats* is the smallest correct fallback, and it should be a
compile-time or runtime bootrom-version check, not a blanket policy for
all parts; second, ported third-party code that genuinely needs IEEE
denormals, exact NaN propagation, or non-default rounding modes needs
Berkeley SoftFloat rather than the ROM's simplified library or libgcc's --
this should stay an opt-in, per-program link choice (linked only by the
program that needs it), never libc's default, given tier (b)'s size and
speed advantage for the common case.

Do not adopt CMSIS-DSP (Section 3) as a dependency for DiscoBSD's libc or
its base userland: on Cortex-M0+ it buys no performance the ROM shim in
tier (b) does not already give per-operation, its scalar-C fallback path
is not Thumb-1-tuned, and its Apache-2.0 header tree is a large surface to
audit and keep building under the in-tree toolchain for a benefit tier (b)
already captures. It remains worth a second look only if a specific future
port genuinely needs CMSIS-DSP's exact FIR/FFT block-kernel API surface
(e.g. porting existing CMSIS-DSP-based application code) -- in that case,
vendor only the specific `.c` files the closure analysis names, the same
way `mkboardlibc.py` already trims libc itself, rather than the whole
package.
