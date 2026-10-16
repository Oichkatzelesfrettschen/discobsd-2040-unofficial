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

The refresh command resets rows from the source corpus and existing bounded
mappings. Use it only when intentionally rebuilding the initial ledger; normal
profile work edits reviewed rows and updates the pinned counts and hash.
