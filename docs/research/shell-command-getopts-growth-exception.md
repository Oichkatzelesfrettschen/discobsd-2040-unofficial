# Shell command and getopts growth exception

Status: pending independent reviewer approval.

This record requests a DPUCP-2024 exception for the `command` and `getopts`
shell builtins. The implementation does not claim POSIX or UNIX conformance.
The behavior evidence covers the named compatibility subset exercised by
`check-posix-sh`.

## Compared states

The clean base is `e7730db124a0324b62afe209b31dd210c270c0dc`. The measured
implementation is `9606d2efc9bdb665434b33aa834ec543ce582bfb`. Both states used
the same GNU Arm Embedded toolchain and caller-selected Python interpreter.
Each state was built from clean through `bmake MACHINE=rp2040
logged-distribution`; `fsutil --check --partition=1`, the target `size` tool,
GNU `arm-none-eabi-size`, and `wc -c` supplied the measurements. The companion
JSON file retains the arithmetic in machine-readable form.

The measurement host ran Linux 7.2.9-2-cachyos on x86-64 with bmake
20260912, GNU Arm Embedded GCC 16.2.0, GNU binutils 2.47.20260726, host GCC
16.2.1, and Python 3.14.7. The caller exported `PYTHON=/usr/bin/python` before
each build and gate. The clean build commands were:

```sh
PYTHON=/usr/bin/python
export PYTHON
bmake MACHINE=rp2040 logged-distribution
bmake MACHINE=rp2040 logged-check
```

| Constrained surface | Base | Candidate | Delta | Retained margin |
| --- | ---: | ---: | ---: | ---: |
| PICO kernel text | 104,598 bytes | 104,598 bytes | 0 | 26,226 flash bytes after text and data |
| PICO kernel data | 248 bytes | 248 bytes | 0 | unchanged |
| PICO kernel bss | 39,848 bytes | 39,848 bytes | 0 | unchanged |
| Shell a.out file | 32,576 bytes | 35,240 bytes | +2,664 bytes | n/a |
| Shell text | 30,132 bytes | 32,776 bytes | +2,644 bytes | n/a |
| Shell data | 2,412 bytes | 2,432 bytes | +20 bytes | n/a |
| Shell bss | 7,152 bytes | 7,168 bytes | +16 bytes | n/a |
| Shell process-window occupancy before stack | 39,696 bytes | 42,376 bytes | +2,680 bytes | 105,080 bytes before stack |
| Packed root data blocks | 682 | 684 | +2 blocks | 295 free 1 KiB blocks, or 302,080 bytes |
| Packed root files | 95 | 95 | 0 | unchanged |

The comparison records unchanged kernel section sizes rather than treating
different build-identification bytes as a kernel growth. Runtime peak stack
and heap remain unmeasured. The process-window margin therefore excludes stack
and constitutes a lower-bound accounting input rather than a peak-RAM claim.

## Examined offsets

The feature-first shell measured 35,544 a.out bytes. The following refactors
reduced the accepted candidate to 35,240 bytes while retaining every behavior
witness:

- Command cleanup and type-reporting state consolidation removed 232 a.out
  bytes and four bss bytes.
- Reusing `findpath()` instead of a second PATH-search loop removed 48 a.out
  text bytes.
- Encoding saved-variable modification in the saved flags removed four heap
  bytes per distinct command-prefix variable without increasing the linked
  image. Two rejected encodings increased target text by 16 and eight bytes.
- Publishing `OPTIND` only when its value changes removed 20 text bytes and
  four bss bytes.
- Sharing the two `getopts` diagnostic tails removed four text bytes.

An external `command` applet cannot bypass shell functions, classify builtins,
or remove special-builtin properties inside the invoking shell. An external
`getopts` applet cannot update `OPTIND`, `OPTARG`, or the named variable in the
invoking shell. Those decompositions fail the required semantics while adding
another executable or multicall dispatch entry. Dropping `-p`, `-v`, `-V`,
leading-colon mode, clustered options, explicit operands, or reset transitions
would reduce the requested compatibility subset rather than offset its cost.
No examined valid decomposition produced nonpositive shell and root deltas.

## User-visible benefit and rollback

Recovery scripts gain `command -p` for a known utility search path, `command
-v` and `command -V` for lookup inspection, function-bypass execution, and
specified failure statuses. Shell scripts gain stateful `getopts` parsing with
`OPTIND`, `OPTARG`, leading-colon mode, clustered options, explicit operands,
diagnostics, and reset behavior. These mechanisms avoid separate utilities and
operate in the only process that can change shell lookup and variable state.

The rollback point is base commit
`e7730db124a0324b62afe209b31dd210c270c0dc`. Reverting the shell implementation
and its evidence commit restores the base shell and two packed-root blocks but
removes both compatibility subsets.

## Approval

Reviewer: pending.

Approval evidence: pending. The branch must not merge until an independent
reviewer confirms the exact deltas, examined alternatives, retained margins,
behavior benefit, and rollback point.
