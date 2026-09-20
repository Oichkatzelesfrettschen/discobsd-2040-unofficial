# Compact, constrained C for DiscoBSD RP2040

## Scope, authority and evidence

The original review inspected
`Oichkatzelesfrettschen/discobsd-2040-unofficial` at
`b0e32f88b612967e8673a9d7a1f60661c0a526b7`. Repository reconciliation began
from `728677b8ff4330decf579b8ea03693ee904bb592`; later migration measurements
identify their commands and artifacts rather than relabeling either revision
as current. This document proposes style and engineering rules. The repository
source, RP2040 datasheet, ARMv6-M Architecture Reference Manual and retained
gate records settle conflicts with the proposal.

Repository paths in this document are relative to the repository root. The
reference map at the end names the source for each architectural claim. The
original review inspected the live checkout and retained evidence; it did not
build the repository, execute an emulator or access a board. Results carrying
a build or gate command were measured when this document landed and say so.

Use these evidence terms consistently:

| Term | Meaning |
| --- | --- |
| Measured | A named command or board capture produced the value for an identified artifact. |
| Documented | Source, a specification or a retained record states the value. |
| Inferred | A stated argument derives the result from measured or documented premises. |
| Not run | The relevant build, emulator or board check did not run. The value is unknown, not zero. |

## 1. Optimize the resource that is actually constrained

Apply this priority order unless a change records a stronger project-specific
constraint:

1. Preserve correctness, security, ABI and durable storage semantics.
2. Reduce peak live RAM while preserving a justified safety reserve.
3. Reduce loaded process bytes and kernel-resident bytes.
4. Reduce packed and raw root blocks, inodes and kernel flash.
5. Reduce execution time, blocking latency, energy and flash wear.
6. Reduce source bytes only when source storage is itself a measured limit.

A smaller reserve requires a tighter demonstrated bound or an explicitly
revised operating assumption; lowering the reserve alone is not a memory
optimization.

Maintain separate accounts for source bytes, executable storage, linked
resident memory, heap peak, stack peak, execution time, blocking latency,
flash erase/program activity and review complexity. A smaller source file
need not improve any of the other accounts.

Ordinary C indentation and comments affect the source file, not the emitted
instructions. Preprocessing preserves meaning through tokens and replaces
comments with spaces. Whitespace inside literals, token boundaries,
preprocessor directives, line splices, stringification and line-number
macros must not be removed indiscriminately. Debug and source metadata form
another account. A whitespace or minification claim requires a retained
fixture, exact compiler command and emitted-section comparison before the
claim counts as measured.

**Policy:** keep canonical source readable. Use compressed distribution copies
only when source storage is a measured limit. Never treat a regular-expression
C minifier as a correctness-preserving build transformation. Preserve required
provenance and licensing material.

## 2. The actual memory and execution boundaries

The PICO configuration selects 125 MHz, Cortex-M0+ Thumb code and software
floating-point ABI. The architecture build selects ARMv6-M rather than the
Cortex-M4 defaults of other ARM ports. `share/mk/sys.mk` and
`sys/arch/rp2040/compile/PICO/Makefile` carry the evaluated flags.

The linker partitions SRAM as follows. These are reservations, not claims of
free space or measured current usage. `sys/arch/rp2040/conf/RP2040.ld` is the
source of truth for the reservations.

| Region | Origin | Reservation | Owner |
| --- | --- | ---: | --- |
| USERRAM | 0x20000000 | 144 KiB | Resident process image and its dynamic space |
| RAM | 0x20024000 | 106 KiB | Kernel data allocations |
| U0AREA | 0x2003E800 | 3 KiB | Process-zero u area |
| UAREA | 0x2003F400 | 3 KiB | Current u area and its kernel stack |
| SCRATCH | 0x20040000 | 8 KiB | USB transmit-ring storage |

The first two u-area reservations are not two wholly available 3 KiB stacks.
The USB scratch reservation is not spare application memory. The active
configuration enables a 16 KiB SwapRAM pool; it lives within kernel storage,
not beyond the 264 KiB SRAM total. A comment calling that option disabled is
stale. The SRAM-bank aliases name existing memory, not additional capacity.

Flash reserves 128 KiB for boot2 plus kernel, 1536 KiB for the root region,
and 384 KiB for raw swap. The physical root region is not entirely usable
file payload: translation and filesystem overhead must be accounted for.
Boot2 occupies the first 256 bytes of the kernel reservation.

The user linker places programs at 0x20000000. Read-only user code and
constants are part of the RAM-resident image; `const` is not a command to
execute a user program from flash. The kernel's flash-mapped code and a
RAM-loaded user executable have different storage costs. The relevant owners
are `sys/arch/rp2040/conf/RP2040.ld` and `lib/elf32-arm.ld`.

Because a user program's read-only data is ordinary RAM, a write through a
`const` pointer lands rather than faulting. `lib/libc/stdio/ungetc.c` is the
worked case: its store into the caller's buffer reaches a string literal when
`sscanf` is the caller, which is silent here and fatal on a host that maps
literals read-only. Host execution is therefore a stricter memory-protection
oracle than the board, not a weaker one.

A conservative process budget is:

```text
loaded a_text + a_data + a_bss
+ reserved heap growth + reserved user-stack growth
+ arguments/environment/alignment not already included
<= 147456 bytes - deliberate safety reserve
```

Count each object once. In particular, do not add a read-only table twice if
it is already in `a_text`, and do not add each overlaid section as if the
sections occupied separate addresses. Runtime liveness can tighten the
bound, but only after the overlap assumptions have been demonstrated.

The live linker and `machparam.h` set the process window to 144 KiB. Retained
documents that quote the earlier 96 KiB window describe historical revisions
and must not set a current budget.

### Protection correction

The RP2040 has an eight-region MPU. The earlier repository statement that the
chip has no MPU was incorrect and `AGENTS.md` now carries the correction.
Datasheet section 2.4.1 lists the eight regions among the core features,
2.4.2.5 states the MPU features, and 2.4.6 gives MPU_TYPE, MPU_CTRL, MPU_RNR,
MPU_RBAR and MPU_RASR. An MPU is not an MMU and does not create virtual
address translation. Its presence does not establish that a particular kernel
configures effective process isolation; this port programs it to fence the
user window from the kernel, the peripherals and the flash
(sys/arch/rp2040/doc/MPU.md), and no more.
Check actual registers, privilege transitions, region coverage, and deliberate
fault tests before claiming protection. ELF segment flags and linker-region
annotations alone are not such proof.

## 3. Preserve the architecture rather than replacing it with a demo

| Boundary | Existing design | Style consequence |
| --- | --- | --- |
| Build | Native bmake, cross compiler, project headers/libc | Add native targets; do not invent a parallel authoritative build |
| Executable | Intermediate ELF, then project a.out conversion | Inspect both; use a.out-aware tools for final programs |
| Port | Shared BSD kernel plus RP2040 machine/device code | Keep silicon rules in the port and reusable logic outside MMIO |
| Root | Explicit manifest and packed executables | Measure installed inode/block counts, not only individual .o files |
| Multicall | One dispatched applet per process lifetime | Preserve exclusive-lifetime proof and shared libc state |
| Native development | Smaller C, Thumb assembler, project linker | Cross-GCC C17 acceptance does not prove native compatibility |
| Evidence | Host, ABI, cross, emulator, board gates | Report which layer ran and what each layer can demonstrate |

These boundaries are documented in the build, agent guide, constrained-C
research and multicall mechanism. The reference map names the owning files.

`sys/arch/rp2040/dev/flash_swap.c` is a useful architectural model: the
mechanism accepts device operations, allowing a host NOR model to execute the
production algorithm and reject invalid transitions. This separates testable
mechanism from hardware access without adding a general-purpose framework. Its
spare sector trades flash operations for a smaller RAM scratch buffer; neither
the function name nor a spare sector alone proves power-failure atomicity.

A second model is the weak-symbol split that keeps an expensive dependency out
of programs that do not use it. `lib/libc/stdio/doprnt.c` declares
`__doprnt_cvt` weak and `share/mk/sys.mk` forces the member only for a program
declaring `PRINTF_FLOAT=yes`; `doscan.c` and `SCANF_FLOAT` mirror it for the
input direction. The rule the pattern encodes: a feature whose cost is a
linked dependency closure belongs behind an opt-in that the link, not the
source, resolves.

## 4. Language profiles: C17 is not one global switch

The shared ARM userland command selects `-std=gnu17 -fno-common`, but leaf
Makefiles can select older dialects later on the command line. `bin/sh` adds
`-ansi`, and `usr.bin/smallc` adds `-std=gnu89`. The warning policy also has
full and legacy profiles. Describe the tree by evaluated compilation command,
not by the first standard flag found in a shared Makefile.

Use four adoption profiles:

| Profile | Rule | Admission evidence |
| --- | --- | --- |
| RP2040 kernel and port | GNU17, freestanding, no builtin assumptions, target ABI and localized assembly/MMIO | Exact kernel compile, link map, linker assertions and relevant host model |
| Modernized cross userland | Production GNU17 plus strict C17 for portable mechanisms, full prototypes and full warning profile | Host behavior gate, exact Cortex-M0+ compile and final a.out inspection |
| Historical cross userland | Retain the existing dialect until one bounded component is repaired and measured | Declared dialect exception, legacy warning count and unchanged behavior gate |
| Native Smaller C | Use only the measured Smaller C language, assembler and libc subset | Native compiler suite, qemu-arm oracle and board run when native support is claimed |

Host compilation is an evidence layer, not a fifth production profile. A host
sanitizer pass cannot establish ARM ABI layout, Thumb-1 code generation,
ELF-to-a.out conversion or native Smaller C acceptance. It can, however,
establish a memory-safety fact the board hides, as section 2 records.

Language syntax, standard-library availability, ABI compatibility and compiler
code generation are separate questions. Selecting C17 does not supply a
missing libc function, implement atomics in hardware or make a Cortex-M4
object executable on ARMv6-M. Preserve project includes and startup objects.
Use freestanding flags where the module and build require them, not as a
blanket substitution for the current build.

### C17 migration unit

Modernize one leaf directory, library member family or other independently
testable component at a time:

1. Record the evaluated production command, warning profile, behavior and
   final target footprint before editing.
2. Repair undefined behavior, error propagation, ownership and ABI contracts
   before changing presentation.
3. Convert definitions and declarations together. Use full prototypes and
   preserve established public and kernel ABI types.
4. Remove the component's older-dialect override only after every production
   translation unit accepts the intended dialect.
5. Move the component to `WARNLEVEL=full` only after `-Wall -Wextra -Werror`
   succeeds without blanket suppression.
6. Run the host behavior gate, exact Cortex-M0+ compilation, ELF-to-a.out gate
   and final footprint comparison. Run the native suite separately when the
   change claims Smaller C compatibility.

A mass syntax rewrite, formatter pass or repository-wide standard-flag change
is not a C17 migration unit.

`lib/libc/stdio/doscan.c` is the worked example: one file with an unchanged
entry signature, so no consumer's ABI moved; a repair of undefined behavior
before any presentation change; a host gate calibrated against the source it
replaced; and a footprint account naming the writable table and the dependency
closure it removed. `docs/research/211bsd-patch-scope.md` records its numbers.

## 5. Formatting and naming rules

These rules govern new files and lines deliberately modernized under a bounded
migration. Existing local conventions win in a focused maintenance patch;
formatting-only changes belong in separate commits.

| ID | Rule |
| --- | --- |
| F01 | Use LF and a final newline in newly maintained text files. |
| F02 | Use tabs for structural indentation at eight columns in BSD-style port code; use four-space continuations. |
| F03 | Prefer an 80-column limit; split at grammatical boundaries rather than shortening meaningful names. |
| F04 | Use one statement per line. A compact declaration or arithmetic expression is not permission to combine unrelated effects. |
| F05 | Put function opening braces on their own line and control opening braces on the control line. |
| F06 | Use braces for new control bodies. Preserve clearly scoped existing unbraced guards when minimizing an unrelated patch. |
| F07 | Keep at most one empty line between logical groups. Avoid decorative banners and vertical padding. |
| F08 | Put spaces around binary operators and after commas; do not put a space between a function name and its call parentheses. |
| F09 | Do not align long runs of assignments with padding. Align actual register/format tables only when it adds information. |
| F10 | Prefer one declared object per declaration, particularly pointers. Keep lifetimes as local as the applicable dialect permits. |
| F11 | Use subsystem-prefixed external names, descriptive local names, and explicit units such as `_bytes`, `_us`, or `_blocks`. |
| F12 | Reserve short conventional names for genuinely small scopes; never shorten a public symbol to save hypothetical executable bytes. |
| F13 | Keep identifiers and low-level interface syntax in the C17 source character set, which every compiler in the tree accepts. Prose may use UTF-8. Project text carries no emoji. |
| F14 | Do not hide control flow behind clever macros, nested conditional operators, or comma-expression chains. |
| F15 | Keep include order when headers have existing dependencies. Make new interface headers self-contained and test them. |
| F16 | Do not mass-format imported history, generated code, assembly, binary fixtures, PDP-11/V6 assets, SIMH configurations and traces, or native build recipes. |
| F17 | Use American English, and `--` where punctuation would otherwise take an em dash. |

F13 and F17 restrict what a rule may require, not which byte values a file may
contain. The repository has no character-set gate and wants none: the binding
text rule is that checked-in text carries no emoji, and a mathematical
operator, a Greek letter in an equation, an arrow in a state transition, a
degree or micro sign, and an accented character in a name all stay verbatim.
F16 keeps the PDP-11, V6 and SIMH material outside every automated rewrite,
so those trees keep whatever encoding their upstream uses.

A formatter reduces disagreement about layout; it does not prove correctness
or enforce ownership. Pin its version, adopt an explicit file allowlist, and
run read-only checks in CI. Validate the proposed formatter against the port's
model files before adoption, and retain a reviewed output diff as calibration.
No formatter configuration accompanies this document.

## 6. Comments and interface contracts

The existing comment policy emphasizes the reason for a mechanism: the
hardware restriction, lifetime, invariant, authority, and consequence. Keep
that policy instead of filling code with paraphrases of the next line.
`AGENTS.md` owns the complete comment rules.

| ID | Rule |
| --- | --- |
| C01 | State why a surprising operation is required, not merely what its syntax does. |
| C02 | Put a local constraint next to the operation; put a cross-function mechanism at file scope. |
| C03 | Name input extents, output capacity, ownership, retained pointers, blocking behavior, and error guarantees at interfaces. |
| C04 | Document whether failure preserves output, consumes input, partially writes storage, or requires cleanup. |
| C05 | Cite durable register names, specification sections, format invariants, and tests rather than task/session chronology. |
| C06 | Distinguish measured results from assumptions and hypotheses. Do not present a source comment's old size as a current measurement. |
| C07 | Centralize a configurable bound; use assertions and tests instead of copying its number into multiple comments. |
| C08 | Keep source comments concise, but retain license notices and essential provenance. |

For example, `sys/arch/rp2040/rp2040/exec_hsaout.c` documents the
preflight-then-commit order and the verdict each failure produces. Call sites
do not need to repeat the file-scope contract at each assignment.
`lib/libc/stdio/doscan_float.c` shows C07 in place: `MANT_DIGITS` and
`EXP_LIMIT` are declared once and each carries a `_Static_assert` tying it to
`DBL_DIG` and the decimal range of a double, rather than a comment repeating
the number.

## 7. Types, bounds and representation

| ID | Rule |
| --- | --- |
| T01 | Use full prototypes; spell a no-argument function as `f(void)` in C17. |
| T02 | Use `size_t` for object extents where the interface permits it. Preserve established ABI types in existing interfaces. |
| T03 | Use fixed-width types for actual storage/wire/register width, not because every variable should have the narrowest type. |
| T04 | Widen arithmetic before a shift or multiplication when required; validate ranges before narrowing. |
| T05 | For operands already validated as representable nonnegative sizes, guard addition with `a <= SIZE_MAX - b` and multiplication with `n <= SIZE_MAX / element_size`, requiring `element_size != 0`. Validate source-domain values before converting them to `size_t`. |
| T06 | Establish the lower bound before subtraction and the accessible extent before dereferencing. |
| T07 | Treat plain-char signedness as an implementation choice; validate ctype inputs or classify known byte domains explicitly. |
| T08 | Use `_Static_assert`, `sizeof`, `offsetof`, and alignment checks for target-specific layout constraints. |
| T09 | Reorder private in-memory fields only after measuring padding and reviewing access patterns. Never silently reorder a shared ABI. |
| T10 | Decode external bytes explicitly. Do not cast an arbitrary buffer to a packed host structure. |
| T11 | Do not serialize structure padding or use whole-struct byte comparison as a semantic equality test. |
| T12 | Use `restrict` only when the no-alias contract is true; use `volatile` for its documented access semantics, not as an optimization charm. |

T07 has a measured consequence in this tree. The V7 scanner reached `isdigit`
and `isupper`, whose macros index the writable `_ctype_` table, so every
program linking `scanf` carried that table; classifying the byte domain
explicitly removed the dependency. The condition matters as much as the
saving: the table returns the moment another object in the same program calls
a ctype macro.

C17 inherits C11's relevant type and layout mechanisms. They do not turn
implementation-defined layouts into portable wire formats. Existing target
examples use `_Static_assert` to pin cache entries, swap descriptors, codec
work areas and fixed transfer sizes. Each assertion establishes one named
target invariant; it is not a universal C layout rule. Code intended for the
native Smaller C profile needs a separate supported mechanism and test.

## 8. Memory lifetime and algorithm rules

| ID | Rule |
| --- | --- |
| M01 | Budget text, initialized data, zero-initialized data, heap and stack separately before composing the process bound. |
| M02 | Treat global/static buffers as permanent commitments for the process or kernel lifetime. |
| M03 | Do not move a stack buffer to static storage and call the cost eliminated; review concurrency, recursion and lifetime. |
| M04 | Prohibit variable-length automatic arrays and unbounded recursion in new constrained paths unless a specific proof justifies them. |
| M05 | Stream input when possible; make lookahead, buffering, and maximum record length explicit. |
| M06 | Prefer bounded arrays, indices, pools and spans where they fit the actual workload; do not invent heap generality unnecessarily. |
| M07 | Allocate at a controlled lifetime boundary, propagate failure, and pair cleanup with ownership. |
| M08 | Reuse a buffer only when the previous owner's last use is established, including callbacks, interrupts and deferred work. |
| M09 | Prefer straightforward bounded search for tiny tables; compare code size and worst-case time before adding a hash table. |
| M10 | Compare the full link closure of a helper against its alternatives, not just the helper's source or .o size. |
| M11 | Treat compression as a storage/time/scratch trade; packed executable size does not establish its expanded process footprint. |
| M12 | Make maximum work and backpressure explicit for sensor streams and terminal/USB queues. |
| M13 | Size a parser's scratch by its output, never by its input. A staging buffer whose required length grows with the input is a defect even when a field width nominally bounds it, because the default width is the one that ships. |

M13 is the rule the V7 scanner violated and both replacement candidates
answer differently. That scanner staged digits in a 64-byte automatic buffer
while an absent field width defaulted to 30000. The 2.11BSD patch 499 scanner
answers with a 513-byte buffer plus a 256-byte class table, both automatic,
which bounds the write but charges 769 bytes of frame to every caller. This
tree's answer accumulates the value as digits arrive, so the integer path
stages nothing and the scanset costs a 32-byte bitmap. Prefer the answer that
removes the buffer over the answer that enlarges it.

A general lifetime bound is shared state plus the peak sum of simultaneously
live private state. For mutually exclusive applets, their private BSS can
contribute the maximum rather than the sum. The existing multicall build
implements exactly this with section ownership, symbol localization,
NOCROSSREFS and conversion gates. A future dispatcher that enters multiple
applets or retains callbacks would require a new lifetime proof.

## 9. Hardware and concurrency rules

| ID | Rule |
| --- | --- |
| H01 | Record the owner of each peripheral, pin function, IRQ, DMA channel, clock/reset dependency and buffer. |
| H02 | Preserve USB-console storage and the UART fallback unless an explicit configuration replaces them. |
| H03 | Match MMIO access widths and side effects to the register specification; avoid C bit-fields for registers. |
| H04 | Do not use read-modify-write on a register with incompatible read or write side effects; use documented set/clear aliases where applicable. |
| H05 | Separate compiler ordering, CPU ordering, atomicity and peripheral completion. They are not interchangeable. |
| H06 | Do not treat volatile as a lock or memory barrier for ordinary data. |
| H07 | Verify that an atomic operation is suitable for its execution context; a runtime helper can violate an interrupt path's assumptions. |
| H08 | Remember that masking one core's interrupts does not stop another core or DMA. |
| H09 | Give IRQ paths a bounded amount of work; defer allocation, formatting, blocking I/O and long computation. |
| H10 | Treat flash/XIP transitions as a complete execution-path constraint, including callees, constants, handlers and other bus masters. |
| H11 | Never count a memory alias or an already-owned hardware buffer as newly available RAM. |
| H12 | Require explicit hardware opt-in for destructive or externally visible tests. |

These are proposed review requirements, not claims that every current driver
already satisfies them. GCC documents the non-ordering of ordinary memory by
volatile and the possibility of fallback atomic helpers. Port configuration
and the linker establish the already-assigned console resources.

## 10. Optimizer and linkage policy

Cross userland uses an `-Os` baseline. The RP2040 kernel uses `-O` unless its
build receives an explicit override. Compare alternative optimization levels
on the complete artifact for the affected execution surface; an optimizer's
name is not a size guarantee. Function/data sections, garbage collection and
LTO are experiments, not a universal two-line repair. Audit vector tables,
assembly references, linker roots, overlays and conversion semantics before
adoption. Retained linker roots may need `KEEP`.

Casts, one-letter variables, `register`, forced inline annotations, unchecked
assumptions and removed error paths are not accepted size evidence. Consider
performance and energy as well as bytes: a smaller loop can require more
instructions, and an extra flash erase can dominate a local CPU saving.

A useful existing counterexample to source-level intuition is the strtol
repair. The repository reports 72 more text bytes for the function but the
removal of a 260-byte writable ctype dependency, yielding 188 fewer process
bytes when no other caller pulls that table back in. The dependency condition
is essential. `docs/research/netbsd-11-micro-backports.md` records the source
revision, member sizes and commands; this review did not rerun them.

Three later repairs strengthen the same rule:

- Bounded `syslog` formatting removes a 512-byte format-copy array and reduces
  the measured target frame by 608 bytes while adding 56 to 80 loaded bytes to
  seven measured consumers. Correctness and the measured target-frame
  reduction take priority over the small loaded-image increase. Any claim
  about whole-path peak stack requires a separate measurement or bound.
- The fixed-block `cat` path removes allocator calls and mutable option state,
  reduces the measured standalone process image by 120 bytes and increases
  the automatic frame. The process-window verdict therefore includes the
  transient stack delta rather than quoting the a.out delta alone.
- The `_doscan` replacement trades text for writable data and for a removed
  dependency: the member gains text while losing a 256-byte writable class
  table outright and losing the `_ctype_` reference that pulled a further
  writable table into every scanning program.
  `docs/research/211bsd-patch-scope.md` carries the commands and numbers.

The first two records live in `docs/research/netbsd-11-micro-backports.md`.
Treat their numbers as retained measurements at the commits named there, not
as current measurements after unrelated source changes.

## 11. Verification and publication rules

| ID | Rule |
| --- | --- |
| V01 | Keep formatting and semantic changes separately reviewable. |
| V02 | Use the real native build commands and project headers to produce tooling metadata. |
| V03 | Keep the existing full/legacy warning distinction; ratchet audited modules toward stricter warnings without blanket suppression. |
| V04 | Exercise portable mechanisms with host sanitizers, malformed inputs and boundary tests. |
| V05 | Test relevant ABI widths, signedness choices, layout assertions and overflow cases. |
| V06 | Inspect actual emitted ARMv6-M instructions and symbols; do not substitute a host object size. |
| V07 | Check ELF-to-a.out conversion and final a.out headers. For overlays, do not sum overlapping ELF sections as distinct allocations. |
| V08 | Rebuild consumers after libc/header changes; the documented incremental dependency gaps make a clean build necessary for acceptance. |
| V09 | Measure packed/raw root blocks separately from expanded process requirements. |
| V10 | Report emulator and board outcomes separately; never label compilation as a hardware pass. |
| V11 | Calibrate gates with known-bad inputs where practical. Passing without a rejecting case can hide an inert test. |
| V12 | Record unmeasured quantities as null or `not run`, never as zero. |
| V13 | Store commit, toolchain, flags, configuration and artifact identities beside every size comparison. |
| V14 | Preserve native historical targets and avoid changes to the separate notes repository without an explicit scope. |
| V15 | Report every affected resource account before and after; identify unchanged accounts and the command that measured them. |
| V16 | Require exact Cortex-M0+ compilation for a C17 migration; a host-only strict-C build is supporting evidence. |
| V17 | Run the overlay, conversion and packed-root gates when a multicall applet changes. |
| V18 | Distinguish an invariant gate from a budget gate; a successful linker assertion does not measure remaining headroom. |

V11 has a concrete form for a replacement: build the gate against the source
being replaced and require it to fail. `check-libc-scanf` was calibrated that
way, and the rejecting run named a defect the reviewer had not predicted,
which is the argument for the rule rather than a decoration on it.

The following record accompanies a size or modernization claim:

| Surface | Required record |
| --- | --- |
| Identity | Source commit, dirty-state classification, configuration, compiler, linker and evaluated flags |
| Behavior | Named positive, boundary, malformed-input and known-bad calibration cases |
| Kernel | Final ELF identity, `unix.map`, section/symbol delta, linker assertions and stack evidence where affected |
| User process | Final a.out header, `a_text`, `a_data`, `a_bss`, heap peak, stack peak and deliberate reserve |
| Installed root | Packed and raw bytes, 1 KiB blocks, inode count, hard links and free-space delta |
| Multicall | Applet object ownership, COMMON/NOBITS verifier, `NOCROSSREFS`, overlay maximum and conversion result |
| Hardware | Emulator and board result kept separate, with explicit opt-in and artifact identity |

`tests/cat_contracts`, `tests/libc_contracts` and its `check-libc-scanf`
target demonstrate the intended host-plus-target pattern, including the
sanitizer tier that detects a scratch-buffer overrun inside a callee's own
frame, where a guard around the caller's destination sees nothing.
`share/mk/warnings.mk` and the warning-policy gates demonstrate the
per-directory warning ratchet. Those paths are examples to reuse, not
substitutes for a gate that exercises the component being changed.

## 12. Acceptance criterion

A compact implementation is accepted when its behavior and boundaries are
clear, its error paths are preserved, its costs are measured in the correct
address space and artifact, and the required profile-specific gates pass. The
change record names every affected resource account, provides before-and-after
values or `not run`, identifies the artifact that produced each value and
states any deliberate regression with its stronger compensating result.

Prefer a somewhat longer source file that eliminates a resident table,
removes a dependency closure or establishes a buffer lifetime over a short
file that hides one of those costs. Reject byte savings that remove required
error handling, weaken an ABI contract or substitute a host result for the
target artifact.

**Keep the source legible. Make representations, dependencies, and lifetimes
small.**

## 13. Placement and maintenance

This document stays under `docs/research` while its rules remain under
review. Adoption so far:

- `AGENTS.md` carries the MPU correction from section 2, and the comment rule
  it reworded no longer names a character set.
- `docs/research/211bsd-patch-scope.md` carries the measurements behind the
  section 10 and section 11 entries added here.
- `sys/arch/rp2040/doc/TESTING.md` and the root Makefile carry
  `check-libc-scanf`, the gate section 11 names.

Rules in sections 5, 7, 8, 9 and 11 remain proposals until a change adopts
one and records the evidence it demanded. When the project adopts a rule:

- Put concise source and comment rules in `AGENTS.md`.
- Put a shipped mechanism's authoritative explanation under
  `sys/arch/rp2040/doc` and list it in that directory's research index.
- Put gate commands and proof boundaries in
  `sys/arch/rp2040/doc/TESTING.md` and the root Makefile.
- Keep measurements, option surveys and historical comparisons under
  `docs/research`, with their source revision and artifact identity.

Refresh the reviewed commit whenever a change alters language profiles,
memory layout, warning policy, multicall ownership, executable conversion or
root packing. A revision update reviews the intervening diff; it does not
silently relabel older measurements as current.

## 14. Reference map

| Subject | Authority |
| --- | --- |
| Project policy and comment shape | `AGENTS.md` |
| Cross-userland dialect, target CPU and linker path | `share/mk/sys.mk` |
| Full and legacy warning profiles | `share/mk/warnings.mk` |
| RP2040 kernel dialect and optimization baseline | `sys/arch/rp2040/conf/Makefile.rp2040` |
| SRAM and flash ownership | `sys/arch/rp2040/conf/RP2040.ld` |
| User executable address and sections | `lib/elf32-arm.ld` |
| Process-window constant | `sys/arch/rp2040/include/machparam.h` |
| MPU presence and behavior | RP2040 datasheet sections 2.4.1, 2.4.2.5 and 2.4.6; `sys/arch/rp2040/doc/DATASHEET-INDEX.md` |
| Weak-symbol float opt-in | `share/mk/sys.mk`, `lib/libc/stdio/doprnt.c`, `lib/libc/stdio/doscan.c` |
| Native Smaller C boundary | `usr.bin/smlrc/README.rp2040.md` and `usr.bin/smlrc/tests` |
| Constrained-C investigations | `docs/research/constrained-c.md` and `docs/research/smlrc-rp2040-tuning.md` |
| C17 and footprint case studies | `docs/research/netbsd-11-micro-backports.md` |
| 2.11BSD divergence and patch scope | `docs/research/211bsd-patch-scope.md` |
| Multicall lifetime proof | `sys/arch/rp2040/doc/MULTICALL-BSS-OVERLAY.md` |
| Verification tiers and proof limits | `sys/arch/rp2040/doc/TESTING.md` |
