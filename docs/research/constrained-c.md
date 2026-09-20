# Constrained-C techniques for the RP2040 port

Two toolchains touch this tree. The host cross-compiler (`arm-none-eabi-gcc`,
driven by `share/mk/sys.mk`) builds every a.out that ships in the root
image. The on-device native chain (`usr.bin/smlrc` with the Thumb-1 back
end `usr.bin/smlrc/cgthumb.c`, `usr.bin/as/as-thumb.c`, `usr.bin/ld`)
builds whatever the user compiles on the board itself. The two have
different levers and are treated separately below. Findings are marked
**Observed** (read in this tree, or measured with the tools in it) or
**Inferred** (reasoned from the observed evidence, not measured here).

Ranked by (saving / effort), highest first within each section.

## 1. smlrc / cgthumb.c: codegen quality

### 1.1 Branch encoding always takes the worst case (Observed, measured)

`usr.bin/smlrc/cgthumb.c:562-582` (`GenJumpUncond`, `GenCondJump`): every
unconditional jump is `bl` (4 bytes, +-16 MB reach) and every conditional
jump is an inverted short branch over a `bl` (6 bytes: 2 + 4). The
comment at `cgthumb.c:75-82` gives the reason: ARMv6-M `b<cond>` reaches
only +-256 bytes and plain `b` +-2048, GNU as does not relax either, and
this compiler is single-pass, so it cannot know a target's distance at
emission time. `usr.bin/as/as-thumb.c:2710` and `:2749` confirm the
on-device assembler independently: "Thumb-1 has no relaxation and
nothing here narrows." Both compiler and assembler pick the safe,
maximal encoding on principle, not because most branches are actually
out of range.

Measured: built `smlrc` for the host (`usr.bin/smlrc/tests/run.sh`'s own
recipe, `cc -O1 -DTHUMB -DNO_ANNOTATIONS -DNO_PPACK -DSTATIC
-DNO_EXTRA_WARNS -DSYNTAX_STACK_MAX=3200 -o smlrc-host smlrc.c`),
preprocessed `usr.bin/smlrc/tests/t13_cat.c` (232 lines, a small `cat`)
with `arm-none-eabi-gcc -E -P`, and compiled it. Assembled with
`arm-none-eabi-as -mcpu=cortex-m0plus -mthumb`, the object is 4118 bytes
of `.text`. Within it:

- 138 `bl` whose target resolves inside the same function (in-function
  jumps: loop back edges, `if`/`else` joins, `break`/`continue`) -- every
  one of these is a candidate for a 2-byte `b` if its target is within
  +-2048 bytes.
- 115 conditional branches, each already costing the full 6 bytes; a
  target within +-256 bytes collapses to a single 2-byte `b<cond>`.
- 39 `bl` whose target is a different function (real calls) -- unaffected,
  correctly always `bl`.

**Measured, not estimated** (disassembled the object with
`arm-none-eabi-objdump -d`, resolved each `bl`'s target address, and
computed the distance from the branch site): all 138 same-function `bl`
instructions resolve within +-2048 bytes -- 100% would narrow to a 2-byte
`b`, saving 138 x 2 = 276 bytes. For the conditional branches, this scan
unambiguously paired 81 of the 115 `b<cond>`/`bl` sequences with their
real target (the rest sit in patterns -- back-to-back short-circuit
conditions, pool flushes between the pair -- this pass's parser did not
resolve); of those 81, 74 (91%) resolve within +-256 bytes, saving
74 x 4 = 296 bytes. Combined, this scan directly accounts for
276 + 296 = 572 bytes, **13.9% of this file's `.text`**, without crediting
the 34 unmatched conditional branches at all -- so 13.9% is a conservative
floor for this one file, not a ceiling. Reproduce with:
`arm-none-eabi-objdump -d t13_cat.o`, matching each `bl`'s target label
against the enclosing function name for the same-function case, and each
`b<cond>` against the `bl` it is paired with for the conditional case.

**Fix and effort.** This is not a cheap peephole in `cgthumb.c` alone,
because the compiler is single-pass and does not know final offsets.
The correct owner is `usr.bin/as/as-thumb.c`: it already resolves fixups
through a table (`RTJUMP8`, `RTJUMP11`, `RTCALL`, see
`as-thumb.c:2079-2103`), so it has the label-address bookkeeping a
narrowing pass needs. Add an iterative branch-shortening pass (classic
"shrink until fixed point": try `bl`-to-`b` and inverted-branch-to-direct
`b<cond>` narrowing, iterate because narrowing one branch can pull a
later target into range for another) after the first assembly pass, before
final code emission and pool placement. `cgthumb.c` need not change --
narrower encodings are legal outputs for the same source it already
emits, so this is purely an assembler transformation. Medium effort
(new pass in a 3315-line assembler, with fixup-table plumbing already in
place); large, tree-wide payoff on every program a user compiles on the
board. Risk: medium -- a narrowing bug that miscalculates a boundary
case corrupts branch targets silently; the existing 16-test host suite
plus 200-program qemu-arm oracle (`usr.bin/smlrc/tests/run.sh`,
mentioned in `sys/arch/rp2040/doc/STORAGE.md`) is the regression gate to
run before and after.

### 1.2 Repeated label-address loads: `_iob` reloaded 68 times in one 232-line file (Observed, measured)

Same measurement as above. Of 141 `ldr r, =label`/`ldr r, =const`
instructions in the compiled `t13_cat.o`, 135 are label addresses and one
label, `_iob`, accounts for 68 of them (48% of all label loads in the
file) -- each one a fresh load costing 6 bytes on the wire (the `ldr`
instruction plus its share of the pool literal it draws from; that is
`GenLoadLabelAddr`'s own accounting -- `cgthumb.c:356` calls
`ThumbSpend(6)`). 68 x 6 = 408 bytes, essentially 10% of this file's
`.text`, from materializing the address of one array, repeatedly, in
functions that never assign to the register holding it in between.

The mechanism (Observed, `include/stdio.h:132-133`):

```
#define getc(p)     (--(p)->_cnt>=0? (int)(*(unsigned char *)(p)->_ptr++):_filbuf(p))
#define putc(x, p)  (--(p)->_cnt >= 0 ? ...
```

`p` is `stdin`/`stdout`/`&_iob[N]`, an address expression, not a
variable, and it appears three or four times in the macro body. A
compiler with cross-statement CSE or a real register allocator (the host
GCC path) hoists the repeated `&_iob[N]` computation once; this backend
has none (`cgthumb.c`'s own comment block at the top names "no register
allocation across statements" as a deliberate scope cut) and materializes
the address fresh at every one of `p`'s appearances, inside a single
macro-expanded expression, let alone across statements.

**Fix, ranked by effort, with a measurement backing the recommendation.**
`lib/libc/stdio/fgetc.c` and `fputc.c` already exist as one-line function
wrappers around the same macro (`return getc(fp);` / `return putc(c,
fp);`), so no new libc code is needed -- only a header change that makes
`getc`/`putc` call them instead of expanding inline, for the smlrc build.
Measured the difference directly: compiled two four-line functions (a
read/echo loop, once with `getc(stdin)`/`putc(c, stdout)`, once with
`fgetc(stdin)`/`fputc(c, stdout)`) with the host `smlrc-host` and
assembled both.

| Variant | `.text` (caller side only, callee not linked) |
|---|---|
| `getc`/`putc` macros | 400 bytes |
| `fgetc`/`fputc` calls | 124 bytes |

276 bytes for two call sites in a four-line function, roughly 138
bytes per call site converted, at the caller. That is not free -- each
converted site now pays a real `bl` (this backend evaluates every
argument onto the stack, per the comment at the top of `cgthumb.c`, so
the call itself costs more than a bare `bl`) and the callee body
(`fgetc`/`fputc`, already 6-8 bytes each of trivial wrapper logic) is
shared once across every caller rather than duplicated, so the net saving
grows with the number of call sites in a program and does not depend on
paying for the callee more than once. `t13_cat.c` calls `getc`/`putc`
across the file at the rate the 68-load count above shows, so this
program is a strong case for the conversion; a program with only one or
two `getc`/`putc` sites sees a smaller, but still positive, net.

**Fix:** gate the macro form the same way `PRINTF_FLOAT` is gated for
rp2040 in `share/mk/sys.mk` -- e.g. `#ifdef __SMALLER_C__` around
`getc`/`putc` in `include/stdio.h:132-133`, defining them as plain
function-call macros (`#define getc(p) fgetc(p)`) for the smlrc build,
while every GCC-built program (host cross-compile, `PRINTF_FLOAT`
machines) keeps the expression macro GCC's own register allocator
handles well. Effort: trivial, a two-line header `#ifdef`, no new
functions. Risk: low -- `fgetc`/`fputc` already exist, are already
linked by any program that calls them, and are already exercised by
whatever in the tree calls them today; the 200-program host/qemu-arm
oracle (`usr.bin/smlrc/tests/run.sh`) is the regression gate.

- **Compiler-level (larger payoff, more risk).** Cache the last label
  loaded into `ThumbOpRegAddr` (r1) in `GenLoadLabelAddr`
  (`cgthumb.c:345-356`) and skip re-emission when the next request names
  the same label and r1 has not been written since. r1's write sites are
  enumerable in this backend (`GenLocalAddr`, `GenAddSp`,
  `ThumbEmitLoad`/`Store` when used as the address register, the
  division helpers, the prolog/epilog, and any branch target -- a label
  arriving from elsewhere invalidates any cached value unconditionally).
  This turns the 1.2 fix into a general one that also helps repeated
  global-variable and struct-field access outside stdio macros. Effort:
  moderate -- the invalidation set has to be exactly right or a stale
  address gets reused after r1 changes meaning, which is silent memory
  corruption, not a compile error. Treat as risk: medium-high, and gate
  it behind the same test suite plus a targeted new test that reads and
  writes the same global twice with a function call in between (to prove
  invalidation fires on `bl`).

### 1.3 `GenLoadConst` never checks for a shift-of-byte pattern (Observed, measured)

`cgthumb.c:321-341`: any constant outside 0..255 and outside -255..-1
goes straight to `ldr r, =N` (6 bytes: instruction + pool word). In the
same 4118-byte sample, all 6 non-label numeric constants -- 61440
(0xF000), 8192 (0x2000), 24576 (0x6000), 61440 again, 32768 (0x8000),
1024 (0x400) -- are a byte shifted left, which Thumb-1 already encodes in
4 bytes as `movs r, #byte` / `lsls r, r, #N` (both `ThumbSpend`-tracked
2-byte encodings already used elsewhere in this file for other
purposes). 6 for 6 in this sample; Unix source is full of exactly this
shape (`S_IFMT`-style mode masks, `0xFF00`-style byte-lane masks,
page/block-size constants), so this is not a coincidence of one test
file (Inferred generalization, Observed on the one sample measured).

**Fix.** Add a branch in `GenLoadConst`, before the `ldr =` fallback:
if `v` (or `u`) equals `(byte << shift)` for some `byte` in 1..255 and
`shift` in 1..24, emit `movs r, #byte` then `lsls r, r, #shift` (4 bytes,
and it does not consume a pool slot, which also delays the next
`ThumbMaybeFlushPool` flush). Cheap: one arithmetic check
(`v == (b << s)` for byte `b`, tried for increasing `s`) and two
`printf2` calls mirroring the existing `movs`/`negs` pattern already in
the function. Saving: 2 bytes per matching constant plus one fewer pool
entry; on the sample, 12 bytes directly and a marginally later pool
flush. Risk: low -- purely a constant-materialization substitution, and
the existing test set includes several arithmetic/constant-heavy cases
that exercise `GenLoadConst`.

## 2. Host GCC build (the shipped root image)

### 2.1 `-ffunction-sections -fdata-sections` + `--gc-sections`, tree-wide (Observed: not applied globally; Observed: already proven in this tree)

`share/mk/sys.mk:83-89` sets `COPTS` to `-Os -fcommon` for ARM, with no
`-ffunction-sections`/`-fdata-sections`, and `LDFLAGS`
(`sys.mk:114-117`) carries no `--gc-sections`. `usr.bin/emg/Makefile:11-14`
already overrides both locally for MicroEMACS, with a comment pointing at
this exact pair -- proof the technique is understood and already applied
in one corner of the tree, but not promoted to `share/mk/sys.mk` where
every other Makefile would inherit it for free.

Static linking already drops whole unused `.o` archive members from
`libc.a` (ordinary `ar`/`ld` behavior), so the marginal win here is
narrower than "every unused libc function is currently linked in" --
it is: (a) unused *functions within a single multi-function `.o`*, and
(b) unused functions within the program's own multi-function `.c` files.
Concrete case: `lib/libc/gen/malloc.c` builds `malloc`, `free`,
`realloc`, and the static helpers `botch`/`allock` into one `malloc.o`
(Observed, `lib/libc/gen/malloc.c:93,164,185,216`); a program that calls
`malloc`/`free` but never `realloc` currently links `realloc`'s body
too, and with function-sections + gc-sections it would not. Every
multi-function file in `lib/libc/stdio` and `lib/libc/gen` is a smaller
instance of the same pattern.

**Fix.** In `share/mk/sys.mk`, append `-ffunction-sections
-fdata-sections` to the ARM branch of `COPTS` (`sys.mk:84`) and
`-Wl,--gc-sections` to `LDFLAGS` (`sys.mk:114`), matching what
`usr.bin/emg/Makefile` already does per-program. This rebuilds
`libc.a` and every program with section-per-symbol, and the linker drops
what nothing references. Effort: a two-line change plus a full rebuild
to confirm nothing relies on cross-referenced sections staying together
(unlikely in this freestanding, no-constructors codebase). Saving:
Inferred, not measured here -- BSD-derived multi-function C files
typically give up a few percent of `.text` this way; STORAGE.md's per-
program sizes (10-26 KB range) suggest low hundreds of bytes per program
is a reasonable expectation, which is small per file but compounds
across 64 shipped files and the 390 KB of separately-linked box tools.
Risk: low. Verify with `--print-gc-sections` on one rebuild to see what
actually dropped before trusting the estimate further.

### 2.2 `-flto` across `libc.a` and each program (Inferred, not attempted here)

Not tested (would need a full rebuild environment: `bmake` tool builds,
`DESTDIR` staging -- out of scope for this pass). Static archive members
built with `-flto` need `gcc-ar`/`gcc-ranlib` or `-ffat-lto-objects` to
stay linkable the way this tree's own `ar cr` step in
`lib/libc/Makefile:34-38` uses them; that is a build-system change, not
a flag flip, and the biggest win (cross-TU inlining and dead-code
elimination between libc and the caller) mostly overlaps with what
`--gc-sections` above already buys once function/data sections exist.
Rank this below 2.1: same category of win, meaningfully more build-
system risk (archive format compatibility with the existing
`AR`/`RANLIB` invocations), and it should be evaluated only after 2.1
lands and its actual gc-sections yield is measured.

### 2.3 Flags already correct -- do not change (Observed)

- `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -mabi=aapcs` (`sys.mk:69-70`)
  match the Cortex-M0+/ARMv6-M target exactly; there is no better `-mcpu`
  choice for this chip.
- `-Os` is already the optimization level (`sys.mk:84`).
- ARM EABI unwind tables are not the win they look like on paper:
  checked a linked ELF (`usr.bin/yacc/yacc.elf`) with
  `arm-none-eabi-readelf -S` and found exactly three loaded sections,
  `.text`/`.data`/`.bss` -- no `.ARM.exidx`/`.ARM.extab` were emitted, so
  `-fno-unwind-tables`/`-fno-asynchronous-unwind-tables` would save
  nothing here; whatever suppresses them (freestanding build, no
  exceptions in C, or the linker script) is already doing its job. Do
  not spend effort on this.
- `elf2aout` (`share/mk/sys.mk:165`) strips to `.text`/`.data`/`.bss`
  regardless, so any ELF-only metadata (debug info, symbol table
  richness) never reaches the shipped a.out; there is no separate
  "strip" step to add.

## 3. libc size and RAM drivers beyond `_doprnt` float

### 3.1 `getc`/`putc` macro expansion cost (see 1.2) -- cross-references here because it is a libc-header decision, not only a codegen one. On the GCC-compiled root image the macro is fine (GCC's register allocator collapses the repeated `&_iob[N]`); the fix in 1.2 should be scoped to the smlrc/on-device build only, via the same machine/compiler conditional the tree already uses for `PRINTF_FLOAT`.

### 3.2 Unbuffered-stream `alloca(BUFSIZ)` is already good design; `BUFSIZ` itself is a tunable (Observed)

`lib/libc/stdio/fputs.c:20`, `fprintf.c:18`, `vfprintf.c:28` allocate a
transient `BUFSIZ`-byte buffer with `alloca` when a stream (typically
`stderr`) is unbuffered, and free it implicitly on return
(`fputs.c:26-31` clears the flag and pointer afterward). This is already
the RAM-efficient pattern -- a malloc'd buffer would persist for the
`FILE*`'s lifetime; the `alloca` version exists only for the duration of
one call. No change needed here; noted so it is not mistakenly "fixed"
into a malloc-backed buffer, which would be worse.

What is a real tunable: `BUFSIZ` is 1024 (`include/stdio.h:8`), so every
one of these transient allocations, and every `setbuf`/`setvbuf`-default
buffered stream (`setbuf.c:43`), claims a full 1 KB. The measured revision used
a 96 KB process window shared by stack, heap, and data; the current reservation
is 144 KiB. An unbuffered `fprintf(stderr,
...)` deep in a call chain borrows 1 KB of stack for the duration of one
diagnostic line. **Fix**: split the constant -- keep `BUFSIZ` at 1024 for
file-backed buffered I/O (matches the block size the filesystem likely
already assumes; do not touch without checking Dhara's page/block size
first, since a smaller buffered-file `BUFSIZ` trades RAM for more, smaller
flash writes) and add a separate, smaller constant (e.g. 128 or 256
bytes) used only in the three `alloca(BUFSIZ)` unbuffered-fallback sites,
since a diagnostic or prompt line rarely approaches 1 KB and the
allocation is on the stack of whatever function happened to call
`fprintf`. Effort: low, three call sites plus one new header constant.
Saving: RAM headroom, not flash -- up to ~768-896 bytes of transient
stack per unbuffered write, which matters more for recursion depth
headroom than for steady-state usage. Risk: low, but
verify no caller of `fprintf(stderr, ...)` ever emits a line close to
1 KB (the shell, `ed`, and `awk`'s error paths are the likely suspects
to check before landing this).

### 3.3 `malloc.c`'s multi-function object (see 2.1) -- the `--gc-sections` fix in 2.1 subsumes this; no separate change needed once 2.1 lands.

### 3.4 `sbrk` growth granule (Observed, not actionable without a rewrite)

`lib/libc/gen/malloc.c:73` (`#define BLOCK 1024`) grows the arena in
1024-byte steps via `sbrk`, the classic pre-bucket first-fit BSD
allocator (confirmed: no `NBUCKETS`/power-of-two free-list structure in
this file, just a circular first-fit search with an LSB busy flag). This
is a coarser growth step than a picolibc/newlib-nano bump allocator
would use, but shrinking `BLOCK` trades allocator call overhead
(`sbrk` is a syscall) for finer-grained RAM use, and the allocator's
first-fit-with-coalescing behavior does not obviously break if `BLOCK`
shrinks. Not enough evidence in this pass to size the saving or clear
the risk (coalescing correctness under many small `sbrk` calls needs a
read of `malloc`/`free`'s coalescing logic this pass did not do); flag
for a follow-up pass rather than recommend a number now.

## 4. Kernel RAM: structural notes

The survey's 96 KB single-process window was later enlarged to the current
144 KiB reservation. The 264 KB total SRAM and why XIP from flash is not
possible for an a.out linked at 0x20000000 are established in
`sys/arch/rp2040/doc/STORAGE.md` and are not re-derived
here.

### 4.1 The static `_iob[]` table costs 400 bytes of `.data`, unconditionally, in every a.out (Observed, measured)

`lib/libc/stdio/findiop.c:15,17`: `#define NSTATIC 20` and `FILE
_iob[NSTATIC]`, three entries initialized (`stdin`/`stdout`/`stderr`),
the rest zero. `struct _iobuf` (`include/stdio.h:9-16`) is `int _cnt`
(4) + `char *_ptr` (4) + `char *_base` (4) + `int _bufsiz` (4) + `short
_flag` (2) + `short _file` (2) = 20 bytes, no padding on this ABI. 20 x
20 = 400 bytes of `.data` (the array has nonzero initializers for the
first three slots, so it is not `.bss`) in every single program that
links `libc.a` -- every one of the 64+ files STORAGE.md lists ships this
same 400-byte table, most of which use two or three `FILE*` at a time.

The dynamic-growth path is already implemented and does not depend on
`NSTATIC`'s value: `_f_morefiles()` (`findiop.c:27-47`) and `_findiop()`
(`findiop.c:59-84`) fall through to `calloc`-based per-`FILE` allocation
once the static array is exhausted, up to `getdtablesize()`. Shrinking
`NSTATIC` narrows the always-paid static cost without removing the
overflow path a program that legitimately opens many files still uses.

**Fix:** lower `NSTATIC` from 20 to a number that covers the common
case with slack -- 8 covers stdin/stdout/stderr plus five concurrently
open files, which STORAGE.md's shipped-program list suggests is already
generous for this root (`sort`, `awk`, and the editors are the few
programs likely to hold more than two or three files open at once, and
all of them fall through to the dynamic path past that). 8 x 20 = 160
bytes, saving 240 bytes of `.data` per program. Effort: one constant.
Saving: 240 bytes x roughly 64 shipped files is on the order of 15 KB of
aggregate flash-image and RAM-at-load reduction across the root, against
a documented 70 KB of free space -- the single largest aggregate number
in this report, even though it is small per file. Risk: low, gated by
whatever program in the tree opens the most files concurrently; grep the
tree for concurrent `fopen` counts before landing the number, and keep it
comfortably above the highest count found rather than at the historical
BSD default of 20, which predates this port's RAM budget entirely.

Two portable techniques from other tiny-libc projects were checked
against this tree and do not add a new lever beyond what is already
recommended above:

- **picolibc/newlib-nano's `_PRINTF_FLOAT`/`_SCANF_FLOAT` split** is
  already the pattern this tree implemented for `_doprnt`
  (`PRINTF_FLOAT=yes` gate in `share/mk/sys.mk:99-103`,
  `lib/libc/stdio/doprnt_float.c`). Nothing further to port; the
  technique is already applied at the one place it matters
  (floating-point conversion) and there is no `scanf`-side float path in
  this libc to mirror it for (Observed: no `doscan_float.c` or
  equivalent exists, and the shipped program list in STORAGE.md has no
  float-`scanf` consumer besides the already-gated interpreters).
- **ELKS/xv6-style single flat process image with no MMU** is already
  this port's model (one resident process, no paging); there is no
  additional structural lever to import from those projects that this
  tree has not already taken, beyond the swap-map fragmentation
  mitigation STORAGE.md documents (the two-box split).

## Summary table

| # | Item | File(s) | Saving | Effort | Risk |
|---|---|---|---|---|---|
| 1.2a | `getc`/`putc` route to existing `fgetc`/`fputc` on smlrc build | `include/stdio.h:132-133` | Measured: 276 bytes/2 call sites in a test function (~138 B/site); scales with call-site count, shared callee cost | Trivial (2-line header `#ifdef`, no new code) | Low |
| 4.1 | Shrink `NSTATIC` (static `FILE` table) from 20 | `lib/libc/stdio/findiop.c:15` | Measured: 240 B/program x ~64 files =~ 15 KB aggregate against 70 KB free | Trivial (1 constant) | Low, verify max concurrent open-file count first |
| 1.1 | Branch narrowing in the on-device assembler | `usr.bin/as/as-thumb.c` | Measured floor: 13.9% of `.text` on one sample file (572/4118 B), conservative (34 of 115 conditional branches not credited) | Medium (new relaxation pass) | Medium |
| 2.1 | `-ffunction-sections -fdata-sections` + `--gc-sections` tree-wide | `share/mk/sys.mk:84,114` | Not rebuilt this pass; Inferred low hundreds of B/program, compounds over 64+ files; `usr.bin/emg/Makefile` is in-tree precedent | Low (2-line diff, full rebuild needed to verify) | Low |
| 1.3 | `movs`+`lsls` for byte-shifted constants | `usr.bin/smlrc/cgthumb.c:321-341` | 2 B/match + fewer pool flushes (Observed: 6/6 in sample) | Low | Low |
| 3.2 | Smaller alloca buffer for unbuffered stdio | `lib/libc/stdio/{fputs,fprintf,vfprintf}.c` | Up to ~896 B transient stack per call | Low | Low, verify line lengths first |
| 1.2b | Address-register CSE in `GenLoadLabelAddr` | `usr.bin/smlrc/cgthumb.c:345-356` | Larger, general version of 1.2a, unmeasured | Medium-high | Medium-high (silent corruption if invalidation is wrong) |
| 2.2 | `-flto` for libc + programs | build system | Unmeasured, likely subsumed by 2.1 | Medium-high (archive/build-system change) | Medium |
| 3.4 | Smaller `sbrk` growth granule | `lib/libc/gen/malloc.c:73` | Unsized this pass | Unclear until coalescing is read | Unclear |

Methodology for the measured numbers: host build of `smlrc` per
`usr.bin/smlrc/tests/run.sh`'s own recipe, `arm-none-eabi-gcc -E -P` to
preprocess `usr.bin/smlrc/tests/t13_cat.c` (and, for 1.2a, two small
hand-written test functions), `smlrc-host` to compile to Thumb-1
assembly, `arm-none-eabi-as -mcpu=cortex-m0plus -mthumb` to assemble,
`arm-none-eabi-size` and grep counts on the result. The 1.1 branch-range
figures additionally disassemble the object with `arm-none-eabi-objdump
-d` and compute each branch's distance to its resolved target address,
rather than reasoning about range from the pool-flush threshold.
Reproducible with the commands embedded in each section above.
