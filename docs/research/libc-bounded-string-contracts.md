# Bounded strings and end-pointer copy contracts

The libc sources provide `strnlen`, `strndup`, `stpcpy`, and `stpncpy` through
separate archive members. The public declarations select POSIX 2008 or later,
the corresponding XSI level, or the default BSD namespace. These interfaces
extend the POSIX profile; ISO C17 alone does not require them. The definitions
compile as strict C17 and production GNU17. Qualification remains open until
the matched resource comparison and aggregate gates complete.

## Requirements and mechanism

The Issue 7 and Issue 8 function pages define the same essential byte-string
contracts. The Issue 8 authorities are
[strnlen](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strnlen.html),
[strndup](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strndup.html),
[stpcpy](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stpcpy.html), and
[stpncpy](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stpncpy.html).
The ledger keeps all four Base rows partial while integration evidence remains
open. `dpucp-2024-donor-ledger.json` owns the donor revision, file hashes and
license identities.

| Interface | State invariant and observable result | Cost and failure |
| --- | --- | --- |
| `strnlen(s,n)` | The index counts nonzero bytes already read, stays at most `n`, and advances only after the extent check. The result is the first NUL index or `n`. | At most `n` reads, constant local state, no allocation, unchanged errno. |
| `strndup(s,n)` | The bounded scan produces `length`; admission proves `length + 1` representable before allocation. Copying publishes a private, terminated result only after successful allocation. | One allocation of `length + 1`; failure returns NULL and ENOMEM. The caller owns `free`. |
| `stpcpy(d,s)` | Both cursors advance together through nonzero bytes. The loop writes the terminator and returns its destination address. | Linear in source length, constant local state, unchanged errno. |
| `stpncpy(d,s,n)` | The remaining count bounds every write. An early NUL freezes the return pointer while a separate cursor pads the remaining destination. A full nonzero span returns `d+n`. | Exactly `n` writes, at most `n` source reads, constant local state, unchanged errno. |

The copy contracts require valid, sufficiently sized, nonoverlapping objects.
The routines own no shared mutable state. `strndup` inherits the allocator's
lifetime and concurrency contract; the fixture does not establish allocator
signal safety or whole-process concurrency.

The zero-bound case performs no source read for the bounded operations.
`strndup` still allocates one byte for its terminating NUL. A large bound does
not force a large allocation when an early NUL ends the input. The synthetic
overflow fixture replaces the length provider with `SIZE_MAX`; this exercises
the arithmetic guard independently of whether such an object fits on RP2040.

## Source comparison and synthesis

The source search uses `rg --files` for candidate names and `git show` at each
retained donor revision. DragonFlyBSD revision
`805ca132aededa6ae4c8b3499a2d5c934d78fe56` supplies the four
`lib/libc/string/*.c` definitions and their unchanged notices. The definitions
retain their FreeBSD lineage; related BSD copies count as correlated evidence.

| Source | Useful mechanism | Target decision and falsifier |
| --- | --- | --- |
| DragonFlyBSD four string sources | Bytewise bounded loops; a separate padding return pointer; `strndup` reuses `strnlen`. | Adapt the definitions, use C17 `restrict`, and guard the allocation addition. Protected pages, return-pointer assertions and overflow injection decide correctness. |
| DiscoBSD `lib/libc/gen/strncpy.c` and `strdup.c` | Existing byte-padding behavior and allocator ownership. | Preserve their callers. A wrapper around `strncpy` still needs a scan to recover the returned end pointer; historical `strdup` uses an `int` length and an unbounded scan, so it supplies neither bounded admission nor a length representation for this interface. |
| Coherent `mwc/romana/relic/b/lib/libc/string/strncpy.c` and `strdup.c`, revision `a00b86ea22c66625cf1838a71c06129f5eda2b49` | Copy-then-pad decomposition and one-allocation duplication. | Use source observation only. The historical declarations and unbounded duplication do not supply the target signatures or extent contract. No Coherent code enters these files. |

The port contribution preserves separate scan, cursor and ownership invariants
under target-width admission and archive selection. The algorithms have prior
art. End-pointer returns let future callers append without rescanning the
prefix; no caller speedup enters the evidence until a measured consumer uses
that property. Separate objects let a static executable select only its used
interfaces, while the installed native archive carries all four members.

## Executable witnesses

`tests/libc_contracts/bounded_string_test.c` checks 91,035 length/bound/byte
combinations, exact output and padding, sentinel preservation, end pointers,
allocation extent, failed allocation and large bounds. Host protected pages
place source and destination ends against inaccessible memory.
`bounded_string_overflow_test.c` checks rejection before allocation.
`bounded_string.py` requires seven mutated definitions to compile and then fail
the behavioral assertion: scan extent, duplicate terminator, allocation extent,
both returned pointers, padding byte and allocation overflow admission.

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-libc-bounded-string check-string-header-contracts
"${PYTHON}" tests/libc_contracts/bounded_string.py . clang
"${PYTHON}" tests/libc_contracts/bounded_string.py . arm-none-eabi-gcc --arm
```

GCC native and ILP32, Clang native, AddressSanitizer/UndefinedBehaviorSanitizer,
and Cortex-M0+ QEMU executions pass the matrix and seven controls. Host lanes
also pass the protected-page cases. QEMU uses newlib support for the fixture;
it establishes execution of the renamed production functions, separately from
the target allocator and kernel. The native-link fixture links all four names
through both a.out archives. The header gate separately exercises exact types,
visibility, repeated/reordered inclusion and expanded Smaller C input.

The root host tier and required ILP32 inventory own the registered native,
ILP32 and sanitizer variants. The QEMU tier owns Thumb-1 execution. Firmware CI
runs those owners. Hardware execution remains unrun and requires authorization.

## Resource and integration boundary

The board-native archive selects its members independently through
`distrib/rp2040/boardlibc-members` and `mkboardlibc.py`. Both selectors include
the four functions, the board recipe tracks their source/header dependencies,
and `libc-sink.c` references them. The link gate removes each string member
from copies of both archives and requires a failed link naming that symbol.
The full ELF and a.out archive Makefiles alone do not establish availability
in the shipped board library.

The first candidate at `f302b5ef` has a zero resource delta because its reduced
board archive omits the four new members. The native-link check rejects that
candidate with `expected one strnlen.o member`. That comparison is a packaging
negative control and supplies no acceptance for the complete capability.

The clean parent distribution and full dependency capture precede the source
change. The candidate requires the same collector and toolchain, a clean
distribution, complete dependency attribution and per-surface comparison.
Packed bytes, allocated blocks, native archive storage, kernel resources and
process occupancy retain separate accounts. A measured positive delta requires
the profile's concrete exception review. Static local state and the exact
allocation formula do not establish whole-workload stack or heap peaks.
