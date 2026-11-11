# Granular K&R interface migration to C17

## Inventory boundary

The lexical scan at `f8183b869479` reports 162 old-style definitions across
23 `sys/kern` files, two unprototyped declarations in one `sys/sys` header,
and 293 definitions plus 33 declarations across 154 libc files. The libc
inventory includes optional and inactive source. It does not establish
shipped membership, compiler acceptance or a count of independent defects.

Reproduce the lexical inventory with `tools/c17/kr_scan.py` over Git-tracked
`sys/kern/*.c`, `sys/sys/*.h` and libc C files. `check-c17-kernel-inventory`
combines the calibrated lexical scan with target compiler diagnostics for
PICO and PICO_UART. Compiler dependencies and final archive selection decide
which libc candidates belong to the shipped closure. PicoCalc and maintained
STM32 callers remain part of a shared-interface migration's caller inventory.

The program order remains in `../c17-semantic-migration-plan.md`. The units
below refine its interface and remaining-source rows after their prerequisite
closure and resource evidence exists.

## Language and ABI contracts

K&R identifier-list definitions and empty-list declarations are distinct.
C17 permits the old definition form as obsolescent syntax. `int f();` declares
unspecified parameters; `int f(void);` declares no parameters. A zero-argument
definition does not justify changing every declaration without finding its
callers and address-taking uses.

Unprototyped calls apply default argument promotions: narrow integers promote
to `int` or `unsigned int`, and `float` promotes to `double`. A prototype can
change conversion and machine argument placement. Recover both the declared
parameter type and the promoted incoming type before choosing the replacement.
Floating, variadic and function-pointer boundaries require explicit ABI checks.

Calling a function through an incompatible function-pointer type remains
undefined after a cast. Recover the actual callback protocol and use an exact
type or a typed adapter. Preserve syscall handlers as `void handler(void)`;
their arguments and results still pass through the u-area. Changing that
protocol is a separate mechanism.

## Implemented interface tranche

The definitions in `kern_proc.c` (3), `kern_resource.c` (8), `tty_subr.c` (9)
and `kern_time.c` (11, including inactive helpers) use typed parameter lists.
Owning public types remain unchanged. The compiler-backed ledger contains
131 remaining definitions and two declarations across 20 files. The four
units compile with strict-prototype and old-style-definition warnings made
fatal in every RP2040 configuration; STM32F407 compilation also passes.

Matched GCC 15.2.1 compiles against `fb4fae87` emit byte-identical executable
sections for all four units in all five configurations. Host resource/clist,
credential, time-I/O and interval-timer fixtures retain their behavior and
mutation checks. The capacity bounds regression executes through emulated
ILP32 userspace with its invalid-kind controls; that is not board evidence.

`libc/gen/err.c` includes its owning header and converts its six old-style
definitions. `check-libc-err` executes 32 subprocess cases for all eight APIs,
null formats, integer/string/double forwarding, prefixes, saved errno,
warning returns and exit status; three output/status mutations calibrate it.
The fixture uses the repository header and production definitions with host
stdio, not the target's formatter.

`ruadd` retains its cross-member pointer loop. Its object-bounds repair is
not established by the interface conversion. The formatter, heterogeneous
driver callbacks, service-generator mismatch, scheduler/context families and
optional libc closure remain separate semantic units.

## Candidate units

| Order | Unit and observed findings | Recovered contract and closure | Acceptance |
| --- | --- | --- | --- |
| 1 | `kern_proc.c`: three definitions, `inferior`, `pfind`, `pqinit` | `proc.h` already declares pointer ancestry, integer PID lookup and no-argument queue initialization; trace process, signal, ptrace, fork and startup callers | `resource_test` plus direct ancestry/hash/queue cases; exact header types; four RP2040 builds; maintained STM32 compilation; ABI and resource comparison |
| 2a | `kern_resource.c`: five syscall entries and private `donice` | Keep u-area dispatch; preserve ownership checks, priority clamping, copy failures, CPU-limit tick conversion and errno precedence | Existing `resource_test`, targeted refusal-state controls, warning-clean target builds and matched resource receipts |
| 2b | `ruadd`, `rucvt`: two definitions in the same file | `resource.h` already owns exact pointer types; `kern_exit.c` also calls both. `ruadd` traverses structure members using pointer arithmetic, which needs a separate C object-bounds analysis | Per-field accounting oracle, zeroed public-result padding, tick boundary/overflow cases and exit/wait caller coverage; record any semantic repair separately |
| 3 | `tty_subr.c`: nine definitions | Recover clist ownership, byte signedness, empty/full returns and queue iteration. Convert definitions against existing declarations and all driver callers | `clist_test`, boundary bytes `0x00/0x7f/0x80/0xff`, exhaustion/reuse and pointer-type controls; preserve block layout and interrupt assumptions |
| 4 | `kern_time.c`: eleven definitions | Split pure timeval/timer helpers from user-copy/privilege syscalls; trace `kern_clock.c`, signal delivery and timer callers | Extend time-I/O and interval-timer fixtures through `check-storage-correctness`; time normalization, borrow/carry, timer expiry and copy failures; separate `adjtime` arithmetic optimization |
| 5 | `libc/gen/err.c`: six old-style definitions | Two private no-argument helpers and four `va_list` entries; `include/err.h` already declares public prototypes | New subprocess stderr/status/errno fixture, null formats, integer/string/float forwarding, returning warning paths, strict C17 and native-header checks; clean archive/consumer rebuild |
| 6 | `config.h`: `svc_attach` and `d_init` declarations | Services are called without arguments in `init_main.c`; driver initialization receives either a controller or device pointer in `machdep.c`. Inventory generator output, every initializer and both ARM ports | Treat service typing separately; resolve the heterogeneous driver slot through an explicit typed design. Reject incompatible callbacks without casts; verify generated tables and startup behavior |

Arithmetic or object-bounds changes in order 2b remain separate from the
interface-only conversion. Helper-level warning closure precedes a
whole-file interface acceptance claim.
`resource_test` currently links both resource and process translation units,
which provides a useful joint consumer without making their migrations one
indivisible change.

`kern_subr.c` is not a new conversion candidate: its four definitions are
already typed. The large lexical findings in libc networking do not establish
priority over shipped RP2040 interfaces. A current closure capture decides
their membership before migration work starts.

## Reconstruction procedure

1. Record definitions, declarations, direct calls, address-taking sites,
   callbacks, generator inputs, assembly references and conditional branches.
   Annotate argument promotions, return values, side effects and error order.
2. Trace representative success, refusal and interruption paths through the
   production implementation. Record ownership and state publication before
   selecting C17 syntax or a new header boundary.
3. Establish an independent behavioral oracle. Compare against the old
   implementation only for defined behavior; use explicit expected outcomes
   for malformed inputs and suspected undefined behavior.
4. Convert one caller-complete interface family. Update its owning declaration,
   definition and callbacks together. Keep arithmetic changes, `restrict`,
   `const` propagation, `noreturn` and data-layout changes independently reviewed.
5. Require `-Wold-style-definition -Wstrict-prototypes` for the admitted scope,
   alongside the existing warning policy. Use strict C17 with pedantic errors
   for portable mechanisms; retain GNU17 for hardware and assembly owners.
6. Reject a reintroduced old-style definition and an incompatible callback or
   declaration fixture. Keep runtime malformed-input controls separate from
   compiler rejection controls.
7. Rebuild actual consumers and compare text/data/BSS, static frames, archive
   members, a.out loaded bytes and packed-root blocks. Retain 8-byte ABI/stack
   alignment and the fixed process window. Runtime peaks require execution.
8. Refresh the source denominator after C/header edits. Regenerate the kernel
   ledger through its compiler-backed owner when its interface set changes.
   Record native Smaller C header acceptance separately from cross-GCC C17.

## Deferred mechanisms

The kernel formatter in `subr_prf.c` walks arguments as four-byte words and
has an ILP32 calling-contract gate. Converting its declaration or parameter
list alone does not establish portable variadic behavior. A `stdarg` rewrite
needs its own ARM argument, format, stack and fault-console evidence.

Scheduler sleep/context functions, signal transitions, filesystem operations
and device-switch callback families remain separate units. Their calling
contracts cross assembly, nonlocal jumps, user memory or storage state. A
prototype cannot establish those contracts by itself.

No size saving follows from C17 syntax alone. Admission requires zero remaining
interface findings in the named scope, calibrated compiler/behavior checks,
preserved ABI and explicit resource deltas. Missing ILP32 headers, emulators
or board captures leave the corresponding execution evidence open.
