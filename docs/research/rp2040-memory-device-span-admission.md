# RP2040 memory-device span admission

Date: 2026-10-23
Baseline revision: `ead253a177d911b136d8da311d909477f3dcdd13`
Implementation revision: `ae668f6700e97668647f1a522811bf06ee10504b`

## Finding and contract

`mmrw()` formerly checked only the first and last byte of a `/dev/mem` or
`/dev/kmem` transfer. A request could therefore start in kernel flash, cross an
unowned address interval, and end in kernel RAM while both tested endpoints
remained accepted. The expression `addr + iov_len - 1` could also wrap before
the endpoint test. Both cases reached `uiomove()`.

The memory-device contract admits the complete transfer before publication:

- a kernel span must fit without wrap inside one kernel RAM or kernel flash
  interval;
- a user span must fit without wrap inside the process window selected by
  `USER_TOP()`;
- `/dev/mem` and `/dev/kmem` accept a transfer when either complete-span
  predicate accepts it;
- a rejected span returns `EFAULT` before `uiomove()` changes transfer state;
- the one-byte `badkaddr()` and `baduaddr()` entry points remain compatible
  wrappers around the complete-span services.

RP2040 uses the subtraction-based `rp2040_user_span_contains()` predicate for
both kernel intervals. STM32 implements the same ordered-bound and subtraction
contract locally so the shared `sys/dev/mem.c` caller retains one interface.
The public device numbers, read/write entry point, error values, syscall ABI,
process-window policy, and `/dev/null` and `/dev/zero` behavior remain
unchanged.

## Deciding evidence

`tests/kernel/mem_span_extract.py` extracts the exact RP2040
`badkaddr_span()` and `badkaddr()` definitions. The fixture links the exact
`sys/dev/mem.c`, runs at native and ILP32 widths, and repeats under address,
undefined-behavior, and alignment sanitizers. Each positive run executes 61
assertions over complete user, kernel RAM, and kernel flash spans; empty vector
handling; provider failure; the other memory-device minors; and rejected
discontiguous, one-byte-overrun, invalid-start, and wrapped spans.

Four warning-clean mutations restore endpoint-only admission, reject every
valid user span, reject every valid kernel span, or reduce the kernel service
to a one-byte check. Each native mutation exits through its named assertion.
The ILP32 endpoint mutation must independently report both
`discontiguous_error == EFAULT` and `wrapped_error == EFAULT`; accepting either
failure alone does not satisfy its oracle. A timestamp-only dependency
calibration confirms that changing `machine/usercopy.h` rebuilds every native,
ILP32, sanitizer, and mutation fixture that consumes the extracted predicate.

`check-kernel`, `check-kernel-ilp32`, `check-test-execution`, and
`check-c17-source-denominator` pass. The execution inventory accounts for 42
tests, and the source-denominator gate rejects all 21 named bad controls. Ruff
accepts both Python tools with its cache disabled. Clean PICO and PICO_UART
distributions compile and link the changed kernel objects under the recorded
GNU17 Cortex-M0+ commands.

The committed STM32 F405 production command also compiles its changed
`machdep.c` for Cortex-M4. A supplementary `-Wextra -Werror` invocation reaches
three pre-existing translation-unit diagnostics in `nodump()`, `boot()`, and
the empty LED-off macros. The focused span mechanism therefore has production
compile evidence for STM32; a full-warning STM32 translation-unit claim remains
open for its later maintained-ARM migration unit.

## Static resources and dependency closure

Matched clean schema-2 distributions compare the baseline with implementation
revision `ae668f6700e97668647f1a522811bf06ee10504b`. Both captures use bmake
20260912, arm-none-eabi-gcc 16.2.0, and GNU ld 2.47.20260726. Their dependency
closures account for 173 PICO C/header paths, 165 PICO_UART paths, all 36
shipped artifact groups, every installed a.out, the native startup object and
libc archive, generated inputs, and the packed root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,454 bytes | 105,478 bytes | +24 bytes |
| PICO_UART flash | 92,980 bytes | 92,996 bytes | +16 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| `machdep.o` text / data / BSS, PICO | 2,536 / 55 / 12,236 bytes | 2,568 / 55 / 12,236 bytes | +32 / 0 / 0 bytes |
| `machdep.o` text / data / BSS, PICO_UART | 2,516 / 55 / 12,236 bytes | 2,548 / 55 / 12,236 bytes | +32 / 0 / 0 bytes |
| `mem.o` text / data / BSS | 245 / 0 / 0 bytes | 233 / 0 / 0 bytes | -12 / 0 / 0 bytes |
| `mmrw()` symbol / static frame | 224 / 24 bytes | 212 / 24 bytes | -12 / 0 bytes |
| `badkaddr()` symbol / static frame | 54 / 0 bytes | 10 / 8 bytes | -44 / +8 bytes |
| `badkaddr_span()` symbol / static frame | absent | 78 / 8 bytes | +78 / +8 bytes |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement, static SRAM allocation, native-artifact size,
archive selection, and archive-member set is unchanged. The largest shipped
loaded text/data/BSS occupancy remains 75,412 bytes for `/usr/libexec/smlrc`.
The candidate retains 25,594 PICO and 38,076 PICO_UART bytes below the 128 KiB
kernel limit. The exact-command object replay attributes the implementation to
32 additional `machdep.o` text bytes and 12 fewer `mem.o` text bytes in each
configuration; final-link placement produces the measured +24 and +16 byte
kernel results. The bounded exception accepts those linked flash and
`badkaddr()` frame costs for complete-span admission. Runtime stack and heap
high-water and storage traffic remain unmeasured.

The retained parent evidence records have these SHA-256 identities:

- parent source snapshot: `a2838b6dec4b7949df094cbdf31fb000c45f39e01992a609b14a5708618266a0`
- parent dependency closure: `562e9dd94c1b94f6877e85f7536c674a3233e54551b0858c8733dcfedea903cd`
- parent schema-2 resources: `39fb971fd24daab4e4ec0f3a4c05e092c4a76bc54b550c91e7d21efcc4065cce`
- parent object and frame: `9d51ea9515396d052fc3a88d964eb04555d13b5eb5d9c30bdace205a56d39bde`
- parent clean status: `a7b2196488ce27361c836849d45dd20fc6630fd8d9912bb505f7e473e256ac04`
- parent distribution status: `05a2be4277c9f20196b54a78fa1200e0b63e61601e0f7f04b26932c47d305aa2`

The exact-head candidate evidence records have these SHA-256 identities:

- candidate source snapshot: `8fa3ead5710c444412f09ea3d660ab1f7ce180c9621c2b36916a5b9ac1be1b26`
- candidate dependency closure: `b85bdbd10aaa33c51638b5dd0e36344a777debea4dcef24a8e82b2d2c1dac614`
- candidate schema-2 resources: `de952068e0ed041c67f22e6c066ed8b078db4c30aae62673656a09d88029f323`
- static-resource comparison: `9ea06a699ab2a9de3aafb5a77c0d69cdc7b137c42638ccc57ad54b68a7b8662d`
- candidate object and frame: `fa34e664a84b3659287b8bbbe4e117642065ddd730070f7e37b67f1a3fecfcb9`
- object and frame comparison: `3df5a03de9928b4679fc5ef82915c1904b939a56205af2395c27c50a4db89e5b`
- candidate clean status: `a01bf4e99f03227406e5ec6a4a51f0c4e29ced31f594e0d4ebe6327d79880c34`
- candidate distribution status: `3ff0257823877bacabcfcbf8238a4ee1165e9480aa44a03084e5eaa1a75698b4`

## Evidence boundaries and residuals

The host gates establish the exercised complete-span arithmetic, publication,
minor-device, mutation, and sanitizer contracts. The two target distributions
establish exact Cortex-M0+ compilation, linkage, dependency closure, and static
resource effects. The STM32 command establishes production Cortex-M4
compilation for the changed source only.

These checks do not execute a memory-device transfer on physical silicon,
establish MPU enforcement, inject a deliberate hardware fault, or measure a
runtime stack or heap peak. Those evidence classes remain separate. `ptrace`
still accepts caller-selected addresses and needs an ARM writable-register and
program-status-register policy before its own span repair. The remaining
RP2040 and STM32 `machdep.c` semantics and the broad `systm.h` declaration set
retain independent migration units.
