# ARM main tree and legacy build isolation

## Status and objective

The maintained source and build surface supports ARM machines only. The
canonical machine registry admits RP2040 and STM32, derives every
architecture value from the selected machine, and rejects inconsistent
caller input before a build recipe runs. Retained non-ARM sources live under
`legacy/non-arm/` as a provenance archive. The PDP-11 emulator, Sixth Edition
UNIX guest pack, and SIMH/V7 reference runner live under
`legacy/pdp11-v6/` as a separately enabled RP2040 option.

The boundary has four properties:

1. An ordinary build reaches maintained ARM source only.
2. A caller selects a complete machine tuple through one machine value.
3. A legacy option defaults to `no` and adds only its declared boundary.
4. Executable gates reject undeclared machines, legacy leakage, changed
   archive identities, stale shared artifacts, and unreviewed portability
   selectors.

This document describes the implemented contract and the evidence required
to change it. It does not claim that archived non-ARM sources compile, that
an emulator result proves board behavior, or that a warning-free build proves
runtime correctness.

## Maintained machine registry

`share/mk/architecture.mk` is the single machine authority. The root
Makefile and `share/mk/sys.mk` include it, and every tracked generated ARM
kernel Makefile includes it.

| Machine | Architecture | CPU | Kernel configurations |
| --- | --- | --- | --- |
| `rp2040` | `arm` | `cortex-m0plus` | `PICO`, `PICO_UART` |
| `stm32` | `arm` | `cortex-m4` | ten tracked STM32F4 configurations |

The default machine is `rp2040`. The registry derives:

```text
MACHINE_ARCH_FLAGS = -mcpu=${MACHINE_CPU} -mabi=aapcs \
                     -mlittle-endian -mthumb -mfloat-abi=soft
TOOLBINDIR          = ${TOPSRC}/tools/bin/${MACHINE}
```

Callers may select a machine and ordinary optimization or feature flags:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 COPTS=-Os build
bmake MACHINE=stm32 COPTS=-O2 build
```

Callers cannot replace `MACHINE_ARCH`, `MACHINE_CPU`,
`MACHINE_ARCH_FLAGS`, `SUPPORTED_MACHINES`, `TOOLBINDIR`, or another private
or derived registry value with an inconsistent value. The parser rejects an
unknown machine, a split tuple such as `MACHINE=rp2040 MACHINE_ARCH=mips`, an
invalid legacy Boolean, and PDP-11/V6 selection for STM32.

The registry uses ordinary assignments for `MACHINE`, `MACHINE_ARCH`, and
`MACHINE_CPU` because bmake predefines host values for those names. A
conditional assignment would preserve the host's x86-64 tuple during a
direct recursive invocation. Command-line values retain bmake's higher
precedence and therefore remain visible to the mismatch checks.

## Architecture-specific flags

The registry owns ISA and ABI flags. A machine owns its CPU selection;
every maintained machine shares little-endian AAPCS, Thumb state, and the
soft-float ABI. Makefiles add optimization and product definitions through
the existing `COPTS`, `CFLAGS`, and `DEFS` surfaces.

The separation prevents three invalid states:

- an RP2040 build compiled for Cortex-M4;
- an STM32 build compiled for Cortex-M0+;
- a machine name selecting source directories from an unrelated
  architecture.

Adding a maintained CPU variant requires a machine registry row or a new
declared board-to-CPU mapping. A free-form caller override is not a supported
variant mechanism.

## Main-tree boundary

The maintained tree contains:

- ARM common headers under `sys/arch/arm`;
- RP2040 kernel, board, distribution, host, and test code;
- STM32 kernel and distribution code;
- architecture-neutral kernel, libc, userland, host tools, and tests that the
  ARM builds consume;
- imported or generic portability code whose retained selectors are listed
  exactly in the selector allowlist.

The maintained tree excludes:

- PIC32 and MIPS kernel, board, library, toolchain, device, example, and
  generated configuration sources;
- non-ARM Smaller C and assembler backends and fixtures;
- PDP-11 and VAX implementation assets and machine-specific manual pages;
- Win32, VMS, Surveyor/Blackfin, and other evidenced non-ARM platform files;
- the separately isolated PDP-11/V6 product option;
- two incomplete Flying Fox PicoC files whose ISA remains unproven.

The boundary concerns implementation and evaluated build selection.
Historical prose may name another architecture when the prose clearly
describes history, a format, an imported registry, or archived provenance.
Historical words do not create a supported build target.

## Legacy source classes

### Non-ARM archive

`legacy/non-arm/` preserves 1,008 source paths. The archive contains the
former MIPS/PIC32 port, non-ARM compiler backends and fixtures, and related
platform assets. The explicit option validates archive identity only:

```sh
bmake MACHINE=rp2040 BUILD_LEGACY_NON_ARM=yes legacy-non-arm-verify
```

`BUILD_LEGACY_NON_ARM=yes` does not add the archive to `SUBDIR`, build,
install, distribution, or a supported test matrix. A successful archive
verification makes no compilation, boot, runtime, or hardware claim.

### PDP-11 and V6 option

`legacy/pdp11-v6/` preserves 22 relocated source and evidence paths plus its
repository-owned boundary files. `BUILD_PDP11_V6` defaults to `no`, accepts
only `yes` or `no`, and accepts `yes` only for `MACHINE=rp2040`.

The opt-in interface separates distinct evidence surfaces:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON

bmake MACHINE=rp2040 BUILD_PDP11_V6=yes legacy-pdp11-v6-build
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes legacy-pdp11-v6-install
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes legacy-pdp11-v6-distribution
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v6
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v7-runner
```

The external V7 oracle needs a separately obtained disk image:

```sh
PDP11_V7_IMAGE=/path/to/v7.dsk \
    bmake MACHINE=rp2040 BUILD_PDP11_V6=yes \
        check-legacy-pdp11-v7-reference
```

The V6 test boots the repository emulator and guest pack on the host. The V7
runner tests exercise the runner without an external image. The V7 reference
target exercises Open SIMH and an external image; it does not test the
repository emulator. Each result stands for itself.

### Unsupported platform archive

`legacy/unsupported-platforms/` contains two incomplete Flying Fox PicoC
files. Source and history do not establish an instruction-set architecture,
so the relocation map classifies them as `legacy-unsupported-platform`
instead of asserting that they are non-ARM. The main build has no selector or
entry point for them.

## Relocation provenance

`tools/architecture-isolation/legacy-path-map.tsv` is the canonical finite
relocation set. Each row records:

```text
class, source, destination, origin Git blob, origin SHA-256, archive policy
```

The map is anchored to the exact pre-relocation `main` revision in its first
line. Its current denominator is:

| Class | Rows |
| --- | ---: |
| `legacy-non-arm` | 1,008 |
| `legacy-pdp11-v6` | 22 |
| `legacy-unsupported-platform` | 2 |
| total | 1,032 |

The generator derives the rows from reviewed path declarations. The verifier
has two modes:

```sh
sh tools/architecture-isolation/verify-legacy-path-map.sh \
    "$PWD" tools/architecture-isolation/legacy-path-map.tsv source
sh tools/architecture-isolation/verify-legacy-path-map.sh \
    "$PWD" tools/architecture-isolation/legacy-path-map.tsv archive
```

Source mode resolves each original path at the recorded revision and compares
its Git blob and SHA-256 with the map. Archive mode requires each original
path to have left the index, each destination to exist exactly once, and each
destination identity to match its declared policy. Symbolic links use Git
object identity rather than regular-file assumptions.

`tools/architecture-isolation/legacy-boundary-files.list` names the new
repository-owned Makefiles, READMEs, and manifest fragments inside legacy
roots. Those files are not represented as historical moves. The architecture
gate requires each archive root to equal relocation destinations plus its
declared boundary files.

After a rebase that changes the source revision, regenerate the map from the
new base and verify both modes before accepting it:

```sh
sh tools/architecture-isolation/generate-legacy-path-map.sh main \
    > tools/architecture-isolation/legacy-path-map.tsv.new
diff -u tools/architecture-isolation/legacy-path-map.tsv \
    tools/architecture-isolation/legacy-path-map.tsv.new
```

The row-key delta must equal the reviewed source delta. Changed blob witnesses
must also appear at their archive destinations. A source-revision update by
itself is insufficient.

## Retained portability selectors

Moving whole files does not remove dormant non-ARM branches from shared
source. Project-owned branches that selected PDP-11, VAX, MIPS, PIC32, x86,
M68K, SPARC, PowerPC, HPPA, and other retired target behavior were simplified
to the maintained ARM path.

`tools/architecture-isolation/main-tree-portability-selector-allowlist.txt`
contains the remaining 22 exact `path:directive` rows. The rows belong to:

- imported compiler runtime host and integer capability logic;
- imported TinyUSB MCU and compiler validation;
- pForth Solaris host portability;
- Whetstone host-platform portability;
- host portability in `elf2aout`.

The repository-wide discovery expression excludes `legacy/`, normalizes
indentation, sorts in the C locale, and requires exact equality with the
allowlist. A selector in an unrelated maintained path changes the set and
fails the gate. The reviewed token family is a lexical boundary; source
review must expand it when a new architecture spelling appears.

The retained rows do not declare supported non-ARM DiscoBSD machines. They
preserve imported or generic host behavior required by maintained code.

## Build and artifact isolation

The source tree writes objects and libraries into source directories. Many
Makefiles derive `TOPSRC` from relative paths, reach sibling objects, and
generate headers or symlinks in place. Setting `MAKEOBJDIRPREFIX` globally
would redirect those assumptions before recipes execute and would not produce
a working out-of-tree build.

The implemented boundary has two parts:

1. Installed host tools use `tools/bin/${MACHINE}`.
2. Shared target artifacts use `distrib/obj/.build-machine` as an atomic
   per-worktree tuple stamp.

The root Makefile declares `.MAIN: all`. RP2040's included distribution
fragment defines a composed-manifest file target before the root build rules;
without an explicit main target, a targetless `bmake` would compose only that
manifest. The architecture gate requires the targetless and explicit `build`
dry runs to be identical and deletes `.MAIN` in a rejecting fixture.

`tools/check-build-machine.sh` publishes
`MACHINE/MACHINE_ARCH/MACHINE_CPU` with atomic hard-link creation. Repeated
use of the same tuple succeeds. A conflicting tuple fails before an
object-producing recipe executes and names the required `cleanall` command.
Two conflicting first writers cannot both publish a different tuple.

`bmake -n` and `bmake -V` remain read-only and create no stamp. Clean-only
target sets bypass the build guard. `cleanall` calls
`tools/clean-build-machines.sh`, cleans every shared userland, tool, kernel,
filesystem, and PDP-11/V6 artifact class, and removes the stamp only after
all cleanup calls succeed. A failed cleanup retains the stamp and therefore
retains failure visibility.

The cleanup helper treats a slash-containing make command as an explicit
pathname and requires that file to be executable. It applies `command -v`
only to a bare command name. The separation gives POSIX shells one exact
admission rule and prevents a non-executable pathname from entering cleanup
through implementation-defined command-search behavior.

One worktree supports one active tuple. Concurrent RP2040 and STM32 builds
use separate Git worktrees. A future source-relative-path conversion may
replace the stamp with true per-machine object roots, but the conversion must
repair and validate every Makefile assumption as its own project.

### Maintained build ordering

Every generated ARM kernel Makefile orders its prerequisites as:

```make
SYSTEM_DEP= Makefile ioconf.c .WAIT machine sys .deps .WAIT ${SYSTEM_OBJ}
```

The first barrier completes configuration generation. Rebuilding `ioconf.c`
can invoke `clean`, which removes `machine`, `sys`, and `.deps`; the second
barrier recreates those paths before any object compile starts. Both source
templates and all 12 tracked generated Makefiles carry the same order. The
architecture gate removes the second barrier in a calibrated fixture and
requires rejection.

STM32 also builds PicoC, which RP2040 omits. PicoC consumes the maintained
libc definitions for `FILENAME_MAX`, `L_tmpnam`, `fgetpos()`, and `fsetpos()`.
Its source-execution helper owns the `setjmp()` exit point, so mode-selection
locals live outside the function a later `longjmp()` resumes. A serial warning
census records 339 `-Wall`/`-Wextra` instances at 165 distinct sites; 158 sites
are interpreter-ABI callback parameters. The directory therefore declares the
repository's measured `WARNLEVEL=legacy` migration state while all enabled
warnings remain fatal.

`usr.bin/tip` invokes its nested ACU build with `${MAKE} -C aculib`. A
`cd aculib; ${MAKE}` recursive recipe changes the working directory observed
by parallel sibling jobs under bmake; the install recipe then searches for
`aculib/tip`. The link target depends on the ACU build, and every nested build,
install, clean, and depend route uses `-C`.

UnixBench builds normal and register Dhrystone variants in parallel. The
variants own `dhry_1.o`/`dhry_2.o` and
`dhry_1_reg.o`/`dhry_2_reg.o`, respectively; each link target declares its
objects as prerequisites, and only the directory clean rule removes them.
That graph prevents either link recipe from overwriting or deleting the other
variant's inputs.

## Filesystem composition

The generic base manifest contains only paths produced by the maintained
build. `tools/architecture-isolation/retired-base-manifest-paths.txt` records
29 removed rows: retired PIC32 programs, headers, libraries, and manuals; the
RP2040-only compiler driver formerly declared generically; and three unbuilt
glob test programs. The architecture gate pins the exact sorted set and
rejects any row reintroduced into `distrib/base/mi`.

The maintained RP2040 manifest and profiles contain five profiles and zero
PDP-11/V6 declarations or paths. The maintained `full` profile means every
maintained ARM closure.

When `BUILD_PDP11_V6=yes`, `distrib/rp2040/Makefile.inc` passes the legacy
manifest and profile fragments as additional checked inputs and selects the
`pdp11` and `v6disk` closures. The composed filename gains the
`.pdp11-v6` suffix, so a maintained and opt-in composition do not share a
manifest pathname.

`mkmanifest.py` accepts repeated manifest and profile inputs, constructs one
closure graph, and rejects duplicate paths, missing parents, dangling hard or
symbolic links, undeclared closures, unmet closure dependencies, and unmet
path dependencies. It checks dependencies instead of silently adding them.
Its selftest mutates maintained compositions and, when the legacy fragments
are present, mutates the emulator and guest dependencies as separate cases.

The default image contains neither `/usr/bin/pdp11` nor `/usr/v6/root.rk`.
The opt-in image contains both or fails composition.

## Architecture-isolation gate

The root target is:

```sh
bmake MACHINE=rp2040 check-architecture-isolation
```

`tools/architecture-isolation/check-architecture-isolation.sh` currently
executes 140 assertions across:

- the two canonical ARM tuples;
- invalid machine, architecture, CPU, private-variable, derived-variable,
  tool-directory, and legacy-option inputs;
- all 12 tracked generated ARM kernel Makefiles;
- targetless, explicit, and opt-in dry-run dependency closures;
- default and legacy filesystem compositions;
- all 1,032 relocation rows in source and archive modes;
- the exact 22-row portability-selector allowlist and the retired MIPS, VAX,
  and PDP-11 token family;
- the exact 29-row retired base-manifest set;
- atomic stamp publication, repeat use, conflict, malformed content, and
  symbolic-link rejection;
- cleanup success, cleanup failure retention, canonical path resolution, and
  inherited-environment clearing.

The gate carries deliberate bad fixtures. It must reject:

- a comment-only registry include where an active include is required;
- a root Makefile whose explicit main target has been removed;
- a default dependency edge into any legacy root;
- an old `usr.bin/pdp11`, `tests/pdp11_reference`, SIMH, or `/usr/v6` edge;
- a duplicate relocation key or changed origin hash;
- a PDP-11/V6 entry in a maintained manifest or profile;
- a retired product path in the generic base manifest;
- unrelated WIN32, MIPS, VAX, or PDP-11 selectors in maintained source;
- two conflicting first stamp writers;
- malformed or symbolic-link stamps;
- stamp removal after an injected cleanup failure;
- inherited Make and machine values leaking into cleanup submakes.

A gate that accepts one of those mutations has stopped deciding and cannot
support an isolation claim.

Source-mode relocation verification resolves the pinned pre-migration Git
revision. Both firmware workflow jobs therefore use full-history checkouts;
the default one-commit checkout cannot execute the provenance gate. CI still
evaluates the exact branch head and remains distinct from local gate results.

## Tool selection

`~/Documents/AI/Notes/1_TOOLS.md` supplied the host tool registry. Tool choice
follows the claim rather than the size of the tool:

| Question | Primary tool | Why it decides the question |
| --- | --- | --- |
| Which tracked paths and selectors exist? | `git ls-files`, `git grep`, `rg` | exact lexical and tracked sets |
| Which source does Make select? | `bmake -V`, `bmake -n` | evaluated variables and dependency traversal |
| How large is the migration surface? | `scc`, `lizard` | bounded review and complexity measurements |
| Does an ambiguous symbol edge cross the boundary? | `ctags`, `readtags`, `cscope`, `clangd` | indexed definition and reference paths |
| Can a repeated structural pattern be rewritten safely? | `ast-grep`, `spatch`, `comby` | syntax-aware matching after calibration |
| Did relocation preserve content? | Git blob IDs, SHA-256, `diffoscope` | repository and byte identity |
| Is the output ARM and correctly linked? | `arm-none-eabi-readelf`, `nm`, `objdump`, `size` | compiled ISA, symbols, sections, and footprint |
| Does the ARM instruction sequence execute? | `qemu-arm` | runtime instruction evidence without a board claim |
| Do scripts and Python pass policy? | ShellCheck, Ruff | repository warning policy |

The host currently resolves every primary source, Make, ARM binutils, lint,
and QEMU tool above. Bare `simh` and `pdp11` commands do not resolve; the
external V7 reference remains a separately provisioned gate. Dynamic tracers,
fuzzers, decompilers, and whole-program analyzers do not decide source
placement or Make reachability, so the implementation does not use them as
decorative evidence.

## Validation matrix and evidence limits

Run the narrow boundary proofs first:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON

bmake MACHINE=rp2040 check-architecture-isolation
bmake MACHINE=rp2040 check-config-makefile
bmake MACHINE=rp2040 check-kernel-metadata
bmake MACHINE=rp2040 check-fs-profiles
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-fs-profiles
bmake MACHINE=rp2040 check-lint
bmake MACHINE=rp2040 check-warning-policy-host
bmake MACHINE=rp2040 check-build-failure
bmake MACHINE=rp2040 check-host-package
```

Then prove maintained build surfaces in separate clean worktrees or with a
complete `cleanall` transition:

```sh
bmake MACHINE=rp2040 cleanall
bmake MACHINE=rp2040 build
bmake MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check-host
bmake MACHINE=rp2040 check-cross
bmake MACHINE=rp2040 check-qemu
bmake MACHINE=rp2040 check-board-build

bmake MACHINE=rp2040 cleanall
bmake MACHINE=stm32 distribution
```

Then prove the explicit legacy product independently:

```sh
bmake MACHINE=rp2040 cleanall
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes legacy-pdp11-v6-build
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v6
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v7-runner
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes legacy-pdp11-v6-distribution
```

The implementation has produced the following local evidence:

| Surface | Observed result |
| --- | --- |
| Architecture gate | 140 assertions, two maintained machines, 12 generated ARM kernel Makefiles, 1,032 relocation rows, 22 retained selectors, and 29 retired manifest paths pass |
| STM32 distribution | A clean parallel build exits zero; the 421,528,576-byte image contains 1,159 files, 98 devices, 349 hard links, and seven symbolic links; every composed `file` and `pack` source exists in the staged root |
| RP2040 distribution | A clean parallel build exits zero; the 1,012,736-byte image contains 54 files, 18 devices, 80 hard links, and one symbolic link; all 54 composed source paths are unique and present |
| Maintained gates | Config generation, kernel metadata, lint, host, cross, QEMU, board-build, and default and opt-in profile gates exit zero; lint covers 99 shell scripts and 59 Python files |
| PDP-11/V6 option | The host emulator boots the V6 pack and prints the expected marker; 25 V7 runner tests pass; the cross-built opt-in image contains 56 unique and present file sources including `/usr/bin/pdp11` and `/usr/v6/root.rk` |
| Workflow syntax | Actionlint accepts the full-history checkout change |

Exact-head CI remains a publication gate. The external SIMH/V7 run remains
unrun because its disk image is deliberately external. Renode and board runs
remain unrun because source/build isolation does not authorize or require a
modeled or physical boot.

Evidence classes remain separate:

- map verification proves declared source and archive identities;
- dry runs prove evaluated dependency reachability;
- a warning-clean build proves compilation and link completion;
- host gates prove their modeled contracts;
- qemu-user proves the named instruction sequence;
- the V6 host test proves the emulator and pack interaction on the host;
- the SIMH/V7 target proves its external reference configuration;
- Renode proves the modeled boot path;
- a board run proves silicon behavior.

One class does not silently substitute for another. The isolation change does
not require a flash or board operation.

## Migration and rollback

The migration order is:

1. Freeze the source revision and classify the complete tracked denominator.
2. Add and calibrate the machine registry and tuple stamp.
3. Generate the relocation map before moving source.
4. Move only declared rows and verify archive identity.
5. Remove default build, install, manifest, test, CI, and runtime edges.
6. Simplify project-owned mixed-source selectors to the ARM path.
7. Freeze the remaining imported or generic selector set.
8. Add rejecting mutations for placement, reachability, composition, and
   artifact switching.
9. Reconcile active documentation and build both maintained ARM machines.
10. Integrate through a reviewed merge commit and synchronize the canonical
    checkout.

A source-only rollback restores a row from its destination to the map's source
path and restores the corresponding selectors from the recorded base. A
supported non-ARM restoration is not a rollback. It is a new port admission
that requires a complete machine/architecture/CPU tuple, owned toolchain,
machine-qualified tools or object roots, warning-clean build, tests, runtime
evidence, documentation, and new calibrated gate rows.

The archive remains preferable to deletion while provenance, licensing, or
future research value exists. Removing an archived row requires a deliberate
map-key deletion, license and documentation review, and proof that no retained
artifact refers to it.

## Maintenance checklist for the next agent

Before changing this boundary:

1. Read `share/mk/architecture.mk`, this document, and the architecture gate.
2. Verify the live branch, base revision, worktree status, and active tuple
   stamp.
3. Run the gate before editing to establish the baseline.
4. Add the proposed machine, source, selector, or legacy row to the canonical
   denominator before changing derived consumers.
5. Add a bad fixture that the old gate accepts and the new rule rejects.
6. Use a separate worktree for a second maintained machine build.
7. Regenerate provenance after rebasing and inspect exact row and blob deltas.
8. Update `docs/INDEX.md`, `sys/arch/rp2040/doc/TESTING.md`, and profile
   measurements when the user-visible build surface changes.
9. Keep host, cross, emulator, Renode, and board verdicts distinct in the PR.
10. Preserve unrelated worktrees and the canonical checkout's untracked
    files through merge and cleanup.

The scratch ledger `docs/scratchpad_isolation_plan.md` retains the audit's
chronological observations and superseded counts. This document is the
reconciled authority for the resulting design.
