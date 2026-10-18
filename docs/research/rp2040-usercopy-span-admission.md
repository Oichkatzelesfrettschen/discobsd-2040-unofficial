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
hashes in `.build-logs/usercopy-span-contracts.json`.

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
The candidate must reproduce the same toolchain, source closure and complete
static denominator before integration. Runtime stack, heap and storage traffic
remain unmeasured and keep their corresponding acceptance states open.

This unit closes the span-admission mechanism for callers that use `copyin` or
`copyout`. Direct `baduaddr` consumers retain separate migration units:
`sys_generic.c` ioctl buffers, `vm_swp.c` physical I/O, `mem.c` mixed address
spaces, `sig_machdep.c` signal contexts and `syscall.c` stack arguments. The
syscall stack-argument unit also owns stale `u_arg` preservation and dispatch
failure behavior. MPU permission coverage, deliberate faults, runtime resource
peaks, persistence effects and physical-board behavior retain their separate
evidence and authorization contracts.
