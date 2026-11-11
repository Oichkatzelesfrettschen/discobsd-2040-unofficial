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

Orders 2a and 2b are separate commits even when the same translation unit is
edited. Helper-level warning closure precedes a whole-file acceptance claim.
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

## Scoped implementation receipt (2026-11-11)

The six candidate units above are implemented on PR #435 after `fb4fae87`.
The deferred mechanisms remain outside this change. The calibrated lexical
scan reports zero findings in `kern_proc.c`, `kern_resource.c`, `tty_subr.c`,
`kern_time.c` (including inactive helpers), `config.h` and libc `err.c`.
`ptyattach` and the STM32 SD probe are also typed to close their callback
families. The compiler-backed kernel ledger has 130 definitions and zero
unprototyped declarations, across 18 files; the original count was 162 and
two. The source denominator includes the new err fixture and refreshed hazards.

The recovered parameter types are pointers and promoted `int` values; none
of these replacements narrows an incoming argument. Syscalls retain
`void (void)` and u-area arguments/results. The err v-forms retain the public
`va_list` type. `struct driver` has separate `d_ctlr_init(struct conf_ctlr *)`
and `d_device_init(struct conf_device *)` slots, with designated initializers
for every maintained ARM driver. Both ports dispatch through the corresponding
slot. Services use `void (void)` in the header, generator and `ptyattach`.
No callback conversion uses a cast.

Caller reconstruction used `rg` for each migrated symbol, `d_init`,
`svc_attach`, `struct driver` and `attach` under `sys`, `include`, `lib` and
`tools/config`. `proc.h`, `resource.h`, `clist.h`, `time.h`, `systm.h` and
`err.h` own the existing declarations. Process consumers include fork,
signal, ptrace and startup; accounting consumers include exit and wait in
`kern_exit.c`. Time consumers include `kern_clock.c` and inode timestamp
operations. Driver tables are generated by `mkioconf.c`; only `ptyattach`
implements an optional service in the maintained source inventory.

`ruadd` is the separate semantic repair: each `k_rusage` member is an
independent C object, so subtracting member addresses and incrementing a
pointer across them does not describe an array traversal. Explicit member
additions preserve counter order and self-addition, without that traversal.
Counter overflow policy is unchanged; this patch does not define overflowing
signed additions. `rucvt` still zeros the complete public result before
assigning fields.

### Behavioral and compiler evidence

- `check-storage-correctness` links production sources. Its resource suite
  passes 251 checks, including ancestry, colliding PID hash chains, queue
  ownership, independent per-member sums, tick boundaries and whole-result
  zeroing. The clist suite passes 340 checks, including `00/7f/80/ff`, block
  exhaustion/reuse and iteration. `unputc` retains plain-char signedness.
  Four additional mutants drop a counter, omit result clearing, skip a hash
  tail and lose the byte high bit; all are rejected by runtime checks.
  Existing time-I/O and interval-timer mutation cases also pass.
- `check-libc-err` passes 16 subprocess message/status cases and two compiler
  controls. Native C17 exercises integer/string/double `va_list` forwarding,
  null formats, saved errno reporting and returning warnings. The owning
  header rejects an incompatible v-form type; old-style syntax is rejected.
- `check-config-generated-sync` passes 26 tests, including execution of typed
  startup callbacks, rejection of swapped controller/device callback types,
  rejection of argument-taking services, and real generation of a
  `void ptyattach(void)` declaration and table entry.
- Admitted kernel compile rules require `-Wstrict-prototypes` and
  `-Wold-style-definition` in both ARM ports, preserving kern_time's RP2040
  size rule. The libc err rule and relevant host fixtures require them too.
  `check-c17-kernel-inventory` passes its two-oracle calibration and ledger.
- All ten maintained STM32 configurations compile the ten selected production
  translation units in their actual source lists, including startup, exit,
  shared helpers and enabled probe implementations. This is compiler evidence;
  it does not establish a complete STM32 link or boot.
- Native Smaller C accepts a preprocessed `err.h` probe that forwards all four
  v-forms (`arm-none-eabi-cpp -nostdinc -Iinclude -U__GNUC__
  -D__SMALLER_C__ -P`, followed by the host-built Thumb Smaller C compiler).
  This is parser/codegen evidence, separate from cross-GCC compilation.

### Matched resource evidence

Debian x86-64 host, GCC 15.3.1, GNU17, default `-Os`, `KERNEL_GC=no`.
The baseline is `fb4fae87`; both sides use the same tools and configurations.
All four kernels compile and link with the production warning policy.

| Kernel | Before text/data/BSS | After text/data/BSS | Delta text/data/BSS |
| --- | --- | --- | --- |
| PICO | 94061 / 256 / 40416 | 94117 / 264 / 40416 | +56 / +8 / 0 |
| PICO_UART | 84984 / 200 / 15408 | 85048 / 200 / 15408 | +64 / 0 / 0 |
| PICOCALC | 109885 / 376 / 53080 | 109949 / 392 / 53080 | +64 / +16 / 0 |
| PICOCALC_PSRAM | 111999 / 424 / 53232 | 112055 / 448 / 53232 | +56 / +24 / 0 |

[kr-migration-resources.json](kr-migration-resources.json) records isolated
object sizes, text hashes and GCC static-frame reports. Process, clist, time
and err text bytes match their controls exactly. Resource text grows by 60
bytes in the isolated PICO compilation; `ruadd`'s static frame falls from eight
to zero bytes. Other reported frames are unchanged. Full-link differences
also include typed driver table storage and resulting placement. No ABI,
stack alignment or 192 KiB process-window constants change.

### Remaining admission evidence

Both baseline and migrated `logged-build` reach all four linked kernels, then
fail in the native a.out assembler on `rom_float_clz.S` with `bad instruction
arguments`. This pre-existing build failure blocks a complete libc archive,
clean consumer relink, loaded a.out size and packed-root-block comparison.
The full Smaller C harness also stops at the missing target `lib/crt0.o`;
the isolated header probe above does not replace it. Picotool is unavailable,
so no UF2 was generated. No board boot, interrupt, fault-console or runtime
peak measurement was requested or executed. Those are open evidence items,
not passing admission checks. The initial development worktree retains a
logged-build quarantine for defunct lexer processes; target measurements use
an independent validation worktree and preserve the quarantine receipt.
