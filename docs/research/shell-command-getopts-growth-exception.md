# Shell command and getopts growth exception

Status: pending independent reviewer approval.

This record requests a DPUCP-2024 exception for the `command` and `getopts`
shell builtins. The implementation does not claim POSIX or UNIX conformance.
The behavior evidence covers the named compatibility subset exercised by
`check-posix-sh`.

## Compared states

The clean base is `e7730db124a0324b62afe209b31dd210c270c0dc`. The measured
implementation is `e089f3c05e913f70561cb2a03ff303a4830de485`. Both states used
the same GNU Arm Embedded toolchain and caller-selected Python interpreter.
Each state was built from clean. The base used `bmake MACHINE=rp2040
logged-distribution`, and the final candidate used the supervised command
below. `fsutil --check --partition=1`, the target `size` tool, GNU
`arm-none-eabi-size`, and `wc -c` supplied the measurements. The companion JSON
file retains the arithmetic in machine-readable form.

The measurement host ran Linux 7.2.9-2-cachyos on x86-64 with bmake
20260912, GNU Arm Embedded GCC 16.2.0, GNU binutils 2.47.20260726, host GCC
16.2.1, and Python 3.14.7. The caller exported `PYTHON=/usr/bin/python` before
each build and gate. The final candidate used the supervised
clean-distribution command below. The runner retained combined output, emitted
30-second heartbeats, enforced a 120-second output-idle deadline and enforced a
1,800-second hard deadline.

```sh
PYTHON=/usr/bin/python
export PYTHON
"$PYTHON" tools/run_logged_gate.py \
    --evidence-root .build-logs \
    --label pr299-e089f3c0-clean-distribution \
    --heartbeat-seconds 30 \
    --idle-timeout-seconds 120 \
    --timeout-seconds 1800 \
    -- bmake MACHINE=rp2040 distribution
```

The terminal receipt is
`.build-logs/pr299-e089f3c0-clean-distribution.latest.json`. It records exit
status 0, 114.4 seconds elapsed and 821,371 output bytes. The clean build ran
at `e089f3c05e913f70561cb2a03ff303a4830de485`. The ILP32 adapter repairs add an
`SH_HOST_RAWDIR` selection in `expand.c`.
The target build leaves `SH_HOST_RAWDIR` undefined, so the selection
preprocesses to the existing `read` call. A clean GNU17 target-shell rebuild at
the measured implementation commit reproduces the 35,908-byte OMAGIC file and
its 33,444/2,432/7,172-byte text/data/bss sections. The rebuilt shell and the
retained clean-distribution shell are byte-identical with SHA-256
`b1b03becbd7debf4ebe3a1a1aec1da058297181bf77979ca3bcd207c19c28126`, so
the packed-root payload and allocation remain unchanged.

| Constrained surface | Base | Candidate | Delta | Retained margin |
| --- | ---: | ---: | ---: | ---: |
| PICO kernel text | 104,598 bytes | 104,598 bytes | 0 | 26,226 flash bytes after text and data |
| PICO kernel data | 248 bytes | 248 bytes | 0 | unchanged |
| PICO kernel bss | 39,848 bytes | 39,848 bytes | 0 | unchanged |
| Shell a.out file | 32,576 bytes | 35,908 bytes | +3,332 bytes | n/a |
| Shell text | 30,132 bytes | 33,444 bytes | +3,312 bytes | n/a |
| Shell data | 2,412 bytes | 2,432 bytes | +20 bytes | n/a |
| Shell bss | 7,152 bytes | 7,172 bytes | +20 bytes | n/a |
| Shell process-window occupancy before stack | 39,696 bytes | 43,048 bytes | +3,352 bytes | 104,408 bytes before stack |
| Packed root data blocks | 682 | 684 | +2 blocks | 295 free 1 KiB blocks, or 302,080 bytes |
| Packed root files | 95 | 95 | 0 | unchanged |

GNU `arm-none-eabi-size` reports 33,442 text bytes for the transient shell ELF.
The target `size` tool reports 33,444 text bytes for the installed OMAGIC a.out.
The process-window accounting uses the shipped a.out format and its target-tool
measurement.

The comparison records unchanged kernel section sizes rather than treating
different build-identification bytes as a kernel growth. Static text, data and
bss occupancy is a lower bound on process-window use. The retained pre-stack
margin is an upper bound because runtime peak stack and heap remain unmeasured.
The measurements therefore establish neither peak RAM use nor board behavior.

## Examined offsets

The feature-first shell measured 35,544 a.out bytes. The following refactors
reduced the candidate to 35,240 bytes while retaining every behavior witness:

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

The first final-review behavior repairs and GNU17 warning cleanup reduced that
candidate by another 72 a.out bytes and 72 text bytes. The first recovery
repair pass added 360 a.out bytes, 360 aligned text bytes and four bss bytes for
child-error routing, redirection and input unwind checkpoints, single-pass
prefix expansion snapshots, initialized allocation-failure cleanup and
nonmutating `getopts` destination validation. Defining the fork-link state
across command recovery jumps removed four target text bytes. The independent
whole-branch review then required explicit recovery eligibility, loop-state
unwind, destructive-exec exclusion, directory rejection, dynamic `set -e`
propagation and contained `getopts` publication errors. Those corrections add
332 a.out and text bytes. A subsequent review found that the two-state `set -e`
propagation reactivated errexit inside conditional compound commands.
Distinguishing configured, active and conditionally suppressed errexit adds
another 52 a.out and text bytes, producing a result 364 bytes larger than the
feature-first shell. The accompanying 64-bit host-directory contract and direct
`getdents64` record pass-through do not enter the target image. An optimized
`-fstack-usage` comparison measured the ordinary `execute()` static frame at 96
bytes in the base and 208 bytes in the candidate, a 112-byte increase. The
compiler measurement bounds one static frame; it does not measure recursion
depth, runtime peak stack or heap use.

The shell-owned host-directory read selection adds zero target bytes. The
ILP32 host object imports `sh_host_getdents`; the Cortex-M0+ target object
retains the existing `read` dependency. The selection therefore repairs the
host evidence path without offsetting or enlarging the measured target image.

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
