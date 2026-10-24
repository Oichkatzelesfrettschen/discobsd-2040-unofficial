# RP2040 ptrace address and register admission

Date: 2026-10-24 UTC
Baseline revision: `6fb7903da37ff0603fca5dd4e0bff803440b3dd9`
Implementation revision: `1e47182015a6bf8b0ee91f4caf1b135cb9bd0d1c`
Aggregate validation revision: `2d2598b36c8a75d58146beab0a15c329ecf66738`

## Finding and historical contract

The ARM `ptrace` path had four independent admission failures. The libc entry
cleared `errno` by replacing the PID and address argument registers before
`svc`. Instruction and data requests checked one byte before accessing an
`int`. User-area reads rounded misaligned byte offsets down, while user-area
writes admitted any derived word in the kernel user block. `PT_CONTINUE`
published a new PC before validating the signal, and `PT_STEP` fell through
without installing a stepping mechanism.

Revision `c041f027b72d72290884905f23ed1e59355020b4` documents the register
model inherited by the port. The MIPS implementation treated `PT_READ_U` and
`PT_WRITE_U` addresses as byte offsets from the user-area base and admitted
writes only when the derived word belonged to the saved trapframe. The same
revision converted `u_frame` to an architecture-specific pointer and left the
ARM write and single-step branches as explicit placeholders. The maintained
ARM tree has no debugger consumer; the quarantined MIPS `adb` remains outside
the ARM image.

The repaired ARM path retains the historical byte-offset namespace and public
request values:

- `PT_READ_I` and `PT_READ_D` copy one complete four-byte word through
  `copyin()` and publish the result only after a successful copy.
- `PT_WRITE_I` and `PT_WRITE_D` copy one complete four-byte word through
  `copyout()`. Byte-copy semantics admit unaligned user addresses without an
  unaligned kernel load or store.
- `PT_READ_U` requires a nonnegative, four-byte-aligned offset whose complete
  word fits in the `USIZE` user block.
- `PT_WRITE_U` applies the same offset geometry and admits only words that
  resolve to the current saved trapframe. General registers r0-r11 and LR
  accept all bit patterns. The saved stack pointer must be eight-byte aligned
  and leave room for the complete eight-word exception-return frame inside the
  process window. The program counter must be halfword aligned and address a
  complete Thumb instruction halfword inside that window.
- An xPSR write may change only the N, Z, C, and V condition flags. Thumb
  state, stacked alignment, exception state, and every reserved field retain
  their saved values.
- `PT_CONTINUE` validates the signal and replacement PC before publishing
  either. Signal zero means no signal; valid signals are greater than zero and
  less than `NSIG`. Address value one preserves the saved PC.
- `PT_STEP` returns the established parent-visible `EIO` refusal. Cortex-M0+
  has no DebugMonitor exception, and the kernel has no software-breakpoint
  ownership protocol that could implement one instruction of stepping without
  colliding with instruction writes or another tracer.

The libc wrapper saves r0-r3 on its aligned stack, clears `errno`, restores all
four arguments, and then executes syscall 26. A successful kernel result of
`-1` therefore remains distinguishable from failure through zero `errno`.
The syscall number, request numbers, public prototype, IPC record, user-area
offset convention, and parent-child traced-stop handshake remain unchanged.

## Deciding evidence

`tests/kernel/ptrace_extract.py` binds the production helpers, `ptrace()`, and
`procxmt()` to a source SHA-256 before the host fixture includes them. Native,
ILP32, and sanitizer runs execute 135 assertions over parent and child lock
ownership, traced-child admission, successful `-1` result propagation,
complete and crossing user words, injected partial-copy failure, user-area
offsets, every writable register, prohibited user fields, stack and program
counter geometry, xPSR preservation, signal endpoints, continuation ordering,
single-step refusal, and unknown requests.

Twelve warning-clean mutations admit a negative trace request, shorten a read,
publish a partial read, round a user offset, admit the user-area endpoint,
write an arbitrary user-area field, relax PC or SP alignment, overwrite xPSR
control fields, publish PC before signal admission, admit signal `NSIG`, or
restore the inherited `PT_STEP` fallthrough. Each mutation must exit through
its named assertion with status 1. The assembly checker reads the Cortex-M0+
object disassembly, requires the exact balanced r0-r3 save, `errno` clear,
restore, and immediate `svc 26` sequence, and checks both `errno` stores and
return sequences. One mutation removes the save and restore; another inserts
an argument clobber after the restore. Both calibrate distinct rejection paths.

The exact PICO and PICO_UART GNU17 production commands compile the shared
kernel translation unit with `-Wall -Wextra -Werror`. The libc entry assembles
for Cortex-M0+ and the checker reads the emitted Thumb instructions. Host and
compiled-target evidence establishes admission logic, calling-convention
preservation, and generated instructions. Exception return, process tracing,
breakpoint behavior, MPU enforcement, and deliberate faults on physical
silicon retain separate hardware evidence states.

## Static resources and residuals

Matched clean schema-2 distributions compare baseline `6fb7903d` with
implementation revision `1e471820`. Both captures use bmake 20260912,
arm-none-eabi-gcc 16.2.0, GNU ld 2.47.20260726, and Python 3.14.8. Their
dependency closures account for 173 PICO C/header paths, 165 PICO_UART paths,
all 36 shipped artifact groups, every installed a.out, the native startup
object and libc archive, generated inputs, and the packed root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,478 bytes | 105,730 bytes | +252 bytes |
| PICO_UART flash | 92,996 bytes | 93,248 bytes | +252 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| `sys_process.o` text / read-only data | 428 / 40 bytes | 684 / 36 bytes | +256 / -4 bytes |
| `ptrace()` / `procxmt()` symbols | 216 / 212 bytes | 232 / 452 bytes | +16 / +240 bytes |
| `ptrace()` / `procxmt()` static frames | 16 / 8 bytes | 16 / 24 bytes | 0 / +16 bytes |
| libc `ptrace` entry text / symbol | 28 / 22 bytes | 32 / 26 bytes | +4 / +4 bytes |
| libc `ptrace` argument-save stack | 0 bytes | 16 bytes | +16 bytes |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 bytes, 0 members |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement, static SRAM allocation, native-artifact
size, archive selection, and archive-member set is unchanged. The candidate
retains 25,342 PICO and 37,824 PICO_UART bytes below the 128 KiB kernel limit.
Exact-command replay attributes each linked 252-byte increase to 256 more
`sys_process.o` text bytes and four fewer read-only-data bytes. The libc entry
adds one balanced four-register save and restore around the errno clear; the
object and disassembly gate record four additional text bytes and 16 bytes of
temporary caller stack.

The resource comparer rejects both linked flash increases under the default
nonpositive-growth rule. The bounded exception accepts the two 252-byte flash
deltas, the 16-byte `procxmt()` static-frame increase, and the libc wrapper's
16-byte argument-save stack. The added code closes reproduced complete-word,
kernel-user-area, exception-return, publication-order, and AAPCS argument
failures while adding zero static SRAM, process-window, packed-root,
native-library, or archive-selection consumption. Compiler-reported function
frames do not establish a complete call chain or runtime high-water mark.

The retained evidence records have these SHA-256 identities:

| Receipt | SHA-256 |
| --- | --- |
| Parent source snapshot | `7455d78428300dac4e0816d0d9222b22aa22132d078e4294cf3c1c422726e6b3` |
| Parent dependency closure | `cc3dccc58543f5796d40e2aeb7d56814eb0ba794432e5f92f6aaad8ceae46726` |
| Parent schema-2 resources | `27a4bbd144b149cdeb47d96efcd49b9676d509cb60ccbfe4a0ef1dd480f3ee61` |
| Parent object and frame | `b4e3a078d225e6b808bd5f382b4bed459cb2ed212a693aac593483634b0f262d` |
| Parent clean status | `7abd1d07f330f13f707e08969dfb9e7c296f4d4e9ba5a3e87c58be7c030ea712` |
| Parent distribution status | `c851e2b73e2270cc8691aab1965bad9e2cb6b2fd6a6288785c54110fd3fe756f` |
| Candidate source snapshot | `0e198ba7577c147ab7b9d6fea03cb9c4b90cf9d1adcc32615fd861d8e1bf7a84` |
| Candidate dependency closure | `8c339e304ffbe6c3521b1b2a50059681bf21a2687442dee61d84208a952e19f4` |
| Candidate schema-2 resources | `04a11f26d1ea160a24c5988f775c9aa39b87374caf5f28bd67abfccd8ae5fb85` |
| Static-resource comparison | `3b9559dfb178ae45d12e7038cf31e4453b1cba419613344720f83e884d987c49` |
| Candidate object and frame | `2a396495603393e571eb3b3d828892c9ef9498f945a307991c0c26328b35f17a` |
| Object and frame comparison | `66420a9d1e5aafe99ae92660606f1adeaf8a752ed993102133cb3233921517e6` |
| Candidate clean status | `4eaef3928a8b76c613e48d75f5d54e63f8f343bac8073b2759e24c31b952c36f` |
| Candidate distribution status | `829f17240c19934e85fa4191736f7aba53f60e43e37fe8bae10ada5bb5b569ac` |
| Exact-head aggregate sequence status | `1dea46df13ea81fb8f117d062dbfdf38643288d3b240804e4154ccef36ef8c76` |
| Exact-head aggregate build status | `556fa661c7a7dfe327b8ff2b492f7900ec6f5578b8387103d49e776d8f74e253` |
| Exact-head aggregate check status | `24f94fbadf577ebe4ff146d4b1b3e79440c1e592cc949fc0f80d177784de7c7b` |

The exact-head `logged-check` invocation
`rp2040-check-local-20261024T071335Z-3047259-81d5a00f` completed with status 0
at aggregate validation revision `2d2598b3`. Its build stage completed with
status 0 in 48.448 seconds, its check stage completed with status 0 in 316.443
seconds, and the complete sequence took 364.893 seconds. The aggregate ran the
host, ILP32, sanitizer, mutation, target-build, archive, image, and accounting
gates registered under `logged-check`. The invocation performed no
physical-board operation.

Runtime stack and heap high-water and storage traffic remain unmeasured.

The bounded unit closes the shipped direct-address `ptrace` path. A real
single-step mechanism remains a separate additive architecture project: it
needs breakpoint instruction ownership, cache and flash policy, signal
delivery semantics, collision behavior, cleanup after tracer death, and board
execution evidence. The signal-return xPSR boundary remains governed by its
own admission record and does not inherit the ptrace mask by implication.
