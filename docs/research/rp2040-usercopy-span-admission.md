# RP2040 user-copy span admission

The RP2040 `copyin` and `copyout` implementations formerly admitted the first
and last addresses independently. On a 32-bit build, a sufficiently large
length could wrap the computed last address back into the process window. The
parent source at `e91a44c7bdb55f1c7473e62a1e89669ee5ee7167` has SHA-256
`6319c4def706c6e13e63ef5550750e91aef0d261f1f277667f87df4784df9bb7`.
An exact-source ILP32 fixture starts at the process lower bound plus `0x1000`
with length `0xfffff001`. The compiled former endpoint expression produces the
lower bound, both copy functions return success, and each publishes one
`bcopy` call for 4,294,963,201 bytes.

`sys/arch/rp2040/include/usercopy.h` owns the portable interval predicate. The
predicate accepts `[start, start + length)` only when the bounds are ordered,
`start` lies in the closed interval from `lower` through `upper`, and `length`
fits in `upper - start`. Ordered checks precede subtraction, so the predicate
never constructs a wrapped endpoint. An empty span at the exclusive upper
bound is valid at the helper boundary. The copy entry points retain their
existing rule that a zero-length operation succeeds for every pointer.

`machdep.c` keeps the public `baduaddr`, `copyin` and `copyout` signatures.
`baduaddr` asks whether one byte is accessible. Each nonempty copy admits the
complete span once, returns `EFAULT` before publication when admission fails,
and calls `bcopy` once after admission succeeds. The lower bound remains
`__user_data_start`; the upper bound remains `USER_TOP(u.u_procp)`, including
the existing conditional process-window extension. The change adds no syscall,
layout, storage definition, translation unit or archive member.

`check-rp2040-usercopy-span` checks the header as a standalone and repeated
include under host GCC, host Clang, Arm GCC and Arm Clang strict C17 profiles.
The host GCC form also executes under AddressSanitizer, UndefinedBehaviorSanitizer
and the alignment sanitizer.
The fixture pins the exact four-argument function type and the empty, full,
last-byte, crossing, wraparound and malformed-bound cases. Exact evaluated
PICO and PICO_UART GNU17 commands compile the production `machdep.c`. A wrapped
endpoint predicate and a removed include guard are calibrated rejected forms.
The gate records thirteen compiler and execution cases with source and tool
hashes in `.build-logs/usercopy-span-contracts.json`. The kernel harness binds
both ILP32 executables to the helper header. Its generated machine-header stamp
tracks changes to the RP2040 include directory and removes stale forwarding
headers before rebuilding the set. `tests/kernel/.gitignore` owns the two
extracted sources and two executables.

`check-kernel-ilp32` extracts the exact four production functions by signature
and runs them with 32-bit `caddr_t`, `u_int` and `size_t`. It covers single-byte
admission, zero-length behavior, first, last and full-window spans, a crossing
span, addresses below and at the upper bound, the reproduced wraparound,
malformed bounds and the conditional upper-bound extension. Every rejected
copy makes zero `bcopy` calls and preserves the destination fixture. Restoring
the former endpoint checks must fail the same executable with status 1.

The parent clean distribution and closure capture completed with 36 shipped
artifact groups. Its schema-2 resource receipt has SHA-256
`81c930bf745df4942cb3b3e29d4bbbb914c68ac6041180a0d1b3ed4c5a45318a`;
the source capture has SHA-256
`2d23fb2f50d2db48add54edeb5b0eed40b7a1b3418aa03a5148b0400114964a6`.
The frozen candidate at `46a0844a28f1a235c4c946905156efd89b992290`
completed a 5.011-second clean and a 103.344-second clean distribution. Its
closure accounts for all 36 shipped groups, including 173 tracked C or header
paths for PICO and 165 for PICO_UART. The candidate source, closure, resource,
delta and root-image SHA-256 values are, respectively,
`38774eb5779cdb30908c67f0a9100d257433ca93ba232a48daa5ca1ee168bfda`,
`5c87c1d08046fe088467e71aff54ded889dd3af85fe1d079d770fafeafa74fba`,
`e29d9e5ff056be1429fd5d159e3692bbbf300c745606f74a4c0af6ae426b3707`,
`58b3d473e891ca33a0e292f7ea2a6b7c7598e3977530c28ab21dc799e335332b`
and `badab7ba8d2bb83d04fdcbe8f71e4386eb688fba05c57d3070533f7a4267262d`.
The distribution receipt has SHA-256
`1ad07ea79ee4a6af699a81ee60fa44e75875ba2c3171ed151cc70337fb71943d`.

The schema-2 comparison accepts all 154 static numeric surfaces. It reports
152 zero deltas and an eight-byte flash reduction in each kernel configuration,
with candidate flash occupancy of 104,758 bytes for PICO and 92,276 bytes for
PICO_UART. All 36 archive-owner groups retain their member selections. The
packed root retains 692 data blocks, 287 free blocks and 33 free inodes. The
largest loaded text, data and BSS occupancy remains 75,412 bytes for
`/usr/libexec/smlrc`; the installed native `libc.a` remains 43,010 bytes with
151 members. The comparison verifies matching toolchain and measurement-tool
identities. Runtime stack, heap and storage traffic remain unmeasured and keep
their corresponding acceptance states open.

This unit closes the span-admission mechanism for callers that use `copyin` or
`copyout`. Direct `baduaddr` consumers retain separate migration units:
`sys_generic.c` ioctl buffers, `vm_swp.c` physical I/O, `mem.c` mixed address
spaces and `sig_machdep.c` signal contexts. The completed syscall stack-argument
unit is recorded in `rp2040-syscall-stack-argument-admission.md`. MPU permission
coverage, deliberate faults, runtime resource
peaks, persistence effects and physical-board behavior retain their separate
evidence and authorization contracts.
