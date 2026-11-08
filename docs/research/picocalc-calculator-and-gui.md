# PicoCalc calculator application and tile GUI

Status: design note, 2026-11-05. No code in this note is implemented. Every
figure carries one of three marks: measured (a named command produced it for a
named artifact), documented (a file or specification states it), or estimate
(derived here, with the derivation). Board behavior is not measured anywhere in
this note.

## Result

- The tree already builds a complete arbitrary-precision calculator: a Plan 9
  `bc` front end that pipes to a Plan 9 `dc` engine, with a `bc -l` math library
  (`usr.bin/bc/lib.b`: sine, cosine, arctangent, natural log, exponential,
  Bessel). The pair measures 48,264 bytes of a.out (dc 19,316 plus bc 28,948)
  and ships in no profile. Shipping it needs a manifest closure and a `NOTICE`
  entry, and no new code. The license is MIT-style (Plan 9 Foundation 2021).
- No vendor or BSD calculator fits better. OpenBSD and NetBSD `dc`/`bc` call
  libcrypto `BN_*` (112 source lines in OpenBSD `bcode.c`, 129 occurrences in NetBSD's copy), which the port lacks. Gavin
  Howard's `bc` (FreeBSD, BSD-2) is a 70-90 KB a.out estimate. NetBSD's Phil Nelson
  `bc` (BSD-3) measures 48.9 KB of object text before libc. The PicoCalc vendor
  repository holds no calculator program, and its only math source
  (`MATHS.c`) carries an advertising clause.
- Scientific functions are cheap because the boot ROM supplies double
  arithmetic: `lib/libm.a` links 4,092 bytes for sin, tan, atan, asin, exp,
  log, pow, sqrt, sinh, tanh and floor (measured). The shipped awk reaches only
  log, exp, sqrt and int.
- The cheapest tile GUI needs no pixel mode. The console cell store is already
  a 53x32 tile map with a 16-color foreground, 8-color background, hardware
  scroll and per-cell redraw. A 128-glyph user-definable bank behind the
  existing line-drawing escape costs about 1.2 KB of RAM and 300-500 bytes of
  text (estimate), keeps text and tiles in one store, and restores itself with
  no mode switch.
- A pixel mode (palette-indexed 8x8 tiles, window streams, hardware vertical
  scroll) belongs on a separate exclusive device node, `/dev/lcdgfx`, so a
  crashed program cannot strand the panel in graphics mode. The panel
  bandwidth, not the CPU, bounds every figure: a full 320x320 repaint costs
  196.6 ms at the console's present 12.5 MHz and RGB666 (derived below).
- License blockers: `MATHS.c` in the PicoMite tree (clauses 3 and 4 of its
  BSD text require the MMBasic name and an advertising acknowledgement), the
  RetroBSD `tftetris.c` example (GPLv2 or later), and the Keen Dreams source
  (GPLv2 or later, reference only). The OpenBSD/NetBSD `dc` and `bc` carry no
  license blocker; the libcrypto dependency blocks them.

## Evidence base

- Worktree: branch `picocalc/lcd-keyboard-console`, HEAD
  `e0c5298ecb895c2f0d5eb43eb444534cce2d2044` (2026-11-05), plus four
  uncommitted edits (`share/mk/architecture.mk`,
  `sys/arch/rp2040/compile/Makefile`,
  `tools/architecture-isolation/check-architecture-isolation.sh`,
  `tools/config/check_generated_sync.py`) and untracked console files. None of
  the edits touches a program measured below.
- a.out sizes: read from the OMAGIC header (magic 0x107, then text, data and
  bss words) of the build outputs present in the worktree on 2026-11-05. The
  awk header (48,108 / 2,532 / 10,796) equals the shipped baseline in
  `docs/research/c17-shipped-resource-baseline.json`, which calibrates the
  reading.
- Object sizes: `arm-none-eabi-gcc` 16.2.0 (Arch Linux package), `-Os
  -mcpu=cortex-m0plus -mabi=aapcs -mthumb -mfloat-abi=soft`, compile only, run
  with `arm-none-eabi-size`. Tree sources use the tree's `include` directory
  with a `machine` symlink to `sys/arch/rp2040/include`. NetBSD `bc` objects
  use the toolchain's newlib headers with libedit disabled. Nothing downloaded
  was executed; byacc and flex only translated grammar sources.
- Upstream reads: GitHub REST API and `raw.githubusercontent.com` on
  2026-11-05, plus a local clone of the vendor repository at
  `fdcfe2c87a56b6dbcd540e91de1161d483872af9` (2026-10-14).
- Panel and keyboard facts come from `sys/arch/rp2040/dev/st7365p.c`,
  `lcdterm.c`, `lcdterm.h`, `lcdfont.h`, `lcdcons.c`, `picokbd.c` and
  `picokbd_map.h` at the HEAD above.
- Packed-root cost: every shipped executable is a heatshrink-packed a.out. The
  32 entries in the shipped baseline pack to 0.70-0.94 of raw size, median
  about 0.83. This note applies 0.85 to raw sizes and rounds up to blocks
  (estimate).

## 1. Calculator code in this tree

| Program | Source (path, bytes) | Builds for rp2040 | In the shipped image | Float | Measured a.out (text / data / bss) |
| --- | --- | --- | --- | --- | --- |
| `dc` | `usr.bin/dc/dc.c` 36,729; `dc.1` 5,014 | yes (`usr.bin/Makefile` SUBDIR `dc`) | no | none (no `double` or `float` token) | 19,084 / 200 / 4,140 |
| `bc` | `usr.bin/bc/bc.y` 13,438; `lib.b` 2,742; `bc.1` 3,895 | yes (SUBDIR `bc`) | no | none; its Makefile sets `PRINTF_FLOAT=yes` anyway | 28,292 / 624 / 22,188 |
| `expr` | `bin/expr/expr.y` 14,931 | yes (`bin/Makefile`) | yes, hard link `/bin/expr` into `/usr/bin/utilbox` (`distrib/rp2040/mi.rp2040` line 380) | none, integer only | standalone 14,580 / 364 / 588 |
| `factor` | `games/factor/factor.c` 1,428 | yes | no (the `games` closure holds only `gamebox`) | none | 7,876 / 200 / 144 |
| `primes` | `games/primes/primes.c` 3,469 | yes (`-lm`, `SCANF_FLOAT=yes`) | no | double | 10,720 / 796 / 1,052 |
| `arithmetic` | `games/arithmetic/arithmetic.c` 3,483 | yes | no | none | 7,548 / 208 / 884 |
| `cal` | `usr.bin/cal/cal.c` 3,470 | yes | no | none | 6,472 / 288 / 476 |
| `calendar` | `usr.bin/calendar/calendar.c` 9,853 | yes | no | none | 14,396 / 884 / 980 |
| `basic` | `usr.bin/basic/basic.c` 46,033 | yes (`-lm`) | no | double | 32,952 / 1,560 / 9,020 |
| `awk` | `usr.bin/awk/*.c` (V7 awk) | yes | yes (`pack /usr/bin/awk`) | double | 48,108 / 2,532 / 10,796 |
| `units` | absent (`sys/arch/rp2040/doc/research/v7-tools.md` records the gap) | no | no | not applicable | not applicable |
| gpanel library | `legacy/non-arm/mips-pic32/lib/libgpanel/*.c`, 19,263 bytes (12 files); examples 44,948 bytes | no (archive only) | no | none | not applicable |

Findings behind the table:

- `dc` and `bc` form one calculator. `bc.y` forks, translates infix text to
  `dc` input in the child, and `execl`s `/usr/bin/dc -` in the parent
  (`usr.bin/bc/bc.y` lines 975-989). `bc -l` reads `/usr/share/misc/lib.b`,
  which defines `e`, `l`, `s`, `c`, `a` and `j`. The directory
  `/usr/share/misc` exists in the manifest (line 47).
- A pipeline of two runnable processes shares one 144 KB user window. The
  second process swaps through SwapRAM or flash
  (`sys/arch/rp2040/doc/research/zswap.md`). The latency of `echo 2+2 | bc` on
  the board is not measured; a measured value above one second falsifies the
  choice of the two-process pair for interactive use.
- `bc` pays 22,188 bytes of bss because `bc.y` declares `bspace[bsp_max]` with
  `bsp_max` 5000 pointers plus two 1,000-byte arrays. The total of 51,104
  bytes (text, data, bss) fits the window.
- The standalone libc floor is 7,876 bytes of text (`factor`, which prints with
  `printf`). `dc` object text is 12,176 bytes and its a.out text is 19,084, so
  libc adds about 6.9 KB.
- `PRINTF_FLOAT=yes` in `usr.bin/bc/Makefile` links `doprnt_float.o`, measured
  at 2,028 bytes in the worktree's `lib/libc.a`. `STORAGE.md` still states
  "about 10 KB" for that member; the figure predates the ROM float provider and
  no longer holds for this build. Whether `bc` needs the member is not tested;
  `bc.y` has no `%f`, `%g` or `%e` conversion (grep).
- The root has 270 free blocks in the `full` profile and 445 in `base`
  (`sys/arch/rp2040/doc/PROFILES.md`). The pair plus `lib.b` costs about 45
  blocks (estimate: 51,006 raw bytes x 0.85 packed, `lib.b` unpacked).

### Float and libm on this port

- Arithmetic: `lib/libc/arm/gen/rom_float_*.S` forward `__aeabi_d*` and
  `__aeabi_f*` to the RP2040 boot ROM float tables. Double entries exist from
  Boot ROM V2; the measured board reports V3. A V1 part exits with status 70
  (`sys/arch/rp2040/doc/research/float-libs.md`, section 4.1). Datasheet cycle
  counts for double (V2/V3): add 91, mul 155, div 183, sqrt 169, sin 1,618,
  exp 804 (float-libs.md section 2.2).
- The libc exposes arithmetic, comparison and conversion only. The ROM
  transcendental entries (`sin`, `exp`, `ln`) are not reachable from libc, so
  `lib/libm` runs its Hart and Cheney polynomials in C over ROM arithmetic.
  Calling the ROM entries from `libm` would cut the cycle counts (an
  estimate: roughly 1.6k cycles, 13 us at 125 MHz, for double sin against a
  polynomial of 10-20 multiply-adds at 155+91 cycles each). Neither path is
  measured on the board.
- `lib/libm.a` (33,300 bytes, 17 members, `Tag_CPU_arch: v6S-M`, double only)
  members by text bytes, measured with `arm-none-eabi-size`: asin 296, atan
  676 (carries atan2), exp 388, floor 128, fmod 588, hypot 152, log 448, pow
  176, sin 520 (carries cos), sinh 476, sqrt 348, tan 516, tanh 120, erf 800,
  j0 1,496, j1 1,600, jn 640; total 9,368. The scientific subset is 4,092
  bytes.
- `/usr/lib/libc.a` is the only archive in the shipped `toolchain` closure
  (`mi.rp2040` lines 237-240). `libm.a` is absent, so an on-board compile
  cannot link `-lm`. The on-board compiler `smlrc` also maps `double` to
  single precision (`usr.bin/smlrc/README.rp2040.md` lines 110-111). A
  scientific calculator therefore ships cross-built, like every other program
  in the image.
- awk ships and evaluates `+ - * / %`, `log`, `exp`, `sqrt` and `int`
  (`usr.bin/awk/run.c`, `fncn()`); it has no `sin`, `cos` or `atan2`.
- The libc has no `getopt_long` (grep of `lib/libc` and `include`) and no
  POSIX `getline` (`sys/arch/rp2040/doc/research/posix-tools.md` lines
  110-111). Both matter for the NetBSD and FreeBSD `bc` candidates.

## 2. What the other BSDs and the vendor offer

Source sizes come from the GitHub contents API on 2026-11-05 unless a local
file is named. "Thumb-1 size" is a measured object size when the row says so;
otherwise it is an estimate from source size or from upstream's published
binary sizes, with the basis stated.

| Candidate | Upstream location and identifier | License | Source size | Thumb-1 size | Fit |
| --- | --- | --- | --- | --- | --- |
| OpenBSD `dc` | `github.com/openbsd/src` `usr.bin/dc`, last commit `f8d7bc5263c47f4fe0b4cdbae341767907b76247` (2025-12-15) | ISC-style, Otto Moerbeek | C and headers 57,725 bytes (`bcode.c` 31,189, `inout.c` 8,585, `stack.c` 7,396) | not built; needs libcrypto `BN_*`, 112 lines in `bcode.c` | blocked by the bignum dependency; the port carries no BIGNUM library |
| OpenBSD `bc` | same repo, `usr.bin/bc`, `8e7768061bedd57d42860056678ff81f368bcb88` (2024-07-31) | ISC-style | `bc.y` 23,030, `scan.l` 7,277, `tty.c` 1,541, plus the `dc` files | not built; needs libedit, libcurses, libcrypto | blocked, same cause plus two more libraries |
| NetBSD `dc` | `github.com/NetBSD/src` `usr.bin/dc`, `ca12074f78c0f49063c59e1ec57848b7f92f9510` (2025-10-19) | ISC-style (OpenBSD derived) | C and headers 61,122 bytes (`bcode.c` 32,068) | not built; links `-lcrypto`, 129 `BN_` lines | blocked, same cause |
| NetBSD `bc` | `NetBSD/src` `external/bsd/bc`, `b88a0ee409609dc9e517cc6cbae8049e36f90089` (2026-01-30); GNU/Phil Nelson `bc` family, `config.h` version string `nb1.0` | BSD-3-Clause, Philip A. Nelson and FSF holders (header of `main.c`) | `.c`, `.y`, `.l`, `.h` sum 182,432 bytes | measured objects, libedit off: 48,897 text, 344 data, 325 bss (`bc.o` 19,753, `scan.o` 8,253, `number.o` 5,908, `storage.o` 3,468, `execute.o` 3,126, `global.o` 2,652, `util.o` 2,817); a.out estimate 57 KB (objects plus the 7.9 KB libc floor) | no libcrypto, single process, interpreter in one binary; needs `getopt_long` (about 1 KB shim, estimate) and the `bc.y` and `scan.l` build steps; 3 yacc conflicts under byacc; 57 KB against 48 KB for the existing pair |
| NetBSD `units` | `NetBSD/src` `usr.bin/units`, `577e25fd4e361ead5f2528945801eef719d77a5f` (2022-11-20) | Adrian Mariano's two-clause notice (no endorsement, no warranty) | `units.c` 21,557; `units.lib` 16,929 | not built; double arithmetic | table dominates; see the unit conversion row in section 3 |
| OpenBSD `units` | `usr.bin/units`, `f24cc00c510c7943784f4ed03754490e4881db13` (2026-06-30) | same notice | `units.c` 14,975; `units.lib` 13,823 | not built; double | smaller source; same table cost |
| FreeBSD `bc`/`dc` | `github.com/freebsd/freebsd-src` `contrib/bc` (Gavin Howard `bc` 7.1.0, `VERSION.txt`) and `usr.bin/gh-bc`; upstream tag `7.1.0` = `70c51d95e02d20fc3fc82791dc3f1aa0e9d47f0d` | BSD-2-Clause (`LICENSE.md`; `history.c` carries its own BSD notice) | release 7.0.3 `src/` 585,670 bytes, `include/` 266,621 | not built. Upstream `manuals/build.md` "Binary Size": 95.3 KB (`bc` alone, no history, no extra math) to 140.4 KB (both calculators, all features) on x86_64, `-Os`, static musl. Estimate for Thumb-1: 60-80 KB of text before libc, 70-90 KB a.out | needs C99 and POSIX 2008 interfaces (README line 39) that the port's libc partly lacks (`getline`); roughly twice the existing pair; no benefit that the target needs |
| 4.3BSD `dc`, `bc` | `github.com/dspinellis/unix-history-repo` tag `BSD-4_3` (tag object `e60dee1e0c99570a5f108fb5e30f2df18f8990ca`): `usr/src/usr.bin/bc.y` 12,514; `usr/src/usr.bin/dc/dc.c` 31,508, `dc.h` 3,234 | Berkeley notice over Bell Labs V7 code (Caldera license, 2002) | 47,256 bytes | not built | superseded in this tree by the Plan 9 pair |
| 2.11BSD `dc`, `bc`, `units` | maintainer's 2.11BSD fork checkout, main `0a38e59d742ff4371b78ea65c5bcc50477b31708` (patch level 499 applied): `usr.bin/bc.y` 12,715; `usr.bin/dc/dc.c` 31,613 and `dc.h` 3,237; `usr.bin/units.c` 6,292; `share/misc/units` 8,652 | Berkeley and Caldera, as `docs/research/licensing.md` records | K&R C | not built; K&R definitions fail the `smlrc` rules | the only candidate whose `units` table is small (8.6 KB); see the unit conversion row in section 3 |
| Tree `bc`, `dc` (Plan 9) | `usr.bin/bc`, `usr.bin/dc`; imported by `ae0306fd` (2023-12-08, "Import MIT-licensed Unix dc from Plan 9"); `bc.y` last changed by `e1a122be` | Plan 9 Foundation 2021, MIT-style (`usr.bin/bc/LICENSE`, `usr.bin/dc/LICENSE`) | 50,167 bytes (`dc.c` 36,729, `bc.y` 13,438) | measured a.out 28,948 + 19,316 | recommended base |
| PicoCalc vendor firmware | `github.com/clockworkpi/PicoCalc` `Code/`, commit `fdcfe2c87a56b6dbcd540e91de1161d483872af9` | no `LICENSE` file within depth 3 of the repository root; per-subproject notices only | see below | not applicable | no calculator program |

Vendor repository details:

- `Code/` holds `FUZIX`, `MP3Player`, `NES`, `PicoMite`, `pico_multi_booter`,
  `picocalc_helloworld`, `picocalc_kbd_tester`, `picocalc_keyboard`,
  `picocalc_lvgl_graphics_demo`, `picocalc_zero` and `uLisp`. A find for
  `*calc*` returns only the board-support directories. The one calculator-like
  surface is the MMBasic immediate mode inside
  `pico_multi_booter/picomite`.
- `Code/pico_multi_booter/picomite/MATHS.c` is 114,025 bytes: matrix and vector
  commands, FFT, complex numbers, statistics (`MEAN`, `SD`, `SUM`, `CORREL`,
  a 51x15 chi-square table), AES and sensor fusion. It depends on the MMBasic
  runtime (`getargs`, `evaluate`, `MMFLOAT`), so no function lifts out
  cleanly. Its header is BSD-3-Clause text with two extra conditions: clause 3
  requires the name MMBasic in documentation and the original copyright message
  on the console at startup, and clause 4 requires an advertising
  acknowledgement. Disposition: read-only reference for which statistics
  functions a calculator exposes; import no code.
- `Code/uLisp` holds a patch and a README for `technoblogy/ulisp-picocalc`. A
  Lisp REPL is a possible expression front end; it is not evaluated here.
- `Code/picocalc_lvgl_graphics_demo` shows the vendor's own approach to the
  frame-buffer-less panel: LVGL 8.3.0, 16-bit color, a 48 KB heap, a 10-row
  strip buffer (`lv_port_disp_picocalc_ILI9488.c` line 71), and a panel clock of
  25 MHz (`lcdspi/lcdspi.h` line 7) with `COLMOD` 0x66 (RGB666,
  `lcdspi/lcdspi.c` line 606). The demo reads GRAM back at 6 MHz
  (`lcdspi.c` lines 118-126), so the panel's MISO line works. LVGL is MIT
  licensed (GitHub license API); the demo is not evaluated for import.

Conclusion: the recommended base is the tree's own Plan 9 pair. The NetBSD
Phil Nelson `bc` is the fallback if the two-process pipe proves too slow on the
board, at 57 KB with a `getopt_long` shim.

## 3. Calculator items: features mapped to sources

The application is a menu of items. The menu launcher already exists:
`usr.bin/menu` (10,700-byte a.out, raw ANSI, `etc/menu` entries run through
`/bin/sh -c`). One program `calc` carries the four-function, RPN, scientific,
base-conversion, unit, statistics and graphing items and selects among them
with the controller's F1-F10 keys, which arrive as xterm sequences
(`sys/arch/rp2040/dev/picokbd_map.h`). Arbitrary precision stays in the
separate `bc` program that `etc/menu` launches. Terminal handling follows `games/gametty.h`
(raw ANSI plus sgtty ioctls) rather than libcurses, whose archive is 60,052
bytes. The new code follows `docs/research/STYLE-GUIDE.md`: a fixed bound on
parser recursion (rule M04) and no variable-length arrays.

| Item | Cheapest implementation that fits | Source | Estimated bytes (text unless stated) |
| --- | --- | --- | --- |
| Four-function with precedence | Recursive-descent or precedence-climbing parser over `double`, recursion bounded at 16 levels, unary minus, right-associative power, `ans` and ten memory registers. ROM arithmetic makes the evaluator cost no soft-float code. | new, original (ISC, as `keen`/`fifteen`) | 1.2 KB parser plus 0.5 KB display; whole `calc` a.out about 20 KB (parser and items 5 KB, libm subset 4.1 KB, libc floor 7.9 KB, `doprnt_float` 2.0 KB, ROM stubs and resolver 0.6 KB, `strtod` 0.6 KB) |
| RPN stack | Four-level stack plus last-x on the same evaluator and the same function table. | new | about 1 KB added |
| Arbitrary precision | Ship the tree's `bc`/`dc` pair. `bc -l` supplies `s`, `c`, `a`, `l`, `e`, `j` at the requested `scale`. | `usr.bin/bc`, `usr.bin/dc` | 48,264 bytes a.out, about 45 blocks packed |
| Scientific functions | `libm` members over ROM double arithmetic: sin, cos, tan, asin, atan, exp, log, pow, sqrt, sinh, tanh. | `lib/libm` (measured) | 4,092 bytes linked; per call about 3,000 cycles estimated (24 us at 125 MHz); the ROM transcendental entries would cost about 1,618 cycles for sin (datasheet, 13 us) |
| Fixed-point alternative | Q16.16 only where range is bounded (graph coordinates). Not a general calculator path: Cortex-M0+ has no hardware divide and no 32x32 to 64 multiply. | `float-libs.md` section 4(a) | 300-500 bytes per helper set |
| Base conversion and bitwise | 32-bit unsigned integer mode: `and`, `or`, `xor`, `not`, shifts, rotate, display in hex, octal, binary (32 digits fit 53 columns) and decimal. Thumb-1 has native 32-bit logic. `bc` `ibase`/`obase` and `dc` `i`/`o` cover arbitrary width. | new; `printf` (shipped in `utilbox`) for one-shots | about 0.7 KB |
| Unit conversion | Table-driven converter, about 60 units in 8 dimensions, factor and offset per entry (temperature needs the offset). Do not port `units(1)`: the V7 lineage reads a 601-slot hashed table at run time and the upstream tables are 8.6 KB (2.11BSD), 13.8 KB (OpenBSD) and 16.9 KB (NetBSD) of data plus 6-22 KB of C. | new; factors from NIST SP 811 | about 1.5 KB code plus 1.2 KB table |
| Statistics | Welford running mean and variance, sample standard deviation, least-squares line, and correlation over up to 64 samples (512 bytes of bss). The shipped awk covers one-off sums with no new bytes. | new; function list from the PicoMite manual, no code | about 1.0 KB |
| Graphing | Evaluate f(x) at 106 columns and plot into a sextant-mosaic cell bitmap (staged plan, mosaic graph item), or at 320 columns on the pixel device. At about 3,000 cycles per evaluation, 320 points cost 7.7 ms (estimate), far below the panel time. | new; reuses the evaluator | about 1.5 KB plus the kernel interface in section 4 |

Notes:

- Zero-byte path today: the shipped awk and `expr` already evaluate integer
  expressions and `log`, `exp`, `sqrt`, `int` with no new code. awk's program
  text is an interpreter input, so a shell wrapper that splices user text into
  an awk program hands the user an interpreter; keep that wrapper out of
  anything but a single-user console.
- The `calc` evaluator needs `-lm` at cross-build time only. The glue is a
  `Makefile` with `LIBS= -lm -lc` as `usr.bin/awk/Makefile` line 17 has, and
  `PRINTF_FLOAT=yes`.

## 4. A GUI on a panel with no frame buffer

### What the hardware gives

All timing below is derived from `st7365p.c`: SPI1 at 12.5 MHz (prescale 2,
SCR 4 from a 125 MHz peripheral clock), eight-bit frames, RGB666 in three
bytes per pixel (`COLMOD` 0x66), write-only use of the bus, chip select and D/C
on SIO. A byte takes 0.64 us, a pixel 1.92 us. The console author's figure of
115 us per 6x10 cell equals 60 pixels x 1.92 us (115.2 us); inter-frame gaps
of the PL022 would lengthen real values and are not measured here.

| Operation | RGB666 (present) | RGB565 (estimate, 2 bytes per pixel) |
| --- | --- | --- |
| Window setup (`CASET`, `RASET`, `RAMWR`: 11 bytes) | 7.0 us plus D/C settling | same |
| 6x10 text cell, 60 px | 115.2 us | 76.8 us |
| 8x8 tile, 64 px | 122.9 us | 81.9 us |
| 16x16 sprite or tile, 256 px | 491.5 us | 327.7 us |
| One 8-pixel tile row of 40 tiles | 4.92 ms | 3.28 ms |
| One pixel line of 320 px | 614 us | 410 us |
| Full 320x320 repaint, 102,400 px | 196.6 ms (5.1 per second) | 131.1 ms |
| Keen-style 20x13 view of 16x16 tiles, 66,560 px | 127.8 ms | 85.2 ms |
| Viewport 20x20 of 8x8 tiles (160x160) | 49.2 ms | 32.8 ms |
| Viewport 40x20 of 8x8 tiles (320x160) | 98.3 ms | 65.5 ms |

Three facts bound the design:

- The bus bounds throughput, so the CPU idles in the `spi_put` busy loops
  (`st7365p.c`). Pre-expanding tiles to RGB666 in RAM saves only user-side
  expansion work (about 10 cycles per pixel, 8 ms for a full screen, estimate)
  and costs 3x the RAM. Keep tiles palette-indexed.
- 12.5 MHz is the fastest legal setting at a 125 MHz peripheral clock. The
  panel specification's 66 ns minimum write cycle (cited in `st7365p.c`) allows
  15.15 MHz, and the next divider step, 125 / (2 x 4) = 15.6 MHz, gives 64 ns.
  The vendor LVGL demo runs the same panel at 25 MHz (`lcdspi.h` line 7). A
  25 MHz console would halve every time above. That choice belongs to the
  console owner and needs a board run.
- RGB565 over four-wire SPI is a comment in `st7365p.c` (page 96 of the panel
  specification), not a measurement. The vendor LVGL demo initializes the
  panel with 0x66. Test `COLMOD` 0x55 on the board before relying on the
  RGB565 column.

The panel has hardware vertical scroll only. `st7365p.c` programs `VSCRDEF`
with a 320-line scroll area and a 160-line bottom fixed area, and requires
`MADCTL` MV clear. There is no horizontal pan. A side scroller therefore pays
a viewport repaint per scroll step, using the table rows above.

Other fixed costs: the keyboard FIFO yields one event per exchange, and an
exchange spans two 16 ms callouts (`KBD_POLL_MS` 16 in `picokbd.c`), so the
design ceiling is one event every 32 ms (about 31 per second); a key press and
its release take two exchanges. `picokbd_map.h` defines the PRESSED, HOLD and
RELEASED states, and `picokbd_translate()` reduces them to xterm bytes, so a
game sees auto-repeat presses but no key-up. The raw key event ioctl
`LCDIOC_GETKEY` (staged plan item 10, below) exposes the states.

### Techniques mapped to this panel

| Technique | Use on the PicoCalc | Cost |
| --- | --- | --- |
| Tile map | A map of one-byte tile numbers; the panel draws only tiles that changed. Text mode already is one: `cells[32][53]` plus `attrs[32][53]` in `lcdterm.c`, 3,392 bytes. | map 1,696 bytes at 6x10, 1,600 at 8x8 |
| Tile cache uploaded once | Pixel bitmaps live in a small atlas. Palette-indexed 4 bits per pixel makes an 8x8 tile 32 bytes; 128 tiles are 4,096 bytes. | kernel RAM 4 KB, or user RAM if the kernel only streams |
| Dirty rectangles | A shadow map in user space (1,600 bytes) and a changed-tile bitmap (200 bytes). Emit one window per run of changed tiles in a row; one window costs 7 us and a one-tile run costs 123 us, so the overhead is 5.7 percent. | user RAM 1.8 KB |
| Palette swap | A 16-entry palette of three bytes (48 bytes) applied when a tile is expanded. There is no hardware color lookup in use, so a swap repaints the tiles that use the changed entries. A 16-bit mask per tile (256 bytes for 128 tiles) names them. | up to a full repaint in the worst case |
| Sprites by window writes | The panel cannot be read back at speed (6 MHz in the vendor demo, 4 us per pixel, slower than a write). Composite in user memory: the background tiles under the sprite plus the sprite pixels with a color key, then one window write. A 16x16 sprite spanning up to 24x24 pixels costs 576 px x 1.92 us = 1.1 ms. | 8 sprites per frame is 8.8 ms |
| Hardware vertical scroll | Ring of physical lines, as `lcdterm.c` does for text: write the line that just left the screen, then move `VSCSAD`. One pixel line of scroll costs 614 us of fill plus 3 bytes (1.9 us) of command. `VSCRDEF` with a nonzero top fixed area keeps a status bar still while the rest scrolls; the console never programs that value, so it is untested. | a 60 pixel-per-second vertical scroller uses 37 ms of bus time per second |
| Reference designs | Commander Keen's refresh manager marks tiles and notes that "scrolling more than one tile / refresh forces a total redraw" (`id_rf.c` header comment in the Keen Dreams source, `github.com/keendreams/keen` at `4348b4719be87a0b873f5f4f77b4de82ddf7050c`, GPLv2 or later: read for technique only). The Game Boy documents tile maps, tile data and palettes at `gbdev.io/pandocs/`. Jill of the Jungle has no published source this note could read, and no claim about its internals is made. | not applicable |

### User-definable glyphs in the text console

The console cell store is the tile map. `lcd_draw_cell()` draws a glyph from
the 95-entry `lcdfont` table (6 columns by 9 rows in a 6x10 cell) and
substitutes `?` for every other code, and `lcdterm_putc()` maps any byte above
0x7e to `?`. Those two points are the extension seams.

Proposal:

- A RAM bank of 128 glyphs, 9 bytes each, 1,152 bytes of bss (estimate),
  indexed by cell codes 0x80-0xff. `lcd_draw_cell()` reads the bank for codes
  at or above 0x80.
- An escape-reachable window onto the bank: reuse the DEC special graphics
  switch the terminal already implements (`ESC ( 0`, `gfx` in `lcdterm.c`).
  Today it maps 0x5f-0x7e through `acs_map[]` to ASCII look-alikes. In the
  proposal those 32 codes map to bank entries 0-31, initialized with the real
  line-drawing shapes. Curses programs then get proper box drawing with no
  change. Private selectors `ESC ( 1` to `ESC ( 4` would address banks of 32.
- One ioctl on the console tty, `LCDIOC_SETGLYPH`, carries `{ u_char code;
  u_char rows[9]; }` (10 bytes, inside the 8-bit `IOCPARM` length).
- Text and tiles share the cell store, the 16/8 color attributes, the scroll
  ring, the cursor and the `check-picocalc-host` model panel. Nothing needs a
  mode switch or a restore path.

Costs (estimates): 300-500 bytes of text, 1,152 bytes of bss, against a
measured PICOCALC kernel flash headroom of 11,500 bytes (130,816 - 119,316,
`size` on the linked `unix`). A tile redraw costs one cell, 115 us. A full-screen update
costs 1,696 cells = 195 ms.

Implementation (staged plan item 6): the bank, the DEC set mapped onto bank
codes 0x80-0x9f and `LCDIOC_SETGLYPH` landed as designed, with three
decisions the proposal left open. A raw byte of 0x80-0x9f stays `?` (C1
controls), so the line-drawing codes are reachable only through `ESC ( 0`
and raw bytes 0xa0-0xff address the rest of the bank; the bank starts with
`?` in those codes. The tenth cell row repeats the ninth so strokes join
across cell rows. An upload redraws the visible cells that show the code.
Measured on the PICOCALC kernel: text +648 bytes, bss +1,152 bytes
(`sys/arch/rp2040/doc/PICOCALC.md`, Glyph bank), against the estimate of
300-500 bytes of text; the shape table accounts for 288 of them. The
`ESC ( 1` to `ESC ( 4` bank selectors are not implemented.

Limits: two colors per cell, 6x10 granularity, no sub-cell sprite movement.
That is the capability of a DOS text-mode game, enough for the calculator's
keypad, bar graphs, a tile puzzle and a mosaic graph.

Sextant mosaic graph: 64 glyphs cover every on/off combination of a 2x3
sub-cell grid per cell. That gives 106x96 plot points at 53x32 cells with the
bank above and no pixel device. Cell color carries the axis and the curve
(estimate: 1.3 KB for the 106x96 bit array).

### A pixel device, `/dev/lcdgfx`

Use a second minor device with exclusive open. Opening switches the panel to
graphics mode; the last close (including process exit) repaints text. Do not
use an ioctl on the console tty for the switch: init and the login shell hold
the tty open, so its close never runs when a graphics program crashes. The
RetroBSD design this follows is `legacy/non-arm/mips-pic32/sys/sys/gpanel.h`
(commit `7771c72b` last touched it) with its driver
`legacy/non-arm/mips-pic32/sys/arch/pic32/dev/gpanel.c`; both carry an
HPND-style permission notice by Serge Vakulenko (2015) that travels with any
reused text.

RetroBSD's commands (`gpanel.h`): `GPANEL_CLEAR` (`gpanel_clear_t`: color,
xsize, ysize), `PIXEL` (color, x, y), `LINE` (color, x0, y0, x1, y1), `RECT`
and `FILL` (`gpanel_rect_t`: color, x0, y0, x1, y1), `CIRCLE` (color, x, y,
radius), `IMAGE` (x, y, width, height, `const unsigned short *image`), `CHAR`
(font pointer, color, background, x, y, sym) and `TEXT` (font pointer, color,
background, x, y, `const char *text`). The scalar fields are `int`. Three
structures hold user pointers nested inside the ioctl payload: `IMAGE` (the
pixel array), `CHAR` (the font) and `TEXT` (font and string). The RP2040 ioctl path copies in only the
structure
(`docs/research/rp2040-ioctl-input-payload-ownership.md`: a finite classifier of
shipped commands, each needing a fixture case), so nested pointers would need a
second span-checked copy in every handler. Replace pointers with `write(2)`
streams, which move through `uiomove`.

Proposed command set (all structures are fixed size and at most 12 bytes; the
mapping to RetroBSD is in the right column):

| Command | Payload | Effect | RetroBSD analog |
| --- | --- | --- | --- |
| `LCDG_FILL` | `{ short x0, y0, x1, y1; u_short color; }` | filled rectangle; the whole screen is the clear | `GPANEL_CLEAR`, `FILL` |
| `LCDG_PIXEL` | `{ short x, y; u_short color; }` | one pixel | `GPANEL_PIXEL` |
| `LCDG_LINE` | `{ short x0, y0, x1, y1; u_short color; }` | Bresenham line | `GPANEL_LINE` |
| `LCDG_FRAME` | rectangle payload | outline | `GPANEL_RECT` |
| `LCDG_CIRCLE` | `{ short x, y, r; u_short color; }` | midpoint circle | `GPANEL_CIRCLE` |
| `LCDG_WINDOW` | `{ short x, y, w, h; u_char fmt; }` | opens a window; the next `write(2)` supplies `w x h` pixels in RGB565 (fmt 0), palette indexes (fmt 1) or 1 bit per pixel with the current foreground and background (fmt 2) | `GPANEL_IMAGE`, `CHAR`, `TEXT` without pointers |
| `LCDG_PALETTE` | `{ u_char index, r, g, b; }` | sets one of 16 palette entries | none |
| `LCDG_TILESET` | `{ u_char first, count; }` | the next `write(2)` supplies `count` tiles of 32 bytes (8x8, 4 bits per pixel) into the kernel atlas | none |
| `LCDG_TILEWIN` | `{ u_char tx, ty, tw, th; }` | the next `write(2)` supplies `tw x th` tile numbers, row-major; the kernel emits one window per tile row and interleaves the eight pixel lines of every tile in the row | none |
| `LCDG_SCROLL` | `{ short line; }` | `VSCSAD` start line | none |
| `LCDG_SCRDEF` | `{ short top, bottom; }` | `VSCRDEF` fixed areas (top + scroll + bottom = 480) | none |
| `LCDG_COLMOD` | `{ u_char fmt; }` | 0x66 (RGB666) or 0x55 (RGB565); the console restores 0x66 on exit | none |
| `LCDG_INFO` | `{ short w, h; u_char tiles, bpp; }` (out) | panel size and atlas capacity | `GPANEL_CLEAR` returned the size |

Colors in the API are RGB565 values as in RetroBSD (`0xf800` red, `0x07e0`
green); the kernel widens each to RGB666 by bit replication.

Cost estimates for the kernel side:

| Item | Estimate |
| --- | --- |
| Text: dispatch 300 B, fill 120, pixel 60, line 200, frame 60, circle 250, window stream 150, palette 60, tile set 120, tile window 300, scroll and define 100, mode switch and repaint 200, open and close 200 | about 2.1 KB (range 2-3 KB) |
| Bss: atlas of 128 tiles x 32 B = 4,096 B, palette 48 B, window state about 16 B | 4.2 KB (8.3 KB with 256 tiles) |
| Stack: a 64-byte chunk buffer for `uiomove` | 64 B of kernel stack on a 3 KB u-area |
| `BOOT-MAP.md` region: kernel data and bss 106 KB, the document states 39,608 bytes used | headroom looks ample; measure with `size` on the real kernel |

Implementation (staged plan item 8): the device landed for the subset of
commands the plan names, without the tile atlas. Decisions the proposal left
open: the device is minor 1 of the console's character major 10, which the
console entry points hand to `lcdgfx.c`, so `conf.c` and its structural gate
keep their tables; coordinates are rejected with `EINVAL` before any panel
write, not clipped, and every field is unsigned so a negative value fails the
same test; `LCDG_WINDOW` carries the palette indexes `fg` and `bg` that the
1 bpp format draws with, in place of "the current foreground and background";
`LCDG_SCRDEF` also moves the scroll start to the top of the scroll area; the
palette is RGB565. Measured on the PICOCALC kernel under GCC 13.2 at `-Os`:
text +2,145 bytes (the estimate was about 2.1 KB), bss +72 bytes (estimate
0.1 KB); PICOCALC_PSRAM text +2,145 bytes (`sys/arch/rp2040/doc/PICOCALC.md`, Pixel device). The
board runs, the timing comparison and the kill test remain open;
`tests/rp2040/lcdgfxtest` is the program for them.

Implementation (staged plan item 9): the 128-tile atlas, `LCDG_TILESET` and
`LCDG_TILEWIN` landed in `lcdgfx.c` as designed, with these decisions. The
atlas is cleared at every open, as the palette is reset, so a program never
sees the tiles of the one before. A tile row is drawn when its last number
arrives: the driver collects the row's numbers in a 40-byte buffer, opens one
panel window of `tw * 8` by 8 pixels and sends line 0 of every tile, then line
1, and so on, through the palette in force at that moment; the window closes
before `write(2)` returns, so a tile stream never holds chip select between
system calls. A tile number of 128 or more fails the `write(2)` with `EINVAL`
before its row is drawn, keeps the rows already drawn and ends the block. An
upload streams through `uiomove()` straight into the atlas with no staging
buffer, and the bytes that arrived stay if another command abandons it. Both
ioctls reject out-of-range fields with `EINVAL` before any panel write.
`LCDG_INFO` reports 128 tiles. The tile library is `games/gametile.h`, a
header of static functions in the manner of `games/gametty.h` and
`games/gameterm.h`, because a header adds no archive, no manifest entry and
no link order, and it costs a program only the functions it calls: a 1,808-byte
structure holds the shadow map (1,600 bytes) and the bitmap of unsent tiles
(200 bytes). Its `gtile_flush()` sends one `LCDG_TILEWIN` per run of marked
tiles in a row and never bridges a gap, because a tile on the bus (123 us)
costs more than a window setup (7 us); the system-call overhead of a run is
the unmeasured term that could move that rule. Measured on the PICOCALC
kernel under GCC 13.2.1 at `-Os`: the atlas costs 440 bytes of text and 4,164
bytes of bss in `lcdgfx.o` (4,096 of atlas, a 40-byte row buffer and 28 bytes
of stream state), against the estimate of about 0.5 KB of text and
4.2 KB of bss (`sys/arch/rp2040/doc/PICOCALC.md`, Tile atlas). The board
timings of the staged plan (40 tiles in 4.92 ms, a tile row of scroll in 4.9
ms) remain open; `tests/rp2040/tiledemo` is the program for them.

Implementation (staged plan item 10): the raw key events landed as an
explicit session, because a queue that always records would keep the
keystrokes of a password prompt for any later reader. `LCDIOC_KEYEVS` starts
or ends a session on the open console file; recording starts only then, from
a descriptor open for reading in the tty's foreground process group, and the
queue is emptied and its bytes cleared at the start and the end. `LCDIOC_GETKEY`
returns up to six `{ state, key }` events from a 32-event queue
(`picokbd_evq.c`) to the same kind of caller, answers a write-only caller with
`EBADF` and a background caller with `EIO` (never `SIGTTIN`), and leaves the
queue alone when it refuses. An explicit start was chosen over enabling on
the first drain because a drain stays free of side effects and a refused
drain cannot create a session. The session ends when its open file is closed
for the last time, which needs a hook in `vn_closefile()` because a character
driver's close entry runs only when the device is closed everywhere; process
exit passes through it. It also ends when the owner ends it and when the
tty's foreground group differs from the owner's, a comparison the poller
makes before each event and each request makes before it acts. The keyboard
poller fills the queue for every controller event before it translates the
event to xterm bytes, so the translated path and the queue see the same
events and neither waits on the other. A full queue drops its oldest event
and counts the drop for the next call: a game that tracks held keys needs the
newest state more than the oldest, and `lost` tells it to rebuild. The ioctls
never block. The queue lives in its own file so that `picokbd_map.c` keeps
no state. The text cost is 392 bytes in `picokbd_evq.o` with 88 bytes of bss,
and 204 bytes in `lcdcons.o`. The board measurement of the event rate against
the predicted 31 per second is open; `PICOCALC.md` (Keyboard) records that the
poller runs 2 ms apart once an event arrives, so the exchange rate is higher
than the 32 ms ceiling this note derived from the 16 ms idle poll.

Atlas in the kernel versus in user space: the bytes for a full-screen tile
redraw are 1,600 for tile numbers against 204,800 for RGB565 pixels, a 128x
difference in `write(2)` payload. Both paths remain bus bound (about 13 ms
of copying at 8 cycles per byte against 197 ms of bus time, estimate), so the atlas costs 4 KB of
kernel RAM to save user-side expansion and syscall chunks. Recommendation:
keep the atlas in the kernel at 128 tiles; the user process keeps its 144 KB
window for the app and the shadow map.

Coexistence with the text console:

- Open of `/dev/lcdgfx` sets `gfx_active`. `lcdterm_putc()` keeps updating the
  cell store (so a background process that writes to the console loses no
  text) and skips the three panel primitives (`lcd_draw_cell`,
  `lcd_fill_cells`, `lcd_set_top` in `lcdterm.h`).
- Close restores: `COLMOD` 0x66, `VSCRDEF` to 0 / 320 / 160, `VSCSAD` to the
  ring top times 10, the two spare columns cleared, then 1,696 `lcd_draw_cell`
  calls from the cell store (195 ms) and the cursor overlay. The restore
  function is the one `lcdterm.c` needs for any redraw and does not exist yet;
  it is new code, about 150 bytes (estimate).
- Input stays on the console tty: the program opens `/dev/tty` for the keyboard
  (TIOCGETP/TIOCSETP raw mode, as `games/gametty.h` does) and `/dev/lcdgfx` for
  the panel. Ctrl-C reaches a cooked-mode program as usual; a raw-mode program
  ends on its own key or is killed from another session, and either exit
  closes the device and restores the text.
- The ioctl classifier in the syscall layer (the finite list in
  `docs/research/rp2040-ioctl-input-payload-ownership.md`) gains one entry per
  command above, each with a fixture case; a command left out of the
  classifier reaches the driver with a raw user pointer.

### Reference points from the RetroBSD examples

`legacy/non-arm/mips-pic32/share/examples/gpanel` holds `circle.c`, `color.c`,
`fill.c`, `flappy.c` (9,908 bytes), `font.c`, `line.c`, `pixel.c`, `rect.c`,
`speed.c`, `tft.c`, `tftetris.c` (21,451 bytes) and a fonts directory of 14 entries (1,357,378
bytes with BDF sources). `tftetris.c` is GPLv2 or later (header text), so it is
not importable. `flappy.c`, `tft.c`, `speed.c` and the example fonts carry no
license text in their first 14 lines, so their provenance is unverified; the
`6x9.c` font header states "Public domain font". `lib/libgpanel` (12 small
C files, each a thin wrapper that calls `ioctl` for one primitive, plus
`fill_triangle.c` and `text_width.c`) carries the Vakulenko notice. The
legacy archive builds nothing (`legacy/non-arm/README.md`), so any reuse is a
copy into a maintained path with the notice preserved.

## 5. Staged import plan

Ordered by dependency. Sizes are the marks defined at the top; the acceptance
test names the existing gate where one exists, or the class of check where a
new gate is needed. Board steps run only with the repository's exact opt-in.

| Order | Item | Source and license | Size | Dependency | Acceptance test |
| --- | --- | --- | --- | --- | --- |
| 1 | Ship the arbitrary-precision calculator as a manifest closure `calc-bignum` | `usr.bin/bc` (13,438 + `lib.b` 2,742), `usr.bin/dc` (36,729); Plan 9 Foundation 2021 MIT-style; add a `NOTICE` section (the current `NOTICE` has no Plan 9 entry) | 48,264 bytes a.out measured; about 45 blocks packed (estimate); `lib.b` installs to `/usr/share/misc` | none; profiles file needs a `needs path /usr/share/misc/lib.b` line | `bmake MACHINE=rp2040 check-fs-profiles` with a new selftest case that rejects `bc` without `dc` (the documented dangling-link rule); then a board run (opt-in) of `echo "scale=20; 4*a(1)" \| bc -l` expecting the first 19 decimal digits `3.1415926535897932384` (the twentieth depends on the library's rounding); any differing digit among them, or a latency over one second for `echo 2+2 \| bc`, rescopes to the NetBSD `bc` fallback. Execution on the target is not run |
| 2 | Verify the `PRINTF_FLOAT=yes` setting in `usr.bin/bc/Makefile` | tree | removes up to 2,028 bytes if the link still succeeds (estimate) | the arbitrary-precision closure | rebuild `bc` without the variable; `bmake MACHINE=rp2040 logged-build` stays warning free; `bc` still prints a 20-digit result |
| 3 | `calc` text-mode engine: infix, RPN, programmer, constants, memory | new, original; ISC header | about 20 KB a.out (estimate: 5 KB own code, 4.1 KB libm, 7.9 KB libc floor, 2.0 KB float print, 1.2 KB ROM stubs and `strtod`) | `lib/libm.a` at cross-build time; no kernel change | host test through a pty, as `games/keen/tests` does: 40 or more expression cases (precedence, unary minus, right-associative power, division by zero, domain errors, recursion bound of 16 rejected cleanly) compared with Python `float` to 1e-12; a mutant that swaps `*` and `+` precedence must fail; the same table under qemu-user on the cross-built a.out; calibrate against a known-bad build that omits the bound |
| 4 | Unit and statistics items inside `calc` | new; factors from NIST SP 811 | about 4 KB (1.5 + 1.2 table + 1.0 stats + 0.3 glue; estimate) | the `calc` engine | 20 conversion pairs checked against NIST values to 1e-9 relative; statistics against Python `statistics` on 3 fixed vectors to 1e-12; the temperature offsets (C, F, K) must round-trip |
| 5 | Menu entry and F-key item bar | `usr.bin/menu`, `etc/menu`; tree license | one line in `etc/menu`; item bar about 0.3 KB | the arbitrary-precision closure and the `calc` engine | pty test that F1-F10 sequences select items (sequence table in `picokbd_map.h`); `check-picocalc-host` unchanged |
| 6 | Glyph bank in the text console and real line-drawing glyphs | new, under `sys/arch/rp2040/dev`, `Copyright (c) 2026 DiscoBSD` ISC header per the project rule | 300-500 B text, 1,152 B bss (estimate) | the console files in this branch; one ioctl classifier entry plus fixture case | extend `check-picocalc-host`: upload a glyph, print its code, require the model panel to record the bank bytes for that cell, require the scroll ring and the existing cases to stay identical; a mutant that ignores the bank must fail; `lcdfont.c` check stays green |
| 7 | Sextant mosaic graph in `calc` | new | about 1.5 KB (estimate) | the `calc` engine and the glyph bank | host test renders `y = x` and `y = sin(x)` into the 106x96 bit array and compares with a golden array; the cell conversion is checked against the 64-glyph table |
| 8 | `/dev/lcdgfx` device: `LCDG_FILL`, `PIXEL`, `LINE`, `FRAME`, `CIRCLE`, `WINDOW`, `PALETTE`, `SCROLL`, `SCRDEF`, `COLMOD`, `INFO`; exclusive open with text restore on close | new; Vakulenko notice kept where RetroBSD code is reused | about 2.1 KB text, 0.1 KB bss for this subset (estimate) | the `lcdterm_repaint()` function; ioctl classifier entries and fixtures; a device number in `conf.c` and a `MAKEDEV` line | host model panel records every window, scroll start and color format; board run (opt-in) with predicted times: full fill 196.6 ms at RGB666 (a deviation above 15 percent falsifies the model and points at PL022 inter-frame gaps); crash test: a program that is killed in graphics mode must leave the text visible; `check-kernel` fixtures for each command |
| 9 | Tile atlas and `LCDG_TILESET`, `LCDG_TILEWIN`; userland tile library with shadow map, dirty bitmap and run emission | new | kernel 4.2 KB bss (128 tiles), about 0.5 KB text; library about 1 KB | the pixel device | model panel check that a dirty run of N tiles produces one window per tile row with tiles interleaved by pixel line; board timing: 40 tiles in 4.92 ms predicted; a sample app (a tile puzzle and a pannable map) with a vertical-scroll step of one tile row costing 4.9 ms |
| 10 | Raw key events ioctl for games | new, `picokbd.c` plus `picokbd_map.h` states | about 0.2 KB (estimate) | the pixel device | host test over `picokbd_translate()` states; board measurement of the key event rate against the predicted 31 per second |
| 11 | Graph mode on the pixel device | new | about 1 KB over the mosaic graph (estimate) | the `calc` engine and the pixel device | host golden comparison of the plotted pixels; board capture of the first plot time (predicted: 320 evaluations 7.7 ms plus 320 pixel windows 2.9 ms) |
| 12 | Optional: raise the panel clock to 25 MHz and test RGB565 | console owner's decision | none | board runs only | `COLMOD` 0x55 and SPI 25 MHz each tested alone on the board; the pixel-correctness capture decides; halve the table times only after both pass |

## Risks and open decisions

- Two-process `bc | dc` under a single 144 KB user window. Not measured. The
  falsifier is stated in the arbitrary-precision closure row of the staged plan.
- The `bc` bss of 22,188 bytes is mostly `bspace`. Lowering `bsp_max` is a
  small source edit; it changes the maximum program size and needs a test.
- RGB565 over four-wire SPI and a 25 MHz clock are not validated for this
  panel; both are decisions for the console owner. The console's HEAD uses
  12.5 MHz and RGB666 deliberately.
- Kernel flash headroom: the PICOCALC kernel links at 119,316 bytes of text
  against the 130,816-byte FLASH region of `conf/RP2040.ld`, leaving 11,500
  bytes at `-O`. At `-Os` under GCC 13.2 the pixel device (item 8) costs
  2,145 bytes of text on both PicoCalc kernels and PICOCALC_PSRAM keeps
  16,936 bytes of the region (`PICOCALC.md`, Pixel device); the tile atlas and
  the raw key events (items 9 and 10) add 1,088 bytes of text and 4,256 bytes of
  bss, leaving PICOCALC_PSRAM 15,856 bytes and PICOCALC 18,072 under the same
  compiler (`PICOCALC.md`, Tile atlas). The kernel data and bss end at
  0x2002f428 on PICOCALC_PSRAM, 62,424 bytes below the 106 KB RAM region's
  end at 0x2003e800.
- Shipping `bc` and `dc` changes the full manifest, which
  `docs/research/c17-shipped-resource-baseline.json` pins by hash and by
  packed program; item 1 therefore lands with a re-recorded baseline from a
  distribution run, in its own change.
- No claim about the vendor firmware's performance or the ST7365P beyond the
  files named is made. The panel specification (`ST7365P_SPEC_V1.0.pdf` in the
  vendor repository) is the authority for `VSCRDEF`, `COLMOD` and the write
  cycle.
- Checks not run: any board execution, any qemu-user or Renode run of the
  proposed programs, any link of NetBSD `bc` or Gavin Howard `bc` against this
  tree's libc, any measurement of swap behavior for a `bc | dc` pipeline.

## Source index

- Tree: `usr.bin/bc`, `usr.bin/dc`, `bin/expr`, `games/factor`,
  `games/primes`, `games/arithmetic`, `usr.bin/cal`, `usr.bin/calendar`,
  `usr.bin/basic`, `usr.bin/awk`, `usr.bin/menu`, `etc/menu`, `lib/libm`,
  `lib/libc/arm/gen/rom_float_*.S`, `distrib/rp2040/mi.rp2040`,
  `sys/arch/rp2040/doc/PROFILES.md`, `STORAGE.md`, `BOOT-MAP.md`,
  `sys/arch/rp2040/doc/research/float-libs.md`, `v7-tools.md`, `posix-tools.md`,
  `zswap.md`, `docs/research/licensing.md`, `STYLE-GUIDE.md`,
  `rp2040-ioctl-input-payload-ownership.md`, `rp2040-ioctl-command-geometry.md`,
  `sys/arch/rp2040/dev/{st7365p.c,lcdterm.c,lcdterm.h,lcdfont.h,lcdcons.c,picokbd.c,picokbd_map.h}`,
  `legacy/non-arm/mips-pic32/{sys/sys/gpanel.h,sys/arch/pic32/dev/gpanel.c,lib/libgpanel,share/examples/gpanel}`.
- OpenBSD: `https://github.com/openbsd/src/tree/master/usr.bin/dc`, `bc`,
  `units`.
- NetBSD: `https://github.com/NetBSD/src/tree/trunk/usr.bin/dc`,
  `usr.bin/units`, `external/bsd/bc/dist`.
- FreeBSD and Gavin Howard: `https://github.com/freebsd/freebsd-src/tree/main/contrib/bc`,
  `https://github.com/gavinhoward/bc` (tags `7.0.3` = `c8dabac65d39fd527a9264a5e505a99b1d8df3d8`,
  `7.1.0` = `70c51d95e02d20fc3fc82791dc3f1aa0e9d47f0d`), `manuals/build.md`.
- 4.3BSD: `https://github.com/dspinellis/unix-history-repo` tag `BSD-4_3`.
- Vendor: `https://github.com/clockworkpi/PicoCalc` at
  `fdcfe2c87a56b6dbcd540e91de1161d483872af9`, `Code/pico_multi_booter/picomite/MATHS.c`,
  `Code/picocalc_lvgl_graphics_demo`, `Code/uLisp`.
- Reference designs: `https://github.com/keendreams/keen` (`id_rf.c`),
  `https://gbdev.io/pandocs/`.
