# Agent guide: DiscoBSD RP2040 port

This RP2040 port forks chettrick/discobsd (`upstream`, fetch-only master);
`origin/main` is the owned branch. `docs/INDEX.md` routes port documentation
under `sys/arch/rp2040/doc` and research under `docs/research`. A document
cited as authority by shipped code, a Makefile or the root manifest belongs
in the former and its `research/README.md` index; investigations and proposals
belong in the latter. The
constrained-C and C17 migration proposal is `docs/research/STYLE-GUIDE.md`,
outside the startup instruction chain.

`AGENTS.md` owns these rules. The regular `.claude/CLAUDE.md` is its generated,
byte-identical compatibility copy. Run `sh tools/sync_agent_instructions.sh`
after editing the canonical file; do not add active imports.

## Target

RP2040 is Cortex-M0+ (ARMv6-M, Thumb-1), without FPU or MMU, with 264 KB
SRAM and 2 MB QSPI flash. The fixed layout is a 128 KB kernel, 1536 KB
Dhara root (989 KB usable), 384 KB raw swap and one flat 144 KB process
window for OMAGIC a.out text, data, bss and stack. The MPU grants permissions,
not address translation. Protection claims require written/read-back register
values, privilege transition, region coverage and a deliberate fault test;
`sys/arch/rp2040/doc/MPU.md` owns the map and evidence. Boot ROM V3 is verified
on the measured board. USB CDC-ACM is the primary console; UART0 on GP0/GP1
is the fallback. Resolve board ports through `/dev/serial/by-id/*DiscoBSD*`.

## Build, image, flash

Select `PYTHON` in the caller before every build or gate that invokes a Python
tool. The build system rejects an empty interpreter value.

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON
    bmake MACHINE=rp2040 distribution      # tools, kernel, world, sdcard.img
    bmake MACHINE=rp2040 flash             # distrib/rp2040/flash.uf2
    bmake MACHINE=rp2040 kernel            # sys/arch/rp2040/compile/PICO/unix.uf2
    bmake MACHINE=rp2040 check            # all non-hardware tiers

`distrib/rp2040/host/DEVELOPMENT.md` owns host setup and manual flashing.
`bmake build` does not relink programs after libc changes: clean first when
the change must reach all programs. Rebuild sysctl and adminbox from clean
after `sysctl.h` or `machine/cpu.h` changes. The generated PICO and PICO_UART
Makefiles are tracked; synchronize both when generation inputs change.

## Tests

- `bmake MACHINE=rp2040 check` runs lint, host, 32-bit POSIX shell, cross,
  qemu-user, host-package and board-build tiers. `check-renode` boots a
  PICO_UART kernel and stands outside the aggregate. Board execution requires
  explicit opt-in. `sys/arch/rp2040/doc/TESTING.md` owns each gate's execution
  class and claim; add new gates there and to the root Makefile and CI owner.
- `BUILD_LEGACY_NON_ARM=yes legacy-non-arm-verify` checks archive identity.
  `BUILD_PDP11_V6=yes` explicitly opts into isolated PDP-11/V6 tests; neither
  makes legacy code part of the maintained ARM build. See
  `docs/research/arm-main-legacy-build-isolation.md`.
- Board tests under `tests/rp2040` are absent from the shipped manifest.
  Stage test-image entries in `distrib/rp2040/mi.rp2040` and revert them before
  committing. Host package tests and setup live in `distrib/rp2040/host`.

## Evidence

Match evidence to the claim. Specifications define intended interfaces;
source describes the implementation; tests establish behavior for their
exercised conditions; hardware observations establish behavior for the
measured configuration. Investigate contradictions rather than resolving
them through a universal ranking or a count of agreeing observations.
Name the source revision, execution environment, exercised conditions and
remaining uncertainty. sys/arch/rp2040/doc/DATASHEET-INDEX.md resolves
RP2040 datasheet sections to pages.

Build, host, cross, qemu-user, Renode and physical-board results are separate
evidence classes. A warning-free build establishes compilation. Qemu-user
exercises an instruction sequence; Renode boots with third-party peripheral
models; a board run measures the exercised silicon configuration. A test
target that skips an optional variant establishes no execution for that
variant. `sys/arch/rp2040/doc/TESTING.md` defines the gates.

A new gate, linter or probe is calibrated against a known-good and a
known-bad input before its verdict counts, because a gate that passes on
input it should reject reports nothing.

For a kernel or driver change that rests on board behavior, record the direct
observation, governing specification or invariant, implementation hypothesis,
falsifier, deciding capture and predicted gate movement before editing.
Investigate deviations instead of rewriting the prediction.

### Stop points

Report and rescope when a result contradicts the model or specification,
or a fix needs an architecture choice the tree leaves unsettled. Name the
evidence chain, alternatives and next discriminating measurement. Preserve
the hardware opt-in boundary while investigating.

Compare implementation, interface requirements and test oracles for each
claim. Identify exercised failure modes and shared oracle assumptions; record
the search, history or binary-inspection command behind a symbol claim.

## Remotes and publication

Push only to `origin`. Treat `upstream` as fetch-only; rebasing upstream into
`main` or submitting a patch upstream requires a deliberate scoped request.
Use merge commits for reviewed pull requests. Rewrite a branch's history
before review when needed; do not squash at integration.

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

Use a component-and-mechanism subject. The body records the failed or
preserved invariant, claim-specific evidence and test outcome; the PR holds
command logs and environment tables. Keep chronology in commits and PRs,
not source comments. Disclose AI participation with `Assisted-by: TOOL (MODEL)`
or `Generated-by:`; reserve `Co-authored-by:` for humans. Each
commit must build and bisect independently; separate formatting changes.

## Reports and patches sent upstream

Send upstream reports only on explicit request. Save the draft under
`docs/research`; disclose assistant drafting in its first line. Lead with
the finding, mechanism and user consequence. Include exact environment,
claim-specific evidence, reproducible steps, a `git am` patch, before/after
measurement and residuals. Follow the recipient's conventions.

## Safety stop-line

Stop feature work and report a secret, token or private hostname in a tracked
file or log;
unquoted data reaching `sh -c`, `eval` or a generated script; an untrusted
input path; or a board-flash/serial path reached without caller authorization.
Hardware gates require an exact opt-in value; unset, empty and zero remain
closed. Root Makefile targets only build board tests. Explicit invocations of
`discobsd-flash` or `picotool` carry the user's authorization.

## Tools

Use source search, history, compiled-object inspection and behavior probes
according to the claim. Treat warnings as errors (`shellcheck -S error`,
`ruff`, `-Wall -Wextra -Werror`). Report unavailable tools and unrun checks
with their blockers; a missing tool never becomes a passing check.

## Conventions

- Work in a worktree under `~/worktrees/discobsd-2040-unofficial/<branch>`,
  push the branch, open a PR against `origin` main, merge, delete branch and
  worktree. Never commit to main directly. Preserve unrelated edits, untracked
  files and other worktrees; remove only proven task residue.
- Build outputs are ignored per directory (a .gitignore holding the
  program name); never commit compile outputs, the sdcard image or test
  a.outs.
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
