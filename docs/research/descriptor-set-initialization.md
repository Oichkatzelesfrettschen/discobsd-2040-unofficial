# Descriptor-set initialization and getty ownership

## Observation, rule, and prediction

The implementation base is owned main `a706c0b851daaa25b019639e8217dda5ac17095d`.
Source lookup uses `rg -n 'FD_ZERO|FD_SET|autobaud'` in `sys/sys/select.h`
and `libexec/getty/subr.c`. The user `FD_ZERO` macro calls `memset` without
declaring it. A consumer that includes only `<sys/select.h>` and invokes
`FD_ZERO` fails ARM strict C17 compilation with `-Wall -Wextra -Werror
-fno-builtin -nostdinc -Iinclude`. Getty's `autobaud` sets descriptor zero
in an automatic descriptor set whose other bits have no initialization.
Its caller in `libexec/getty/main.c` selects that path for autobaud mode.

[POSIX Issue 8 select](https://pubs.opengroup.org/onlinepubs/9799919799/functions/select.html)
defines `FD_ZERO` through `<sys/select.h>` and requires it to initialize the
null set, including when the input set contains members. The macro requires
no additional application header. Descriptor arguments outside the valid
descriptor domain and arguments with side effects have undefined behavior.

The header invariant is zero in every element of the actual `fds_bits`
array, with adjacent storage preserved. The caller invariant is the singleton
set containing stdin before the readiness call. The proposed user macro
stores zero through the array's declared element type and derives its bound
from the array itself. The kernel keeps its existing `bzero` path. Getty
initializes its set before inserting stdin. Both changes preserve layout,
descriptor operations, timeouts, I/O ordering, and byte-to-baud mapping.

The prediction precedes the production edits: standalone header consumers
change from compiler rejection to acceptance; the poisoned-stack getty caller
changes from exit 20 to exit zero. Removing caller initialization restores
exit 20. A no-op clearing macro restores the set fixture's exit 10. Incorrect
baud selection and omitted initial flush restore exit 20. A failure outside
those predictions requires investigation rather than an adjusted expectation.

The tests use `-ftrivial-auto-var-init=pattern` to supply deterministic
nonzero automatic storage. This compiler instrument exposes omitted source
initialization; ordinary production flags remain separate. Typed hooks replace
only `ioctl`, `select`, and `read` in the extracted final `autobaud` function.
No device or physical board participates. The fixture checks all 256 byte
values across readiness -1/0/1 and read results -1/0/1, including early
returns and an interrupted timeout-only call.

Strict caller compilation also exposes the inherited request conversion:
`ioctl` declares an `int` request, while `TIOCFLUSH` contains the input bit
above `INT_MAX`. The caller explicitly converts that constant to the declared
request type. The conversion preserves the existing implementation-defined
integer conversion and target bit pattern while satisfying pedantic compilation;
it changes neither the request nor its ordering. Suppressing that diagnostic
would leave the boundary implicit.

## Donor mechanism comparison

The donor development revisions are pinned independently of their checkouts:

| Source | Revision | Mechanism and local fit |
| --- | --- | --- |
| DragonFlyBSD `sys/sys/_fd_set.h` | `e0f962ddd750a94049c962632074b1b240ceb40e` | `__builtin_memset` clears its variable-sized unsigned-word set. Compiler builtin availability needs a separate Smaller C path. |
| FreeBSD `sys/sys/select.h` | `6871285f194d3f83fd270b4b5d5783503c2b86d6` | A descending word loop clears a set sized from `FD_SETSIZE`. The transferable property is dependency-free typed stores. |
| NetBSD `sys/sys/fd_set.h` | `43604aca965597330ebc7c4e39c288acc3a0431e` | A builtin path and portable loop fallback clear the allocated word array. The portable path provides a second adaptation reference. |

Source inspection uses `git show REVISION:PATH`; these BSD mechanisms share
Berkeley ancestry and supply correlated design evidence. Their different set
capacities do not establish DiscoBSD ABI compatibility. The local adaptation
uses the actual array extent rather than a donor's configurable capacity.
Including `<string.h>` through `<sys/select.h>` also makes standalone use
compile, but expands a pervasive header dependency without requiring it for
the zero-store mechanism. Compiler-specific builtin branches add a second
implementation without a target requirement. The portable loop supplies the
single user implementation, including Smaller C input.

The contribution is a constrained-system composition of established
mechanisms, rather than a new clearing algorithm. Its falsifier is a valid
consumer that fails inclusion, a changed target layout, a missing clear,
an adjacent-storage overwrite, or a caller that passes stray descriptors.

## Scope and qualification

DiscoBSD has a one-word `fd_set`, default `FD_SETSIZE=32`, and
`OPEN_MAX=30` in `sys/sys/syslimits.h`. Valid admitted descriptors are 0--29.
The fixture derives its descriptor loop from `OPEN_MAX`. Its bit-set witnesses
exercise representable indices without opening those descriptors; the typed
I/O hooks model ready stdin. They establish neither kernel descriptor admission
nor real-device readiness. A signed shift into
bit 31 does not establish a valid-domain defect on this target. Configurable
capacity, other macro interfaces, and complete `select`/`pselect` semantics
remain separate audit leaves. Getty retains both `nfds=32` arguments; reducing
them is an independent optimization without a role in initialization.

`tools/c17/fd_set_contracts.py` owns compiler, Smaller C, host, and ARM
qemu-user witnesses. `tests/libc_contracts/fd_set_test.c` checks set
initialization, idempotent insertion/removal, membership, adjacent guards,
statement-macro syntax, and the real extracted caller. Compile-time cases
cover standalone, repeated, and reordered inclusion with types, time, signal,
and string headers, four feature modes, and both time widths.

The clean matched baseline capture completes before production edits. Its
invocation receipts bind source identity, compiler executions, selected
archive members, distribution contents, and resource measurements. Candidate
resource costs require a matched capture; positive deltas require the profile's
exact reviewer approval. Existing byte-search approvals cover their recorded
costs only. Host, cross, qemu-user, Renode, and board evidence remain distinct;
physical-board execution stays unrun.

Candidate `15113728` passes 240 valid header compilations, six compiler
rejection controls, 80 Smaller C inputs, and 2,304 caller combinations on host,
ILP32 host, and Cortex-M0 qemu-user, with four runtime defect controls in each
runtime profile. Existing getty contracts, Clang analyzer, repository lint,
source accounting, and donor/profile calibration pass. The clean aggregate,
required 216-variant inventory, hosted CI, UART Renode, and union-kernel Renode
also pass. The [qualification receipt](descriptor-set-qualification.json)
binds their source/artifact identities and preserves the inventory retention
boundary. The [resource exception](descriptor-set-resource-exception.md)
records one positive shipped packed-byte delta and two positive optional
standalone deltas; their exact approval and the bounded source review remain
open. These witnesses retain partial standards claims.
