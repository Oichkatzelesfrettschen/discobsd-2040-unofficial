# C17 formatting and linting for DiscoBSD

This guide turns `docs/research/STYLE-GUIDE.md` into a reproducible tooling
plan. It covers project-owned C source from K&R through C11 as a migration
frontier: every maintainable translation unit moves to the repository C17
profile. SIMH/V6/PDP-11 guest source, immutable licensed history, generated
files, assembly and binary fixtures keep their own language or provenance
boundary. A boundary is recorded and reviewed; it is not silently counted as
C17-conforming.

The guide is intentionally staged. The current tree has a real `check-lint`
target for ShellCheck and Ruff, but no repository-wide C formatter or semantic
linter. A formatter candidate rewrote substantial continuation and macro
layout in representative BSD files, and a broad clang-tidy run produced 196
diagnostics plus reserved-identifier errors from project and system headers.
Those observations require a scoped rollout, not a global rewrite or a warning
suppression file.

## Current K&R and C17 findings

The recursive audit at `d5eedba159b7998ca21b2a11c0a660413af09c5d`
covered every Git-tracked C and header file. The source corpus contains 1,971 C
files, 567 headers and 820,309 combined lines. The audit refreshed the
whole-tree structural Graft graph in the integrated checkout from 26,311 to
26,396 nodes.
The graph records 2,609 parsed files, 23,760 symbols and 16,700 structural
edges. Graft navigation identified source owners and candidate paths; AST
queries and direct source reads decide the syntax findings below.

The requested deep refresh of `sys/arch/rp2040/dev` reached a terminal
`partial` state because the configured loopback model endpoint was stopped.
The job retained 159 structural nodes and produced zero semantic records.
The prior deep caches cover `sys/arch/rp2040/dev`,
`sys/arch/rp2040/rp2040` and `usr.bin/login`; only login remains source-current.
A read of the stale flash summary omitted the new storage telemetry in
`flash.c` and carried obsolete line ranges. Deep summaries therefore remain
advisory leads, not current-source evidence.

### What K&R means here

K&R C names the pre-prototype function interface inherited from the first
edition of Kernighan and Ritchie. This audit uses the term for four related
constructions rather than for indentation or general age:

1. A function definition names parameters in the declarator and declares their
   types in a separate declaration list before the body.
2. A function definition uses empty parentheses instead of `(void)`.
3. A declaration uses empty parentheses, which in C17 supplies no parameter
   type or arity information.
4. A call or definition depends on implicit `int`, an implicit function
   declaration, default argument promotions, or `<varargs.h>`/`va_dcl`.

ISO C17 retained the first three only as obsolescent syntax. WG14 N2310
6.7.6.3 paragraphs 3, 6, 10 and 14 distinguish a parameter type list from an
identifier list and distinguish `(void)` from `()`. Section 6.9.1 paragraphs
5 through 8 define both function-definition forms and make an unprototyped
variadic definition undefined. Sections 6.11.6 and 6.11.7 mark empty function
declarators and separate parameter declaration lists obsolescent. Section
6.5.2.2 paragraphs 6 through 9 applies default promotions and leaves mismatched
unprototyped calls undefined. C99 had already removed implicit `int`; C17 does
not restore it. C23 draft N3096 removes identifier-list definitions and makes
an empty parameter list equivalent to `(void)`.

Primary language references:

- WG14 N2310: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2310.pdf>
- WG14 N3096: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf>

### Measured syntax frontier

An ast-grep rule selected `function_definition` nodes whose parameter list
contains no `parameter_declaration`. A separate rule selected declaration
nodes with the same non-prototype parameter-list shape. The rules were
calibrated against typed prototypes and `(void)` as accepting controls and
against identifier-list definitions, empty definitions and empty declarations
as rejecting controls.

- Old-style definitions: 3,825 occurrences in 916 files. The total comprises
  2,255 identifier-list definitions and 1,570 empty-parenthesis definitions.
- Non-prototype declarations: 1,114 occurrences in 290 files. The declarations
  and function-pointer types carry no argument contract in `()`.
- Historical `legacy/`: 357 definitions and 47 declarations. The directory is
  a quarantine target rather than a maintained ARM migration target.
- Maintained-tree candidates: 3,468 definitions and 1,067 declarations pending
  source classification. The set includes shipped, optional, host-only,
  inactive-architecture and imported boundaries.
- `<varargs.h>`: four files -- `lib/libc/gen/err.c`, `usr.bin/m4/misc.c`,
  `bin/sh/trace.c` and `usr.bin/xargs/xargs.c` -- plus their old-style variadic
  functions.
- `WARNLEVEL=legacy`: 140 Makefiles -- 66 `usr.bin`, 25 games, 15 bin, 12 sbin,
  9 lib, 6 usr.sbin, 5 tools, 1 libexec and 1 isolated legacy unit.

The definition count by top-level owner is:

| Owner | Definitions |
| --- | ---: |
| `games/` | 1,282 |
| `usr.bin/` | 1,145 |
| `lib/` | 380 |
| `legacy/` | 357 |
| `bin/` | 238 |
| `sys/` | 190 |
| `sbin/` | 123 |
| `usr.sbin/` | 87 |
| `share/` | 10 |
| `libexec/` | 7 |
| `tests/` | 5 |
| `benchmarks/` | 1 |

The kernel frontier is unusually well bounded: the AST rule found 187 of the
190 `sys/` definitions in 26 `sys/kern` files, two inactive STM32 definitions,
and one in `sys/arch/rp2040/rp2040/sysctl.c`. The AST count is a lower bound.
The cross compiler's `-Wold-style-definition` over the PICO and PICO_UART
builds reports 12 `sys/kern` definitions the rule missed: tree-sitter loses
identifier-list definitions whose declaration list carries `__unused`
(`tty_tty.c`'s five device entries, `seltrue`, `gatherstats`), pointer-
returning definitions (`getf`, `pfind`, `nextc`, `swapout`) and `brk()`
after an `#endif`. The compiler in turn misses inactive code (`timevalsub`
under `NOT_CURRENTLY_IN_USE`, `ptyattach`) and files neither configuration
builds (`subr_log.c`, `kern_glob.c`). The union is 200 definitions in 28
`sys/kern` files plus four unprototyped declarations in `sys/sys`.
`check-c17-kernel-inventory` records that union in
`tools/c17/kernel-ledger.txt` with the oracle behind each row and fails when
the compiler reports a finding the calibrated scan lacks. The same parser
gaps make the repository-wide AST totals above lower bounds for those shapes.
The existing `check-ufs-prototypes` gate covers 13 UFS translation units, but
it does not close the remaining kernel files. The largest userland concentrations are
`games/rogue` (308), `usr.bin/tip` (131), `usr.bin/forth` (122 in two files),
`usr.bin/re` (97), `usr.bin/picoc` (96), `games/sail` (91),
`games/battlestar` (84), `games/backgammon` (80) and `games/adventure` (74).
These concentrations define migration units; a repository-wide search-and-
replace would cross ABI, generated-source, provenance and memory boundaries.

The AST parser also produced 102 implicit-int lexical leads. Macro-generated
forms such as `foreachship(sp)` are among them, so 102 is a review queue, not a
defect count. The compiler with the unit's evaluated include and macro flags
must decide each row. The same restriction applies to implicit calls: a host
compile with host headers can both hide target omissions and invent host-only
ones.

### Required migration unit

Convert one complete call contract at a time:

1. Inventory the definition, every declaration, function-pointer type and
   lexical caller. Record generated declarations separately.
2. Resolve the actual return type and parameter types from implementation,
   callers, ABI headers and behavior tests. Never infer a pointer return from
   the historical implicit `int` rule.
3. For an empty definition, prove every call supplies zero arguments before
   writing `(void)`. For an empty declaration, derive a real prototype rather
   than mechanically writing `(void)`.
4. For an identifier-list definition, preserve old default-promotion behavior.
   Audit `char`, `short`, `_Bool`, enum and `float` parameters, variadic calls,
   callbacks and function-pointer casts before changing the declarator.
5. Replace `<varargs.h>` only after selecting a stable last named parameter and
   proving each call shape. Use `<stdarg.h>`, `...`, `va_start`, `va_copy` where
   ownership crosses a helper, and a balanced `va_end` on every exit.
6. Compile the definition and all callers under GNU17 and strict C17 with the
   production target flags. Then run the unit's host, ILP32, Cortex-M0+ and
   behavior gates.
7. Compare symbols, ABI, object sections, final ELF/a.out, stack usage and
   packed-root blocks before accepting the unit.

Prototype conversion is a security change because it lets the compiler check
arity and conversions at the call boundary. Prototype conversion is also an
ABI change when old promotions, callbacks or independently compiled objects
disagree. The declaration, definition and callers therefore land in the same
bisectable migration unit.

### Security build contract

The current global compiler already supplies `-std=gnu17`, `-fno-common` and a
fatal warning policy. Full units receive `-Wall -Wextra -Werror`; 140 legacy
units receive only `-Werror` plus warning groups named locally. The RP2040 link
also makes RWX-segment warnings fatal. Those controls are necessary but do not
establish a repository-wide C17 or memory-safety build.

Each migrated unit must add these diagnostics without local suppression:

```text
-Werror=implicit-function-declaration
-Werror=implicit-int
-Werror=old-style-definition
-Werror=strict-prototypes
-Werror=missing-prototypes
-Werror=return-type
-Werror=incompatible-pointer-types
-Werror=int-conversion
-Wformat=2
```

Add `-Wconversion`, `-Wsign-conversion`, `-Wshadow`, `-Wcast-align` and
`-Wvla` only after a known-good/known-bad calibration for the migration family.
The target build must keep `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
-mabi=aapcs`, the project headers, generated configuration, linker scripts and
ELF-to-a.out conversion. Host ASan, UBSan, scan-build, cppcheck and GCC analyzer
runs supplement that build; they cannot replace it.

Generic hardening flags need target-specific dispositions:

- Stack protector: pilot per migration unit. Measure guard/runtime closure,
  text, stack and failure behavior before enabling it. A 3 KiB u-area stack
  cannot absorb an unmeasured global flag.
- `_FORTIFY_SOURCE`: defer until project libc provides and tests the required
  checked builtins. Host libc fortification does not harden target libc.
- PIE/ASLR: inapplicable to the fixed OMAGIC 0x20000000 process image and
  single process window without a loader and ABI redesign.
- W^X: the user window is one RWX OMAGIC text/data/bss image. C17 cleanup does
  not create separation. MPU register, read-back and fault evidence owns any
  protection claim.
- ASan/UBSan: host and representable ILP32 or qemu evidence only. Runtime cost
  excludes them from the shipped image unless a measured diagnostic profile
  fits.
- LTO: defer until archive tools, `elf2aout`, inline assembly, weak hooks and
  bisectability have an exact equivalence gate.
- Section GC: run a measured pilot. Compile with function/data sections, link
  with GC and inspect every discarded section, callback table, linker `KEEP`,
  multicall overlay and packed image before promotion.

Security closure also requires typed external-byte decoding, overflow-safe
size arithmetic, bounded pathname and string handling, explicit ownership and
error paths, format attributes, and tests that execute malformed inputs. A
warning-free build proves compilation under its exact flags; it does not prove
runtime bounds, MPU behavior, flash durability or physical-board behavior.

### Tiny-target acceptance contract

The RP2040 reserves 128 KiB for boot2 plus kernel, a 144 KiB OMAGIC user
window, 106 KiB of kernel RAM, two 3 KiB u areas, 8 KiB of USB scratch,
1,536 KiB of physical Dhara root and 384 KiB of raw swap. The root exposes
989 KiB of logical blocks after Dhara overhead. Every C17 migration must keep
these accounts separate:

- kernel flash: `.boot2 + .text` against the 128 KiB region;
- kernel resident RAM: `.data`, `.bss`, `.ramfunc`, SwapRAM and scratch by
  actual overlap and lifetime;
- process peak: `a_text + a_data + a_bss + heap + user stack + arguments`
  against 144 KiB with an explicit reserve;
- kernel stack: `.su` static leads plus capacity sentinel measurements inside
  the 3 KiB u area;
- installed root: raw and packed 1 KiB blocks, inodes and hard-link identity;
- durability: logical writes, Dhara map/checkpoint traffic and physical NOR
  page/sector counters as separate evidence classes.

An existing untracked kernel artifact dated 2026-10-03 measures 101,630 bytes
of `.text`, 256 bytes of boot2, 248 bytes of `.data`, 312 bytes of `.ramfunc`,
15,240 bytes of ordinary `.bss`, 16,384 bytes of SwapRAM and 8,192 bytes of
scratch. It predates the audited HEAD by two days, so those values are a local
baseline candidate, not current-build proof. A clean build must replace them
before the first migration unit claims a delta.

Seven packed multicall binaries save root blocks by sharing libc, code and
exclusive applet BSS. A smaller standalone object can still make a box worse
by raising its largest overlaid BSS extent or pulling a new libc member into
every applet path. Measure standalone, box, raw a.out, packed a.out and complete
root image together. Preserve `PRINTF_FLOAT`, `PRINTF_LLONG` and `SCANF_FLOAT`
closure controls; adding a format conversion can import software arithmetic
that dwarfs the source edit.

### Recursive execution order

The complete refactor is a sequence of finite frontiers:

1. Land the tracked-file classifier and quarantine manifest. Unknown files
   fail closed.
2. Land calibrated AST/compiler inventories for old definitions,
   non-prototype declarations, implicit types/calls and old varargs. The
   kernel ledger (`check-c17-kernel-inventory`) is the first; each later
   frontier extends the same two-oracle union to its own sources.
3. Close `sys/kern` by subsystem, extending `check-ufs-prototypes` into exact
   kernel compile units before changing userland.
4. Close RP2040 machine/device code and generated PICO/PICO_UART consumers,
   retaining MPU, syscall-frame, flash-XIP and capacity gates.
5. Close libc by archive member and public header so each consumer rebuilds
   from clean and ABI changes cannot hide behind stale objects.
6. Close the seven multicall families with standalone and combined-object
   gates, then migrate remaining shipped set-id and standalone programs.
7. Close the native assembler, linker and Smaller C compiler with their host,
   qemu-user, a.out and on-board compilation contracts.
8. Close optional programs and games by dependency family; quarantine only
   provenance-bound imported bodies.
9. Ratchet each completed Makefile from `WARNLEVEL=legacy` to `full`, remove
   old dialect overrides, and add the unit to the full Class A gate.
10. Finish when the generated ledger reports zero unclassified tracked C/header
    files, zero maintained K&R definitions, zero maintained non-prototype
    declarations, zero implicit type/call diagnostics and zero uncalibrated
    legacy warning units, with the full build, aggregate checks, image budgets
    and required hardware gates recorded separately.

At tree head `a2a7ef9e494df2753bb24ae62dad803dbc505ddb`, stock LLVM style in
clang-format 22.1.8 proposed
306 replacement records for `lib/libc/gen/ctime.c`, 579 for
`lib/libc/stdio/doscan.c` and 297 for `sys/kern/kern_clock.c`. The counts are
calibration evidence, not a formatting budget; they justify the Class A
allowlist and a reviewed BSD candidate rather than a repository-wide rewrite.

## 1. Tool and evidence contract

The command that decides a C warning must use the same source root, generated
headers, preprocessor definitions, target triple, include order and ABI flags as
the production build. Host analysis supports a claim about host behavior; it
does not prove Cortex-M0+ code generation, ELF-to-a.out conversion, MPU
behavior, flash/XIP safety or the native Smaller C toolchain.

Record the following beside every rollout result:

| Field | Required value |
| --- | --- |
| Source identity | Git commit, dirty-state classification, migration-unit path and quarantine row if applicable |
| Tool identity | Executable path and version for clang-format, clang-tidy, compiler, cppcheck, sparse, scan-build, GCC analyzer and formatter wrapper |
| Build identity | `bmake` target, evaluated flags, target CPU/ABI, generated-header path and compile database hash |
| Scope | Exact file list, include/exclude reason, generated or imported inputs and changed lines |
| Verdict | Exit status, diagnostics, known-good and known-bad fixture result, and whether the result is host, cross, emulator or board evidence |
| Resource result | Changed ELF/a.out sections, stack/heap account, packed/raw root blocks, flash operations and retained artifact paths; use `not run` for unknown values |

The tool inventory used to calibrate this guide is:

| Tool | Observed version | Role |
| --- | --- | --- |
| clang-format / clang-tidy | 22.1.8 | C17 layout and target-aware semantic checks |
| GCC / arm-none-eabi-gcc | 16.2.1 / 16.2.0 | Production diagnostics and exact Cortex-M0+ compilation |
| cppcheck | 2.21.1 | Portable defect, bounds, style and performance checks |
| sparse | 0.6.5-rc1 | Kernel address-space, integer and annotation checks |
| scan-build | installed with Clang | Host path static analysis |
| `bear` | 4.2.2 | Compile database capture from the real build |
| `lizard` | 1.24.0 | Complexity and migration sizing, informational until calibrated |
| ShellCheck / Ruff | installed; versions recorded by CI | Existing shell and Python gate; unchanged by this guide |

The executable paths and package ownership are resolved on the execution host
before a result is published. A missing tool is reported as `not run`; the
guide does not replace it with a different version or a guessed compiler.

That earlier tree-head inventory contains 1,934 tracked C files, 556 tracked
headers and 810,340 combined C/header lines. Twenty-three tracked Makefiles or
make fragments select `-ansi`, C89/C90/C99/C11 or a GNU equivalent. Those are
scope measurements, not defect counts: generated, imported and quarantined
rows still need classification, and a lexical K&R search produces false
positives around control statements and calls.

## 2. Source classes and enforcement boundaries

The file list is a policy input. It must be generated from Git and the build,
not from a broad `find` that includes stale objects or vendored data.

| Class | Examples | Formatter | C17 compiler/linter | Extra proof |
| --- | --- | --- | --- | --- |
| A: project-owned maintainable C | `lib/`, `bin/`, `sbin/`, `usr.bin/`, `sys/kern/`, RP2040 C sources and maintained host tools | Changed-file check first; eventual full allowlist | Full warning profile, targeted clang-tidy/cppcheck and the unit's native gates | Exact target build, ABI/footprint and behavior |
| B: native compiler implementation | `usr.bin/smlrc`, its Thumb-1 backend and support files | C17 formatter after the implementation migration is reviewed | C17 host and target build; targeted analyzers | Existing Smaller C suite, qemu-arm and board evidence when claimed |
| C: C at a maintained boundary | Emulator adapter, generated-header consumer, libc shim or POSIX host fixture | Formatter only for project-owned lines | C17 wrapper/adapter; imported body keeps provenance | Interface, license and generated-input checks |
| D: quarantined historical or licensed guest | SIMH/V6 guest source and immutable images; only the corresponding rows under `legacy/pdp11-v6/usr.bin/pdp11` | Excluded and documented | No rewrite of the guest source; analyze the project-owned emulator and adapters separately | Quarantine manifest, source hash and license record |
| E: generated, imported or machine-emitted text | Generated headers, assembler output, tables and vendored snapshots | Excluded from mutation; format the generator if project-owned | Compile or lint the generator and boundary code | Reproducibility and provenance |
| F: assembly and binary fixtures | Thumb assembler, boot blobs, a.out fixtures and traces | Never run a C formatter | Use assembler/ABI checks where applicable | Disassembly, symbol and conversion gates |

Class A is the end state for all maintainable C, including files that currently
select K&R, C89/C90, C99 or C11. Classes D through F are narrow source
boundaries, not a second project-wide legacy profile. The PDP-11 emulator
lives under `legacy/pdp11-v6/`, a separately enabled RP2040 option
(`docs/research/arm-main-legacy-build-isolation.md`); when that option builds
it, its project-owned implementation is Class A unless a specific imported
guest file is named in the quarantine manifest. Sources under
`legacy/non-arm/` are a provenance archive outside every build and outside
this plan.

## 3. Candidate clang-format policy

The following configuration is a reviewed candidate derived from the BSD style
rules. It is not yet a repository `.clang-format`; committing one before the
allowlist and fixture calibration would format files that are intentionally
outside Class A.

```yaml
Language: Cpp
BasedOnStyle: LLVM
Standard: Cpp11
UseTab: ForIndentation
TabWidth: 8
IndentWidth: 8
ContinuationIndentWidth: 4
ColumnLimit: 80
PointerAlignment: Right
DerivePointerAlignment: false
BreakBeforeBraces: Custom
BraceWrapping:
  AfterFunction: true
  AfterControlStatement: Never
  AfterStruct: false
  AfterUnion: false
  AfterEnum: false
SpaceBeforeParens: ControlStatements
SpaceBeforeAssignmentOperators: true
SpacesInParentheses: false
SpacesInSquareBrackets: false
AlignConsecutiveAssignments: None
AlignConsecutiveDeclarations: None
AlignEscapedNewlines: Left
SortIncludes: Never
ReflowComments: false
SkipMacroDefinitionBody: true
AllowShortFunctionsOnASingleLine: Inline
AllowShortBlocksOnASingleLine: Empty
```

The `Language: Cpp` spelling is clang-format's language selector; it formats C
and C++ syntax with the same configuration. `Standard: Cpp11` describes the
parser option accepted by clang-format and does not change the compiler's C17
standard. The repository's actual C compiler command remains authoritative.

The candidate intentionally does not sort includes, rewrite comments, or
normalize macro bodies; `SkipMacroDefinitionBody: true` keeps each `#define`
replacement list as written, where LLVM style alone rewrites `a+b` as `a + b`. Include order carries project-header dependencies, and
comments carry hardware, license and evidence authority. A formatter change
that moves an include or comment is a semantic review, not whitespace cleanup.

### Read-only formatter checks

Run the formatter against an explicit Class A file list. The command must exit
non-zero if a file would change and must not write source in CI. The tree has
no `.clang-format`, and `--style=file` without one falls back to LLVM style, so
every check names the candidate file and disables the fallback. `$CAND` is the
section 3 configuration written to an untracked file:

```sh
clang-format --version
clang-format --dry-run --Werror --style="file:$CAND" --fallback-style=none \
  --assume-filename=foo.c path/to/maintained.c
```

For a migration unit, generate the list from Git and remove the known boundary
classes in a reviewed script rather than using an unchecked glob:

```sh
git ls-files '*.c' '*.h' |
  tools/c17-file-classify --class A --null |
  xargs -0 -r clang-format --dry-run --Werror --style="file:$CAND" \
    --fallback-style=none
```

`tools/c17-file-classify` is a planned narrow wrapper, not a command currently
present in the tree. Until it lands, the reviewer supplies the exact paths and
records the class decision in the migration ledger. A future wrapper must
reject unknown classifications instead of treating them as Class A.

For a local review, `git clang-format` shows the proposed patch without
mutating the worktree:

```sh
git clang-format --style="file:$CAND" --diff <base-commit> -- path/to/maintained.c
```

The diff is reviewed for token movement, macro continuation, string literals,
preprocessor conditionals, comments and generated-code markers. A formatter
pass is a separate commit from a semantic C17 repair unless the migration unit
requires a one-line layout change to make the repair legible.

## 4. Compiler warning profiles

`share/mk/warnings.mk` already distinguishes the `full` and `legacy` profiles.
The C17 migration ratchets a unit from the latter to the former; it does not
silence a diagnostic to keep an old profile green.

The baseline for a maintainable unit is:

```text
-Wall -Wextra -Werror
```

The following findings are mandatory C17 migration diagnostics where the
selected compiler provides them:

```text
-Werror=implicit-function-declaration
-Werror=implicit-int
-Werror=old-style-definition
-Werror=strict-prototypes
-Werror=missing-prototypes
-Werror=return-type
-Werror=pointer-sign
```

Add `-Wconversion`, `-Wsign-conversion`, `-Wshadow`, `-Wcast-align` and
`-Wvla` one migration family at a time. These checks are useful but interact
with BSD ABI casts, MMIO access, generated headers and deliberate signed
protocol fields. Each addition first passes a known-good/known-bad fixture and
records the remaining findings by owner. GCC and Clang diagnostics are
separate evidence; a clean host build does not replace the exact
`arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -mabi=aapcs` build.

The target command retains the tree's production includes, startup objects,
linker script and `-fno-common`. A strict portable pass may add `-std=c17
-pedantic`, but it does not replace the production `-std=gnu17` pass where a
documented GNU extension or target builtin is part of the interface.

## 5. Compile database and semantic analyzers

Capture commands from the real build. Do not hand-write a compile database
with host flags:

```sh
rm -f compile_commands.json
bmake MACHINE=rp2040 cleankernel
bear -- bmake MACHINE=rp2040 kernel
```

Bear records only the compiler executions it observes, so an up-to-date tree
yields an empty or partial database; `cleankernel` before the capture makes every
kernel translation unit compile, and a database missing an expected unit is
rejected rather than analyzed. The capture is an untracked analysis artifact. Inspect it for target CPU,
include roots, generated headers, `-D` values, language flags and duplicate
commands before passing it to a semantic tool. A libc or public-header change
requires a clean consumer build because the repository documents incremental
dependency gaps.

Use clang-tidy as a targeted analyzer, not as a global style oracle. Start
with the checks available in the pinned binary:

```sh
clang-tidy -checks='*' -list-checks
clang-tidy -p . -checks='-*,'\
'clang-analyzer-core.*,'\
'clang-analyzer-security.*,'\
'bugprone-sizeof-expression,'\
'bugprone-misplaced-widening-cast' \
  --extra-arg=-D__KERNEL__ path/to/maintained.c
```

The compiler command comes from `compile_commands.json` through `-p`.
`--extra-arg` appends to that command; arguments after `--` would replace it
and drop the captured target defines, include roots and CPU/ABI flags. The
example adds only a boundary-specific define. Add a check after its diagnostic is reviewed
against BSD headers and a rejecting fixture. Do not enable broad
`cert-*`, reserved-identifier, or project-wide readability/cppcoreguidelines
sets: `_IO*`, `_SYS_*`, `__unused`, ABI macros and target headers are deliberate
in this tree. A check that cannot distinguish those names from a new mistake is
not a gate.

Run cppcheck on portable Class A units with the project include roots:

```sh
cppcheck --std=c17 --enable=warning,style,performance,portability \
  --inline-suppr --error-exitcode=1 path/to/maintained.c
```

Every suppression names the mechanism and source line, and the reviewer first
checks whether a C17 repair removes the finding. `--inline-suppr` is not a
blanket escape hatch.

Run sparse only where kernel annotations and address spaces are meaningful:

```sh
bmake -C sys/arch/rp2040/compile/PICO \
  CHECK="sparse --clang --Wbitwise --Waddress-space --Wcontext"
```

The exact invocation follows the kernel Makefile's generated command; the
example is a routing description rather than a claim that the current Makefile
accepts `CHECK` unchanged. Sparse output is a kernel evidence class, not a
userland C17 verdict.

Run scan-build and GCC's analyzer on host-representable units, keeping the
target build separate:

```sh
scan-build --status-bugs --keep-going bmake -C path/to/host-unit clean all
gcc -std=c17 -Wall -Wextra -Werror -fanalyzer -c path/to/host-unit.c
```

The host analyzer may identify ownership and path errors hidden by the board's
flat memory map. It cannot establish RP2040 register side effects, Thumb-1
instruction selection or a.out loading.

Use `ast-grep`, `weggli` or `rg` for structural migration inventories:

```sh
rg -n --glob '*.[ch]' '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*\([^;]*\)$'
rg -n --glob '*.[ch]' '#[[:space:]]*define|__attribute__|asm[[:space:]]*\('
```

Regex results are leads. A declaration or macro is accepted only after the
compiler, AST or target build identifies its semantics. `lizard` and similar
metrics size a migration unit and expose a trend; they do not reject a source
because a small, bounded loop has a high numerical score.

## 6. Calibration fixtures and mutation tests

Every new formatter or linter gate gets at least one accepting and one
rejecting case. The rejecting cases cover the defects this tree actually sees:

| Fixture | Expected rejection |
| --- | --- |
| K&R function definition and implicit call | Old-style definition, strict-prototype and implicit-declaration diagnostics |
| Header tentative definition included by two C files | `-fno-common` duplicate-definition link failure |
| VLA or input-sized scratch buffer | `-Wvla` or a project semantic check, followed by a bounded rewrite |
| Signed overflow, invalid shift or unchecked narrowing | Compiler warning, UBSan/host boundary failure or analyzer finding |
| MMIO register with wrong access width | Target review check or sparse/ABI fixture |
| Unformatted modernized C file | clang-format `--dry-run --Werror` failure |
| SIMH/V6 guest file | Classifier exclusion with a quarantine-manifest match, never a silent pass |

Mutation tests change one token or invariant at a time: remove a prototype,
increase a field width, delete a bound, swap a register alias, or add one
unclassified file. A gate that passes both the known-good and known-bad form is
inert and cannot count as evidence. Retain fixture source and tool output only
when the repository's evidence policy calls for it; otherwise keep them in the
untracked calibration workspace and record hashes in the report.

## 7. Rollout and CI wiring

The rollout has four gates and expands only after the preceding gate has a
calibrated rejecting case:

1. **Inventory.** Build a source ledger with class, owner, evaluated dialect,
   license/provenance, migration blocker and exact gate. Flag every K&R,
   C89/C90, C99, C11 and implicit-declaration occurrence; assign quarantine
   rather than silently dropping a row.
2. **Changed-file checks.** Run the candidate formatter and targeted compiler
   warnings on Class A files changed by a pull request. Keep ShellCheck and
   Ruff in the existing `check-lint` target.
3. **Migration-unit checks.** Add the unit's compile database, clang-tidy,
   cppcheck and host/target behavior gates. Remove the old dialect override and
   update the ledger in the same reviewed change.
4. **Full Class A gate.** After the inventory reaches zero unassigned rows,
   format and analyze every maintainable project-owned C file in CI. Quarantine
   matches are checked against the manifest; an unknown path fails the gate.

The initial inventory uses lexical searches only to produce leads. The compiler
and AST then decide whether a lead is real:

```sh
rg -n --glob '*.[ch]' --glob 'Makefile*' --glob '*.mk' \
  -- '-std=(gnu89|gnu90|gnu99|gnu11|c89|c90|c99|c11)|-ansi|__STDC_VERSION__'
rg -n --glob '*.[ch]' \
  '^[[:space:]]*[A-Za-z_][A-Za-z0-9_[:space:]*]*\([^;]*\)[[:space:]]*$'
```

The first query flags every old dialect selector, including a leaf override;
the second finds likely K&R definitions and prototype gaps. `-Wimplicit-*`,
`-Wold-style-definition`, `-Wstrict-prototypes` and `-Wmissing-prototypes`
remain the deciding diagnostics. A search hit in a SIMH/V6 row becomes a
quarantine record, not a silently green result.

The eventual root targets should be named by mechanism, for example:

```text
check-c17-format       read-only clang-format check for Class A
check-c17-compile      GNU17 and strict C17 warning profiles
check-c17-analyze      clang-tidy/cppcheck/sparse by source class
check-c17-calibration  known-good, known-bad and mutation fixtures
```

This guide does not add those targets, a root `.clang-format`, a Class A
allowlist, a quarantine manifest or the classifier; it is documentation only.
The next safe change is a reviewed, finite allowlist artifact. Adding a root
target before its classifier, compile database and calibration fixtures exist
would either rewrite quarantined files or turn missing metadata into a false
pass. When the targets land, `.github/workflows/firmware.yml` and the local
`bmake MACHINE=rp2040 check` tier must invoke the same wrappers and record the
same tool versions.

## 8. Formatting, linting and footprint claims

Formatting is not a code-size optimization, but it is not always
behavior-neutral either. Comments become whitespace before translation and
indentation does not alter tokens, yet whitespace inside a stringified macro
argument reaches the string (`S(a+b)` becomes `"a + b"` once reformatted), and
a moved line changes `__LINE__` and the debug line tables. A formatter-only
patch therefore carries a preprocessed-output comparison, or a behavior and
size comparison, for every unit that uses `#`, `__LINE__` or a location-sensitive
macro. A lint-clean result also says nothing about flash erase count,
SRAM peak, swap wear or process lifetime.

A C17 rewrite can reduce the complete artifact by removing a dependency table,
staging buffer or duplicate conversion, or increase it by adding alignment,
diagnostic strings, atomic helpers or a wider representation. Keep those
effects separate:

```text
source layout       -> formatter diff only
semantic C17 repair -> compiler warnings, behavior and ABI
link closure        -> ELF/a.out sections and symbols
runtime lifetime    -> stack, heap, DMA and overlay peak
storage durability  -> flash program/erase and swap writes
```

The acceptance record therefore reports `a_text`, `a_data`, `a_bss`, stack and
heap peaks, packed/raw root blocks, inode count, flash operations and relevant
latency separately. It names unchanged accounts as unchanged and unknown
accounts as `not run`. Readability, analyzability and C17 explicitness are
valuable engineering outcomes; they are never relabeled as measured bit or
wear savings without the corresponding artifact and command.

## 9. Maintenance and review checklist

Before merging a formatter or linter change, the reviewer confirms:

- the source class and quarantine decision are explicit;
- the tool path, version, compiler flags and compile database are recorded;
- the candidate accepts the known-good fixture and rejects the known-bad one;
- no include, macro continuation, string literal, license notice or hardware
  comment moved without semantic review;
- C17 warnings are fixed at the owning declaration, lifetime or ABI boundary;
- POSIX/SUSv4 behavior is tested per interface rather than inferred from style;
- exact Cortex-M0+ compilation and the relevant a.out, overlay, root and board
  gates are named, with `not run` for every gate that did not run;
- the diff contains no generated output, compile database, secret, token,
  emoji or unowned vendored rewrite;
- the formatter-only and semantic commits remain separately reviewable.

The authoritative language and resource rules remain in
`docs/research/STYLE-GUIDE.md`. This document supplies the executable tooling
path and the staged adoption boundary; it does not broaden either the C17
contract or the repository's hardware evidence claims.
