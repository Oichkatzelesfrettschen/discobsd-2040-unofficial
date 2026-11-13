# Independent quick-exit callback ownership

C17 7.22.4.3 and 7.22.4.7 define an independent callback registry, reverse
registration order and termination through `_Exit`. POSIX Issue 8 preserves
those rules. A successful registration during termination joins the remaining
callbacks; the implementation may instead report registration failure.
The [donor lifecycle witnesses](coherent-boundary-lifecycle-witnesses.md)
retain a single-thread counterexample to snapshot traversal in DragonFlyBSD.

## Mechanism and falsification

The candidate uses a private live-head list. Termination removes its head
before invoking that callback, so successful nested registrations precede
the remaining older callbacks. Registration initializes a node before
publishing it. Allocation failure leaves every pending registration intact.
An empty registry uses one reusable static node; additional pending entries
use the existing allocator. Registration and termination occupy separate
archive members. The termination member owns only the list head and calls
`_Exit`; the registration member owns static storage and the allocator
dependency. Normal exit keeps its independent registry and cleanup path.

The [allocator accounting](callback-allocation-accounting.md) distinguishes
twelve-byte ILP32 dynamic blocks from cold arena growth. A fixed 32-slot
array has predictable admission but reserves its full storage for a caller
that registers one handler. The candidate preserves the existing libc's
per-registration allocation policy, supports at least 32 registrations with
available storage, and reports exhaustion. Nodes survive until process
teardown. The first and last-handler-only registrations reuse static storage;
mixed nested registrations can consume additional heap until termination.
This policy supplies no bound on user callbacks or their allocations.

The falsifier is any lost, duplicated or misordered callback, change to a
registry after failed registration, normal/quick registry interference,
stream cleanup during quick exit, or changed termination status. The host
witness runs twelve cases through renamed production registration, normal
exit, quick exit and `_Exit` sources. Allocation and kernel termination are
modeled boundaries; callback traversal and registry ownership execute the
production code. Saved traversal, cleanup substitution, discarded status
and false allocation success controls must fail by their named behavioral
oracle. GCC/Clang strict C17, ILP32, target archive selection and native
compiler declarations require separate results before qualification closes.

## Dependency closure

Source discovery uses `rg -n 'atexit.o|_Exit.o'` over the library Makefiles
and `rg -n 'quick_exit|at_quick_exit'` over public headers and libc sources.
The reduced native archive omits `_Exit` at the parent revision
`6894810f9be79a5834657802702971661b7cc9c6`. Adding quick-exit members therefore
also requires that backend in the reduced archive. The full cross library,
converted archive, reduced native archive, public declarations and execution
inventory form one caller closure. Native linker removal controls must reject
each missing member. Static resource growth remains subject to a new matched
capture and the profile exception process; earlier exit and string exceptions
cover their own source identities only.

The maintained process model is single-threaded. Signal-handler registration,
threaded registration, recursive termination, callback `longjmp`, shared XIP
state ownership and physical-board execution remain outside these witnesses.
No source-reading or host result closes those separate obligations.

## Qualification evidence

GCC native and ILP32, Clang, and ARMv6-M QEMU each pass twelve modeled
cases and the process-termination fixture. Six compiled controls reject:
five registry/cleanup/status mutations and replacement of `_Exit`'s backend
with normal `exit`. The process fixture writes `Q` directly from the quick
handler, retains exit status 7 and leaves buffered stdio unflushed. The
normal-exit control reaches the separately registered normal handler and
returns status 99 instead. QEMU uses the existing newlib/Linux-EABI support;
production objects use DiscoBSD headers, while fixtures use newlib headers.
The initial newlib-header build collides with its internal `__atexit`
declaration and supplies no behavior evidence. Header separation removes
that host-library namespace collision without changing production sources.

The complete GNU17 target build passes. The cross gate passes strict C17
compilation, exact and repeated declarations, a rejected incompatible
signature, Smaller C input/assembly, and a rejected malformed native input.
A termination-only ELF consumer contains 52 text bytes, zero data bytes and
four BSS bytes. Symbol inspection selects `quick_exit`, `_Exit` and `_exit`,
with neither allocation nor normal cleanup. The target objects contain:

| Object | Text bytes | Data bytes | BSS bytes |
| --- | ---: | ---: | ---: |
| `at_quick_exit.o` | 52 | 0 | 8 |
| `quick_exit.o` | 32 | 0 | 4 |
| `_Exit.o` | 6 | 0 | 0 |

The a.out gate links the full and reduced native archives and rejects each
individually removed lifecycle member, giving six missing-member controls.
These object and link measurements precede the matched whole-distribution
resource comparison. They establish no heap peak or shared-XIP reservation.
Clang-tidy core and Unix analyzer checks report no diagnostics with the
ARMv6-M target and DiscoBSD headers. An invocation with no enabled checks
reports an instrument error; only the explicitly enabled invocation supplies
analysis evidence.

Replay the focused checks after the required logged build:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-libc-quick-exit check-libc-quick-exit-cross
bmake MACHINE=rp2040 check-libc-aout-contracts
bmake -C tests/libc_contracts MACHINE=rp2040 REQUIRE_QEMU=1 check-qemu
clang-tidy -quiet -checks='-*,clang-analyzer-core.*,clang-analyzer-unix.*' \
    -warnings-as-errors='*' lib/libc/stdlib/at_quick_exit.c \
    lib/libc/stdlib/quick_exit.c -- -target arm-none-eabi \
    -mcpu=cortex-m0plus -mthumb -std=c17 -nostdinc -Iinclude
```

The root wrapper delegates compiler selection to `tests/libc_contracts`, whose
`ARM_COMPILER` derives from the selected target toolchain. The first wrapper
uses an unset root-level `GCCPREFIX` and fails before compilation; that failed
invocation establishes no target execution. The corrected root invocation
passes the same focused gate. The [integrated resource review](libc-lifecycle-integrated-resource-exception.md)
owns the matched clean measurements, exact owner approval and complete
193-variant ILP32 result and completed combined aggregate qualification.
Hosted CI and reviewed integration remain open.
