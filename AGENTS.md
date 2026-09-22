# Agent guide: DiscoBSD RP2040 port

This is the DiscoBSD port to the Raspberry Pi Pico (RP2040), a fork of
chettrick/discobsd (remote `upstream`, branch master) published as
github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial (remote `origin`,
branch main). The tree carries the code, the man pages, the port
documentation under sys/arch/rp2040/doc, and the research corpus under
docs/research. docs/INDEX.md maps every document in both and is the
entry point for a reader who does not yet know which one holds an answer.
The constrained-C and C17 proposal and its migration evidence live at
`docs/research/STYLE-GUIDE.md`. Read that proposal when the task concerns
style migration; the proposal is outside the startup instruction chain.

Two doc homes, one rule: a document that code, a Makefile or the root
manifest cites as the authority for a shipped mechanism lives in
sys/arch/rp2040/doc and is listed in its research/README.md. Every other
note -- an option survey, a size audit, a tuning report, a handback, an
investigation -- lives in docs/research. The sibling repository
github.com/Oichkatzelesfrettschen/discobsd-2040-notes keeps the vendored
RP2040 and Pico datasheets under docs/rp2040/, which
sys/arch/rp2040/doc/DATASHEET-INDEX.md cites by section and page rather
than carrying them here.

AGENTS.md owns these rules. CLAUDE.md is a regular compatibility wrapper
containing only `@AGENTS.md`. The wrapper imports the canonical policy;
the canonical policy uses literal references instead of active imports.

## Target

RP2040: Cortex-M0+ (ARMv6-M, Thumb-1), no FPU, no MMU, 264 KB SRAM,
2 MB QSPI flash. Datasheet 2.4.1 lists eight MPU regions and 2.4.6 gives
MPU_TYPE, MPU_CTRL, MPU_RNR, MPU_RBAR and MPU_RASR, so the core carries a
Protected Memory System Architecture MPU. sys/arch/rp2040/rp2040/mpu.c
programs it once at startup with a static three-region map (boot ROM
read-execute, the 144 KB user window read-write-execute as a 128 KB and a
16 KB region) over the privileged default map, reads every register back,
and reports the result on the console and through `machdep.mpu`;
sys/arch/rp2040/doc/MPU.md is the authority. An MPU grants region
permissions and faults a violation to HardFault; it supplies no address
translation, so one flat process window stays the layout. A protection
claim cites the register values written, the privilege transition, the
region coverage and a deliberate fault test (usr.bin/mputest, run by
check-renode); a linker region annotation proves none of them.
Layout: 128 KB kernel, 1536 KB Dhara root (989 KB usable),
384 KB raw swap. A process gets one 144 KB window for text, data, bss and
stack; user programs are a.out OMAGIC. Boot ROM V3 on the verified board.
The console is CDC-ACM over the board's own USB cable (/dev/ttyACM*,
resolved by /dev/serial/by-id/*DiscoBSD*); UART0 on GP0/GP1 is the
fallback. Login as operator (no password) and `su` for root (blank
password). The console shell echoes keystrokes, so serial captures need
escape sequences stripped.

## Build, image, flash

Select `PYTHON` in the caller before every build or gate that invokes a Python
tool. The build system rejects an empty interpreter value.

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON
    bmake MACHINE=rp2040 distribution      # tools, kernel, world, sdcard.img
    bmake MACHINE=rp2040 flash             # distrib/rp2040/flash.uf2
    bmake MACHINE=rp2040 kernel            # sys/arch/rp2040/compile/PICO/unix.uf2
    bmake MACHINE=rp2040 check-divider     # no kernel SIO divider use
    bmake MACHINE=rp2040 check-swapram     # linked SwapRAM tier matches Config
    bmake MACHINE=rp2040 check-cache-footprint # cache ABI and chain invariants
    bmake MACHINE=rp2040 check-elf2aout    # a.out layout gate

Reflash from a running kernel: `distrib/rp2040/host/discobsd-flash
<uf2>...` (kernel, filesystem image, or both), which reboots into
BOOTSEL, unmounts the RPI-RP2 volume, loads and reboots; by hand it is
`picotool reboot -u -f`, `picotool load <uf2>`, `picotool reboot`. A hung
kernel needs BOOTSEL held through a replug. On macOS the toolchain is
`brew install bmake byacc bison flex groff mandoc pkgconf picotool` plus
`brew install --cask gcc-arm-embedded` (not the arm-none-eabi-gcc
formula); distrib/rp2040/host/DEVELOPMENT.md has the per-OS table. `bmake build` does not relink
programs when libc changes; run `bmake MACHINE=rp2040 clean` first when a
libc or header change must reach every program, and rebuild sbin/sysctl and
sbin/adminbox from clean after a sysctl.h or machine/cpu.h change. The
config-generated Makefiles under sys/arch/rp2040/compile/PICO and PICO_UART
are tracked; commit synchronized outputs when their generation inputs change.

## Tests

- `bmake MACHINE=rp2040 check` runs every tier; the tiers are check-lint
  (shellcheck -S error, ruff), check-host (host cc and python),
  check-posix-sh (32-bit Linux), check-cross (after build), check-qemu,
  check-host-package and check-board-build. check-renode boots
  the kernel under Renode and stands outside check, because it wants the
  emulator and a fetched model tree.
  sys/arch/rp2040/doc/TESTING.md lists each gate and what it proves;
  a new test joins a tier there and in the root Makefile. CI runs the
  tiers in .github/workflows/firmware.yml.
- `BUILD_LEGACY_NON_ARM=yes legacy-non-arm-verify` checks archive identity
  without adding a supported build. `BUILD_PDP11_V6=yes` is an RP2040-only
  opt-in for the isolated emulator, guest pack, and their explicit tests;
  `docs/research/arm-main-legacy-build-isolation.md` owns that boundary.
- On the board, from tests/rp2040: fptest (Boot ROM float, bit-exact),
  sigtest (signal frames), streamtest (NSTATIC), tartest, romprobe (ROM
  table dump), swapmaptest (the swap map through sysctl(3)). They are not
  in the root manifest; stage them by adding
  `file /usr/bin/<name>` lines to distrib/rp2040/mi.rp2040 for the test
  image and revert those lines before committing.
- Kernel trace: `sysctl -w kern.systrace=1` (syscalls) or 2 (signal
  frames), `kern.systracepid` to filter; under "options SYSTRACE".
- Host tools: distrib/rp2040/host is the discobsd-host Python package
  (discobsd-term, discobsd-web with `--bind 0.0.0.0 --token SECRET` for the
  LAN, discobsd-link, discobsd-console up/down/status), with the udev rule,
  systemd user units, packaging/ (PKGBUILD, debian/, PyInstaller spec), and
  tests (`ruff check . && pytest` there). CI: .github/workflows/host.yml
  builds and smoke-installs every platform artifact; firmware.yml builds
  the UF2 files.

## Evidence

Match evidence to the claim. Specifications define intended interfaces;
source describes the implementation; tests establish behavior for their
exercised conditions; hardware observations establish behavior for the
measured configuration. Investigate contradictions rather than resolving
them through a universal ranking or a count of agreeing observations.
Name the source revision, execution environment, exercised conditions and
remaining uncertainty. sys/arch/rp2040/doc/DATASHEET-INDEX.md resolves
RP2040 datasheet sections to pages.

Build, host-gate, cross, qemu, Renode and board results are separate
evidence classes, and each stands for itself. A warning-free build proves
compilation. A qemu-user run exercises the tested instruction sequence. check-renode
boots the PICO_UART kernel from the real RP2040 boot ROM against
third-party peripheral models, asserts the console from the device probe
through a logged-in shell, and holds the emulator's warnings to a named set
of classes. A board run measures the exercised silicon configuration.
sys/arch/rp2040/doc/TESTING.md is the
authority for what a given gate proves.

A new gate, linter or probe is calibrated against a known-good and a
known-bad input before its verdict counts, because a gate that passes on
input it should reject reports nothing.

### Falsification record

A kernel or driver change that rests on board behavior records, before the
edit:

- the direct observation;
- the governing datasheet section, ARM ARM clause or kernel invariant;
- the implementation hypothesis;
- the criterion that falsifies it;
- the command or capture that decides it;
- the gate movement it predicts, named by target.

A result that deviates from the prediction is the finding; it opens the
next investigation rather than rewriting the prediction after the fact.

### Stop points

Report and rescope when a result contradicts the model or specification,
or a fix needs an architecture choice the tree leaves unsettled. Name the
evidence chain, alternatives and next discriminating measurement. Preserve
the hardware opt-in boundary while investigating.

### Review checks

- Compare implementation, interface requirements and test oracles for the
  claimed behavior. Challenge the strongest contrary explanation.
- Identify the failure modes each test exercises and any shared oracle
  assumptions. Tests of the same object may exercise different failures;
  separate tests may share an incorrect oracle.
- Record the discovery mechanism beside a symbol claim: `(rg
  --fixed-strings flash_swap_append sys/)`, `(git log -S SYMBOL)`,
  `(arm-none-eabi-nm -g <object>)`.

## Remotes and publication

`origin` is the owned fork, its default branch is `main`, and it is the
only push target. `upstream` (chettrick/discobsd, branch master) is the
project this port forked from and is fetch-only: upstream reaches `main`
through a deliberate rebase that records the divergence, and a submission
back to it happens under an explicit request naming the scope.

Pull requests use merge commits so `main` retains the reviewed commit
boundaries and their individual validation records. Squash merging is
forbidden. A branch that needs a smaller or clearer history rewrites that
history before review rather than replacing it at integration time.

## Source comments

Describe current behavior, invariants, and non-obvious constraints. Put
investigation chronology, review discussion, and change history in commits,
issues, or research notes. Name related symbols and files when they clarify
the mechanism. Comment length follows the mechanism's explanatory needs;
individual words such as "this", "now" or "previously" do not decide whether
a comment describes implementation behavior or investigation history.

A TODO names the missing mechanism, the constraint that defers it and its
durable tracking artifact. New work lands complete.

## Commit messages

The subject carries a component prefix and the mechanism:
`rp2040: attach uart0, which the configuration names and the probe never
accepted`. The body is the review: the invariant that held or failed, the
change at the depth a maintainer needs, claim-specific evidence (a register
dump, a measurement, a gate name), and the test outcome, in one to five
paragraphs. Build invocations, logs and environment tables go in the PR
description. Historical debate and rejected alternatives live here rather
than in the source. AI participation is disclosed in trailers,
`Assisted-by: TOOL (MODEL)` or `Generated-by:`, and `Co-authored-by:`
names humans only. A commit builds and bisects on its own, and a
formatting change is its own commit.

## Reports and patches sent upstream

A report to another project is written for a maintainer who has minutes
and owes this port nothing. It leads with the finding in one sentence,
then the mechanism, then the consequence a user of their project sees. It
carries the environment by exact version, including where it differs from
what the project pins; claim-specific evidence that distinguishes observations
from inference; a reproduction the maintainer can run; the fix as a `git am`
patch with its own message; the measurement before and after; and what
the patch leaves open. Hardware claims name the measured configuration or
cited specification; source and gate claims retain their exercised boundary.
The project's own conventions
win where they exist -- its file headers, its test layout, its subject
tags -- and the report says where it could not follow them.

A report drafted with an assistant opens with one line that says so and
leaves the decision with the maintainer. It is saved under docs/research
before it is sent, and sent only on an explicit request that names it.

## Safety stop-line

Stop feature work and report on finding a secret, token or private host
name in a tracked file or a log; an unquoted expansion reaching `sh -c`,
`eval` or a generated script; a path built from untrusted input; or a
flash, BOOTSEL or /dev/ttyACM* path a caller reaches without asking for it.

A gate or test that reaches the board opens that path on an exact opt-in
value; unset, empty and zero stay closed. No root Makefile target reaches
the board: check-board-build builds the board tests and stops. A tool the user invokes to flash --
distrib/rp2040/host/discobsd-flash, picotool -- carries the request in
the invocation and needs no gate.

## Tools

Reach for the tool that matches the claim.

- Symbols and call paths: `clangd`, `git grep`, `rg`, `fd`, `git log -S`,
  `git log -G`, `git blame`, and `ast-grep` for a structural pattern.
- Compiled output: `arm-none-eabi-objdump -d`, `readelf`, `nm`, `size`, the
  tree's own gates under tools/, and capstone and pyelftools under
  `${PYTHON}` as the cross tier uses them.
- Behavior: qemu-arm for the instruction sequence, `bmake check-renode`
  for a boot from the real boot ROM, the board over
  /dev/serial/by-id/*DiscoBSD* for everything else, `sysctl -w
  kern.systrace=1` for the syscall stream, romprobe for the ROM table.
- Hygiene: `shellcheck -S error`, `ruff`, `-Wall -Wextra -Werror`.

A tool the host lacks is an environment fact: name the package, install it,
run the check. A check that stays unrun is reported `not run` with the
blocker.

## Conventions

- Work in a worktree under `~/worktrees/discobsd-2040-unofficial/<branch>`,
  push the branch, open a PR against `origin` main, merge, delete branch and
  worktree. Never commit to main directly.
- Build outputs are ignored per directory (a .gitignore holding the
  program name); never commit compile outputs, the sdcard image or test
  a.outs.
- Multicall binaries: box, sysbox, adminbox, textbox, utilbox, gamebox; the
  manifest's `link` lines name their entry points. Editors shipped: ed and
  stevie (vi and vim are hard links to stevie); re and kilo build but are
  not shipped.
- Retire a worktree when its branch merges: confirm the tree is clean and
  HEAD is reachable from a pushed ref, `git worktree remove` it without
  `--force`, and confirm the removal through `git worktree list --porcelain`.
- After a fix, read `git diff --staged` adversarially: every removed line is
  an intended correction, every test label names the command that actually
  runs, and every symbol a comment or document names resolves in the source.
  A defect worth fixing is worth the gate that catches its class.
- A subagent collects evidence read-only unless the task grants it more, and
  carries an explicit `model`. Synthesis, edits, commits and the final claim
  stay with the parent.
- A new file under sys/arch/rp2040 written for this port carries
  `Copyright (c) <year> DiscoBSD` and the ISC permission notice. NOTICE
  section 1 names those files as an ISC-licensed category beside the
  BSD 3-Clause LICENSE, so the header is the grant a redistributor relies
  on rather than an invented attribution, and the global no-copyright-line
  rule yields to it here.
- The ledgers under sys/arch/rp2040/doc/research are retained evidence.
  audit-findings.md is a verbatim capture with its source SHA-256 in the
  header, so it records the tree as it stood at the commit it names.
  Corrections go to audit-response.md; the ledger stays as captured.
- The user's global instructions apply (emoji-free text, `--` not em dash,
  American English, POSIX sh with set -eu, ${PYTHON}, no local paths or
  secrets in commits, `Assisted-by:` trailers).
