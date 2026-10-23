# RP2040 file-owner control admission

## Contract

The file-owner control seam accepts a positive process identifier or the
public negative process-group convention through `F_SETOWN` and
`FIOSETOWN`.  `F_GETOWN` and `FIOGETOWN` publish a negative process-group
value only after the driver returns a valid group.  The change preserves
syscall 54 (`ioctl`), syscall 92 (`fcntl`), the command constants, public
layouts, descriptor and access checks, positive-PID lookup through `pfind()`,
and the established driver error.

The inherited `fsetown()` negated every nonpositive `int`, including
`INT_MIN`, before validating the process-group magnitude.  The inherited
`fgetown()` passed its caller's destination to `TIOCGPGRP`, then negated and
published the destination even when the driver returned an error.  The two
file-owner ioctl branches also dereferenced the caller's `int *` directly.

The repaired seam applies these rules:

- A nonpositive set-owner value must be at least `-SHRT_MAX` before negation.
- A positive process identifier must resolve through `pfind()`, and the
  selected process group must satisfy `PGRP_VALID` before driver dispatch.
- `TIOCGPGRP` writes into initialized kernel storage.  A driver error or an
  invalid returned group leaves the caller's destination unchanged.
- `FIOSETOWN` copies one complete `int` into kernel storage before conversion
  or dispatch.  `FIOGETOWN` copies one complete successful result back after
  validation.
- `fcntl`, `fioctl`, `fgetown`, `fsetown`, and `ioctl` use typed definitions.

PICO and PICO_UART omit `INET`.  The conditional socket-owner branches remain
source-preserved and unexecuted by the shipped configurations.  General ioctl
payload staging, `FIONBIO`, `FIOASYNC`, and `F_SETFL` rollback remain separate
migration units.

## Calibrated behavior

`tests/kernel/file_owner_test.c` compiles the production definitions from
`kern_descrip.c` and `sys_generic.c` against bounded host shims.  Its 36
checks cover the minimum admitted negative group, zero, an existing and a
missing positive PID, an invalid group from `pfind()`, `INT_MIN`, driver
errors, successful and malformed get-owner output, invalid descriptors,
missing access modes, injected copy failures, unaligned accessible payloads,
and complete-span refusal.  Every refusal pins the applicable driver-call,
copy, return-value, and caller-output state.

The native fixture, its address/undefined-behavior/alignment sanitizer form,
and the ILP32 form each pass all 36 checks.  Three independent mutations
calibrate the gate:

1. `unchecked-negation` restores signed negation without the magnitude check;
   an admitted undefined-behavior sanitizer runtime must terminate it with
   status 1 and the signed-negation diagnostic.  The mutation gate records an
   explicit skip when that sanitizer runtime is unavailable.
2. `unconditional-publication` restores publication after driver failure and
   before returned-group validation; the native fixture must fail its named
   assertion with status 1.
3. `direct-user-pointer` restores direct dereferences in the two ioctl cases;
   the native fixture must fail its named assertion with status 1.

The ILP32 tier runs the positive fixture and does not claim sanitizer-dependent
mutation execution.

The fixture keeps each kernel source in function and data sections because the
host link admits only the caller-complete file-owner unit.  A calibrated
linker-selection check requires Apple ld's `-dead_strip` spelling on Darwin
and GNU ld's `--gc-sections` spelling on ELF hosts; the deliberately swapped
Darwin spelling must fail the check.

The registered kernel aggregate reaches 1,618 assertions after adding the
file-owner fixture.  The execution inventory records native sanitizer and
ILP32 variants separately.  Exact GNU17 Cortex-M0+ PICO and PICO_UART builds
compile and link both translation units with warnings as errors.  The
maintained STM32 forms also compile under GNU17 with `-Wall -Wextra -Werror`.

## Static resource acceptance

Clean schema-2 distributions compare parent
`a740bc40594eb8447733b028a49f7aeecb39a5a1` with implementation commit
`290fd39474e6dd325c83f3eebf8fbd6aa40f90e5`.  Both captures used bmake
20260912, arm-none-eabi-gcc 16.2.0, GNU ld 2.47.20260726, and Python 3.14.8.
The closures account for 173 PICO C/header paths, 165 PICO_UART paths, 36
shipped artifact groups, every installed a.out, the native startup object and
libc archive, generated inputs, and the packed root.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO flash bytes | 104,958 | 105,054 | +96 | 26,018 bytes below 128 KiB |
| PICO `.bss` / SwapRAM pool / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 | unchanged |
| PICO_UART flash / `.bss` bytes | 92,484 / 14,832 | 92,580 / 14,832 | +96 / 0 | 38,492 flash bytes below 128 KiB |
| Native `libc.a` bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 | unchanged |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 | unchanged |
| Packed-root free inodes | 33 | 33 | 0 | unchanged |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Every selected archive/member set | measured baseline | unchanged | 0 | unchanged |

Exact recorded compiler commands replayed with `-fstack-usage` attribute the
linked delta to the two changed objects in each configuration.
`kern_descrip.o` text grows from 1,638 to 1,686 bytes: `fgetown()` grows 28
bytes, `fsetown()` grows 20 bytes, and data and BSS remain unchanged.
`sys_generic.o` text grows from 2,024 to 2,072 bytes, all in `ioctl()`, with
unchanged data and BSS.  The `fgetown()` static frame grows from 8 to 16 bytes,
and the `ioctl()` static frame grows from 16 to 24 bytes.  The measured
`fioctl()`, `fsetown()`, and `fcntl()` frames remain 8, 16, and 16 bytes.
Per-function compiler frames do not establish a complete call-chain or
runtime high-water mark.

The resource comparer rejects both 96-byte linked flash deltas under the
default nonpositive-growth rule.  The bounded exception accepts the linked
flash and static-frame deltas because the added code closes reproduced signed
overflow, user-pointer, and failure-publication defects across all four
owner-control entries.  The change adds zero static SRAM allocation,
process-window, packed-root, native-library, or archive-selection
consumption.  Runtime kernel stack high-water remains open.

| Receipt | SHA-256 |
| --- | --- |
| Parent source snapshot | `f7f855639eefaed7050f590d3660a55098afb9171c642cb835b6564f65bcac27` |
| Parent dependency closure | `8706238d6bb7ce04856ff28258bf9ffd762098f93d38347513d9f68a3f48d055` |
| Parent schema-2 resources | `3143a32b0079ab069e69af77b1ba128a50dbc00ae5c69db63b245bd96171a6cf` |
| Candidate source snapshot | `c825f41f81275a08b7f5479d2754a9ad437761015a2d05c1ace94bb749b5983f` |
| Candidate dependency closure | `c55eab15ec6c22ba07769beffebd8faa563b794cfa80107ec25fd2dad8bc9fb7` |
| Candidate schema-2 resources | `e190ec83e63282737bd7ed2cfb7119905209c5e1e75967da4bb07efef834cb5a` |
| Static-resource delta | `c6ad2d0ebf116c29cd17d40ff41a66ec9f7be35c7d12026a76b691fad7f333f2` |
| Parent exact-command object/frame receipt | `6701750a12f654171c564d8add70c172b31ac849d9fc96bad6739400eeaf47df` |
| Candidate exact-command object/frame receipt | `a5f9fa5261903924ab335b3cd29bdaa00a4657bb24fe38c2ad6cd8e6814f26c9` |
| Object/frame comparison | `d25999766bf8afaa576813584adbef8854837702fa77ad09b8a1edf009eea003` |

## Evidence states and residuals

The host fixtures establish the exercised conversion, complete-span copying,
failure preservation, and publication contracts.  Exact target builds
establish compilation, linkage, static placement, and selected dependency
closure.  The evidence does not execute a file-owner transition on RP2040
silicon, measure runtime stack or heap high-water, or exercise storage
traffic.  File-owner controls do not write persistent storage, so the unit
makes no flash-wear claim.

The complete ioctl payload path still applies first/last-byte checks before
passing other caller-selected payloads directly to drivers.  `/dev/mem` and
`ptrace` retain separate direct-address boundaries; the latter first needs an
ARM writable-register and program-status-register policy.  `FIONBIO`,
`FIOASYNC`, `F_SETFL`, descriptor duplication and locking, conditional INET
owner handling, broad kernel declarations, runtime resources, MPU behavior,
and physical-board execution retain separate acceptance states.
