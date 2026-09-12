# CPU benchmarks for the DiscoBSD RP2040 port

The RP2040 core is a dual Cortex-M0+ (ARMv6-M, Thumb-1 only, no hardware
integer divide, no FPU), 264 KB SRAM total, one 96 KB user process window
under DiscoBSD, an a.out toolchain, and a small static libc. DiscoBSD is a
real unix -- fork/exec, files, a shell -- so a benchmark is an ordinary user
program, cross-compiled today with `arm-none-eabi-gcc` and eventually
buildable with the in-tree `usr.bin/smlrc` Thumb-1 compiler. This report
surveys the benchmarks an embedded-systems reviewer would expect a "how fast
is it" claim to cite, names each one's governing body, license, and size,
and ranks them for porting to this tree. No board was touched and no code
was built for this pass; every number below is a citation, not a
measurement.

## 1. The industry standard: EEMBC CoreMark and CoreMark-PRO

**CoreMark** is EEMBC's (Embedded Microprocessor Benchmark Consortium)
scalar/integer benchmark, at
[github.com/eembc/coremark](https://github.com/eembc/coremark) and
[eembc.org/coremark](https://www.eembc.org/coremark/). EEMBC is the
standards body that owns and governs it; the reference implementation is a
handful of `.c` files under Apache License 2.0 (each source file carries a
`Copyright ... EEMBC` / `Licensed under the Apache License, Version 2.0`
header, confirmed directly in
[`core_main.c`](https://github.com/eembc/coremark/blob/main/core_main.c)).
A separate CoreMark Acceptable Use Agreement governs the CoreMark
trademark and the right to publish a number as an official "CoreMark"
score; it specifically bars using the trademark on a modified/derivative
copy ("Licensee shall not use the Trademark in connection with any use of
a modified, derivative, or otherwise altered copy of the Software," per
[`LICENSE.md`](https://github.com/eembc/coremark/blob/main/LICENSE.md)).
Practically: the code can be built, run, and modified freely under Apache
2.0; only the name "CoreMark" on a published, uncertified result carries
the AUA's extra strings, and EEMBC additionally runs a certification lab
that verifies a submitted score followed the run rules
([eembc.org/coremark/scores.php](https://www.eembc.org/coremark/scores.php)).

### Why CoreMark replaced Dhrystone

CoreMark was written in 2009 by Shay Gal-on at EEMBC specifically to
replace Dhrystone as the industry's default integer/scalar number
([EEJournal, "Dhrystone Is Dead; Long Live
CoreMark!"](https://www.eejournal.com/article/20090602_coremark/);
[Wikipedia, Coremark](https://en.wikipedia.org/wiki/Coremark)). Two defects
in Dhrystone motivated the replacement, both stated directly in EEMBC's own
explainer
([Embedded.com, "CoreMark: A realistic way to benchmark CPU
performance"](https://www.embedded.com/coremark-a-realistic-way-to-benchmark-cpu-performance/)):

- **Compiler gaming.** Large parts of Dhrystone's timed loop are constant or
  predictable at compile time, so an aggressive compiler can constant-fold
  or dead-code-eliminate work the benchmark meant to time -- "Dhrystone is
  thus more revealing as a compiler benchmark than as a hardware
  benchmark." CoreMark's fix: every computation is seeded from a
  runtime-provided value (the CRC/seed inputs), so a compiler cannot
  precompute the result at build time.
- **No governed reporting rule and library-time leakage.** Dhrystone calls
  library functions (notably `strcpy`) inside the timed region, so a
  faster or slower libc silently changes the score, and nothing in the
  benchmark's distribution enforces one comparable configuration or
  publication format. CoreMark keeps every timed line in the benchmark's
  own source (no library calls in the timed loop) and ships an explicit,
  EEMBC-enforced run-rule and reporting-string format (see below), with an
  EEMBC certification lab that checks compliance before a score is called
  official.

### Run and reporting rules

Per the upstream
[`README.md`](https://github.com/eembc/coremark/blob/main/README.md):

- The benchmark must run at least 10 seconds (iteration count is the free
  knob a porter tunes to reach that floor on a given core).
- Output must validate against fixed seed triples (`0,0,0x66` and
  `0x3415,0x3415,0x66`, with a 2000-byte working buffer); a separate
  profile-guided-optimization mode uses seeds `8,8,8` with a 1200-byte
  buffer.
- All translation units build with identical flags; the benchmark's fixed
  8/16/32-bit typedefs (`ee_u8`, `ee_s16`, `ee_u16`, `ee_s32`, `ee_u32`)
  must map to the stated widths.
- A porter may change: iteration count, toolchain and build flags, how
  memory is obtained, how seed values are sourced, the `core_portme.c`
  port file, and configuration constants. A porter may **not** change any
  other source file (`make check` validates this against known-good
  checksums).
- The canonical result line is `CoreMark 1.0 : N / C [/ P] [/ M]`, where
  `N` is iterations/second, `C` names the compiler and flags, `P` describes
  the memory-allocation method, and `M` (if present) describes parallel
  execution.

### Footprint and self-containment

The reference implementation is small: roughly 1,083 source lines across
six core files (2,707.5 "adjusted equivalent source lines" plus about 316
lines of port-specific glue), per the algorithm/footprint figures on
[eembc.org/coremark](https://www.eembc.org/coremark/). It exercises three
integer-only workloads -- linked-list find/merge-sort with CRC, matrix
multiply, and a state-machine input classifier -- with no floating point
and no calls outside the benchmark's own translation units during the
timed region, which is exactly the "no library-time leakage" property that
motivated its design.

### Published RP2040 / Cortex-M0+ reference points

A CoreMark score for the RP2040 at its default 125 MHz clock, single core,
is on record from independent porters on the official Raspberry Pi forum
thread ["CoreMark"](https://forums.raspberrypi.com/viewtopic.php?t=304012):

| Reporter | Score (iter/s) | Compiler / flags | Notes |
|---|---|---|---|
| nick.mccloud | 235.844143 | GCC 10.2.1 20201103, `-O3` | single core, STACK memory method |
| rogin7g | 249.49 | (default 125 MHz) | -- |
| HermannSW | 246.198288 | GCC 7.3.1 20180622 (arm/embedded-7-branch), `-O3` | STACK, 6000 iterations |

That clusters around 236-250 CoreMark at 125 MHz, roughly **1.9-2.0
CoreMark/MHz** for a single Cortex-M0+ core with a modern GCC at `-O3` --
consistent with EEMBC's own single-core Cortex-M0(+) figures in the
0.9-2.0 CoreMark/MHz band seen across the vendor scores at
[eembc.org/coremark/scores.php](https://www.eembc.org/coremark/scores.php)
(exact per-part entries not individually re-verified in this pass; treat
the forum figures above as the load-bearing reference since they name
compiler and clock directly). For comparison, the same forum/benchmark
collection lists STM32F103 (Cortex-M0, 72 MHz) at 81 CoreMark (1.13
CoreMark/MHz) and STM32F411 (Cortex-M4F, 100 MHz) at 172 CoreMark (1.72
CoreMark/MHz), per
[kreier.github.io/benchmark/CoreMark](https://kreier.github.io/benchmark/CoreMark/)
-- unverified against the primary EEMBC score sheet in this pass, cited
here only as a rough cross-check that the RP2040 figures sit in the
expected range for a Cortex-M0-class core. A DiscoBSD CoreMark run, once
ported, has a direct reference point: a native a.out build should land
somewhere below the bare-metal GCC `-O3` figures above (unix process
overhead, a.out relocation model, and `usr.bin/smlrc`'s or the cross
`arm-none-eabi-gcc` soft toolchain's own code-generation quality all pull
the number down from the bare-metal ceiling), and the gap between a
DiscoBSD result and ~240 CoreMark at 125 MHz is itself a meaningful,
citable figure for how much a real unix layer and this port's toolchain
cost against the bare-metal ideal.

### CoreMark-PRO: the floating-point companion

**CoreMark-PRO** ([eembc.org/coremark-pro](https://www.eembc.org/coremark-pro/))
is EEMBC's follow-on suite that adds workloads CoreMark deliberately
omits: floating-point kernels, larger data sets, and both single- and
multi-core scoring. Per the
[FAQ](https://www.eembc.org/coremark-pro/faq.php), a target with no
hardware FPU "may use the provided software floating-point emulation, but
it will be slow" -- CoreMark-PRO explicitly anticipates running on
soft-float cores and expects the resulting score to reflect that, which is
the same caveat that applies to every FP benchmark in this document on the
RP2040. Its reporting string mirrors CoreMark's (`CoreMark-PRO 1.0.x : N /
C [/ P] [/ M]`); license terms were not independently re-confirmed for
this pass beyond EEMBC's general Apache-2.0-plus-AUA pattern for CoreMark
-- treat as **unverified** until the CoreMark-PRO repository's own license
file is read directly. CoreMark-PRO is substantially larger than CoreMark
(multiple workload directories, a common harness, larger data tables) and
is not a first port candidate for this tree; see Section 4.

## 2. The classics, and where they still matter

### Dhrystone (DMIPS)

Reinhold P. Weicker's 1984 synthetic integer benchmark, current definition
Dhrystone 2.1 (Weicker and Richardson, May 1988), per
[Wikipedia, Dhrystone](https://en.wikipedia.org/wiki/Dhrystone) and
[Wikipedia, DMIPS](https://en.wikipedia.org/wiki/DMIPS). DMIPS ("Dhrystone
MIPS") is the Dhrystone score divided by 1757, the throughput a VAX-11/780
(defined as the nominal 1-MIPS reference machine) achieved on the same
code -- so "DMIPS" is always relative to that one 1978 machine, not an
absolute instruction count. The reference source
([`dhry_1.c`](https://github.com/wuhanstudio/dhrystone/blob/master/dhry_1.c))
carries only an authorship header ("Author: Reinhold P. Weicker"), no
explicit license grant -- Dhrystone circulated for decades as
public-domain-by-convention academic/industry code rather than under a
modern OSI license; treat any specific license claim beyond "freely
redistributed in practice" as **unverified**. Dhrystone is integer-only (no
floating point at all), tiny (three `.c` files, a few hundred lines), and
still the number every MCU vendor datasheet quotes in DMIPS/MHz because
decades of prior-generation silicon have no other comparison point. It is
exactly the benchmark CoreMark was built to retire for new comparisons
(Section 1) -- its own library-call and compiler-optimization weaknesses
are why EEMBC built CoreMark, and a DiscoBSD Dhrystone number would mean
less on its own than a CoreMark number, but it remains useful **only** as
a cross-reference against the very large body of legacy DMIPS/MHz figures
already published for other Cortex-M0/M0+ parts.

### Whetstone (KWIPS / MWIPS)

Brian Wichmann and Harold Curnow's Algol (November 1972) and Fortran
(April 1973) benchmark, the first general-purpose benchmark to set an
industry performance-comparison convention, documented in detail on Roy
Longbottom's benchmark archive
([roylongbottom.org.uk/whetstone.htm](http://www.roylongbottom.org.uk/whetstone.htm);
[Wikipedia, Whetstone
(benchmark)](https://en.wikipedia.org/wiki/Whetstone_(benchmark))).
Score unit is KWIPS (thousand Whetstone instructions/second), later scaled
to MWIPS. Whetstone is explicitly a **floating-point** benchmark -- trig,
sqrt, exponential, and array-indexing kernels mixed with integer control
flow -- and ships separate single-precision (MWIPS SP) and double-precision
(MWIPS DP) variants, so a Whetstone port produces two numbers, not one.
License was not found in a governing, citable form in this pass (no SPDX
header located on the canonical Longbottom mirror or in the searched
secondary sources); treat as **unverified**, likely similarly
public-domain-by-convention to Dhrystone given the shared 1970s-benchmark
provenance, but that is an inference, not a sourced fact. On a soft-float
Cortex-M0+ (Section 4), a Whetstone MWIPS number is dominated by whichever
soft-float library backs `float`/`double` arithmetic and by the transcendental
routines (`sin`, `sqrt`, `exp`) in libm/libc -- it measures the compiler's
soft-float codegen and the C library's math routines at least as much as
it measures the core, which is the same caveat CoreMark-PRO's own FAQ
states about running FP work with software emulation (Section 1). This is
the direct companion point to `board-libc.md`'s finding that the board's
reduced libc currently omits `libc/runtime`'s `__aeabi_f*`/`__eqdf2`/`__ledf2`
soft-float routines and that `usr.bin/smlrc`'s own `double` is only an
alias for `float` (Section 4 below spells out the exact dependency set).

### Linpack (single- and double-precision)

Jack Dongarra's 1979 benchmark, originally an appendix to the LINPACK
Users' Guide, timing `SGEFA`/`SGESL` (single precision) and
`DGEFA`/`DGESL` (double precision) LU-decomposition-with-partial-pivoting
routines on a dense matrix -- the founding member of the HPC
benchmark lineage that later became the TOP500's HPL
([people.math.sc.edu/burkardt, LINPACK_BENCH](https://people.math.sc.edu/burkardt/f77_src/linpack_bench/linpack_bench.html);
[ICL/UTK, "The LINPACK Benchmark: Past, Present, and
Future"](https://icl.utk.edu/~luszczek/pubs/hplpaper.pdf)). Reference C,
Fortran, Java, and Python translations of the classic fixed-size (100x100
matrix) benchmark are distributed under the GNU LGPL, per the Burkardt
mirror pages. Linpack is pure floating-point linear algebra -- on a
soft-float M0+ it is an even purer soft-float/libm stress test than
Whetstone (nothing but multiply-add and division in the timed loop), and
its 100x100 double-precision matrix (80 KB of doubles alone) does not fit
this port's 96 KB process window without shrinking the problem size far
below the "classic" comparison point, which would forfeit comparability
against the published Linpack 100 table entirely. Not a near-term port
candidate for this tree; useful mainly as the reason CoreMark-PRO and
Embench both include a small linear-algebra-flavored kernel of their own
rather than the classic Linpack 100.

### Livermore Loops

Francis H. McMahon's 1986 Fortran kernel suite from Lawrence Livermore
National Laboratory ("Livermore Fortran Kernels: A Computer Test of the
Numerical Performance Range"), a set of 24 numerical kernels drawn from
real LLNL physics codes, per
[Wikipedia, Livermore loops](https://en.wikipedia.org/wiki/Livermore_loops)
and the [netlib mirror](https://www.netlib.org/benchmark/livermore).
License was not found in a governing, citable form in this pass (netlib
hosts it without an attached SPDX/license file in the searched pages);
treat as **unverified**. It is Fortran-first (a C mirror exists at
[netlib.org/benchmark/livermorec](https://www.netlib.org/benchmark/livermorec),
not independently read in this pass), double-precision throughout, and
sized for 1980s supercomputer vector units -- both its language heritage
and its floating-point intensity make it a poor fit for a C89 Thumb-1
compiler with no double-precision support in-tree (Section 4); relevant
here only as the historical bridge between Whetstone-era synthetic FP
benchmarking and Linpack-era HPC benchmarking, not as a port candidate.

### nbench (BYTEmark)

BYTE magazine's "Native Mode Benchmarks," later BYTEmark, released around
1995 and ported to Linux/Unix as nbench by Uwe F. Mayer in 1996, per
[Wikipedia, NBench](https://en.wikipedia.org/wiki/NBench) and the
[Utah mirror](https://www.math.utah.edu/~mayer/linux/bmark.html). Its
license is recorded by downstream packagers (Gentoo, FreeBSD ports,
OpenEmbedded) as "freedist" -- freely redistributable, but not a modern
OSI-approved license with explicit modification terms; the license text
itself is understood to have been lifted from BYTE's original README
rather than a standalone grant, per the OpenEmbedded license notes at
[meta-openembedded/.../licenses/nbench-byte](https://github.com/openembedded/meta-openembedded/blob/master/meta-oe/licenses/nbench-byte).
nbench mixes integer, memory, and floating-point (single- and
double-precision) kernels -- numeric sort, string sort, FFT, assignment,
IDEA, Huffman, LU decomposition, neural net -- into one suite with a
composite "index" score normalized against a reference machine (an AMD
K6-233, per the original BYTE convention). It is heavier than CoreMark or
Dhrystone (roughly a dozen distinct kernels, each with its own data
tables), its scoring convention assumes a Unix or DOS host with a real
timer and file I/O, and its license ambiguity is itself a reason to prefer
CoreMark (unambiguous Apache-2.0 source) or Embench (unambiguous GPL3) for
a result meant to be published and compared. Not recommended as a first
or second port.

## 3. The modern open option: Embench-IoT

**Embench-IoT** ([embench.org](https://www.embench.org/),
[github.com/embench/embench-iot](https://github.com/embench/embench-iot))
is the free/open alternative positioned specifically for deeply embedded
cores, governed by the Embench Group, which operates under the Free and
Open Source Silicon (FOSSi) Foundation, per
[fossi-foundation.org/blog/2021-01-19-embench-1-0](https://fossi-foundation.org/blog/2021-01-19-embench-1-0).
It succeeds BEEBS (Bristol/Embecosm Embedded Benchmark Suite); most of its
19 benchmark programs are derived from BEEBS's real-world C programs
rather than written as synthetic kernels, per the "BEEBS: Open Benchmarks
for Energy Measurements on Embedded Platforms" paper referenced from the
Embench documentation set. The suite's own repository is licensed GPLv3
(`COPYING`, confirmed directly at
[github.com/embench/embench-iot/blob/master/COPYING](https://github.com/embench/embench-iot/blob/master/COPYING)),
though the upstream README notes individual constituent benchmark programs
may carry their own (not independently re-verified) licenses -- a GPLv3
suite wrapping variously-licensed real-world programs is a materially
different legal shape than CoreMark's single Apache-2.0 grant, and worth
flagging before pulling code into a tree that otherwise favors permissive
licenses (`board-libc.md` and `ondevice-c-compilers.md` both track license
per component for exactly this reason).

**Rationale**: Embench's explicit position is that synthetic benchmarks
(Dhrystone, CoreMark alike) are not reliably representative of real
embedded workloads, and that real small programs -- compression,
cryptography, string search, signal-processing kernels drawn from actual
embedded use -- are more informative, per the Embench README and the
Hackster.io coverage of the 1.0 release
([hackster.io, "Embench 1.0..."](https://www.hackster.io/news/embench-1-0-already-in-use-at-seagate-promises-fully-open-real-world-comparatives-for-iot-devices-faf19e91c729)).
Unlike Dhrystone, which has no maintaining organization, Embench is
explicitly maintained by a standing group (the FOSSi Foundation-hosted
Embench Group) precisely so the suite can be revised as compilers and
hardware move -- the same governance argument CoreMark makes against
Dhrystone, applied by Embench against both Dhrystone and CoreMark's closed
governance model.

**Scaling and harness fit**: each of the 19 programs is sized and
iteration-scaled so a run takes about 4 seconds of CPU time on the
reference platform, and the full suite completes in a few minutes,
targeting boards with at least 64 KB ROM and 64 KB RAM -- both figures
inside this port's budget, per the Embench README summary gathered in
this pass. Its stated design assumption is bare-metal: "no OS, minimal C
library support and in particular no output stream" (paraphrasing the
upstream README language surfaced in this pass) -- that assumption is
**friendlier** to DiscoBSD than it sounds, not harder: DiscoBSD supplies a
real OS, a real libc, and real I/O, so a DiscoBSD port drops Embench's own
bare-metal timer/output shims entirely and replaces them with ordinary
`gettimeofday(2)`/`printf(3)` calls, rather than fighting an RTOS
assumption Embench does not actually make. The porting work is in
`config/<arch>/{boards,chips}` (confirmed present in the repository tree
navigation during this pass) -- a new `config/rp2040` (or generic
`config/arm-generic`) directory supplying the board/chip descriptors and
timer hookup, following the pattern of the existing architectures, is the
concrete unit of work, not a rewrite of the 19 benchmark programs
themselves.

## 4. Portability verdict for DiscoBSD

Ranking by how little machinery each benchmark needs beyond a C89/C99
front end and this tree's already-reduced libc (`board-libc.md`), given
the target has **no hardware FPU** -- so any benchmark that touches
`float`/`double` is also benchmarking whatever soft-float and libm/libgcc
routines the port supplies, not a fixed hardware unit:

| Rank | Benchmark | Language need | Extra libc/soft-float pulled in | Fits `usr.bin/smlrc` today? |
|---|---|---|---|---|
| 1 | **CoreMark** | Strict C89/C99, no FP, no library calls in the timed loop | None beyond what `libc-sink.c`'s everyday closure already proves (`board-libc.md`) | Yes -- integer-only, matches smlrc's supported subset directly |
| 2 | **Dhrystone 2.1** | C89, no FP, but calls `strcpy` in the timed region | `strcpy`/`strcmp`, already in the 89-member reduced archive | Yes |
| 3 | **Embench-IoT** | C99, no FP requirement (the suite is integer/control-flow heavy; a handful of programs may touch FP -- not confirmed per-program in this pass), needs per-program porting glue plus a timer/report harness | Depends per-program; likely close to the existing closure, unconfirmed until each of the 19 programs is read | Probably, pending per-program license and per-program feature audit |
| 4 | **CoreMark-PRO** | C99, explicit FP workloads, multi-core scoring harness, larger data tables | Full soft-float path (`__aeabi_f*`/`__aeabi_d*`, `__eqdf2`/`__ledf2`/... per `board-libc.md`'s finding) plus a heavier harness than plain CoreMark | Only if smlrc gains real `double` (see below) or the cross `arm-none-eabi-gcc` build path is used instead |
| 5 | **Whetstone** | C or Fortran, FP-first (both SP and DP variants) | Same soft-float path as CoreMark-PRO, plus libm (`sin`, `sqrt`, `exp`) | SP variant only, and only via the cross-compiled path -- smlrc's `double` is an alias for `float` (below), so its own DP variant is not meaningfully separate from SP on this compiler |
| 6 | **nbench** | C, FP-heavy, dozen-plus kernels, license ambiguous | Soft-float path plus libm plus larger data footprint than the 96 KB window comfortably holds for its FFT/neural-net kernels | Not recommended; license and size both argue against it |
| 7 | **Linpack / Livermore Loops** | Fortran-heritage, double-precision throughout, 80 KB+ working sets at classic problem sizes | Full double-precision soft-float path; classic problem size does not fit the 96 KB window at all | Not recommended as ported; cite only as the historical HPC-lineage reference |

**The soft-float dependency, named exactly** (cross-referencing
`board-libc.md`'s own finding): `arm-none-eabi-gcc`'s soft-float ABI needs
`__aeabi_fadd`/`fsub`/`fmul`/`fdiv`/`fneg`/`f2iz`/`i2f` and `__lesf2`/`__gesf2`
for single precision (all present in `lib/libc/runtime` per `board-libc.md`,
and independently reimplemented as plain C in `usr.bin/smlrc/lb.c`'s
`__addsf3`/`__subsf3`/`__mulsf3`/`__divsf3`/`__negsf2`/`__lesf2`/`__gesf2`/
`__floatsisf`/`__floatunsisf`/`__fixsfsi`/`__fixunssfsi`), and additionally
`__aeabi_dcmp*`/`__eqdf2`/`__ledf2` (and the rest of the double-precision
comparison/arithmetic family) for double precision, which `board-libc.md`
found **absent** from the board's current 89-member reduced archive --
double precision is not yet wired into this port's libc closure at all.
`usr.bin/smlrc` compounds this on the compiler side: its lexer treats
`double` as a bare alias for `float` (`smlrc.c:6537`, `// double is an
alias for float`), and its own soft-float runtime in `fp.c`/`lb.c` is
single-precision-only (`fp.c` asserts `sizeof(float) == sizeof(unsigned)`
and implements only the `sf`-suffixed single-precision routines). A
double-precision FP benchmark (Whetstone DP, Linpack, Livermore Loops)
built with `smlrc` therefore silently runs at single precision, which
would misrepresent itself against every published double-precision
reference figure -- such a benchmark is only meaningful on this port via
the `arm-none-eabi-gcc` cross-compiled path, and only once `board-libc.md`'s
double-precision gap is closed.

### Port-order recommendation

1. **CoreMark first.** It is the single benchmark on this list with zero
   new libc or soft-float dependency, a governed and simple run/report
   rule, an Apache-2.0 license with no ambiguity, a tiny footprint (about
   1,100 lines) that fits the 96 KB window with enormous headroom, and a
   directly citable reference figure already on record for this exact
   part (~236-250 CoreMark at 125 MHz single-core, Section 1). It compiles
   today under `usr.bin/smlrc`'s supported C subset with no new codegen
   work, giving DiscoBSD a scalar/integer number the moment the port file
   (`core_portme.c`: timer via a DiscoBSD syscall, e.g. `gettimeofday(2)`,
   and memory via `malloc(3)` or static allocation) is written -- a small,
   bounded task, not a compiler or libc change. This is the scalar half of
   the "both a scalar number and a float number" goal.
2. **Dhrystone second**, purely for legacy-DMIPS cross-reference against
   the large body of pre-CoreMark MCU datasheets; it is nearly as trivial
   to port as CoreMark (same integer-only profile, one extra `strcpy` call
   already satisfied by the existing libc closure) but should be reported
   only as a secondary, legacy-comparison figure, not as the port's
   headline number, given the compiler-gaming and library-leakage defects
   Section 1 documents.
3. **Whetstone single-precision third**, as the float half of the
   headline pair. It needs only the single-precision soft-float path
   `usr.bin/smlrc/lb.c` already implements plus `sqrtf`/`sinf`/`expf`-class
   libm entry points (the same dependency set `llama89-and-toolchain.md`
   independently identifies for its own FP workload), so it is reachable
   with `smlrc` once those libm entry points are confirmed present or
   added to the reduced archive -- smaller new work than standing up
   CoreMark-PRO's full harness. Report it explicitly as **single-precision
   soft-float MWIPS**, not double, and do not claim comparability against
   any published double-precision Whetstone figure.
4. **Embench-IoT fourth**, once CoreMark and Whetstone have established
   the port's scalar and float baselines: it is the most defensible
   real-code, actively governed alternative, its bare-metal assumption is
   not an obstacle for a tree with a real OS (Section 3), and its
   per-benchmark license variability is the main reason to do it after,
   not before, the two single-file benchmarks above -- each of the 19
   programs needs its own quick license and feature read before it lands
   in the tree.
5. **CoreMark-PRO, nbench, Linpack, and Livermore Loops stay out of
   scope** for this port until `board-libc.md`'s double-precision gap is
   closed and the cross `arm-none-eabi-gcc` path (not `smlrc`) is the
   accepted build route for a benchmark binary; even then, Linpack and
   Livermore Loops' classic problem sizes need shrinking below their
   standard comparison points to fit the 96 KB window, which forfeits
   direct comparability against the published tables that make those two
   benchmarks worth running in the first place.
