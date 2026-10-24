# RP2040 ptrace address and register admission

Date: 2026-10-24 UTC
Baseline revision: `6fb7903da37ff0603fca5dd4e0bff803440b3dd9`

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
object disassembly, requires the balanced r0-r3 save and restore before the
single `svc 26`, and checks both `errno` stores and return sequences. Removing
the save and restore calibrates its rejection path.

The exact PICO and PICO_UART GNU17 production commands compile the shared
kernel translation unit with `-Wall -Wextra -Werror`. The libc entry assembles
for Cortex-M0+ and the checker reads the emitted Thumb instructions. Host and
compiled-target evidence establishes admission logic, calling-convention
preservation, and generated instructions. Exception return, process tracing,
breakpoint behavior, MPU enforcement, and deliberate faults on physical
silicon retain separate hardware evidence states.

## Static resources and residuals

The matched parent and candidate distributions, dependency closure, archive
selection, object sizes, static frames, packed root, and remaining kernel
capacity are recorded after the candidate source is committed and captured
through the schema-2 resource workflow. Runtime stack and heap high-water
remain separate measurements.

The bounded unit closes the shipped direct-address `ptrace` path. A real
single-step mechanism remains a separate additive architecture project: it
needs breakpoint instruction ownership, cache and flash policy, signal
delivery semantics, collision behavior, cleanup after tracer death, and board
execution evidence. The signal-return xPSR boundary remains governed by its
own admission record and does not inherit the ptrace mask by implication.
