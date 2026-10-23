# RP2040 file-status control admission

## Contract

The file-status control seam changes nonblocking and asynchronous driver state
through `F_SETFL`, `FIONBIO`, and `FIOASYNC`. The change preserves syscall 54
(`ioctl`), syscall 92 (`fcntl`), command values, public layouts, descriptor and
access checks, the `FAPPEND` and `FFSYNC` flag meanings, and driver errors.

The inherited `fset()` published `FNONBLOCK` or `FASYNC` before the driver
accepted the transition. The inherited `F_SETFL` path published all requested
flags before either driver call and cleared nonblocking state unconditionally
when asynchronous setup failed. The direct ioctl paths also dereferenced the
caller's `int *` payload.

The repaired seam applies these rules:

- `fset()` calls the driver before publishing its one status bit. A rejected
  transition preserves the original in-kernel flag state.
- `F_SETFL` stages the requested flags. A rejected nonblocking transition
  preserves every original flag and suppresses asynchronous dispatch.
- A rejected asynchronous transition asks the driver to restore the original
  nonblocking state. Successful compensation restores the matching in-kernel
  bit. Failed compensation leaves the successfully established nonblocking
  state visible while preserving the original asynchronous error.
- `FAPPEND` and `FFSYNC` publish only after both driver transitions succeed.
- Direct `FIONBIO` and `FIOASYNC` copy one complete `int` into kernel storage
  before driver dispatch.

The compensation path cannot make two independent driver operations atomic.
The visible flag after a failed compensation records the driver transition
that succeeded. General ioctl payloads and conditional INET file controls
retain separate migration units.

## Calibrated behavior

`tests/kernel/file_owner_test.c` compiles the production definitions from
`kern_descrip.c` and `sys_generic.c` against bounded host shims. Its 72 checks
retain the 36 file-owner assertions and add 36 file-status assertions. The new
cases cover helper success and failure, nonblocking clear failure, asynchronous
setup, both-driver success, first-driver refusal, second-driver refusal,
successful compensation, failed compensation, restoration of an originally
set nonblocking state, unaligned direct-ioctl payloads, injected copy failure,
complete-span refusal, and driver failure after successful copying. Every
refusal pins driver-call history, payloads, observed flags, caller copying,
error precedence, and final file flags.

The native fixture, its address/undefined-behavior/alignment sanitizer form,
and the ILP32 form each pass all 72 checks. Four warning-clean mutations
calibrate the deciding gate:

1. `early-fset-publication` restores helper publication before driver
   acceptance.
2. `early-setfl-publication` restores complete `F_SETFL` publication before
   its driver calls.
3. `zero-setfl-rollback` always clears nonblocking state instead of restoring
   the original requested state.
4. `direct-status-pointer` restores direct caller-pointer dereferences in the
   two ioctl cases.

Each mutation must fail its named assertion with status 1. Exact GNU17
Cortex-M0+ PICO and PICO_UART builds compile and link both translation units
with warnings as errors.

## Static resource acceptance

Clean schema-2 distributions compare parent
`c6f64b2b0b99c4b3f63a0e9db1c630510133f609` with implementation commit
`88a9c1c0f75635f0e3d3e9d8ed60490b1528505d`. Both captures used bmake
20260912, arm-none-eabi-gcc 16.2.0, GNU ld 2.47.20260726, and Python 3.14.8.
The closures account for 173 PICO C/header paths, 165 PICO_UART paths, 36
shipped artifact groups, every installed a.out, the native startup object and
libc archive, generated inputs, and the packed root.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO flash bytes | 105,054 | 105,078 | +24 | 25,994 bytes below 128 KiB |
| PICO `.bss` / SwapRAM pool / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 | unchanged |
| PICO_UART flash / `.bss` bytes | 92,580 / 14,832 | 92,604 / 14,832 | +24 / 0 | 38,468 flash bytes below 128 KiB |
| Native `libc.a` bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 | unchanged |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 | unchanged |
| Packed-root free inodes | 33 | 33 | 0 | unchanged |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Every selected archive/member set | measured baseline | unchanged | 0 | unchanged |

Exact recorded compiler commands replayed with `-fstack-usage` attribute the
linked delta to the two changed objects in each configuration.
`kern_descrip.o` text grows from 1,686 to 1,698 bytes, all in `fset()`;
`sys_generic.o` text grows from 2,072 to 2,084 bytes, all in `ioctl()`.
Object data and BSS remain unchanged. The `fset()` and `fcntl()` static frames
each grow from 16 to 24 bytes. The measured `ioctl()` frame remains 24 bytes.
Per-function compiler frames do not establish a complete call-chain or runtime
high-water mark.

The resource comparer rejects both 24-byte linked flash deltas under the
default nonpositive-growth rule. The bounded exception accepts the linked
flash and static-frame deltas because the added state closes reproduced early
publication, wrong-state compensation, and caller-pointer defects across all
three status-control entries. The change adds zero static SRAM allocation,
process-window, packed-root, native-library, or archive-selection consumption.
Runtime kernel stack high-water remains open.

| Receipt | SHA-256 |
| --- | --- |
| Parent source snapshot | `03b3faa9765a2f3109f9fe3539ed16db7a9ff8c75e02fa76d3d08c29d12db712` |
| Parent dependency closure | `4cca8b6fdab0fae158b72371909e5b7768e0d72815731716c5bd9429d3679e84` |
| Parent schema-2 resources | `9b4876d10de7c4cdb5ef78ba81de2650882a784d7965e3254b897d62b58e94e8` |
| Candidate source snapshot | `76935ded066eb7db86875089cbfcc8ff5f109e2e7178f8bc0224a660ad8839b2` |
| Candidate dependency closure | `1ebf7f34db6968c70aa28b9b36159481a0c57160847a911cb03c2d344fd2254d` |
| Candidate schema-2 resources | `44fbb5d9002705f609f346836978bd82841818c0c4a850bd2c24e3eefd8657fe` |
| Static-resource delta | `3dc567d06793b7ab6434c174bbce3aab6712aecb78d681f78f4bc98c50650405` |
| Parent exact-command object/frame receipt | `c907c5693ed30399bba07529313fb423ec1ada8223cb3afb78a17472873c804f` |
| Candidate exact-command object/frame receipt | `e0c13d0990eff66fd6e7d2256590e8edee1a383c15f2b3d69e590b39a3c7fad8` |
| Object/frame comparison | `6cc6ee826b43b743c1a126c53a29d9caf60f31fdac5349c65f9d699c2a920a0c` |

## Evidence states and residuals

The host fixtures establish the exercised complete-span copying, driver-first
publication, compensation, error precedence, and failure-preservation
contracts. Exact target builds establish compilation, linkage, static
placement, and selected dependency closure. The evidence does not execute a
file-status transition on RP2040 silicon, measure runtime stack or heap
high-water, or exercise storage traffic. File-status controls do not write
persistent storage, so the unit makes no flash-wear claim.

The remaining general ioctl payloads, `/dev/mem`, and `ptrace` retain separate
direct-address boundaries. `ptrace` first needs an ARM writable-register and
program-status-register policy. Descriptor duplication and locking,
conditional INET controls, broad kernel declarations, runtime resources, MPU
behavior, and physical-board execution retain separate acceptance states.
