# ARM Main Tree and Legacy Architecture Isolation Scratchpad

Status: working evidence ledger

Source commit: `c8066894671158220d978c741e5377d89a52eb3e`

## Purpose

The repository needs an ARM-only default source and build surface. Retained
non-ARM ports belong behind a visibly separate legacy boundary. PDP-11
emulation and V6 guest material need an additional opt-in boundary because
their provenance, language profile, and build contract differ from maintained
ARM code.

This scratchpad records commands, observations, decisions, and falsifiers
during the audit. The completed work will reconcile the evidence into one
indexed design document under `docs/research/`.

## Required outcomes

- The default build selects an ARM target explicitly and accepts
  architecture-specific flags through one documented interface.
- The maintained main tree contains ARM architecture support only.
- Every retained non-ARM source path lives under an explicit legacy boundary
  and remains absent from default dependency closure.
- PDP-11 emulator support and V6 guest material use a dedicated build option
  that defaults to disabled.
- An executable gate rejects a newly reachable non-ARM source, an undeclared
  architecture, and an enabled-by-default legacy option.
- The migration preserves the active uncommitted work owned by other
  worktrees.

## Live integration observations

- Local `main` and `origin/main` both resolved to the source commit after
  `git fetch --all --prune`.
- The canonical checkout contained one pre-existing untracked `.ignore`; the
  isolation work does not own that path.
- The active `feature/kernel-metadata-capacity-evidence` worktree contained
  uncommitted changes in shared kernel files, RP2040 files, PIC32 files, STM32
  files, generated kernel Makefiles, and the root Makefile. The isolation
  branch must preserve those bytes and record a path-by-path reconciliation
  prerequisite before moving overlapping files.
- The fetched GitHub repository reported zero open pull requests at the audit
  start.

## Evidence model

The architecture denominator starts from tracked paths, evaluated Make
selection, generated build files, manifests, tests, workflows, and
documentation. A directory name alone does not establish build reachability.
Each row needs a stable key, an owner class, a default-build state, an explicit
build option, and a verifier.

The initial row states are:

- `arm-main`: maintained ARM source reachable by an explicit ARM build.
- `shared-main`: architecture-neutral source required by ARM builds.
- `legacy-non-arm`: retained non-ARM source excluded from the main tree and
  default build.
- `legacy-pdp11-v6`: PDP-11 emulator or V6 guest material excluded by its own
  disabled-by-default option.
- `remove`: source with no retained build, provenance, or documentation owner.
- `unknown`: evidence remains insufficient to classify the row.

## Known unknowns

- The architecture-specific tracked-path denominator is closed at the pinned
  source commit; retained mixed-source branches still need simplification and
  executable verification.
- Root and recursive Make defaults may admit targets without a `MACHINE`
  assignment.
- Generated PIC32 and STM32 Makefiles may encode shared configuration inputs
  that cannot move independently.
- PDP-11 host emulator code, PDP-11 kernel code, and V6 guest images may have
  different licensing and build owners.
- Host tests named for MIPS may validate architecture-neutral conversions
  needed by RP2040; the audit must distinguish test input ISA from supported
  target ISA.
- Documentation and CI may advertise build support that source reachability no
  longer provides.

## Tool routing

- `/usr/bin/rg`, `git grep`, and `git ls-files` establish the lexical and
  tracked-path denominator.
- `bmake -n`, Make variable inspection, and `bear` expose evaluated selection
  and compiler flags.
- `ctags`, GNU Global, and `cscope` resolve ambiguous symbol and include edges.
- `ast-grep` and Coccinelle remain reserved for calibrated structural checks or
  reviewed mechanical rewrites.
- `scc` and `lizard` measure surface area and review risk.
- `arm-none-eabi-gcc`, `arm-none-eabi-nm`, `arm-none-eabi-readelf`,
  `arm-none-eabi-objdump`, and `arm-none-eabi-size` verify ARM build artifacts.
- `diffoscope` and cryptographic hashes compare moved retained sources and
  generated artifacts.
- ShellCheck, Ruff, compiler warnings-as-errors, and repository gates verify
  integration hygiene.

## Observation ledger

Further entries will name the command, bounded surface, result, limitation,
and next discriminating action.

### Tracked architecture roots

Command: `git ls-tree -d --name-only HEAD:sys/arch` and a tracked-file count
grouped by the third path component.

Result:

| Root | Tracked files | Initial class |
| --- | ---: | --- |
| `sys/arch/arm` | 5 | `arm-main` common Cortex-M headers |
| `sys/arch/pic32` | 132 | `legacy-non-arm` candidate |
| `sys/arch/rp2040` | 116 | `arm-main` |
| `sys/arch/stm32` | 112 | `arm-main` |

Limitation: architecture-dependent code also lives under `lib`, `tools`,
`usr.bin`, `distrib`, `include`, tests, and shared Make fragments.

Next action: close the architecture rows through evaluated build reachability,
not directory names alone.

### Default selection and flag ownership

Command: `/usr/bin/rg -n` over root and shared Makefiles, followed by `bmake
-V` for `rp2040`, `stm32`, and `pic32` selections.

Result:

- `Makefile` and `share/mk/sys.mk` both assign `MACHINE=stm32` and
  `MACHINE_ARCH=arm`.
- The caller can override `MACHINE` and `MACHINE_ARCH` independently. The
  build system carries no registry that rejects `MACHINE=rp2040
  MACHINE_ARCH=mips` or another invalid pair before compiler selection.
- `share/mk/sys.mk` maps ARM to Cortex-M0+ only for RP2040 and to Cortex-M4
  for every other machine. The fallback silently treats an undeclared ARM
  machine as Cortex-M4.
- The same shared file embeds both ARM and MIPS compiler discovery, compiler
  flags, optimizer flags, disassembler selection, linker warnings, and a.out
  linker-script selection.

Limitation: root-level `bmake -V` does not include every recursive Make
fragment, so recursive closure was measured separately.

Next action: replace duplicate free-form assignments with one architecture
registry that derives the family and CPU from the selected machine and fails
closed on an undeclared pair.

### Evaluated default closures

Command: `bmake -V` in `usr.bin`, `tools`, and `lib/libc` for each declared
machine.

Result:

- RP2040 `usr.bin` selects `lex smlrc as ld pdp11 mputest`. The PDP-11
  emulator therefore belongs to the shipping ARM build rather than an opt-in
  legacy closure.
- STM32 `usr.bin` selects a native compiler set whose comments and source
  identify MIPS output: `cc`, `cpp`, `lcc`, `lcpp`, `smallc`, and `aout`.
- PIC32 selects that MIPS-emitting compiler set plus `adb`, and its host tools
  add `icache`, `mkrd`, and `virtualmips`.
- `lib/Makefile` constructs `startup-${MACHINE_ARCH}` and `lib/libc/Makefile`
  adds `${MACHINE_ARCH}` directly. Free-form architecture values therefore
  become source-directory names.

Limitation: a neutral directory name does not establish neutral behavior.
`lccom`, `ccom`, `smallc`, `aout`, `as`, `smlrc`, `ld`, and `adb` need
source-level classification.

Next action: split mixed ARM/MIPS programs at their backend boundary and move
MIPS-only programs into the retained legacy tree.

### PDP-11 and V6 default reachability

Command: `rg` over the root Makefile, `usr.bin/Makefile`, the PDP-11 emulator,
the V6 pack harness, and the SIMH reference harness.

Result:

- `usr.bin/Makefile` builds and installs `pdp11` for RP2040.
- `check-host` includes `check-pdp11-reference` and `check-pdp11-v6` through
  its normal dependency variables.
- `usr.bin/pdp11/install` installs both the emulator and the decompressed V6
  root pack under `/usr/v6`.
- `tests/pdp11_reference` is a separate host-only Open SIMH/V7 oracle with an
  externally supplied disk image. Its provenance and execution contract differ
  from the on-device V6 emulator.

Limitation: historical prose elsewhere mentions PDP-11 without providing a
build path. Those references do not belong to the architecture denominator.

Next action: place the on-device emulator, V6 guest pack, and SIMH reference
oracle under one provenance quarantine with distinct explicit targets inside
that boundary.

### Lexical floor and migration size

Command: `git ls-files` path matching, `git grep -Il` over build and source
files, and `scc` over the initial non-ARM and PDP-11 candidate directories.

Result:

- Path matching found 138 PIC32-path files, 257 MIPS-named-path files, and 15
  PDP-11-path files. The sets overlap.
- Content matching found MIPS or PIC32 build/source tokens in 170 tracked
  code or build files: 53 under `sys`, 54 under `tools`, 31 under `usr.bin`,
  23 under `share`, 6 under `lib`, and one each under the root Makefile,
  `include`, `tests`, and `.github`.
- `scc` measured the initial candidate directories at 509 files, 179,771
  total lines, 140,610 code lines, and 24,447 reported complexity units.

Limitation: lexical matches include architecture-neutral format definitions,
historical comments, and ARM files that deliberately reject MIPS behavior.
The counts establish audit scale rather than a deletion set.

Next action: promote each candidate through ownership, build reachability, and
retained-value checks before assigning `legacy-non-arm` or `remove`.

### Resumption checkpoint and ordered work

Command: `git status --short --branch`, `git rev-parse HEAD`, `git worktree
list --porcelain`, and focused reads of the root, shared, userland, profile,
and manifest Make inputs.

Result:

- The isolation branch remains based on
  `cc8d14c414e58a2637119d7cfd8a0059cf3aad5c`; this scratchpad is its only
  worktree-local file.
- The canonical `main` checkout resolves to the same commit. The separate
  `feature/kernel-metadata-capacity-evidence` worktree remains present, so its
  uncommitted bytes stay outside this worktree.
- The root Makefile does not include `share/mk/sys.mk`. The root and recursive
  builds therefore assign their defaults independently, and a registry that
  lives only in `sys.mk` cannot constrain root selection.
- `lib/Makefile` selects `startup-${MACHINE_ARCH}`, `lib/libc/Makefile` selects
  `${MACHINE_ARCH}`, and `share/mk/sys.mk` selects
  `lib/elf32-${MACHINE_ARCH}.ld`. An unchecked caller value controls three
  source or linker-script paths.
- `distrib/rp2040/profiles` makes `full` the default and selects `pdp11` plus
  `v6disk`; `distrib/rp2040/mi.rp2040` installs both `/usr/bin/pdp11` and
  `/usr/v6/root.rk` for that profile.
- `usr.bin/pdp11` contains the emulator, the compressed V6 pack, its pack
  generator, the WTFPL grant, and the Caldera license PDF. The separate
  `tests/pdp11_reference` directory contains the SIMH/V7 oracle. Both surfaces
  need to move with their provenance rather than becoming detached files.

Limitation: the historical `pdp11`, VAX, x86, 8080, M68K, MIPS, and PIC32
tokens in shared sources include dead compatibility branches, comments, host
implementation choices, and live target support. Token presence establishes a
review row, not a move by itself.

Ordered work:

1. Close the tracked architecture denominator and classify every mixed path.
2. Add one fail-closed registry shared by the root and recursive makes.
3. Archive retained MIPS/PIC32 and PDP-11/V6 paths with provenance.
4. Refactor mixed ARM/MIPS tools and remove legacy entries from default
   manifests and profiles.

### Whole-file denominator closure

Command: independent tracked-file classification against source commit
`cc8d14c414e58a2637119d7cfd8a0059cf3aad5c`, duplicate source and destination
checks, and a sorted source-to-destination SHA-256 projection.

Result:

- `legacy-non-arm` contains 999 exact move rows.
- `legacy-pdp11-v6` contains 22 exact move rows.
- The combined preserved relocation map contains 1,021 rows, with zero
  duplicate sources and zero duplicate destinations.
- Fifty new PDP-11/VAX paths include five build or utility assets and 45
  machine-specific manual pages. Six VAX/VMS utility assets form a separate
  archive class. The dormant zmodem Unix/Xenix build matrix belongs under the
  toolchain archive.
- The lexically source-sorted two-column projection of the 999 non-ARM rows
  has SHA-256
  `eed598d391d40958ecf2d225a7e24f5e7d6be8fb4829076edb0b865fc39033be`.

Limitation: the projection proves finite whole-file relocation coverage. It
does not prove that retained mixed files contain ARM-only active behavior.

Next action: regenerate the pinned map, apply the 57 additional moves, remove
their build and manifest selectors, then close the 114 project-owned dormant
non-ARM conditional lines while retaining the 15 exact imported portability
allowlist lines.
5. Add calibrated gates for registry validity, default closure isolation, and
   main-tree path ownership.
6. Reconcile this ledger into one indexed design document.
7. Run targeted and integrated gates, then use the repository PR lifecycle.

Next action: finish the mixed-source table before the first archival move.

### Tool-note calibration

Command: read the source-navigation, static-analysis, provenance, and binary
inspection views in `~/Documents/AI/Notes/1_TOOLS.md`, then resolve each
candidate with `command -v`.

Result:

- `/usr/bin/git`, `/usr/bin/bmake`, `/usr/bin/fd`, `/usr/bin/scc`,
  `/usr/bin/ctags`, `/usr/bin/readtags`, `/usr/bin/global`, `/usr/bin/gtags`,
  `/usr/bin/cscope`, `/usr/bin/spatch`, `/usr/bin/comby`, `/usr/bin/bear`,
  `/usr/bin/clangd`, `/usr/bin/clang-tidy`, `/usr/bin/cppcheck`,
  `/usr/bin/sparse`, `/usr/bin/smatch`, `/usr/bin/shellcheck`, and
  `/usr/bin/diffoscope` resolve on the host.
- User-local `ast-grep`, `weggli`, `lizard`, and `ruff` resolve under
  `/home/eirikr/.local/bin`. The `lizard` row confirms that `python-lizard`,
  rather than the conflicting Arch package named `lizard`, owns the required
  executable.
- `arm-none-eabi-gcc`, `arm-none-eabi-nm`, `arm-none-eabi-readelf`,
  `arm-none-eabi-objdump`, and `arm-none-eabi-size` resolve under `/usr/bin`.
- Bare `rg` resolves through Codex's bundled path. Architecture-denominator
  commands use `/usr/bin/rg` so the retained evidence names the system package
  recorded by the tool note.
- `git`, `/usr/bin/rg`, `fd`, evaluated `bmake` queries, `scc`, and `lizard`
  answer source ownership, reachability, and migration-size questions.
  `diffoscope` and SHA-256 hashes answer byte-preservation questions after
  archival moves. ARM binutils answer compiled-ISA and linked-artifact
  questions after the Make graph closes.
- Ctags, GNU Global, and cscope remain escalation tools for ambiguous symbol
  edges. Coccinelle, Comby, ast-grep, clang-tidy, cppcheck, sparse, and smatch
  require calibrated patterns or real compile flags before their output can
  decide a move. Dynamic tracers, fuzzers, and decompilers do not decide the
  source/build isolation claim and stay outside the execution plan.

Limitation: executable presence proves host availability, not suitability for
a specific row. Each later invocation must name the question and its evidence
boundary.

Next action: use the cheapest deciding tool for each row and reserve semantic
or compiled probes for lexical ambiguities.

### Build registry and artifact-boundary audit

Command: evaluate root, recursive, assembler, compiler, and tool Makefiles for
the default selection, both retained ARM machines, PIC32, unknown machines,
MIPS overrides, and CPU overrides.

Result:

- Every tested invalid pair parses successfully. Examples include
  `MACHINE=rp2040 MACHINE_ARCH=mips`, `MACHINE=pic32 MACHINE_ARCH=arm`,
  `MACHINE=bogus MACHINE_ARCH=arm`, and
  `MACHINE=rp2040 MACHINE_ARCH=bogus`.
- Bmake 20260824 supplies host built-ins `MACHINE=x86_64` and
  `MACHINE_ARCH=x86_64`. A registry that uses `MACHINE?=` cannot replace those
  values during a direct recursive Make invocation. Ordinary Make assignments
  override the host built-ins while command-line assignments retain the
  precedence needed for mismatch rejection.
- STM32 is nominally ARM but selects MIPS-emitting user programs and assembler
  backends. `usr.bin/as` and `tools/aoututils/as` select `as.c`, and
  `usr.bin/smlrc` selects `-DMIPS`, `lb.o`, and `-mips16`.
- Root paths interpolate the unchecked machine into host-tool, kernel,
  filesystem, cleanup, and `include/machine` operations. Recursive paths
  interpolate the unchecked architecture into startup, libc, compiler, and
  linker-script selection.
- The source tree stores userland, libraries, tools, and one
  `include/machine` link in shared output locations. A correct registry cannot
  prevent Cortex-M4 objects from surviving an STM32-to-RP2040 switch. Separate
  per-machine object roots or a fail-closed selection stamp must close the
  artifact boundary as a second mechanism.
- The default host tier includes PDP-11 and V6 tests. The default aggregate
  gate also mixes RP2040-only gates with the STM32 default. CI specifies
  RP2040 directly and therefore does not test the default or the STM32 ARM
  closure.

Decision:

- `share/mk/architecture.mk` will become the single machine registry included
  by both the root Makefile and `share/mk/sys.mk`.
- The registry will map `rp2040` to `arm/cortex-m0plus` and `stm32` to
  `arm/cortex-m4`, assign `rp2040` as the default, export the resolved tuple,
  and reject every unknown or conflicting command-line value while parsing.
- Installed host tools will use machine-qualified paths. Shared source-tree
  objects and libraries will use an atomic fail-closed tuple stamp: one
  worktree may build one machine tuple until `cleanall` removes every shared
  artifact and the stamp. Separate worktrees remain the supported concurrent
  build boundary.

Limitation: generated kernel Makefiles carry their own `_mach` and `_arch`
inputs, and RP2040 generated Makefiles write `.params` while parsing. The gate
must inspect or generate temporary copies rather than evaluating tracked kernel
directories in place.

Next action: identify every generated and recursive registry consumer before
installing the authority.

### Object-directory feasibility and selected isolation mechanism

Command: read bmake 20260824's `.OBJDIR`, `MAKEOBJDIR`, and
`MAKEOBJDIRPREFIX` contract; search every active Makefile for source-root and
tool-output derivation; inspect root, recursive, generated-kernel, library,
multicall, and test consumers.

Result:

- Bmake selects an alternate object directory before recipes execute and
  changes the working directory to `.OBJDIR`. `MAKEOBJDIR` and
  `MAKEOBJDIRPREFIX` need an existing directory and should arrive through the
  environment or command line.
- Many repository Makefiles calculate `TOPSRC` with `cd ..; pwd`, address
  sibling source objects directly, or expect generated headers and symlinks in
  source compilation directories. A blanket object-prefix setting redirects
  those calculations into the object tree and breaks the build before it
  separates outputs.
- Repairing every relative-source and generated-file assumption is a distinct
  repository-wide out-of-source-build conversion. Treating the prefix alone
  as isolation would create a non-working build and violate the no-shortcuts
  policy.
- The cross-machine collision has two bounded parts. Host tool installation
  collides at `tools/bin`, and target objects/libraries collide in shared
  source directories. Qualifying `TOOLBINDIR` by machine closes the first
  part. An atomic tuple stamp checked from root, recursive, tool, and generated
  kernel Makefiles closes the second by rejecting a machine switch before any
  recipe executes.
- A `.BEGIN` guard executes before a real build, prints without executing under
  `bmake -n`, and does not execute for `bmake -V` queries. Real object-producing
  targets therefore receive the protection while read-only registry probes and
  dry-run inspection create no stamp. Clean-only target sets can bypass the
  guard and remove the stamped artifacts.

Decision:

- `TOOLBINDIR` will resolve to `tools/bin/${MACHINE}`. All active source,
  script, test, and generated-kernel consumers will use the qualified path.
- An atomic helper will create
  `distrib/obj/.build-machine` with the canonical
  `MACHINE/MACHINE_ARCH/MACHINE_CPU` tuple. A different tuple will fail with a
  command that names the required `cleanall` transition. The helper will use
  atomic hard-link publication so two first builds cannot admit different
  tuples concurrently.
- `cleanall` will clean shared userland, tools, and both maintained ARM kernel
  trees before removing the stamp. A partial clean will leave the stamp in
  place and preserve failure visibility.
- A future full object-root conversion remains compatible with the registry
  but is outside the architecture-removal denominator. The current stamp is
  the complete supported single-worktree isolation contract, not an
  unverified prefix workaround.

Limitation: the tuple stamp serializes machine builds inside one worktree.
Concurrent RP2040 and STM32 builds require separate worktrees, which is already
the repository's collision-avoidance model.

Next action: calibrate same-tuple admission, mismatched-tuple rejection,
concurrent first-write behavior, read-only query behavior, and clean-reset
behavior.

### PDP-11 and V6 provenance-boundary audit

Command: trace `usr.bin/pdp11`, `tests/pdp11_reference`, RP2040 manifests and
profiles, host-test dependencies, firmware CI, host-console affordances,
license notices, and guest-asset hashes.

Result:

- Twenty-two tracked files and 223,760 bytes form the owned quarantine: the
  on-device emulator, V6 pack and generator inputs, external SIMH/V7 oracle,
  and `docs/research/v6-emulator-on-discobsd.md`.
- The default RP2040 userland, `check-host`, `PROFILE=full`, firmware CI,
  README, and motd all expose or advertise PDP-11/V6 today.
- The SIMH/V7 oracle consumes an externally supplied image and validates it
  against pinned size, image, profile, transcript, and simulator-banner
  identities. The oracle does not test the project emulator and therefore
  retains a distinct explicit target inside the same quarantine.
- The default host package contains hard-coded V6 terminal and web UI
  affordances. Strict isolation requires an architecture-neutral default
  console plus optional legacy affordances, or a move of the affordances under
  the quarantine.
- The retained guest asset identities are:

  | Artifact | Size | SHA-256 |
  | --- | ---: | --- |
  | `v6.rk.gz` | 68,938 | `61678c2e916120922a5fc4ffaa24afd47af68097c176b0ab294291905c40e9bf` |
  | decompressed V6 pack | 1,024,000 | `300443c727acda3c7aa55e23ac5339390b00cd5479436450f4d9e3ac0cebe5c4` |
  | `Caldera-license.pdf` | 12,298 | `16514a4d9ea6426b85a9c4baea883dce2d71e5342eea0ebb6a32a40efc7035e3` |
  | emulator `COPYING` | n/a | `7637386b5f81e8a719ca336233149005e5fa28b5e6054ea7b67de49355b0ad40` |

- The checked-in gzip stream records a source filename and timestamp that the
  prose recipe cannot reproduce, and the prose omits the upstream image
  digest. The archive must preserve the blob byte-for-byte and add a
  provenance record instead of claiming reproducible regeneration.
- The Caldera and WTFPL sections in `NOTICE` remain required. Their paths need
  updates after the move; their grants remain unchanged.

Decision:

- `legacy/pdp11-v6/` will contain `emulator/`, `guest-v6/`, `simh-v7/`, an
  RP2040 manifest fragment, legacy documentation, one boundary Makefile, and a
  provenance README.
- `BUILD_PDP11_V6?=no` will accept only `yes` or `no`. The value `yes` will be
  valid only with `MACHINE=rp2040` and will control every build, install,
  manifest, profile, test, and CI edge.
- Default `usr.bin`, `check-host`, maintained profiles, documentation, and CI
  will contain zero PDP/V6 dependencies.
- Explicit entry points will separate emulator/V6 validation from the
  externally supplied SIMH/V7 reference validation. The distribution entry
  point will require the exact opt-in value and will add one checked legacy
  manifest fragment.
- The maintained `full` profile will mean the complete maintained ARM image.
  The legacy option will augment a selected maintained profile rather than
  changing the meaning of `full`.

Limitation: `mkmanifest.py` currently accepts one manifest and one profile
file. The image boundary needs repeated manifest/profile inputs or another
equally checked composition interface; raw concatenation would bypass the
dependency oracle.

Next action: design the manifest-fragment interface and calibrate omission,
partial opt-in, digest-change, and default-leak failures.

### Provisional non-ARM source denominator

Command: combine `git ls-files`, evaluated `bmake -V` selection, directory
ownership, device-table reachability, backend inclusion, and source-level
inspection for every earlier MIPS/PIC32/x86 lead.

Result:

- The first non-ARM pass contained 941 tracked source rows: 922 rows moved as
  whole files or whole directories and 19 mixed-program rows required an
  ARM-retained source plus a legacy backend or fixture move.
- A contrary review then found `lib/libc/runtime/sc_case.S`, a MIPS-only
  implementation selected by both active libc build graphs. The row raises the
  provisional denominator to at least 942 and invalidates the earlier mapping
  digest. The active `sc_case.o` selections must leave both libc Makefiles when
  the source moves to
  `legacy/non-arm/mips-pic32/lib/libc/runtime/sc_case.S`.
- The whole-directory closure includes `sys/arch/pic32`, `distrib/pic32`,
  `include/pic32`, `lib/libc/mips`, `lib/startup-mips`,
  `lib/elf32-mips.ld`, `lib/libicache`, `share/mk/mips-toolchain.mk`, the
  PIC32 OpenBSD port material, PIC32 device headers and examples, MIPS host
  tools, and the MIPS-only native-compiler programs.
- The hardware-bound programs `glcdtest`, `portio`, `pwm`, `wiznet`, and
  `smux` have PIC32 implementations or board contracts and no corresponding
  RP2040 or STM32 runtime contract. Their complete source, library, header,
  and example closures belong under `legacy/non-arm/mips-pic32/`.
- `gtest` reaches the generic `rdglob` and `wrglob` kernel interface on ARM.
  The source remains in the maintained tree as an explicit diagnostic rather
  than joining the PIC32 peripheral group.
- The mixed assembler rows are `usr.bin/as/as.c`, its MIPS instruction-set
  note and six MIPS fixtures, plus `tools/aoututils/as/as.c` and the
  `tools/aoututils/aout/` object inspector. The Thumb implementations remain
  at `usr.bin/as/as-thumb.c` and `tools/aoututils/as/as-thumb.c`.
- The mixed Smaller C rows are `usr.bin/smlrc/cgmips.c`,
  `usr.bin/smlrc/lb.c`, and `usr.bin/smlrc/cgx86.c`. The maintained source is
  `smlrc.c`, `cgthumb.c`, and `fp.c`.
- `usr.bin/ld/ld.c` remains in the maintained tree after the MIPS relocation,
  global-pointer, and ELF-machine branches are removed. Generic ELF and a.out
  structures required by ARM remain shared source.

Destination decision:

- MIPS/PIC32 source moves to `legacy/non-arm/mips-pic32/<original-path>` so
  each archived path retains its source-tree identity.
- The x86 Smaller C backend moves to `legacy/non-arm/toolchains/` because it
  has no PIC32 ownership.
- The 22 PDP-11/V6/provenance rows remain a separate denominator and move to
  `legacy/pdp11-v6/`; they do not count toward the 941 non-ARM rows.
- The move must preserve every tracked blob identity. The post-move verifier
  will compare each source blob object ID with its declared destination and
  will reject a missing, duplicated, or undeclared row.

Limitation: the counts close the reviewed migration set, not every historical
architecture word in prose or generic format definitions. Imported TinyUSB
architecture registries remain in maintained third-party source under a
documented allowlist because editing their upstream enumeration would damage
provenance without changing build reachability.

Next action: repeat the mixed-source audit against compiled selections, then
materialize the resulting stable keys in a checked TSV, perform only declared
`git mv` operations, and calibrate the verifier against a removed row, an
undeclared row, and a changed destination blob.

### Shared-source non-ARM conditional audit

Command: `git grep -n -E` over C, headers, and assembly for dormant PDP-11,
VAX, NS32000, x86, M68K, SPARC, PowerPC, HPPA, Z8000, MIPS, PIC32, RISC-V,
Alpha, and SuperH preprocessor selections after excluding whole legacy move
roots.

Result:

- The scan found 96 conditional lines outside the first whole-file move set.
  Eighty-three project-owned lines implement dormant non-ARM portability,
  five belong to files already moving, and eight belong to imported generic
  registries that an ARM build does not select.
- The project-owned simplification set spans
  `bin/csh/{sh.h,sh.set.c,sh.time.c}`, `bin/sh/{io.c,main.c}`,
  `games/hunt/driver.c`, libc `gen/{malloc,swab,valloc}.c`,
  `net/res_comp.c`, `stdio/{getw,putw}.c`,
  `sys/kern/{exec_subr,kern_clock,sys_pipe,sys_process,syscalls}.c`,
  `sys/sys/{systm,types}.h`, and
  `usr.bin/{ar/archive.c,compress/compress.c,diff/diffdir.c,find/find.c,
  od/od.c,smlrc/smlrc.c,sort/sort.c}` plus
  `usr.bin/uucp/{acucntrl,chksum,tio}.c` and
  `usr.bin/zmodem/{rz,sz}.c`. Each file will retain the branch selected by the
  maintained ARM builds and drop the dormant target selection.
- The five move-line hits are `lib/libc/runtime/sc_case.S`, two MIPS files
  within the already archived `usr.bin/aout` directory, and the PDP-11 header
  guard within the PDP quarantine.
- The exact imported/generic allowlist is:

  | Path | Lines | Boundary |
  | --- | ---: | --- |
  | `lib/libc/runtime/fp_lib.h` | 31 | imported compiler-rt FreeBSD/i386 compatibility |
  | `lib/libc/runtime/int_types.h` | 63 | imported compiler-rt 128-bit capability registry |
  | `sys/dev/usb/tusb_mcu.h` | 168, 172 | imported TinyUSB MCU registry |
  | `sys/dev/usb/tusb_verify.h` | 84, 87 | imported TinyUSB breakpoint registry |
  | `usr.bin/picoc/platform.h` | 17, 55 | imported PicoC host portability |

Decision: project-owned dormant architecture branches leave the maintained
tree. Imported registries remain byte-faithful and receive exact path/line
allowlisting. Historical prose and standardized ELF/a.out numeric constants
form separate non-support allowlists; neither class may participate in an
evaluated build selector.

Next action: simplify the 83 project-owned lines with per-file ARM-selected
branch review, then rerun the same scan and require the result to equal the
eight-row imported allowlist exactly.

### Registry implementation review findings

Command: inspect the in-progress registry diff and re-evaluate both ARM
closures after the first propagation pass.

Result:

- RP2040 and STM32 resolve to the intended ARM/CPU tuples, and the active
  userland and tool selectors choose the Thumb assembler and Smaller C
  backend.
- `usr.bin/smlrc/tests/run.sh` still compares every libc against ARMv6. A
  correct STM32 Cortex-M4 library therefore fails the current oracle. The
  expected ELF CPU attribute must derive from the selected machine tuple.
- Three touched harnesses still invoke a literal `python3`:
  `usr.bin/as/tests/thumb-encoding.sh`,
  `usr.bin/as/tests/thumb-compile.sh`, and
  `usr.bin/smlrc/tests/fuzz.sh`. Each harness must require `PYTHON`, and its
  Makefile caller must pass the selected interpreter.
- `AOUT_AOUT` remains in `share/mk/sys.mk` after the MIPS object inspector
  left the active host-tool closure. The variable has no maintained consumer
  and should leave the registry.
- A successful `cleanall` is the only safe point for removing the machine
  stamp. The existing recursive cleanup loses earlier submake failures, so an
  unconditional final unlink would admit a machine switch after a partial

### Closed non-ARM move denominator

Command: enumerate each declared whole root with `git ls-files`, add the
individually classified mixed-program files, sort the union, and compare the
source and destination sets for duplicates and overlap.

Result:

- Forty-six whole-root or whole-file declarations contain 923 unique tracked
  files. Nineteen individually classified backend or fixture files raise the
  exact non-ARM move denominator to 942 tracked files.
- The whole-file set includes the MIPS/PIC32 kernel and distribution roots,
  MIPS libc/startup/linker inputs, legacy compilers and host tools, peripheral
  programs and libraries, device examples, and
  `lib/libc/runtime/sc_case.S`.
- The individual set contains the MIPS assembler implementation and fixtures,
  the MIPS a.out inspector, the Smaller C MIPS backend, and the Smaller C x86
  backend. Maintained Thumb implementations stay at their existing main-tree
  paths.
- Each MIPS/PIC32 destination is
  `legacy/non-arm/mips-pic32/<original-path>`. The x86 Smaller C backend moves
  to `legacy/non-arm/toolchains/usr.bin/smlrc/cgx86.c`.
- The separate PDP-11/V6 denominator remains 22 files. It does not contribute
  to the 942-file non-ARM total.

Limitation: move membership proves ownership and source placement. A separate
evaluated-build gate must prove that default and maintained ARM targets have
no dependency edge into either legacy root.

Next action: materialize all 964 rows in a checked mapping with source blob and
destination content identities before moving any path.

### Machine registry and cleanup calibration

Command: evaluate the registry with `bmake -V`, feed invalid tuples and option
values, run the atomic stamp helper twice with one tuple and once with a
conflicting tuple, build `tools/binstall` for RP2040, attempt an STM32 build in
the same worktree, and run `bmake MACHINE=rp2040 cleanall`.

Result:

- The default resolves to `rp2040/arm/cortex-m0plus`; STM32 resolves to
  `stm32/arm/cortex-m4`. Both derive the AAPCS, little-endian, Thumb,
  soft-float flags and a machine-qualified host-tool directory.
- The registry rejects PIC32, unknown machines, a MIPS architecture override,
  a mismatched CPU, an overridden architecture flag set, an invalid legacy
  Boolean, and PDP-11/V6 enabled for STM32 during Makefile parsing.
- The stamp helper accepts repeated identical tuples and rejects a conflicting
  tuple with the admitted tuple and the required `cleanall` command.
- A real RP2040 host-tool build wrote the canonical tuple to
  `distrib/obj/.build-machine`; a subsequent STM32 host-tool build stopped in
  `.BEGIN` before executing its recipe.
- `bmake MACHINE=rp2040 cleanall` cleaned both machine-qualified tool
  directories and all generated ARM kernel directories, returned status zero,
  and removed the tuple stamp only after the complete cleanup.
- The cleanup pass used explicit RP2040 and STM32 tool, destination, and
  release paths. The STM32 pass did not inherit the RP2040 tool directory.

Limitation: the manual calibration must become a repository gate with
rejecting mutations for malformed stamps, conflicting concurrent first
writes, and cleanup failure retention.

Next action: add the calibrated gate after the legacy path map becomes the
canonical source-placement contract.

### Byte-preserving legacy relocation

Command: generate `tools/architecture-isolation/legacy-path-map.tsv` from the
pinned source commit, validate it in `source` mode, apply one `git mv` for each
declared row, and validate the resulting index in `archive` mode.

Result:

- The map contains 964 unique source and destination rows: 942
  `legacy-non-arm` rows and 22 `legacy-pdp11-v6` rows.
- The non-ARM source/destination projection has SHA-256
  `4daf23ebadcc5222d9ab0917069b14cd6d631c1b84cd8e4102bb7a6b721d4e9b`,
  matching the independently constructed denominator.
- Every source row resolved to the recorded Git blob and SHA-256 at
  `cc8d14c414e58a2637119d7cfd8a0059cf3aad5c`.
- Every source path left the index, every destination path entered the index,
  and all 964 destination blobs and SHA-256 values equal their source records.
- The verifier handles the tracked `include/pic32` symbolic link through Git
  object identity instead of filesystem regular-file assumptions.

Limitation: new repository-owned Makefiles, provenance notes, and manifest
fragments will join the legacy roots as explicitly listed boundary files. They
are not archival move rows and must remain distinguishable from the 964
preserved objects.

Next action: remove all stale main-tree build edges, add the explicit legacy
boundary files, and require the verifier from the host gate.

### Retained conditional denominator correction

Command: expand the architecture-alias scan with Interdata, Gould/SEL, Sun,
HP-UX, Amiga, PARIX, Borland, and `M_I86`, while excluding STM32 register-field
false positives.

Result:

- The candidate union contains 106 conditional lines across 44 files.
- Eighty-six lines across 32 project-owned files select dormant non-ARM
  behavior and require ARM-selected simplification.
- Five lines belong to files already relocated by the legacy map.
- Fifteen lines belong to exact imported or generic portability boundaries.
  The earlier 83/5/8 partition was incomplete and is superseded by the
  86/5/15 partition.
- The added removal rows are two historical target selections in
  `usr.bin/compress/compress.c` and one 16-bit x86 selection in
  `usr.bin/zmodem/crc.c`.
- The added allowlist rows are compiler-rt endianness detection, upstream
  pForth Solaris support, and upstream Whetstone host portability.

Limitation: lexical closure remains subject to one final architecture-alias
review. The 942-file move map does not change.

Next action: finish the alias review, then simplify the project-owned set and
require the residual scan to equal the exact imported allowlist.

### Post-relocation reachability audit

Command: search every retained build, configuration, test, manifest, CI, and
runtime source outside `legacy/` for MIPS, PIC32, PDP-11, V6, and x86 selectors,
then inspect each hit against the evaluated default closures.

Result:

- The root default host and aggregate checks have dropped their PDP-11/V6 and
  MIPS target dependencies, but the explicit targets need archive-relative
  paths and the declared legacy options need real, independently named entry
  points rather than an unused `LEGACY_SUBDIRS` append.
- The RP2040 `full` profile and active manifest still install `pdp11` and the
  V6 disk. `mkmanifest.py` also hard-codes those dependencies in its
  self-tests. The default filesystem therefore remains outside the ARM-only
  contract after source relocation.
- `tests/rp2040/elf2aout_layout` still requires a MIPS compiler, linker script,
  fixtures, and verifier backend. The active `elf2aout` converter still
  accepts `PT_MIPS_REGINFO`.
- `usr.bin/ld/ld.c` still defaults to MIPS layout and global-pointer behavior,
  treating every object other than `MID_ARM6` as MIPS. The maintained linker
  must accept ARM objects alone and reject every other machine identifier.
- The config generator still parses and emits PIC32 configuration files.
  Active a.out libc Makefiles still select MIPS source and flags. Shared
  kernel, libc, and utility sources still compile dormant MIPS, PIC32, PDP-11,
  and other non-ARM branches.
- Smaller C moved its MIPS and x86 backend files but still dispatches to those
  backends from `smlrc.c` and advertises them in active documentation.
- The default host package still detects PDP-11 banners, enters a V6 mode, and
  emits V6 exit sequences. That behavior is a runtime dependency on the
  quarantined option even when the firmware excludes the emulator.
- README, NOTICE, motd, CI comments, testing documentation, and Linux setup
  notes still describe removed paths or default non-ARM support.

Decision:

- `legacy-non-arm` validates the retained non-ARM archive only. It does not
  join `SUBDIR`, `build`, `install`, or any default check closure.
- `legacy-pdp11-v6`, `check-pdp11-v6`, and `check-pdp11-v7` are explicit
  RP2040-only opt-in entry points guarded by `BUILD_PDP11_V6=yes`. Distribution
  composition needs a checked legacy manifest fragment; the maintained
  `full` profile remains ARM-only.
- Active mixed programs lose their non-ARM dispatch rather than reaching into
  `legacy/`. Main-tree source may retain generic ABI numeric constants and
  byte-faithful imported portability registries only when the ARM build
  consumes them or provenance requires them.
- The architecture gate must prove both placement and reachability. A passing
  blob map alone cannot establish an ARM-only default closure.

Limitation: the post-relocation scan supplies a finite candidate set. Each
source edit still requires branch-level review and a focused build or test.

Next action: remove default distribution/runtime edges first, then simplify
the retained build tools and shared sources before calibrating the final
residual gate.

### Checked optional PDP-11/V6 composition

Command: run `mkmanifest.py --check-all` and `--selftest` over the maintained
RP2040 inputs, then repeat with the two legacy inputs and explicit `pdp11` and
`v6disk` closure selections. Inspect default and opt-in root build dry-runs and
the evaluated `LEGACY_SUBDIRS` and `MI_MANIFEST` values.

Result:

- The maintained manifest contains five ARM product profiles and zero
  PDP-11/V6 closure declarations or entries. Its full composition contains 19
  directories and 54 installed files.
- The opt-in manifest adds one directory, the emulator, and the guest pack.
  The resulting full composition contains 20 directories and 56 installed
  files and uses the distinct generated name
  `distrib/rp2040/_manifest.full.pdp11-v6`.
- The manifest calibration rejects an emulator without `v6disk`, a PDP-11
  closure without `/usr/bin/pdp11`, and a V6 closure without
  `/usr/v6/root.rk`.
- `bmake MACHINE=rp2040 -n build` contains zero paths under
  `legacy/pdp11-v6`. `BUILD_PDP11_V6=yes` adds exactly that subtree to build
  and install traversal. STM32 rejects the option during parsing.
- Explicit wrapper targets separate emulator build, install, distribution,
  V6 boot, SIMH runner tests, and the external V7 reference. The V7 reference
  remains an independent oracle and requires its externally supplied image.

Limitation: archive verification still uses the provisional 942-file non-ARM
denominator. The final whole-file audit has identified additional VAX/VMS and
PDP/VAX implementation assets, so source relocation integrity must be
recomputed before the boundary gate can pass.

Next action: freeze the corrected denominator, extend the exact relocation
map, then remove retained non-ARM branches and stale documentation paths.

### Superseding PicoC denominator closure

Command: independently inspect every retained PicoC platform selector, paired
implementation, project file, upstream-history description, and emitted-code
contract after the 999-row relocation map was generated.

Result:

- Eight additional whole-file rows have evidenced non-ARM ownership. Five
  rows belong to the Win32/MSVC port: the three Visual Studio project files,
  `platform_msvc.c`, and `library_msvc.c`. Three rows belong to the Blackfin
  BF537 Surveyor/SRV-1 port: `platform_surveyor.c`, `library_surveyor.c`, and
  `library_srv1.c`.
- The superseding denominator contains 1,007 `legacy-non-arm` rows and 22
  `legacy-pdp11-v6` rows, for 1,029 preserved relocation rows. The eight
  additions overlap none of the existing rows.
- Destination paths preserve the source identity under
  `legacy/non-arm/x86-win32/` and
  `legacy/non-arm/blackfin-surveyor/`. The lexically sorted 1,007-row
  non-ARM source/destination projection has expected SHA-256
  `13c2c7f2ae10ff775691656533b7973ce0cb4bbe01abed8bf04c7f875fba75e0`.
- The retained mixed-source conditional set expands to 151 candidate lines
  across 61 files. The provisional partition is 135 project-owned removals
  and 16 imported or generic residual rows.
- `platform_ffox.c` and `library_ffox.c` name an incomplete Flying Fox port
  without identifying an ISA. `UMON_HOST` names a monitor ABI without
  identifying an ISA. The architecture audit leaves those imported
  unsupported-platform surfaces unresolved rather than labeling them
  non-ARM without evidence.
- PicoC, pForth, RetroForth, BASIC, and `pdc` execute interpreters or virtual
  instruction formats rather than emitting a non-ARM native ISA. Host build
  files and VM data remain outside the architecture move denominator.

Limitation: the corrected count becomes canonical only after the generator,
map, archive verifier, boundary README, and deliberate failure fixtures all
agree on the same 1,029 stable keys.

Next action: add the eight declarations, regenerate the map from the pinned
source commit, perform only those declared moves, simplify retained PicoC
conditionals to the Unix path, and prove the archived set equals the map plus
the repository-owned boundary list.

### Registry audit before durable-gate implementation

Command: rerun the machine-tuple, command-line override, build-traversal,
generated-Makefile, tuple-stamp race, malformed-stamp, and shell-lint probes
against the live worktree.

Result:

- All 12 tracked generated ARM kernel Makefiles include the canonical
  registry. Root and generated evaluations resolve RP2040 as
  `rp2040/arm/cortex-m0plus` and STM32 as `stm32/arm/cortex-m4`.
- Twenty-four registry probes reject undeclared or inconsistent machines,
  architecture and CPU overrides, private or derived-variable overrides,
  invalid legacy Booleans, STM32 PDP-11 opt-in, and an inconsistent tool
  directory.
- Six traversal probes show that both default ARM builds exclude `legacy/`,
  the non-ARM verification option adds no ordinary build edge, the PDP-11/V6
  option adds only its dedicated subtree, opt-in clean reaches that subtree,
  and `cleanall` reaches the subtree independently of the option.
- The stamp helper admits exactly one of two conflicting first writers,
  accepts the same winner repeatedly, rejects the other tuple, and rejects
  multiline and symbolic-link stamps.
- ShellCheck reports zero errors for the existing architecture-isolation
  scripts and tuple-stamp helper.

Limitation: these passing commands remain ephemeral. The root
`check-architecture-isolation` target currently verifies only the path map.
The repository owns no executable oracle for the 24 registry probes, six
traversal probes, generated-Makefile coverage, stamp concurrency, or a
failed-cleanup stamp-retention transition.

Next action: create one deterministic host gate with known-good assertions
and deliberate bad mutations, then make the root architecture target run the
gate before archive verification.

### Unsupported-platform refinement

Command: remove the Win32 and Surveyor branches from the retained PicoC build,
then inspect the two remaining Flying Fox implementation files and their full
Git history.

Result:

- The maintained PicoC program selects only its Unix implementation and uses
  the ARM cross-build flags inherited from the machine registry.
- `platform_ffox.c` contains unimplemented console and file operations, and
  `library_ffox.c` provides an empty platform library. The import history does
  not identify an instruction-set architecture.
- Leaving the pair beside the ARM program would retain a dormant platform port
  in the main tree. Labeling the pair non-ARM would exceed the evidence.
- The relocation contract therefore adds the distinct
  `legacy-unsupported-platform` class and the
  `legacy/unsupported-platforms/` root. The proven counts remain 1,007
  non-ARM and 22 PDP-11/V6 rows; the map separately preserves two
  unsupported-platform rows, for 1,031 total moves.
- Removing the Flying Fox and uMon selectors supersedes the provisional
  mixed-source partition. The same 151 candidates now contain 137 removed
  project-owned selectors and 14 exact imported or generic residual rows.

Limitation: the archive makes no ISA, compilation, or functionality claim for
the Flying Fox pair. A future restoration needs primary target evidence before
an architecture tuple can be declared.

Next action: regenerate the 1,031-row map, move the two declared files, and
require all three archive classes to equal the map plus boundary-file list.

### Durable architecture-isolation gate

Command: run ShellCheck at error severity over the registry, cleanup, and
architecture-isolation scripts, then run
`tools/architecture-isolation/check-architecture-isolation.sh` with the live
source root and resolved `bmake` executable.

Result:

- The repository-owned gate passes 71 assertions over two maintained ARM
  machines, 12 generated kernel Makefiles, and the 1,031-row relocation map.
- Canonical evaluation resolves RP2040 to `arm/cortex-m0plus`, STM32 to
  `arm/cortex-m4`, machine-qualified host tools, and disabled legacy options.
- Seventeen invalid registry inputs reject undeclared machines, mismatched
  architecture or CPU values, private or derived overrides, invalid Boolean
  values, an inconsistent tool path, and PDP-11/V6 selection on STM32.
- Default RP2040, STM32, non-ARM archive-option, and host-tier dry runs reach
  zero legacy roots. `BUILD_PDP11_V6=yes` adds only
  `legacy/pdp11-v6`; an injected default legacy edge is rejected.
- Both relocation-map modes pass. Duplicate-key and changed-origin-hash
  mutations fail for their intended reasons.
- The maintained profile and manifest contain zero PDP-11/V6 entries. The
  opt-in profile and manifest require the emulator, guest pack, and their
  dependency, while injected maintained-profile and manifest entries reject.
- Two conflicting first stamp writers publish one tuple atomically; the other
  rejects. Repeated identical selection succeeds. Multiline and symbolic-link
  stamps reject.
- A failed cleanup retains the tuple stamp, a complete cleanup removes it, and
  cleanup through a symbolic-link checkout resolves and removes the same
  canonical stamp.
- `clean-build-machines.sh` derives the unlink target from the normalized
  source root. The helper no longer accepts a caller-supplied stamp path, so a
  successful fake cleanup cannot name an arbitrary file for removal.
- ShellCheck reports zero errors for all three scripts.

Limitation: the gate proves the declared build boundary and relocation
denominator. Source compilation, manual table-of-contents reconciliation,
documentation accuracy, and full RP2040/STM32 build closure remain separate
work.

Next action: close non-ARM residue and compile regressions outside `legacy/`,
then add the exact residual set to the durable verifier.

### Wired registry-gate adversarial repair

Command: invoke the architecture gate first as a script and then through the
same `bmake MACHINE=rp2040 check-architecture-isolation` surface that
`check-host` and CI use. Repeat the earlier bad fixtures for inherited make
overrides, comment-only includes, direct generated-Makefile defaults, obsolete
PDP-11/V6 paths, cleanup traversal, and conflicting tuple publication.

Result:

- A parent `bmake MACHINE=rp2040` exports its override through `MAKEFLAGS`.
  The direct script passed while the wired target made its nested STM32 query
  resolve `tools/bin/rp2040`. The gate now clears `MAKEFLAGS` and `MFLAGS`
  before any nested query, so each explicit probe owns its tuple.
- The root Makefile and `share/mk/sys.mk` now pass independent active-include
  assertions. A comment containing the registry path fails the calibrated
  fixture. The PICO and F405 generated Makefiles also evaluate their machine,
  architecture, CPU, and machine-qualified tool directory directly.
- Default traversal rejects all three `legacy/` roots plus the former
  `usr.bin/pdp11`, `tests/pdp11_reference`, SIMH, and `/usr/v6` surfaces.
  ARMv6-M remains outside that token predicate. Dry-run assertions distinguish
  default clean, opt-in PDP-11/V6 clean, and unconditional `cleanall`.
- The tuple race verifies the losing writer's owner, requested tuple, and
  exact `cleanall` recovery command. Repeating the losing tuple fails with
  status 2 and the same diagnostic, while repeating the winning tuple passes.
- The gate removes its owned temporary fixture with bounded `unlink` and
  bottom-up `rmdir` operations rather than recursive removal. Its two stamp
  transitions use the same explicit unlink helper.
- ShellCheck at error severity passes. The direct script and the wired root
  target both report:
  `architecture isolation verified: assertions=94 machines=2 generated-kernel-makefiles=12 relocation-rows=1031`.

Limitation: the 94 assertions do not yet pin the imported or generic selector
residual. The earlier 71-assertion result is superseded rather than cumulative.

Next action: check in the exact residual selector allowlist, calibrate it with
an injected maintained-source MIPS selector, and have an independent reviewer
rerun the wired surface.

### Residual selector normalization correction

Command: remove line numbers from each candidate before applying the C-locale
sort, then hash the stable `path:text` projection.

Result:

- The final mixed-source partition contains 137 removed project-owned rows and
  14 retained imported or generic rows.
- The stable 14-row projection has SHA-256
  `ce0ca7e79e0a6c748af79fcb206394d841a4bee61ca9cffc34e6a169cf91184b`.
- The earlier `74beaaf3b8d10cbbcc9c2c743af3d83b7e5a13df929151ae1c7f922a1a043282`
  digest sorted `path:line:text` before deleting line numbers. That order can
  change when unrelated edits move a row, so the earlier digest is obsolete.
- The retained rows belong to compiler-rt integer support, TinyUSB MCU
  validation, pForth Solaris host portability, and Whetstone host-platform
  portability. Their presence records imported or generic compatibility; it
  does not declare a maintained non-ARM DiscoBSD target.

Limitation: an allowlist that scans only the seven files containing present
rows would not by itself discover a new selector in another maintained file.
The durable gate needs a closed candidate-path rule or a repository-wide
discovery predicate in addition to exact row comparison.

Next action: close the selector discovery surface before calling the residual
allowlist executable evidence.

### Relocation and portability-selector closure

Command: run `bmake MACHINE=rp2040 check-architecture-isolation` after adding
the repository-wide preprocessor-selector scan, the exact allowlist
comparison, the unrelated-file mutation, and an explicit relocation-row
assertion. Run ShellCheck at error severity over the changed gate and hash the
normalized allowlist.

Result:

- The relocation verifier proves 1,032 rows in both source and archive modes:
  1,008 `legacy-non-arm`, 22 `legacy-pdp11-v6`, and two
  `legacy-unsupported-platform` rows. Every earlier 1,031-row total in this
  scratchpad is superseded.
- The repository-wide `git grep` projection excludes `legacy/**`, strips line
  numbers and indentation, sorts in the C locale, and equals 20 exact
  `path:directive` rows. Every earlier 14-row residual count is superseded.
- The 20-row allowlist SHA-256 is
  `b2e5c3e7f1ef69c2006a88fe848fd2fd25c20721467fdba5ae6eaf8d07437594`.
- The retained rows belong to compiler runtime portability, TinyUSB imported
  MCU validation, pForth Solaris host portability, and Whetstone host-platform
  portability. The rows declare neither a supported non-ARM DiscoBSD machine
  nor default build reachability.
- A fixture places `#ifdef WIN32` under an unrelated maintained path. The same
  selector expression discovers the directive, the normalized set gains one
  row, and the exact allowlist comparison rejects it.
- The wired target reports `architecture isolation verified: assertions=103
  machines=2 generated-kernel-makefiles=12 relocation-rows=1032`.
- ShellCheck reports zero error-severity findings for the architecture gate.

Limitation: the scan recognizes the reviewed family of architecture and host
portability tokens. A new spelling outside that token family still requires
source review or expansion of the discovery expression. The final design must
state that lexical boundary explicitly.

Next action: repair the remaining lint, host-package, failure-fixture,
filesystem-profile, cleanup-environment, and interpreter-contract failures.

### Host, fixture, profile, cleanup, and interpreter closure

Command: run the focused warning-policy, build-failure, default profile,
PDP-11/V6 profile, architecture-isolation, lint, host-package, and workflow
syntax gates with `PYTHON` set to the resolved interpreter. Run the root
`check-python` guard once with an empty value and once with that resolved
value.

Result:

- `check-warning-policy-host` passes four real Makefile routes under default
  flags and `CFLAGS=-O0`, then validates 143 warning-level declarations. The
  evacuated `usr.bin/smux/linux` directory is absent from the route list, and
  every route now rejects a missing Makefile before a bmake built-in can
  masquerade as a project build.
- `check-build-failure` passes the ten-child library traversal and all kernel
  link failure cases. The scratch tree now exposes
  `tools/check-build-machine.sh`, so `share/mk/architecture.mk` publishes its
  stamp below the fixture root and exercises the production guard.
- `check-host` creates `include/machine` before any host gate executes. The
  prerequisite order closes the three suites that otherwise depend on
  unstated preparation.
- Default `check-fs-profiles` composes five maintained profiles and passes its
  rejection calibration. `BUILD_PDP11_V6=yes check-fs-profiles` consumes both
  evaluated manifest/profile inputs, adds only the `pdp11` and `v6disk`
  closures, composes the same five profiles, and passes three additional
  PDP-11/V6 rejection cases.
- The cleanup mutation seeds inherited `MAKEFLAGS`, `MFLAGS`, `MACHINE`,
  `MACHINE_ARCH`, and `MACHINE_CPU`. A fake make records five empty values for
  each of the five cleanup calls, for 25 empty observations, and the cleanup
  removes only the canonical stamp after all calls succeed.
- Root, shared, RP2040, and PDP-11 reference build files carry no literal
  Python default. `check-python` rejects an empty value with status 2 and
  accepts the caller-resolved interpreter. Ubuntu CI writes the resolved
  `command -v python3` result to `GITHUB_ENV`; the macOS job already writes its
  venv interpreter there.
- `check-lint` passes 96 shell scripts and 57 Python files. The complete host
  package gate passes Ruff and 37 pytest cases. Actionlint accepts the changed
  firmware workflow.
- The architecture gate grows to 105 assertions after the cleanup environment
  calibration and still reports 1,032 relocation rows.
- The map generator and verifier replace recursive temporary-directory removal
  with prefix-validated, bottom-up `unlink` and `rmdir` cleanup. ShellCheck
  accepts both scripts, and source-mode verification still proves all 1,032
  rows. Archive mode requires the changed maintained PDP-11 Makefile to be
  staged before its index-versus-worktree identity assertion can pass.

Limitation: these results prove host and composition behavior. They do not
prove RP2040 or STM32 cross compilation, QEMU execution, Renode boot, or board
behavior.

Next action: reconcile the active documentation and replace the scratchpad's
superseded working conclusions with a durable design document.

### Rebase and relocation-provenance reconciliation

Command: rebase the architecture-isolation commit onto `main` at
`c8066894671158220d978c741e5377d89a52eb3e`, resolve the two content conflicts
by retaining the newer target-neutral C17 `setvbuf()` implementation and
regenerating `sys/kern/syscalls.c` from its canonical input, then regenerate
`legacy-path-map.tsv` from that exact `main` revision. Verify the regenerated
map in both source and archive modes before replacing the pre-rebase map.

Result:

- The branch now points at `7771c72b` and contains one rebased implementation
  commit above `main`.
- The relocation denominator remains exactly 1,032 rows: 1,008
  `legacy-non-arm`, 22 `legacy-pdp11-v6`, and two
  `legacy-unsupported-platform` rows.
- The regenerated provenance changes the source revision from `cc8d14c4` to
  `c8066894` and updates 21 PIC32 blob identities that changed on `main`.
- Source-mode verification resolves every original path and blob against the
  recorded `main` revision. Archive-mode verification resolves every
  destination and SHA-256 against the rebased worktree.
- The unchanged row key set plus the updated blob witnesses proves that the
  rebase propagated the 21 source updates into their archived destinations;
  the proof does not assert support or buildability for those archived files.
- `lib/libc/stdio/setvbuf.c` retains the newer C17 implementation from `main`.
  `sys/kern/syscalls.c` retains generated metadata without the obsolete
  PDP-11 conditional.

Limitation: the successful rebase and relocation proof establish source
identity and boundary preservation. They do not replace clean ARM builds,
explicit PDP-11/V6 opt-in validation, emulator execution, or board evidence.

Next action: author the durable design, reconcile all active path and build
claims, and run the post-documentation validation matrix.

### Maintained STM32 prerequisite and PicoC repair

Command: run a clean STM32 distribution build with parallel make after the
post-documentation matrix, trace the first failure through the generated kernel
Makefiles and then through `usr.bin/picoc`, and repair only the mechanisms that
fail under the maintained tuple.

Finding:

- A generated kernel Makefile can start `${SYSTEM_OBJ}` before the `machine`,
  `sys`, and `.deps` prerequisites exist. Rebuilding `ioconf.c` invokes
  `bmake clean`, so one prerequisite barrier before `ioconf.c` cannot retain
  those paths. The source templates and all 12 generated ARM Makefiles now
  order `Makefile ioconf.c`, then the path prerequisites, then object builds
  with two `.WAIT` barriers.
- The repaired kernel compiles and advances the STM32 build into userland.
  PicoC then supplies obsolete `FILENAME_MAX=64` and `L_tmpnam=30` definitions
  that conflict with the maintained libc headers (`256` and `12`). The PicoC
  Makefile now consumes the libc definitions.
- `main()` originally contained `setjmp()` through
  `PicocPlatformSetExitPoint()`. Moving only the source loop into a helper did
  not close the warning: a direct STM32 compile still diagnosed the two mode
  locals because `main()` retained the exit point. The source helper now owns
  both the exit point and the work that can call `longjmp()`. Its nonzero
  return reaches the same cleanup path in `main()`, while the mode locals live
  in a function that contains no `setjmp()`.
- The direct compile accepts the exit-point boundary and advances to
  `table.c`. Its hash-bit offset was a signed `int` compared with a
  `sizeof`-derived unsigned width. The offset represents a nonnegative bit
  position and now uses `unsigned int`, matching both the shift operand and
  the computed width.
- A keep-going compile first exposes 341 `-Wall`/`-Wextra` instances. The
  canonical serial census after removing the dead compatibility shim measures
  339 instances at 165 distinct file, line, and category sites. Of those
  sites, 158 are unused callback parameters fixed by PicoC's interpreter ABI.
  The established warning policy assigns the directory `WARNLEVEL=legacy`
  with the 165-site residual rather than folding that interpreter migration
  into architecture isolation.
- Two warning instances and two hard conflicting declarations come from
  `retrobsd.c`. That compatibility shim implements `fgetpos()` and `fsetpos()`
  with obsolete `int *` signatures, while the maintained libc supplies both
  functions with `fpos_t *` signatures. Removing the dead shim from the tree
  makes PicoC consume the maintained libc API.

Limitation: the source repair is a compiler-driven hypothesis until the direct
PicoC build and complete STM32 distribution both pass. A successful STM32
build proves compilation and image composition for that tuple; it does not
prove execution on STM32 hardware.

Result update:

- The direct STM32 PicoC build compiles 23 translation units, links a 67,424
  byte ELF image, and converts it to a.out successfully.
- The first clean full STM32 build compiles every kernel configuration and all
  userland, then fails during image composition because `/etc/phones` is
  absent from the staged root. `usr.bin/tip` built its executable earlier in
  the same log, but its install invocation transiently reported the source
  executable absent and was marked ignored before the manifest exposed the
  missing configuration file. A direct repeat of the exact tip install
  succeeds and stages `tip`, `remote`, and `phones`.

Limitation: the one transient tip-install failure has no established source
mechanism. The clean distribution must reproduce or clear it before any code
change is justified; the direct retry alone does not close the full-build
gate.

Mechanism update:

- A second clean distribution and a direct parallel `usr.bin` install
  reproduce the missing tip source. `strace` plus an `INSTALL` wrapper that
  prints its working directory show `binstall` running from
  `usr.bin/tip/aculib`. The sibling `aculib` recipe uses
  `cd aculib; ${MAKE}` under parallel bmake, and that recursive-make form leaks
  the changed working directory across sibling jobs. Every aculib recursion
  now uses `${MAKE} -C aculib`; `tip` depends on `aculib`, which also orders the
  library before the link.
- The second image attempt reaches `/usr/bin/aout`, whose only producer was
  relocated to the non-ARM archive. Comparing every base and STM32 `file` or
  `pack` entry with the clean staged root finds a closed 29-path residual:
  retired PIC32 tools, headers, libraries and manuals; the RP2040-only compiler
  driver in the generic base; and three unbuilt glob test programs. The generic
  base manifest no longer names those paths.
- `retired-base-manifest-paths.txt` records the exact sorted residual. The
  architecture gate pins its 29-row size, requires sorted uniqueness, rejects
  any row in `distrib/base/mi`, and calibrates the rule by injecting one retired
  path.

Limitation: the aculib recursion and manifest cleanup remain hypotheses until
the direct parallel install, architecture gate, and clean STM32 distribution
all pass.

Result update:

- Separate clean, parallel build, and parallel install invocations for
  `usr.bin/tip` pass and stage `tip`, `remote`, and `phones`.
- ShellCheck accepts the changed architecture gate. The direct gate passes
  122 assertions across two machines, 12 generated kernel Makefiles, 1,032
  relocation rows, 20 retained selectors, and 29 retired manifest rows.
- The wired RP2040 gate correctly refuses to run while the shared-artifact
  stamp names STM32 and prints the exact `bmake MACHINE=stm32 cleanall`
  recovery command. That refusal proves the tuple guard rather than a gate
  failure.
- A third clean STM32 distribution exits zero. `fsutil` installs 1,159 files,
  98 devices, 349 hard links, and seven symbolic links in the root partition;
  the resulting image is 421,528,576 bytes.
- Re-evaluating every STM32 `file` and `pack` entry against
  `distrib/obj/destdir.stm32` finds zero missing sources. The staged root
  contains both `/usr/bin/tip` and `/etc/phones`.

Limitation: the STM32 result proves clean cross compilation, link, install,
and filesystem composition. It does not prove an STM32 boot or board runtime.
The root architecture target still needs a wired run after the required clean
transition to RP2040.

Next action: clean the STM32 tuple, run the wired architecture gate, then
rebuild RP2040 from clean and execute its host, cross, QEMU, and board-build
gates.

### RP2040 parallel UnixBench object isolation

Command: clean the STM32 tuple, run the wired architecture gate under RP2040,
then run a clean parallel RP2040 distribution and trace its first build-graph
failure.

Finding:

- The wired architecture target passes all 122 assertions after `cleanall`
  removes the STM32 tuple stamp.
- The RP2040 build reaches UnixBench, where `dhry2` and `dhry2reg` concurrently
  compile different variants into the same `src/dhry_1.o` and `src/dhry_2.o`
  paths. The register variant removes both objects after linking while the
  normal variant still needs them. The observed normal link therefore lacks
  `dhry_1.o`.
- Normal and register Dhrystone variants now own four distinct object paths,
  and their program targets depend on those objects. The recipes leave object
  removal to the directory clean rule. The compatibility `dhry2reg` target is
  an alias of the program target rather than a third compilation into shared
  paths.
- Status-6 failures reported for `ar` and `smlrc` occur only after the
  Dhrystone failure while bmake cancels the remaining parallel jobs. A direct
  conversion of the retained `smlrc.elf` succeeds, so the log does not support
  a converter defect.

Limitation: the distinct-object repair remains a dependency-graph hypothesis
until a direct parallel UnixBench build and a new clean RP2040 distribution
both pass.

Next action: run the focused UnixBench build, then restart the clean RP2040
distribution.

### RP2040 clean-distribution rerun and tool selection

Command: inspect `~/Documents/AI/Notes/1_TOOLS.md`, verify the
selected executables on the live host, and launch the clean RP2040 build in a
durable tmux session:

```sh
PYTHON=/usr/local/bin/python3
export PYTHON
bmake MACHINE=rp2040 cleanall
bmake -j12 MACHINE=rp2040 distribution
```

Result:

- The live host supplies `bmake`, ShellCheck, Ruff, the ARM GCC and binutils
  suite, `diffoscope`, `scc`, `lizard`, `bear`, `ctags`, GNU Global, `cscope`,
  `ast-grep`, and Coccinelle at the paths recorded by the tool inventory.
- Lexical source discovery, evaluated Make selection, compiler diagnostics,
  ARM binary inspection, exact hashes, and repository-native gates answer the
  remaining architecture-isolation questions. Dynamic tracing, fuzzers,
  decompilers, network scanners, GPU tools, and hardware probes would add no
  discriminating evidence to the remaining source and build claims.
- The tmux session `discobsd_rp2040_arm_isolation_distribution` writes the
  complete transcript to
  `~/logs/tmux_discobsd_rp2040_arm_isolation_distribution_20260921_135309.log`.
  The clean build exits zero after compiling and installing the RP2040 kernel,
  userland, libraries, host tools, and filesystem inputs.
- `fsutil` creates a 1,012,736-byte image with a 988 KB root partition and
  installs 19 directories, 54 files, 18 devices, 80 hard links, and one
  symbolic link.
- An independent pass extracts all `file` and `pack` rows from the composed
  `distrib/rp2040/_manifest`. All 54 rows are unique, and all 54 sources exist
  as regular files under `distrib/obj/destdir.rp2040`.

Limitation: the result proves clean cross compilation, link, install, and
filesystem composition for RP2040. The result does not prove QEMU behavior,
Renode boot, or board execution.

Next action: run the maintained gate matrix and then exercise the explicit
PDP-11/V6 opt-in targets.

### Adversarial default-entry, selector, and CI review

Command: compare a targetless root `bmake -n` with `bmake -n build`, inspect
the selector scanner against restored MIPS, VAX, and PDP-11 directives, trace
the relocation verifier's Git-object requirements into both firmware jobs,
and review maintained-machine documentation against evaluated Make selection.

Finding:

- The RP2040 distribution include defined `${MI_MANIFEST}` before the root
  `all` rule. A targetless `bmake` therefore selected only the composed
  manifest instead of the documented full build.
- The portability expression found the surviving 20-row allowlist but omitted
  retired spellings such as `MIPS`, `__mips__`, `VAX`, `vax`, `PDP11`, and
  `pdp11`. Reintroducing one of those directives would evade the gate.
- Both firmware jobs used the one-commit checkout default. `check-host` runs
  source-mode relocation verification, which resolves every original path at
  the pinned pre-migration revision; a shallow CI checkout cannot supply that
  Git object.
- `distrib/stm32/README.md` used bare root `make` commands, which select the
  RP2040 default. `usr.bin/smlrc/README.rp2040.md` still described live MIPS
  dispatch even though maintained machines both select `cgthumb.c` and the
  MIPS backend is archived.
- The Thumb link test leaves `thumb-toolroot/` until its clean target runs,
  while the directory's ignore file named the obsolete `thumb-wrap/` only.

Repair and calibration:

- Root `.MAIN: all` binds a targetless invocation to the complete maintained
  build. The architecture gate compares targetless and explicit RP2040 dry
  runs byte for byte and proves that deleting `.MAIN` changes the result.
- The selector expression covers the retired token family. Seven independent
  fixtures prove that WIN32, MIPS, `__mips__`, VAX, vax, PDP11, and pdp11 each
  reach the scanner. The exact maintained allowlist remains 20 rows.
- Both firmware checkouts request full history. Actionlint accepts the changed
  workflow.
- STM32 instructions select `bmake MACHINE=stm32`, the Smaller C note names
  both maintained ARM users and the archived MIPS backend, and the assembler
  test ignore list owns `thumb-toolroot/`.
- The direct architecture gate passes 132 assertions; a targetless RP2040 dry
  run is byte-identical to an explicit `build` dry run.

Limitation: the expanded gate proves the named lexical token family. A new
architecture spelling still requires a reviewed scanner expansion. Full Git
history in CI supplies the required provenance objects but does not replace
exact-head CI execution.

Next action: complete the explicit PDP-11/V6 build, emulator, runner, and image
composition targets, then reconcile all final evidence into the design.

### Explicit PDP-11/V6 opt-in closure

Command:

```sh
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v6
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes check-legacy-pdp11-v7-runner
bmake MACHINE=rp2040 BUILD_PDP11_V6=yes legacy-pdp11-v6-distribution
```

Result:

- Archive-mode relocation verification passes all 1,032 rows before each
  legacy test surface runs.
- The host emulator compiles as C17 with `-Wall -Wextra -Werror`, boots the V6
  guest pack, executes `echo hello from v6`, observes the expected output, and
  exits after 11,873 K guest instructions.
- The separate V7 reference runner passes all 25 unit tests for profile,
  transcript, image, process-boundary, timeout, replacement-race, source-image,
  and evidence-publication contracts. The command does not use an external V7
  image or claim a SIMH guest result.
- The RP2040 cross build links an 82,354-byte emulator a.out with warnings
  fatal, installs it as `/usr/bin/pdp11`, expands the V6 pack as
  `/usr/v6/root.rk`, and composes the opt-in image successfully.
- `fsutil` creates a 1,012,736-byte image with 20 directories, 56 files, 18
  devices, 80 hard links, and one symbolic link. An independent audit finds 56
  unique `file` or `pack` rows, zero missing staged sources, and both required
  legacy paths in the composed manifest.

Limitation: the V6 host run proves the repository emulator and guest pack on
the host. The cross build proves RP2040 compilation and image composition. The
external SIMH/V7 guest reference, Renode boot, and board execution remain
separate evidence classes; the isolation change performs no flash operation.

Next action: update the reconciled design with the final observed matrix, run
the post-edit lint and provenance gates, and complete diff review and
integration.

### Final selector denominator and calibrated repository scan

Command: expand the POSIX ERE to the complete audited retired-target
vocabulary, run the same `git grep` scanner against the maintained checkout
and a temporary Git repository containing an unrelated `#ifdef MIPS`, then
run ShellCheck and both direct and root-wired architecture gates.

Finding:

- The imported compiler runtime retains two `_MSC_VER` directives that the
  earlier 20-row set did not enumerate. The reconciled allowlist contains 22
  exact `path:directive` rows and has SHA-256
  `d209e52de37b23939419d210178f25a33a6ce3b5dfeae7a6c57b7d1f1141b21a`.
- The explicit vocabulary covers PDP-11, VAX/VMS, NS32000, MIPS and PIC
  families, x86/I86/Xenix, HPPA, SPARC, PowerPC, Alpha, M68K, SuperH,
  Interdata, Z8000, PC XT, SEL, MS-DOS, and the archived embedded-host
  selectors. Exact token boundaries keep unrelated identifiers out.
- Fourteen representative restored directives reach the expression. The
  temporary repository scan produces the exact row
  `tests/unrelated-maintained.c:#ifdef MIPS`; exact allowlist comparison
  rejects that unrelated maintained-file mutation.
- ShellCheck reports zero errors. The direct and root-wired gates each report
  `architecture isolation verified: assertions=140 machines=2
  generated-kernel-makefiles=12 relocation-rows=1032`.
- The cleanup implementation recursively enumerates files, symbolic links,
  and directories only inside its owned mode-0700 temporary directory. The
  path guard bounds that recursive cleanup to the canonical temporary parent.

Limitation: lexical vocabulary remains finite by design. A newly introduced
architecture spelling requires an explicit reviewed token and a rejecting
fixture; the exact allowlist prevents silent admission of a matching row.

Next action: preserve the chronological ledger as evidence, treat
`docs/research/arm-main-legacy-build-isolation.md` as the reconciled design,
and complete exact-head CI and review before merge.

### CI cleanup-fixture mode normalization

Observation: exact-head Ubuntu firmware CI completed the build, flash image,
warning, and lint steps, then failed the host tier because the cleanup-path
negative control did not emit its expected non-executable-command diagnostic.
The fixture created its command file without declaring the permission mode,
so the test depended on the runner's file-creation policy rather than the
condition it intended to exercise.

Repair: the fixture applies mode 0600 before passing the file as the cleanup
command. The mode makes the negative control deterministic across local and CI
filesystems while preserving the arbitrary file for the separate unlink-target
assertion.

Next action: rerun the direct and wired architecture gates, push the focused
fixture repair, and require the replacement exact-head firmware run to pass.

### Exact-head cleanup-command portability failure

Command: inspect the failed Ubuntu firmware job at exact head `0e36acfa`,
locate the architecture-isolation assertion that failed, and compare the
cleanup helper's make-command admission rule with its deliberate
non-executable-path fixture.

Finding:

- The maintained RP2040 build, host tests, filesystem profiles, and preceding
  architecture assertions completed before the cleanup-path assertion.
- `tools/clean-build-machines.sh` combined an executable-file test with
  `command -v` for both bare command names and slash-containing paths. The
  Ubuntu shell did not produce the fixture's required non-executable-path
  diagnostic, while the local host did. A command search and an explicit
  pathname are different interfaces and require separate validation.
- The helper now requires `-x` for every command containing `/` and uses
  `command -v` only for a bare command name. The existing fixture continues
  to preserve an arbitrary non-executable file and requires the exact
  rejection diagnostic.

Limitation: a local pass proves the corrected rule on the development host.
The rerun at the updated branch head decides whether Ubuntu accepts the same
contract.

Next action: rerun ShellCheck and the 140-assertion architecture gate, commit
and push the repair, then require green exact-head CI before merge.

### macOS cleanup-fixture command lookup

Observation: the replacement Ubuntu firmware matrix passed, while macOS
completed its build, image, warning, lint, and host tests before the
symbolic-checkout cleanup fixture rejected `/bin/true`. The macOS runner does
not provide the Linux-specific `/bin/true` pathname.

Repair: every fixture that needs POSIX `true`, `false`, or `echo` passes the
portable bare command name. The cleanup helper resolves a bare command through
the shell's command search, which is the production interface those fixtures
need; the non-executable temporary pathname and fake executable scripts cover
explicit-path admission separately.

Next action: rerun the calibrated isolation gate and require every replacement
exact-head job, including macOS firmware, to pass before merge.
