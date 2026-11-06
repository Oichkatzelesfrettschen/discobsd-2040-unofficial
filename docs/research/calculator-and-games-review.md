# Calculator and games: review ledger

Status: advisory review, 2026-11-05, of the uncommitted work on branch
`picocalc/calculator-and-games` (base `main` at `076675a8`). Drafted by an AI
assistant acting as reviewer; every finding below names the file and line it
rests on and is marked demonstrated (a command, probe or source read in this
review shows it) or suspected (an inference with the evidence that would
settle it). Severity: blocker (the PR cannot merge green or breaks a hard
rule), major (a claim, gate or decision that must change before the work is
relied on), minor (a defect or gap with a contained consequence).

## Scope and inputs

Reviewed, all untracked in the worktree:

- `lib/libcalc` (14 objects, `mathematics.h`, `calc_internal.h`) and
  `tests/libcalc`; specification `docs/research/picomite-maths-clean-room-spec.md`;
  record `docs/research/libcalc-implementation.md`; copying review
  `docs/research/copy-reviews/libcalc-vs-picomite-maths.txt`.
- `lib/libbn` (`bn.c`, `bn.h`) and `tests/libbn` including `dc-differential/`;
  record `docs/research/openbsd-dc-bn-port.md`.
- `games/fourfall` and its tests; specification
  `docs/research/falling-block-clean-room-spec.md`; record
  `docs/research/fourfall-implementation.md`; copying reviews rounds 1-3 in
  `docs/research/copy-reviews/`.
- `docs/research/picocalc-games-survey.md`, and the one tracked change
  (`docs/INDEX.md`, one row for `openbsd-dc-bn-port.md`).

Re-run in this review (host Linux 6.18 WSL2, `cc` GCC 16.2.1,
`arm-none-eabi-gcc` 16.2.0, `qemu-arm` 11.1.3, Python 3.14.9, `PYTHON=python3`
exported; outputs landed only in the directories' ignored paths):

| Command | Result |
| --- | --- |
| `bmake MACHINE=rp2040 check check-cross` in `tests/libcalc` | native, ILP32 (`-m32 -msse2 -mfpmath=sse`), ASan+UBSan and tree-libm runs each `PASS calc_test: 5323 checks`; `calc mutants: 28 of 28 rejected`; allocator negative control rejected; per-object text sums to 24,593 bytes, data and bss 0 |
| `bmake MACHINE=rp2040 check check-cross check-qemu` in `tests/libbn` | native, ILP32, sanitized and qemu-arm Thumb-1 runs each `vectors=63550 checks=842292 failures=0 injected_failures=42`; `bn mutants: 14 of 14 rejected`; 64-bit-helper negative control rejected |
| `bmake MACHINE=rp2040 test` in `games/fourfall` | 132 vectors, 1,257,687 checks, 0 failed; 21 of 21 rule mutants and 12 of 12 front-end mutants rejected; `test-play` 11 cases, 151 checks |
| `shellcheck -S error`, `ruff check` on every new script | clean |
| `tools/check_changed_comments.py --working` with the new files marked intent-to-add in a scratch copy of the index (`GIT_INDEX_FILE`) | `violations=0`, 30 advisories (license and file-head comments over 12 lines) |
| `tools/c17/source_denominator.py` under the same scratch index | `FAIL ... tracked paths absent from denominator` (28 new `.c`/`.h` paths) |
| A host probe linked against `lib/libcalc/libcalc_host.a` | results quoted under Correctness |

Not run: any board execution (no hardware opt-in was given); the OpenBSD dc
differential (needs the downloaded dc sources and the owner's approval, which
covered only the original run); a fresh copying comparison against the two
originals (this review checked the reviews' method and conclusions, not the
token runs themselves).

## Clean-room integrity

Verdict: no evidence that either implementer saw an original. Both records
carry explicit files-read lists that exclude the originals; a grep of the
implementations finds none of the section 6.2/6.3 identifiers or constants of
the MATHS specification and none of the reference identifiers, strings or
misspellings of the falling-block checklist; the copying reviews' calibrations
show the mechanical gate separates a renamed copy (100 percent normalized
shingles) from unrelated code (0.4 to 1.6 percent). The provenance rests on
the sessions' own files-read lists; no transcript digest or tool log binds
those lists to the sessions, which is the residual uncertainty of both clean
rooms.

**CR-1 major, suspected legal risk, owner decision: falling-block trade dress.**
The commercial name is absent from the game (demonstrated: case-insensitive
grep of `games/fourfall` finds no hit; the name appears only in the
specification's prohibition at `falling-block-clean-room-spec.md:26`, its
reviewer-only checklist, and the survey). The combination the game presents
-- a 10-wide by 20-high well (`falling-block-clean-room-spec.md`, Board
section), a next-piece preview, a ghost outline on by default
(`fourfall.c:632`, `ghost_on = !testmode` in `main`), one distinct color per
piece, and the letter names `IOTSZJL` shown in monochrome mode
(`fourfall.c:64`, `ob_cell`) -- is the combination that Tetris Holding, LLC v.
Xio Interactive, Inc., 863 F. Supp. 2d 394 (D.N.J. 2012) treated as
protectable look and feel. The copying reviews compared against `tftetris.c`
only and say so (round 3, "Residual uncertainty"). The survey states that
genre mechanics "are not protected expression"
(`picocalc-games-survey.md:288-289`) and that the port uses "no Tetris piece
names" (`picocalc-games-survey.md:281-282`); the first is broader than the
Xio holding and the second contradicts the letter names the game prints. The
ASCII two-column cells differ strongly from Xio's near-identical visual
clone, and BSD systems have shipped `tetris(6)` with the same well for three
decades, so the risk rating is low; a lawyer's opinion or a decision to
change the well width, the ghost default or the piece letters would settle
it. Fix: record the owner's decision in the survey, correct the two survey
sentences, and run the trademark search the specification says was not run
for `fourfall` (`falling-block-clean-room-spec.md:24-25`).

**CR-2 minor, demonstrated: the libcalc implementer read the reviewer checklist.**
`picomite-maths-clean-room-spec.md:1206-1209` labels section 6 "reviewer only"
and then states "The implementer sees this list too, which is intentional".
Section 6 reproduces the original's error-message literals
(`:1265-1294`), distinctive internal identifiers (`:1222-1251`) and code-shape
descriptions with original line ranges (`:1296-1306`), and
`libcalc-implementation.md` ("Provenance statement") confirms the implementer
read the whole document. The falling-block protocol withholds the equivalent
material behind `REVIEWER-ONLY BELOW`. The outcome was reviewed clean, and
short error phrases carry little expression, so the verdict stands; the
protocols differ. Fix: split future revisions of the MATHS specification at
the same marker, and add one sentence to the copying review noting that the
implementer saw section 6.

**CR-3 minor, demonstrated: the libcalc copying review lacks a stated rejection threshold and a same-domain known-good.**
`libcalc-vs-picomite-maths.txt` ("CALIBRATION AND SHINGLE NUMBERS") calibrates
with a renamed copy (100 percent) and one unrelated file, `lib/libbn/bn.c`
(1.61 percent normalized); the candidate measures 1.97 percent, above the only
known-good, and no threshold is declared. The falling-block review declares 5
percent and four known-goods (0.4 to 1.4 percent). The manual pass covers
every shared run, so the clean verdict is supported, but the mechanical
screen as recorded does not establish a discrimination margin. Fix: add a
same-domain known-good (the tree's `lib/libm` sources, or another
independently written numerical file), a restructured-copy known-bad, and a
declared threshold, then rerun the screen.

**CR-4 minor, demonstrated: the falling-block specification carries reviewer information into Part 1.**
`falling-block-clean-room-spec.md:74-79` tells the implementer that an
earlier palette had "six of its seven assignments" matching the reference,
and `:63-67` that the cell order was chosen to avoid the reference
enumeration. Round 1 found the analyst's first Part 1 palette and row-major
cell order matched the reference; the rework replaced them with design
values chosen to differ. Design-around instructions carry no reference
content, so the verdict stands; the sentences go beyond the "region X
matches" channel the protocol names. Fix: move both sentences to Part 2.

**CR-5 minor, demonstrated: the libcalc review's "forced" classifications hold.**
The cross-product components (`calc_vector.c:107-109`), the RFC 4648
alphabet, catalog CRC parameters, MT19937 tempering, and the in-place swap
are fixed by definition. The 54-token PID run (`calc_pid.c:71-73`) is the
Tustin-discretized PID with a band-limited derivative that the specification
states exactly (`picomite-maths-clean-room-spec.md:552-558`); a formula
transmitted by the specification is functional, so "forced" is correct. Two
provenance facts the specification leaves open are likely resolvable
(suspected, moderate confidence; settled by comparing the original against
the candidates): the FFT identifiers `Fft_transformRadix2` and `exptable`
listed at section 6.2 match Project Nayuki's MIT-licensed "free small FFT"
naming, and the original's 14-element PID array matches the field set of a
widely published PID tutorial implementation. Neither affects libcalc, which
copies neither; the specification's section 1.2 should record them as
unverified attributions.

**CR-6 minor, suspected: the survey omits a BSD-licensed falling-block reference.**
`picocalc-games-survey.md` lists `tftetris.c` (GPLv2 or later) as the only
falling-block source. The 4.4BSD-derived `tetris(6)` in NetBSD and OpenBSD
carries the Regents' BSD license (not fetched in this review; a fetch of
`src/games/tetris` settles it). It would have been an importable reference
and a shipped-precedent data point for CR-1. Fix: add a row.

## Correctness and tests

Overall: the three suites discriminate. Vectors come from independent
oracles (the MATHS analyst's Python/mpmath/numpy values, cross-checked
against `cmath` by `gen_complex_vectors.py`; Python integers for libbn;
specification vectors for fourfall), every gate has a known-good baseline and
calibrated mutants, and each mutant's first failing line names a check that
targets the seeded fault. Spot checks of the numerics found the algorithms
sound: CRC presets match the catalog check values the tests assert
(`calc_crc.c:30-40`); the Cody-Waite split in `calc__sincos`
(`calc_util.c:114-134`) keeps `k * piece` exact for the whole reduced range
(33-bit pieces, `k < 2^20` below 1e6 radians); the Stirling coefficients in
`log_gamma` (`calc_stats.c:262-266`) are the Bernoulli terms through a^-11;
a scan of `calc_chi_square_p` over 92,800 (df, chi) points with df up to
5,000 found no non-convergence; the two-pass compensated variance returns
exactly 30 for `{1e9+4, 1e9+7, 1e9+13, 1e9+16}`; libbn's Algorithm D keeps
every product inside 32 bits (the `qh > LIMB_MASK` test short-circuits before
`qh * v[nd-2]`, `bn.c:647-648`), the remainder takes the dividend's sign and
the quotient the product of signs (`bn.c:744-748`), and the add-back mutant,
estimate mutant and normalization mutants are each rejected.

**T-1 major, demonstrated: the libbn record contradicts itself on the differential run.**
`openbsd-dc-bn-port.md:17-18` ("The differential run ... has not run") and the
"Not run" section (`:280-287`) predate the section at `:339-363` that reports
the owner-approved run (2,995 of 3,000 identical). The same document says
"Three behaviors" (`:123`) and later "the four manual-page assumptions"
(`:353`). The "Gates run" bullet claims `check-c17-source-denominator` passes,
which holds only while the files are untracked (see INT-1). Fix: rewrite the
summary and "Not run" against the final state, reconcile three versus four,
and drop the denominator claim. Add that the differential compared an LP64
host dc (64-bit `u_long`, 64-bit libcrypto words) against an LP64 dc with
32-bit `BN_ULONG`; the ILP32 dc the target builds remains unexercised by the
differential (a `-m32` rebuild of the libbn side settles it).

**T-2 minor, demonstrated: the FFT-versus-DFT check does not cover what the record claims.**
`calc_test.c:1512` iterates `for (n = 2; n <= CALC_FFT_MAX; n *= 4)`, so it
runs n = 2, 8, 32, 128, 512 and never 1024, while its comment says "at the
largest supported size" and `libcalc-implementation.md` ("Test evidence")
says "4, 16, 64, 256 and 1024 points with and without the twiddle table". The
branch at `:1520` uses the twiddle table for every n above 2, so the per-stage
`calc__sincos` path is compared with a DFT only through the 4- and 8-point
specification vectors. Fix: iterate n = 4 to 1024 by doubling and run both
twiddle paths at each size.

**T-3 minor, demonstrated: the FFT tolerance is five orders looser than the algorithm.**
The DFT comparison asserts `1e-9 * max(peak, 1)` (`calc_test.c:1535-1536`);
a radix-2 FFT with accurate twiddles attains about `log2(n) * eps * sum|x|`,
near 1e-13 at n = 512 for unit inputs. A twiddle routine wrong in the tenth
digit passes. Fix: keep the specification's 1e-9 for the section 5.11 vectors
and assert a derived bound (for example `64 * DBL_EPSILON * log2(n) * sum|x|`)
in the extra check, stating the derivation beside it.

**T-4 minor, demonstrated: `calc_quat_inverse` forms the squared norm unscaled.**
`calc_quat.c:56-62`. Probe: `(1e-170, 0, 0, 0)` returns `CALC_E_DOMAIN`
although the inverse 1e170 is representable; `(1e200, 0, 0, 0)` returns
`CALC_OK` with w = 0 instead of 1e-200. `calc_magnitude` already scales
(`calc_quat.c:76-77` uses it). Fix: divide the components by the largest
magnitude before squaring, as `calc_magnitude` does, and add both probes as
checks.

**T-5 minor, demonstrated: inverse and solve accept ill-conditioned matrices silently.**
The singular test is `n * DBL_EPSILON * max|a|` on the pivots
(`calc_matrix.c:298-300`), as the specification requires. Probe: the 12x12
Hilbert matrix returns `CALC_OK` with `max|A*inv - I| = 0.0625`; the 10x10
gives 7.6e-5; `[[1, 1], [1, 1 + 1e-15]]` returns `CALC_OK` with an entry near
9e14. A calculator user receives those digits with no warning. Fix: compute a
cheap 1-norm condition estimate from the LU factors (Hager's method) and
either return it through an optional out-parameter or report a distinct
status above `1 / (n * DBL_EPSILON)`; test it with the Hilbert family.

**T-6 minor, demonstrated: non-finite matrix entries take the wrong path.**
Probe: an infinite entry returns `CALC_E_SINGULAR` (the threshold becomes
infinite, `calc_matrix.c:144`), and a NaN entry returns `CALC_OK` with NaN
output (`mag > best` is false for NaN, `:139`). Fix: reject a non-finite
input with `CALC_E_DOMAIN` in `lu_check` and test both.

**T-7 minor, demonstrated: `BN_mask_bits` is stricter than libcrypto.**
`bn.c:333` fails when the value has fewer than n bits, the manual page's
wording; libcrypto fails only when `n / BN_BITS2` reaches the word count. dc's
one caller (`to_ascii`, n = 8 after checking `BN_num_bits > 8`,
`openbsd-dc-bn-port.md:112`) never reaches the difference. A bc port or other
caller could. Fix: state the stricter rule in `bn.h` and re-check callers when
bc is ported.

**T-8 minor, demonstrated: the libbn mutant runner has no time bound.**
`tests/libbn/check_mutants.sh:21` runs each mutant without a timeout; a
mutant that loops (a broken estimate loop) hangs the gate instead of failing
it. `tests/libcalc/check_mutants.sh` uses `timeout 120`. Fix: add the same
bound.

**T-9 minor, demonstrated: fourfall's Makefile defaults the interpreter.**
`games/fourfall/Makefile:10` sets `PYTHON?= python3`, and the recipes at
`:41`, `:44`, `:47` run the scripts without `-I`. `AGENTS.md` ("Build, image,
flash") requires the build to reject an empty interpreter, and the libcalc and
libbn test Makefiles do (`.error PYTHON must name the intended interpreter`).
Fix: drop the default, add the same `.error`, and use `${PYTHON} -I`. The
logic target also hard-codes `-fsanitize=address,undefined` (`:12`, `:31`)
without the `tools/compiler-probe.sh` guard the other suites use, so a host
without the sanitizer runtime fails instead of recording a skip.

**T-10 minor, suspected: `test_timed` depends on wall-clock scheduling.**
`games/fourfall/tests/play.py` asserts a falling piece within 1.4 s at level
9. On a loaded CI runner the pty read can miss the window. A run under
`stress` or a parallel `check-host -j` settles it; if it flakes, widen the
window or drive time through the existing `GAMEBOX_TEST` hook.

## Target fit

**TF-1 major, suspected: the libcalc oracle assumes IEEE semantics the target's arithmetic lacks.**
Every libcalc variant runs on host IEEE hardware. The target routes
`__aeabi_dadd`, `dsub`, `dmul` and `ddiv` to the boot ROM
(`lib/libc/arm/gen/rom_float_*.S`), which flushes subnormal inputs and
outputs and maps NaN results to infinities
(`sys/arch/rp2040/doc/research/audit-findings.md:275-279` and `:581`;
comparisons stay IEEE through the software `rom_float_dcmp_core.S`). libcalc
detects NaN by comparison (`calc_internal.h`, `calc__isnan`), which survives,
but any NaN produced or propagated through arithmetic (`0/0`, `inf - inf`, a
NaN element in `calc_sum` or `calc_mean`, the Annex G special-value grid in
`calc_test.c`) becomes an infinity on the board, and the scaling paths that
work near the subnormal range (`calc__hypot`, the Smith division and the
`0x1p-50` scale at `calc_complex.c:209`) lose their low-end behavior.
`libcalc-implementation.md` reports no ISA execution. Fix, in order: add a
`check-qemu` target for libcalc using the `tests/libbn/qemu_support.c`
pattern (Thumb-1 instruction execution with libgcc's IEEE soft-float, which
settles code generation and ARMv6-M width but not ROM semantics); declare
libcalc's contract under the ROM provider in `mathematics.h`; run
`calc_test` on the board under the hardware opt-in to measure which checks
move. Prediction to falsify: the NaN-propagation and subnormal-range checks
fail on the board and every finite normal-range check passes.

**TF-2: sizes fit the window; the root budget moves for fourfall only.**
libcalc measures 24,593 bytes of text across 14 objects with no data or bss
(demonstrated); a calculator links per object, and the GUI note's planned
`calc` a.out of about 20 KB (`docs/research/picocalc-calculator-and-gui.md` on
the PR #344 branch, row "Four-function with precedence") leaves room for the
statistics, complex and CRC objects. The largest frame is 256 bytes
(`calc_quat_from_euler`); a 1,024-point FFT needs 16 KB of caller data plus 8
KB of twiddles, which a program must hold in bss or heap, not on the stack.
libbn is 3,293 bytes of text; dc's 771 KB extended register table, not
libbn, is the blocker for dc in the 144 KB window (`openbsd-dc-bn-port.md`,
step 3). fourfall measures 5,536 text, 8 data, 6,968 bss as a standalone
a.out (6,144 bytes are the frame buffer). The four-game `gamebox` measures
17,900 bytes raw and 15,968 packed (17 blocks) with `hsaout -s`, against
12,248 bytes for the three-game box
(`docs/research/c17-shipped-resource-baseline.json`, gamebox record); the
`games` closure costs 18 blocks and the `full` profile keeps 281 free blocks
(`sys/arch/rp2040/doc/PROFILES.md`, measured by `check-fs-profiles`).
Neither library enters the image.

**TF-3 minor, suspected: console behavior is unmeasured.**
fourfall waits in `select()` on descriptor 0 (`fourfall.c:949-960`) and tells
a bare Esc from a sequence by `FIONREAD` right after the Esc byte
(`:989`). The kernel has `HZ` 1000 (`sys/arch/rp2040/include/machparam.h:41`)
and names `usbselect`, but no run shows `select` waking on the console, and a
sequence split across USB or UART deliveries reads as pause plus stray keys.
The board check is `not run` in the record and here. Fix: one opt-in board
smoke run on the PicoCalc console and the USB console, recording redraw time
and whether arrows ever split.

## Licensing

**L-1 blocker, demonstrated: local absolute and home-relative paths in files to be committed.**
`docs/research/fourfall-implementation.md` named absolute home-directory
paths to the owner's instruction files, and `docs/research/picocalc-games-survey.md`
named home-relative checkout and download locations. The global hard rule
keeps local absolute paths out of commits. The survey also named a private
owner repository with its commit and size. Fix: replace the paths with
repository names or "the owner's instruction files". Resolution: the owner
chose generic wording for private repositories; both files were rewritten.

**L-2 major, demonstrated, owner decision: NOTICE and the header rule do not yet cover the new files.**
Every new C and header file carries `Copyright (c) 2026 DiscoBSD` and the ISC
notice, byte-identical to `sys/arch/rp2040/include/scb.h` (demonstrated by
`diff`), and the fourfall Python tests carry it too. `AGENTS.md` grants that
header only to new files under `sys/arch/rp2040`; outside it the owner's
global rule gives a new file no copyright line unless a license requires
one. `NOTICE:47-52` names as ISC only the `sys/arch/rp2040` files and the
games keen, fifteen and bubble, which themselves carry no header (the review
assignment's premise that `games/keen` uses this form is wrong;
`fourfall-implementation.md` notes the same and follows
`tools/flashimg/flashimg.c`, an existing precedent outside
`sys/arch/rp2040`). Options: keep the headers and extend `NOTICE` section 1
and the `AGENTS.md` convention to "files written for this port that carry the
ISC notice", naming `lib/libcalc`, `lib/libbn`, `games/fourfall` and their
tests (recommended: the header is the grant a redistributor reads); or strip
the headers to match keen and rely on `NOTICE`. Either way the test scripts
without headers stay as they are.

**L-3: third-party license statements checked.**
`tftetris.c` measures SHA-256
`27aaba14c203ed90763e49707216d757ebe16f41127c6c88b1dc069e95eb4f54` and carries
"Copyright (C) 2015 Joao Andre Esteves Vilaca" under GPLv2 or later, as the
specification and survey state (demonstrated). The MATHS license text, the
MIT notice on its CRC block and the Mersenne Twister credit are quoted
accurately in the specification's section 1.2; libcalc imports none of them,
so `NOTICE` gains nothing for libcalc beyond L-2. The OpenBSD dc and bc
sources are ISC (Otto Moerbeek, 2003) and their manual pages and
`bc.library` carry the Caldera license that `NOTICE` section 3 already
reproduces; the dc port, when it ships, adds an ISC section and retires the
Plan 9 MIT entries for the replaced `usr.bin/dc` and `usr.bin/bc`. The
`tests/libbn/dc-differential` shim compiles downloaded ISC sources only on
the owner's machine and ships nothing.

**L-4 hand-off, owner decision: the puzzles repository license.**
The survey records that the owner's puzzles repository carries
`SPDX-License-Identifier: BSD-3-Clause` in every `pb_core` header and no
`LICENSE` file, contradicting `sys/arch/rp2040/doc/research/games.md` ("GPL or
unlicensed"). The shipped `games/bubble/bubble.c:4-6` repeats the "GPL or
unlicensed" claim and a home-relative path. The owner decides the license;
then `bubble.c`'s header comment and `games.md` follow it in their own
commit.

**L-5 minor, demonstrated: British spelling in source.**
`lib/libcalc/calc_stats.c:406` reads "centre"; American English is required.
`picocalc-games-survey.md:138` writes "colour table" in prose (the quoted
identifiers `frontend_default_colour` and `LICENCE` keep the owner's
spelling).

## Integration plan

PR #344 (`picocalc/lcd-keyboard-console`) edits `Makefile`, `NOTICE`,
`sys/arch/rp2040/doc/TESTING.md`, `tools/c17/source-denominator.json` and
`docs/research/c17-semantic-migration-plan.md`, the same files this work
must touch. Land this work after #344 merges and rebase onto it; re-measure
every image figure on the rebased tree.

**INT-1 blocker, demonstrated: the C17 source denominator fails once the files are tracked.**
`tools/c17/source_denominator.py` compares `git ls-files` against
`tools/c17/source-denominator.json` (`:190-201`, `:512-515`). With the new
files intent-to-added in a scratch index it fails, naming the 28 new `.c`
and `.h` paths (14 libcalc objects and 2 headers, 2 libbn files, `fourfall.c`
and `tests/logic_test.c`, `calc_test.c`, `tl_errno.c`, `complex_vectors.h`,
`bn_test.c`, `qemu_support.c`, and the three dc-differential headers). Fix:
add 28 rows (class, scope, owner, profile, classification) and update the
count sentence at `docs/research/c17-semantic-migration-plan.md:1041` (1,931
unreviewed rises to 1,959 unless rows are classified on entry).

**INT-2 major, demonstrated: the ILP32 and sanitizer variants bypass execution receipts.**
`tests/libcalc/Makefile` and `tests/libbn/Makefile` print
`SKIP ... no ILP32 execution occurred` with `@echo`; `tests/libc_casefold/Makefile`
includes `tools/test-execution.mk` and runs `${TEST_EXECUTION} run` or `skip`
with a variant id, which lets CI's `REQUIRE_ILP32=yes` and
`TEST_EXECUTION_REQUIRED=yes` (`.github/workflows/firmware.yml`,
`check-ilp32-execution`) turn a skip into a failure. Fix: convert both
Makefiles to the receipt form and add variants to
`tools/test-execution-inventory.json` (`libcalc.native`, `libcalc.ilp32`,
`libcalc.sanitized`, `libcalc.treelibm`, `libbn.native`, `libbn.ilp32`,
`libbn.sanitized`, and fourfall's sanitized logic run), with the edges
`check-test-execution` enforces.

**INT-3: wiring, file by file.**

| File | Change |
| --- | --- |
| `lib/Makefile` | add `libbn` and `libcalc` to `SUBDIR`, so `logged-build` compiles both under the tree's flags |
| `games/Makefile` | add `fourfall` to `SUBDIR` before `gamebox`; `gamebox: fifteen keen bubble fourfall` |
| `games/gamebox/Makefile` | `TOOLS= fifteen keen bubble fourfall`; the header comment names four games |
| `distrib/rp2040/mi.rp2040` | in the `#closure games` link block (`:447-453`): `link /usr/games/fourfall`, `target /usr/games/gamebox`; the comment above names fourfall |
| root `Makefile` | `check-libcalc` and `check-libbn` (host suites) in `HOST_GATES`; `check-fourfall-host` (`${MAKE} -C games/fourfall test`) in `HOST_PROGRAM_GATES` (`:946`) and the `.PHONY` list (`:1292`); `check-libcalc-cross` and `check-libbn-cross` in `CROSS_CONTRACT_GATES`; `check-libbn-qemu` (and the TF-1 `check-libcalc-qemu`) in the `check-qemu` tier |
| `sys/arch/rp2040/doc/TESTING.md` | rows for the libcalc and libbn gates with their execution classes (host, ILP32 host, sanitizer, tree-libm host, cross object, qemu-user); fourfall joins the games row at `:576` |
| `tools/test-execution-inventory.json` | the variants under INT-2 |
| `tools/c17/source-denominator.json`, `c17-semantic-migration-plan.md` | INT-1 |
| `docs/research/c17-shipped-source-baseline.json` | `root.usr.games.gamebox` gains `games/fourfall/fourfall.c` and the headers it reaches (`sys/select.h`, `sys/time.h`, `signal.h`, `errno.h`, `sys/ioctl.h`) |
| `docs/research/c17-shipped-resource-baseline.json` | the gamebox record (`:2654-2700`): new libc members (at least `select.o`, `signal.o`, `getpid.o`; `check-c17-tool-capture` rejects an added member without review), text, bss, packed blocks and SHA-256 re-recorded from the rebuilt image |
| `docs/research/dpucp-2024-multicall-composition.json` | gamebox entry (`:148-162`): `tools`, `shipped_names`, `libc_member_count`, sizes and the `games/gamebox/Makefile` digest (`:23`) |
| `sys/arch/rp2040/doc/PROFILES.md` | the `games` closure cost (14 blocks, 2 inodes, `:38`) and the profile free-block table (`:56-71`) from a rebuilt image; `check-fs-profiles` verifies |
| `sys/arch/rp2040/doc/STORAGE.md`, `sys/arch/rp2040/doc/research/games.md` | gamebox size and the game list |
| `NOTICE` section 1 | L-2 |
| `docs/INDEX.md` | rows for the five other new research notes, the copy-review directory and this ledger; the one new row for `openbsd-dc-bn-port.md` is present |

Gates that move and what each needs: `check-c17-source-denominator` (INT-1
rows); `check-c17-tool-capture` and the shipped-resource baseline (gamebox
record re-measured, new libc members reviewed); `check-fs-profiles` (rebuilt
counts in `PROFILES.md`); `check-multicall-composition` (relinks gamebox from
the map seam; the composition JSON must match); `check-test-execution`
(inventory edges); `check-comment-hygiene` passes with advisories only.
Neither library enters `mi.rp2040`, so no profile moves for them.

## Recommended before the PR

1. Remove the local paths and decide on naming the private repository (L-1).
2. Rebase after PR #344 merges, then add the 28 denominator rows and the
   count sentence (INT-1).
3. Convert the libcalc and libbn test Makefiles to execution receipts and
   register the inventory variants (INT-2), then wire the root gates,
   `TESTING.md`, `lib/Makefile`, `games/Makefile`, `gamebox` and
   `mi.rp2040` (INT-3).
4. Settle the header and `NOTICE` policy and edit `NOTICE` section 1 (L-2).
5. Rewrite the libbn record's summary and "Not run" sections (T-1).
6. Re-measure gamebox and the profiles on the rebased tree and update the
   shipped-resource, shipped-source and composition baselines (INT-3).
7. Fix the fourfall Makefile interpreter and sanitizer probe (T-9) and add
   the libbn mutant timeout (T-8).
8. Add libcalc qemu-arm execution and state the ROM-float contract (TF-1).
9. Correct the FFT check and its record (T-2, T-3); scale the quaternion
   inverse (T-4); reject non-finite matrices (T-6); "centre" (L-5).
10. Split the work into bisectable commits by component (libbn, libcalc,
    fourfall, integration), each carrying an `Assisted-by:` trailer.

## Hand-off items

- Owner decision: falling-block look and feel and the piece letters, plus a
  trademark search for `fourfall` (CR-1).
- Owner decision: ISC headers outside `sys/arch/rp2040` versus the global
  no-copyright-line rule (L-2).
- Owner decision: the puzzles repository license, then the `bubble.c` header
  and `games.md` correction (L-4).
- Owner decision: whether research notes name private repositories (L-1).
- Board run under the hardware opt-in: `calc_test` against the ROM float
  provider (TF-1) and a fourfall console smoke on the PicoCalc and USB
  consoles (TF-3).
- Matrix conditioning reporting for the calculator front end (T-5); a design
  choice between a status and an out-parameter.
- Specification hygiene for future revisions: the reviewer-only split for the
  MATHS specification (CR-2), Part 1 cleanup (CR-4), the libcalc copying
  calibration (CR-3), the FFT and PID attribution notes (CR-5) and the BSD
  `tetris(6)` survey row (CR-6).
- An ILP32 rebuild of the libbn side of the dc differential (T-1).

## Resolution

The findings above stand as written for the tree they reviewed. Their
disposition on branch `picocalc/calculator-and-games`:

| Finding | Disposition |
| --- | --- |
| CR-1 | Owner ruling: a grayscale presentation that keeps the genre recognizable. fourfall now uses a 12 by 22 well, per-piece ASCII textures, a two-piece preview, and color and the plumb line off by default (`fourfall-implementation.md`, "Presentation"); copying reviews four and five (`copy-reviews/`) compare it with the reference's look and end clean at 1.1% shared runs. |
| T-1, T-8 | `openbsd-dc-bn-port.md` states the 2,995 of 3,000 differential result once; the mutant runner bounds each mutant at 120 seconds. |
| T-2, T-3 | The FFT check covers n from 4 to 1024 on both twiddle paths against 4 eps log2(n) sqrt(n) times the input 2-norm. |
| T-4, T-5, T-6 | `calc_quat_inverse` scales by the largest component; inverse and solve return `CALC_E_ILLCOND` above an estimated condition number of 1/(n eps); non-finite entries return `CALC_E_DOMAIN`. |
| T-9 | `games/fourfall/Makefile` takes `PYTHON` from the caller and refuses an empty value. |
| TF-2 | Measured: the `games` closure costs 18 root blocks and the `full` image leaves 281 (`sys/arch/rp2040/doc/PROFILES.md`). |
| L-1 | Local paths and private repository names are removed from every committed file. |
| L-2 | Owner ruling: new files outside `sys/arch/rp2040` carry no copyright header and fall under the repository `LICENSE`; the headers are removed and `NOTICE` needs no entry. |
| L-5 | American spelling throughout. |
| INT-1 | All 28 new C and header files have denominator rows; the 26 that build as strict C17 are accepted, and the two qemu harness files stay unreviewed with their GNU-extension blocker. |
| INT-2 | The native, ILP32, sanitizer, tree-libm and mutant variants record execution receipts. |
| INT-3 | Wired: host, cross, qemu and ILP32 tiers, `TESTING.md`, the inventory, gamebox, the manifest and `PROFILES.md`. `check-multicall-composition` replays its retained ledger only while the ledger's sources are unchanged and checks the live tree otherwise. Open: `lib/Makefile` `SUBDIR` waits for the first program that links either library, and `c17-shipped-source-baseline.json` and `c17-shipped-resource-baseline.json` remain retained records of revision `e7040aa1`. |
| CR-2, CR-3, CR-4, CR-6, T-7, T-10, TF-1, TF-3, L-4 | Open as written. |
