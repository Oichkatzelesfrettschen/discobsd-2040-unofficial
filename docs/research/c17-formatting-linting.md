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

The same tree-head inventory contains 1,934 tracked C files, 556 tracked
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
AllowShortFunctionsOnASingleLine: Inline
AllowShortBlocksOnASingleLine: Empty
```

The `Language: Cpp` spelling is clang-format's language selector; it formats C
and C++ syntax with the same configuration. `Standard: Cpp11` describes the
parser option accepted by clang-format and does not change the compiler's C17
standard. The repository's actual C compiler command remains authoritative.

The candidate intentionally does not sort includes, rewrite comments, or
normalize macro bodies. Include order carries project-header dependencies, and
comments carry hardware, license and evidence authority. A formatter change
that moves an include or comment is a semantic review, not whitespace cleanup.

### Read-only formatter checks

Run the formatter against an explicit Class A file list. The command must exit
non-zero if a file would change and must not write source in CI:

```sh
clang-format --version
clang-format --dry-run --Werror --style=file --assume-filename=foo.c \
  path/to/maintained.c
```

For a migration unit, generate the list from Git and remove the known boundary
classes in a reviewed script rather than using an unchecked glob:

```sh
git ls-files '*.c' '*.h' |
  tools/c17-file-classify --class A --null |
  xargs -0 -r clang-format --dry-run --Werror --style=file
```

`tools/c17-file-classify` is a planned narrow wrapper, not a command currently
present in the tree. Until it lands, the reviewer supplies the exact paths and
records the class decision in the migration ledger. A future wrapper must
reject unknown classifications instead of treating them as Class A.

For a local review, `git clang-format` shows the proposed patch without
mutating the worktree:

```sh
git clang-format --diff <base-commit> -- path/to/maintained.c
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
bear -- bmake MACHINE=rp2040 kernel
```

The capture is an untracked analysis artifact. Inspect it for target CPU,
include roots, generated headers, `-D` values, language flags and duplicate
commands before passing it to a semantic tool. A libc or public-header change
requires a clean consumer build because the repository documents incremental
dependency gaps.

Use clang-tidy as a targeted analyzer, not as a global style oracle. Start
with the checks available in the pinned binary:

```sh
clang-tidy -checks='-*' -list-checks
clang-tidy -p . -checks='-*,'\
'clang-analyzer-core.*,'\
'clang-analyzer-security.*,'\
'bugprone-sizeof-expression,'\
'bugprone-misplaced-widening-cast' \
  path/to/maintained.c -- -D__KERNEL__
```

The exact trailing flags come from `compile_commands.json`; the example shows
only a boundary-specific define. Add a check after its diagnostic is reviewed
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
make -C sys/arch/rp2040/compile/PICO \
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

This guide does not add those targets or a root `.clang-format` yet. The
current safe change is documentation plus a reviewed allowlist. Adding a root
target before its classifier, compile database and calibration fixtures exist
would either rewrite quarantined files or turn missing metadata into a false
pass. When the targets land, `.github/workflows/firmware.yml` and the local
`bmake MACHINE=rp2040 check` tier must invoke the same wrappers and record the
same tool versions.

## 8. Formatting, linting and footprint claims

Formatting cannot reduce emitted instruction bits. C comments become whitespace
before translation, and indentation does not alter tokens. A formatter may
change debug line tables or expose a semantic diff, but it is not a code-size
optimization. A lint-clean result also says nothing about flash erase count,
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
