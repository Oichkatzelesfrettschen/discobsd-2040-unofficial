# DiscoBSD/Pico Unix Compatibility Profile 2024

DPUCP-2024 is a measured compatibility profile for DiscoBSD on RP2040. It
does not claim POSIX or UNIX conformance. The profile accounts for every
Issue 8 entity while allowing implementation and evidence to advance one
bounded contract at a time.

## Finite denominator

`sus-requirement-entities.tsv` remains the standards-source denominator. The
DPUCP ledger contains exactly the 1,518 rows with an Issue 8 source, including
all 152 Issue 8-only rows. The 1,617-row Issue 7/Issue 8 union remains visible
so an edition change cannot silently remove an older requirement from review.

`dpucp-2024-requirements.tsv` assigns every Issue 8 row an applicability,
implementation, evidence and residual state. Initial rows remain open. A row
uses `base` as the conservative DPUCP accounting default until source analysis
identifies a standard option boundary. The default does not assert that the
corresponding POSIX page belongs to the normative Base option.

The verifier permits `unselected-standard-option` only when `option_owner`
names the standard-defined option. Issue 8 Base gaps stay open. An applicable
row closes only with `implementation=present`, a nonempty evidence class and a
cataloged executable witness. Every open or blocked row names its next deciding
action.

## C language contract

The production compiler mode is GNU17. Portable mechanisms also compile as
strict C17. Kernel, assembly, Boot ROM and direct-register seams use the exact
GNU17 Cortex-M0+ flags. Smaller C remains a separately measured acceptance
surface. Donor code that requires C23 is rewritten to C17 before admission.

## Evidence and size

The evidence ladder is `none`, `static`, `host`, `cross`, `qemu-user`,
`renode`, and `board`. The labels identify execution surfaces rather than an
automatic ranking. Each witness catalog entry supplies its command, evidence
class and known-good and known-bad calibration artifacts.

The default growth rule requires a nonpositive delta on every constrained
surface. Flash savings do not offset SRAM growth, and one small executable does
not offset a larger process-window maximum. An exception records exact deltas,
examined alternatives, retained margins, user benefit, rollback point and
reviewer approval.

## First hardware release

The first hardware release binds each shipped acceptance surface to an Issue 8
row and its calibrated pre-board witness. The ordered surface is shell
`command`, shell `getopts`, utility `printf`, `readlink`, `realpath`, `uudecode`
and `timeout`. Group labels cannot stand in for individual utilities because a
group could remain unchanged while one shipped program silently leaves the
board workload.

Host, ILP32, sanitizer and cross evidence prepare these utilities for the
release. Renode remains a separate emulator class. Physical-board evidence is
recorded only after the approved UF2 pair executes each named surface through
the measured USB console.

## Later compatibility families

The schema-2 `later_families` records keep seven deferred families inside the
finite profile instead of treating them as an unbounded wish list. Each record
names its Issue 8 Base or standard-option boundary, Base residual, dependency
closure, minimum useful subset, RP2040 feasibility questions, screening
footprint estimate, deciding experiment and repository evidence paths.

The screening classes describe expected engineering pressure, not measured
bytes. A family advances only after its deciding experiment produces the
kernel, process-window, packed-root and runtime-memory measurements required by
the normal growth policy. An unavailable standard option never closes a Base
row that POSIX places outside that option.

The seven families are the on-device C environment, shared BRE/ERE and text
processing, networking, locale, threads, realtime interfaces and dynamic
linking. Their order records dependency planning only; it grants no conformance
or implementation status.

## Reproduction

Set the caller-selected interpreter and run the calibrated repository gate:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-dpucp-profile
```

The committed ledger is deterministic. Regeneration is an explicit reviewed
operation:

```sh
"${PYTHON}" tools/check_dpucp_profile.py --root . --refresh
git diff -- docs/research/dpucp-2024-profile.json \
    docs/research/dpucp-2024-requirements.tsv
```

The refresh command re-derives corpus-ordered rows while preserving reviewed
state columns by requirement identifier. It then refreshes the row count,
ordered-row hash and state counts while preserving the profile contract,
including the later-family records. Use it only when intentionally reconciling
corpus or bounded-mapping changes, and inspect both generated-file diffs.
