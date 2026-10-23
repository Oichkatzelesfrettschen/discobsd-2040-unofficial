# C17 semantic migration plan

## RP2040 modernization execution contract

The maintained target is a tiny BSD on Cortex-M0+, ARMv6-M and Thumb-1.
The program modernizes the shipped kernel, libc, shell and utilities before
expanding to the remaining maintained ARM source and host tools. Production
uses GNU17; portable mechanisms also pass strict C17. The native Smaller C
implementation is maintained code, while its accepted input language remains
a separate compatibility contract. Full on-device C17 development stays in
the later DPUCP family.

The integration baseline is `42bb56fb10c0b2620b2e862fda52fc03241d512c`.
The DPUCP profile, donor ledger, allocation and line-reading libc members,
process/runtime interfaces, shell command and getopts, utility printf,
recovery utilities, timeout and multicall accounting are delivered foundations.
`dpucp-2024-board-acceptance.md` records seven surfaces and thirteen cases
on its identified image pair. The capture leaves the complete Issue 8 rows
partial. Revalidating the record does not execute the image again.

The source denominator at that baseline contains 2,590 C/header paths:
4 accepted, 64 migration-open, 543 excluded and 1,979 unreviewed. Those states
measure review accounting rather than independent defects. The shell already
passes its strict-C17 compilation gate; pointer-through-int transport and
runtime resource acceptance remain separate obligations. The finite DPUCP
ledger contains 1,518 Issue 8 rows, with one closed and 1,517 open.

### Architecture and resource invariants

- Preserve the 128 KiB kernel region, 144 KiB base process window, Dhara root
  partition and raw-swap allocation. Existing conditional window extensions
  retain their own admission contracts.
- Account for loaded text/data/BSS, alignment, simultaneous scratch, heap and
  stack. Packed file size does not establish process-window fit.
- Preserve syscall numbers, calling conventions and public layouts. Additive
  interfaces require separate kernel/libc contracts and measured acceptance.
- Preserve erase-sector ownership, program-page alignment, transaction commit
  points, recoverability and the meaning of measured storage counters.
- Require nonpositive growth on each constrained surface by default. A positive
  delta needs the existing growth-exception record and review; savings across
  distinct resource surfaces do not cancel.
- Retain fixed-table values and USIZE until the capacity corpus admits a change.
  The retained two-slot inode reserve and 220-byte stack-paint remainder are
  workload observations rather than available optimization budgets.
- Keep language, interface, security, native compatibility, compile footprint,
  runtime resources, persistence and hardware acceptance separate.

### Structural refactoring and header ownership

Headers, translation units and archive members may be split or consolidated
when ownership, dependency closure or measured target cost justifies the
boundary. Historical filenames retain provenance rather than architectural
authority. A structural change records its invariant before choosing its file
layout and rebuilds every affected consumer.

Public ABI headers own exported types, constants and declarations. Private
subsystem headers own internal contracts. Portable mechanism headers avoid
unnecessary kernel configuration and MMIO dependencies. RP2040 hardware
headers own access widths, alignment, ordering and initialization requirements.
Each ABI type has one authoritative definition. Generated syscall and assembly
interfaces derive from their owning description when generation prevents drift.
Public include names remain compatibility entry points when consumers require
them; compatibility wrappers do not duplicate declarations or storage.

Header gates exercise supported standalone inclusion, repeated inclusion,
include orders, exact function-pointer types, target sizeof/alignment/offsets,
assembly consumers and Smaller C inputs. Deliberately invalid fixtures have
expected-rejection contracts rather than a portable-C17 acceptance claim.

### Ordered modernization queue

The queue below owns program order. The historical ordered work queue later
in this document records delivered units and their residuals; it does not
restart those implementations. Each row expands into one caller-complete
mechanism per PR. A blocked row records its deciding experiment before the
next independent row starts.

| Order | Mechanism | Dependency | Deciding output |
| --- | --- | --- | --- |
| 1 | Baseline and document reconciliation | Identified source revision and retained records | Delivered work, bounded residuals and one current queue |
| 2 | Configuration-keyed shipped dependency closure | Evaluated commands, compiler dependencies, generated inputs, installed manifest and final links | Calibrated closure checker for PICO and PICO_UART, with archive/member and native-header ownership |
| 3 | Reproducible resource comparison | Clean matched-toolchain parent/candidate distributions | Artifact-bound kernel/SRAM/a.out/root/member receipts; runtime and wear states remain explicit |
| 4 | Header ownership boundaries | Shipped closure and resource baseline | Caller-complete headers with inclusion, ABI, assembly and native-input gates |
| 5 | Calibrated compiler and analysis coverage | Header contracts and exact production commands | GNU17 target and strict-C17 portable coverage, expected-rejection fixtures and scoped analysis |
| 6 | Shipped trust boundaries | Caller-complete interfaces and calibrated negative controls | Executable admission, copying, path, credential and parser invariants with target resource acceptance |
| 7 | Libc and public interfaces | Existing libc evidence reconciled; all consumers identified | One interface family per change, clean consumer rebuild and native-header acceptance |
| 8 | Typed shell execution input | Existing C17 shell behavior and parser ownership contracts | Typed string/argv and descriptor entry points; eval/source/trap/nesting/failure tests and resource receipts |
| 9 | Filesystem and archive identity | Additive directory-relative kernel/libc contracts | Separate compress, tar and cpio race/failure gates with bounded linked costs |
| 10 | RP2040 hardware and persistence ownership | Register/ROM/IRQ/buffer contracts and portable controls | PICO/PICO_UART builds; separately authorized silicon and recovery evidence |
| 11 | Remaining shipped utilities and multicall units | Shipped T1 obligations addressed before broad T3 cleanup | Shipped language/interface closure and lifetime-safe composition measurements |
| 12 | Remaining maintained source | Shipped-system milestone | Optional ARM, host tools, adapters and Smaller C implementation acceptance; justified exclusions and global closure |

`c17-shipped-build-baseline.md` records the implemented capture/closure/resource
foundation at `e7040aa1`. Its source and resource companions bind 580 tracked
C/header paths and 36 artifact groups to that revision. Shipped language
acceptance, unreviewed migration ownership/risk decisions, runtime measurements
and hardware qualification retain separate open states.

`shipped-archive-selection-comparison.md` extends the resource comparison from
libc/native projections to every closure archive, including kernel libgcc and
non-libc packed-program dependencies. Schema-1 records keep their historical
scope; complete new comparisons require schema-2 captures. The password-header
inclusion unit records its independent ABI/native and paired static acceptance
in `libc-passwd-header-inclusion.md`.

`syscall-dispatch-header-contract.md` records the generated callable
header boundary: generated handler declarations, compatible systm/glob entry
points, configuration-selected table checks and native-input acceptance.
The surrounding kernel declarations and handler implementations retain their
independent migration obligations.

`rp2040-usercopy-span-admission.md` records the first shipped user-pointer
admission unit. `copyin` and `copyout` now admit a complete non-wrapping span
before publication while preserving zero-length behavior, `EFAULT`, the
conditional process-window upper bound and their public ABI. Direct
`baduaddr` consumers and syscall stack-argument state retain separate units.

`rp2040-syscall-stack-argument-admission.md` records the caller-complete
syscall stack-tail unit. The RP2040 adapter clears the fixed six-word argument
array, preserves the four-register calling convention, copies a one- or
two-word tail through complete-span `copyin`, and withholds handler dispatch
after argument admission fails. Exception entry, signal-context copying and
the remaining translation-unit semantics retain separate obligations.

`rp2040-file-owner-control-admission.md` records the bounded file-owner unit.
The four `fcntl` and `ioctl` entries reject unrepresentable process-group
values before negation or dispatch, stage driver and caller payloads, and
publish results only after successful validation. General ioctl payloads,
flag rollback, conditional INET behavior and remaining descriptor semantics
retain separate obligations.

`rp2040-signal-context-admission.md` records the next RP2040 user-pointer
boundary. `sigreturn` rejects frame-offset wrap, copies exactly one complete
72-byte context into aligned kernel storage and publishes process state only
after that snapshot succeeds. The public context layout, syscall number,
trampoline convention and handler-visible register values remain unchanged.
Arbitrary restored xPSR contents, exception-return behavior, runtime kernel
stack peaks and physical hardware execution retain separate obligations.

`rp2040-elf-load-admission.md` records the flat ELF loader boundary. The
loader admits its one program header and file-backed segment against the
inode size, requires one RWX segment at the process base, validates the Thumb
entry inside file-backed bytes and completes every geometry check before
`exec_estab`. A segment read failure after commitment propagates a private
terminal state through recursive script execution; the syscall owner releases
exec resources before calling `exit(SIGKILL)` directly. Raw and packed a.out
loaders use the same terminal contract for their post-commit failures.
Section-header policy, runtime resource peaks and physical hardware execution
retain separate obligations.

A leaf unit may calibrate a compiler gate, but convenient leaf migration does
not displace shipped T1 trust boundaries. The shipped denominator follows real
build and link dependencies, including selected libc members, generated headers
and multicall overlays, rather than only the union of manifest source names.

Each PR records its contract, caller closure, baseline and falsifier; calibrates
its deciding test; implements the complete unit; runs behavior/compiler/ABI and
resource gates; updates the existing ledgers; and passes review and CI before
merge. Public-header or libc changes require a clean consumer rebuild. New
gates register with the root Makefile, TESTING.md and the CI owner. Use the
logged local entry points with caller-selected PYTHON and preserve the product
lock and cleanup/quarantine contracts.

Release states are reconciled baseline, modernized shipped system,
hardware-qualified shipped system and maintained-source closure. Each state
requires its own evidence. Hardware execution requires explicit authorization
for the exact operation and artifacts. Networking, regex/text facilities,
locale, threads, realtime, dynamic linking and full native C17 development
remain separately gated DPUCP families.

## Scope

`docs/research/c17-formatting-linting.md` measures the syntactic frontier:
where pre-prototype C remains, which warning profile each unit uses, and the
kernel ledger of K&R interfaces. That inventory is frontier zero. A prototype
is the entrance condition for a C17 migration, not the migration: several
units already landed in this tree changed representation, ownership and
algorithms because a modernized signature would have preserved the defect.

This plan turns the migration into finite rows with layer-specific exit
states. It does not restate the repository's taxonomy:

- `docs/research/STYLE-GUIDE.md` section 4 defines the language profiles
  (RP2040 kernel and port, modernized cross userland, historical cross
  userland, native Smaller C implementation, quarantined sources) and the
  C17 migration unit; section 4B defines the standards layers (ISO C17,
  GNU17, POSIX.1-2017, BSD/2.11 ABI, RP2040 project policy, native Smaller C
  language); sections 11 and 12 define verification and acceptance.
- `docs/research/security-c17-frontier.md` ranks the security residual
  frontier and records the measured C17 work register.
- `docs/research/memory-ownership-plan.md` records the resource ownership
  decisions that several rows depend on.

Every row below names its layer and profile from those sections. A claim that
belongs to the RP2040 project-policy layer (W^X, PIE, MPU coverage, overlay
lifetimes, flash wear) is not a C17 claim, even when the same commit carries
both; it is tracked in the hardening or resource ledger.

## Dialect rule

Strict C17 is not the universal production dialect.

| Code | Production command | Additional dialect gate |
| --- | --- | --- |
| Portable userland mechanisms and libc members | The evaluated GNU17 cross command with `WARNLEVEL=full` | GCC and Clang, `-std=c17 -pedantic-errors -Wall -Wextra -Werror`, on the host seam |
| Freestanding kernel, MMIO, assembly seams, target attributes | The exact GNU17 Cortex-M0+ kernel command for each configuration | Strict C17 only for an extracted portable helper with a host seam (as `check-dhara-metadata` and `check-namei-user-path` do) |
| Native Smaller C input | The on-device compiler | A Smaller C acceptance test; see below |
| Quarantined historical sources | None; byte-stable | Quarantine manifest only |

A unit leaves the language ledger when its production command passes the full
warning profile and, where it is portable, both compilers accept it strictly.
The kernel is never required to pass `-pedantic-errors` as a whole.

## Claim-specific evidence

The claim selects the evidence. A specification states the required interface;
source describes the implementation; an executable test establishes behavior
for its inputs; a board capture measures its exercised configuration. A
contradiction starts an investigation. Agreement among several source-derived
oracles does not settle a contradictory behavioral result.

| Question | Deciding surfaces and limits |
| --- | --- |
| Which source and dialect participate? | Evaluated build command, source classification, preprocessed translation unit and include dependencies for each configuration |
| Does an active interface compile correctly? | Compiler diagnostics with warning-option provenance, caller prototypes, target ABI and relocation inspection |
| Which bytes reach the image? | Archive membership, linked symbols and sections, a.out headers, selected manifest and packed-root block accounting |
| Does a contract hold? | Explicit behavioral expectations, failure injection and a rejected known-bad control; sanitizers cover their exercised operations |
| Does a silicon mechanism hold? | Register readback, privilege/interrupt state, deliberate fault or workload, and retained board capture |
| What remains outside active builds? | Calibrated text discovery and quarantine identity; DWARF and call graphs supplement source navigation |

`check-c17-kernel-inventory` already combines lexical discovery with compiler
diagnostics under the evaluated PICO and PICO_UART flags. Its union exposes
oracle disagreement. `check-kernel-printf-formats` uses bounded source parsing
alongside the compiler's format attributes. Preprocessing-backed discovery is
a remaining improvement for call/format extraction: macros, splices, aliases,
wrappers, included headers and conditional branches need explicit coverage.
Inactive branches retain a lexical witness because a configuration's compiler
does not see them.

## Risk tiers

Sanitizers and mutation controls are risk-driven, not mandatory per file.

| Tier | Members | Obligations |
| --- | --- | --- |
| T1 boundary | Syscall and user-pointer paths; executable loading; filesystem, Dhara and flash metadata; USB requests; archive, compression and other external-byte parsers; setuid and privilege-changing programs; libc machinery those reach (stdio, scanning, directory streams) | The STYLE-GUIDE C17 migration unit in full, plus: an executable host seam under AddressSanitizer and UndefinedBehaviorSanitizer including `-fsanitize=alignment`; a known-bad or pre-change falsifier that the gate rejects; malformed-input cases; exact target compile; layout and linked-closure checks; resource accounts; a hardware gate when a silicon behavior is claimed |
| T2 shared | Library members and kernel internals that are not boundaries | Exact production compile at the full warning profile; strict C17 under GCC and Clang where portable; behavioral tests; a sanitizer run where a host seam already exists; clean rebuild of linked consumers and a final-image size comparison |
| T3 leaf | Utilities that parse no external bytes beyond their own command line | Exact compilation at the full warning profile; focused behavioral tests; final a.out and packed-root comparison. Sanitizers only where a runnable host seam exists |
| Q quarantine | Licensed or byte-stable historical sources | Byte stability and provenance; surrounding maintained units take their own tier |

A row's tier comes from its most exposed caller. A libc member reached from a
T1 parser is T1 for that row even when most of its callers are T3.

## ARMv6-M alignment

Cortex-M0+ has no unaligned access support: a misaligned word or halfword
access is a HardFault, not merely undefined behavior.
`sys/arch/rp2040/doc/BOOT-MAP.md` lists an unaligned word access as an early
HardFault cause, and `sys/arch/rp2040/doc/CAPACITY.md` records that ARMv6-M
cannot trade alignment for a packed pointer record. A row that decodes
external data (on-disk records, a.out headers, USB packets, flash metadata)
owes:

- byte-wise or `memcpy` decoding of any field that may be unaligned;
- `-Wcast-align=strict` under GCC (Clang offers `-Wcast-align` only);
- `-fsanitize=alignment` on every executable host path;
- target layout (`offsetof`, `sizeof`, `_Static_assert` where the target
  compiler accepts it) and disassembly inspection of the decoding sequence;
- a board run or a calibrated architectural test where the runtime address
  alignment remains uncertain.

The qemu-user tier does not establish alignment behavior.
`usr.bin/smlrc/tests/run.sh` and `fuzz.sh` run `qemu-arm` with its default CPU
model, and `docs/research/static-analysis.md` already declined emulator
evidence for an alignment question. Until a known-bad unaligned fixture shows
the configured emulator faulting, qemu-user results carry no alignment claim.

## Native Smaller C rule

"The compiler implementation is written in C17" and "the native compiler
accepts C17" are separate claims (STYLE-GUIDE section 4, Native Smaller C
implementation). `docs/research/netbsd110-tinyspace.md` records that Smaller
C's acceptance of `_Static_assert` and designated initializers is unmeasured.
`_Static_assert`, designated initializers, compound literals, `_Alignas`,
`_Generic` and other C99/C11 constructs enter native-compiler input -- a
header a natively compiled program includes, or a source the on-device
toolchain builds -- only after a Smaller C acceptance test compiles, links and
runs the construct under the native suite. Until then they stay in
cross-built translation units.

Every row records whether any of its files is native-compiler input. A libc
header change is native input whenever a natively compiled program can
include it.

## Row schema

| Field | Content |
| --- | --- |
| Unit | Files or library members, one independently testable component |
| Layer and profile | STYLE-GUIDE section 4B layer(s) and section 4 profile |
| Standards authority | Publication edition, volume/clause, required option or profile, project disposition and any historical ABI conflict |
| Tier | T1, T2, T3 or Q, from the most exposed caller |
| Native input | Whether any file is Smaller C input, and its acceptance test |
| Contract | Inputs, outputs, ownership, failure and errno preservation, ABI representation, caller closure |
| Invariants | Bounds, lifetime, transaction and commit points, alignment, privilege, malformed-input behavior |
| Falsifier | The known-bad or pre-change input the gate must reject, and how it was calibrated |
| Gates | Make targets and what each establishes |
| Resources | Kernel flash, resident RAM, process peak, stack frame, packed-root blocks, flash traffic and wear: values, or `unmeasured` |
| Evidence class | Build, host, cross, qemu-user, Renode or board, per claim |
| Ledger states | One entry per ledger below |
| Residuals | Each open item with an owner and its deciding gate |

## Completion ledgers

The migration is not one "done" state. Each ledger closes separately:

1. **Language.** Every maintainable translation unit is classified; complete
   call contracts exist; implicit declarations and old-style definitions are
   absent; the production GNU17 command passes the full warning profile;
   portable mechanisms pass strict C17 under GCC and Clang; legacy overrides
   are removed or explicitly quarantined.
2. **Interface.** Each exported contract records inputs, outputs, ownership,
   failure preservation, ABI representation and caller closure.
3. **Native toolchain.** Every claimed Smaller C input compiles, links and
   passes its applicable native, qemu or board test.
4. **Hardening.** Each ranked trust boundary has explicit bounds, lifetime,
   transaction, alignment, privilege and malformed-input invariants with a
   rejecting falsifier.
5. **Resource acceptance.** Keep separate states for compile-footprint
   acceptance, measured runtime peak, and measured storage traffic/wear.
   Recording an unmeasured residual completes its documentation; the
   corresponding measurement remains open. Historical seed rows that say
   resource done cover only their enumerated measurements.
6. **Program closure.** Every classified row has reached its layer-specific
   exit state, and every residual has an owner and a deciding gate.

The language ledger's frontier is the existing inventory: the kernel ledger in
`tools/c17/kernel-ledger.txt` (195 definitions and 4 declarations at
`e3aa6d38`) and 140 `WARNLEVEL=legacy` Makefiles, all outside `tests/`.

## Ordering of unseeded rows

Rows are classified in this order, and within a group by the cost of their
evidence:

1. T1 units in the shipped RP2040 manifest, starting with the open P1 rows of
   the ranked frontier in `security-c17-frontier.md` (executable loading and
   syscall copying, the `compress` descriptor lifecycle, USB reset execution
   evidence).
2. T1 units outside the manifest that a manifest change could ship, such as
   the source-only setuid utilities (P2 there).
3. T2 units whose resource account touches kernel BSS, the process window or
   flash traffic.
4. T3 units with `WARNLEVEL=legacy`, grouped by directory so one consumer
   rebuild covers several rows.

## Seed rows

The seven rows below record bounded migrations with repository evidence. They
show what a reviewed row looks like and which of its ledgers remain open.

### R1 -- LZW decoder (`usr.bin/compress`)

| Field | Content |
| --- | --- |
| Layer and profile | ISO C17 and POSIX utility behavior; modernized cross userland |
| Tier | T1: decodes untrusted compressed input |
| Native input | No |
| Contract | Decoded output or a nonzero exit; corrupt named input keeps the archive and removes partial output |
| Invariants | A code lies at or below the one permitted KwKwK frontier; a dictionary reference lies below `free_ent`; each prefix strictly decreases; every stack write stays below the end of `htab`; magic, header and code width 9 through 12 validated before the width shifts |
| Falsifier | A generated mutation removing the four dictionary and stack checks; AddressSanitizer must reproduce the `htab` overflow on the recorded 12-bit stream |
| Gates | `check-compress-host` |
| Resources | Object text 4,657 to 4,814 (+157), data -24, BSS -4; final a.out 13,452 to 13,572 (+120); packed root blocks 12 to 12; no second arena (the decoder stack still overlays `htab`) |
| Evidence class | Host and cross; Cortex-M0+ cycles and board peak water unmeasured |
| Ledger states | 1 language: done (strict C17 host, `WARNLEVEL=full`). 2 interface: open, not recorded (the command-line contract is stated; caller closure is not). 3 native: n/a (no native input). 4 hardening: done for the decoder. 5 resource: done. 6 program closure: open, with the residual below |
| Residuals | Descriptor lifecycle: `stat`, `freopen`, then pathname `chmod`/`chown`/`utimes`/`unlink` admit rename and symlink races. Owner: the P1 frontier row; deciding gate: a competing-rename and symlink harness with failure injection |

Source: `security-c17-frontier.md`, "LZW memory-safety repair" through "Size,
RAM and runtime evidence".

### R2 -- formatted input scanner (`lib/libc/stdio/doscan.c`, `doscan_float.c`, `ungetc.c`)

| Field | Content |
| --- | --- |
| Layer and profile | ISO C17 library semantics (7.21.6.2) and the established BSD/2.11 ABI; the separate stdio core unit owns the 24-byte `FILE` layout; modernized cross userland |
| Tier | T1: `sysctl -w kern.hostid=<digits>` reaches `%ld` conversion with caller-written input |
| Native input | The libc members are cross-built. `t18_stdio` accepts the expanded `include/stdio.h` declarations and exercises a Smaller C scanning consumer; on-device preprocessing and board execution retain separate states |
| Contract | The project defines integer-conversion saturation at the destination limit as an extension; C17 does not generally define an unrepresentable scanf conversion result. Separately, `%X` and uppercase floating conversions follow C17 lowercase semantics; `%D` and `%O` remain as documented extensions; `%n`, `%i`, `%p` and the C99 length modifiers are supported |
| Invariants | No staging buffer on the integer path; a 32-byte automatic scanset bitmap with nothing carried between directives; float staging bounded by `MANT_DIGITS` with `_Static_assert` ties to `DBL_DIG` |
| Falsifier | The replaced scanner aborts under the sanitizer, first on `%X` storing a long through `unsigned int *`, then on a digit run overrunning the 64-byte `_innum` staging buffer |
| Gates | `check-libc-scanf` (host width, ILP32 and AddressSanitizer), plus `check-c17-smaller-c` for the expanded-header/native consumer boundary |
| Resources | `doscan.o` text +176, data -256; frame 128 to 120 bytes; over 244 programs built both ways, 42 changed and the sum is -1,096 bytes; `trek` and `primes` gain about 1.3 KiB by linking the float scanner |
| Evidence class | Host and cross |
| Ledger states | 1 language: done. 2 interface: done. 3 native: expanded-header parser/codegen and exercised scanning paths covered by `t18_stdio`; on-device preprocessing and board execution remain open. 4 hardening: done. 5 resource: done. 6 program closure: open, with the residual below |
| Residuals | Repeat the expanded-header/native consumer gate after public-header changes. On-device preprocessing, board execution and full native C17 development remain separate obligations. The pushback slot belongs to the stdio core unit (`stdio-core-c17-unit.md`) |

Source: `211bsd-patch-scope.md`, "The first migration unit: `_doscan`".

### R3 -- directory streams (the five `DIR` members)

| Field | Content |
| --- | --- |
| Layer and profile | ISO C17, POSIX directory interfaces and the BSD/2.11 ABI; modernized cross userland |
| Tier | T1: parses directory records read from the filesystem |
| Native input | `t18_stdio` consumes expanded `sys/dir.h` and checks the 1,040-byte `DIR` layout through Smaller C; the fixture starts after host preprocessing |
| Contract | Allocation failure preserves `errno`; free entries are skipped; cookies need no allocation; a failed seek leaves the stream unchanged; close owns the buffer |
| Invariants | A 1 KiB directory block held intact; record lengths and names bounded before use; `telldir` is arithmetic only |
| Falsifier | The pre-change source fails strict C17 at its K&R definitions before the layout fixture rejects the 1,108-byte stream |
| Gates | `check-dirent-contracts`, `check-dirent-contracts-cross` |
| Resources | `sizeof(DIR)` 1,108 to 1,040 (-68 heap bytes per open directory); five-member text +80 |
| Evidence class | Host and cross; board enumeration unmeasured |
| Ledger states | 1 language: done. 2 interface: open in this row; the scanner, directory admission/CLOEXEC and close-result units (continuation item 7) carry the calibrated interface evidence. 3 native: expanded declarations and target layout covered by `t18_stdio`; on-device preprocessing and board execution remain open. 4 hardening: open (the six malformed-record classes are not calibrated against the pre-change reader). 5 resource: done. 6 program closure: open, with the residual below |
| Residuals | The recorded falsifier is syntactic and layout-based; the six malformed-record classes are not recorded as calibrated against the pre-change reader. Owner: this row; deciding gate: a pre-change behavioral run of those cases. Board cross-block cookie replay: owner, the hardware gate. The scanner, directory admission/CLOEXEC and integer close-result units below supply separate calibrated interface evidence |

Source: `bsd-workspace-directory-stream-c17.md`.

### R4 -- syscall pathname copy (`sys/kern/ufs_namei.c`)

| Field | Content |
| --- | --- |
| Layer and profile | GNU17 freestanding kernel and the BSD/2.11 errno ABI; RP2040 kernel profile, with the copy helper compiled strictly on the host seam |
| Tier | T1: syscall user-pointer boundary |
| Native input | No |
| Contract | User paths use `copyin`; kernel-owned paths keep `copystr`; every kernel initialization states its origin; operation-only resets preserve the pathname and its origin |
| Invariants | An invalid pointer or a missing NUL at the process-window boundary returns `EFAULT`; an in-range terminator at the boundary succeeds; no terminator within `MAXPATHLEN` returns `ENOENT` |
| Falsifier | Direct user copying, a dropped reset origin, a changed caller origin or pathname, a direct field write, or an unexpected caller fails its calibrated oracle |
| Gates | `check-namei-user-path` (finite caller inventory plus strict-C17 address and undefined-behavior sanitizers), clean PICO and PICO_UART dependency/resource captures |
| Resources | PICO kernel flash is unchanged; PICO_UART kernel flash decreases eight bytes; both SRAM maps, all shipped programs, packed-root occupancy, native artifacts and archive selections are unchanged. Fourteen affected objects retain identical sections, symbols and static frames; both `vfs_vnops.c` objects lose four text bytes while retaining data, BSS and static frames |
| Evidence class | Host and exact kernel build |
| Ledger states | 1 language: done for the bounded helper and header seam; remaining translation-unit semantics stay migration-open in the source denominator. 2 interface: done for the unchanged ABI representation and all 30 kernel transitions. 3 native: n/a (no native input). 4 hardening: done for the helper and origin boundary. 5 resource: done for the declared static surfaces; runtime stack, heap and storage traffic remain unmeasured. 6 program closure: done for the pathname-origin mechanism |
| Residuals | The broader lookup traversal, name-cache union semantics and caller translation units retain separate migration units. Protection beyond software bounds checks needs MPU fault evidence (`MPU.md`). Runtime resource peaks and physical-board execution require their own authorized workloads |

Source: `rp2040-namei-path-origin-contract.md`, `TESTING.md`
(`check-namei-user-path`) and `security-c17-frontier.md`, "Ranked residual
frontier".

### R5 -- Dhara page metadata (`sys/arch/rp2040/dhara/journal.c` and `map.c`, raw NOR callbacks in `sys/arch/rp2040/dev/flash.c`)

| Field | Content |
| --- | --- |
| Layer and profile | GNU17 kernel and RP2040 project policy; RP2040 kernel profile with strict C17 for the extracted reader and predicate |
| Tier | T1: persistent metadata read back from flash |
| Native input | No |
| Contract | Metadata reads validate the device page range, reject checkpoint slots and check the slice against the NAND page before any buffered copy or NAND read; raw callbacks check the page index before forming an address |
| Invariants | Subtraction-based byte-range checks with no `offset + length` overflow |
| Falsifier | A validator-bypass mutation reproduces the original checkpoint-slot stack-buffer overread (52 bytes past `journal.page_buf`) |
| Gates | `check-dhara-metadata`, `check-flash-device-bounds` |
| Resources | Journal object text 2,608 to 2,710, data and BSS unchanged; `dhara_journal_read_meta` frame 32 to 40 bytes |
| Evidence class | Host and exact kernel build |
| Ledger states | 1 language: open for `journal.c` and `map.c`; the extracted reader and predicate are strict C17. 2 interface: open, not recorded. 3 native: n/a (no native input). 4 hardening: done for the reader and predicate. 5 resource: done at compile time. 6 program closure: open, with the residual below |
| Residuals | Runtime stack high-water, physical flash behavior and unprivileged control of corrupted QSPI metadata. Owner: the hardware gate and the storage plan |

Source: `security-c17-frontier.md`, "Dhara page metadata bounds".

### R6 -- erase-aligned flash staging (`sys/arch/rp2040/dev/flash.c`, `sys/kern/subr_rmap.c`, swap and SwapRAM)

| Field | Content |
| --- | --- |
| Layer and profile | RP2040 project policy (resource ownership), not a C17 language row; RP2040 kernel profile |
| Tier | T1: persistent storage integrity across process images |
| Native input | No |
| Contract | Every live process image owns every 4 KiB sector its writes erase; process writes stream through a 256-byte program page; `/dev/tempN` rewrites copy through the reserved first swap sector |
| Invariants | 4 KiB erase alignment, 256-byte program alignment, one-to-zero programming, adjacent-image preservation, all-or-none allocation |
| Falsifier | Not recorded as calibrated: the host NOR model enforces the invariants, but no known-bad allocator or driver is named |
| Gates | `check-flash-swap`, `check-swapram` |
| Resources | Kernel BSS -3,840 bytes in both configurations; text +732 (PICO) and +724 (PICO_UART); `flscratch` 256 bytes; storage cost of the first 4 KiB swap sector plus zero to three padding blocks per image (12 KiB in the eight-image fixture) |
| Evidence class | Host model and linked kernels; hardware flash unmeasured |
| Ledger states | 1 language: n/a (a project-policy row, not a C17 language row). 2 interface: open, not recorded. 3 native: n/a (no native input). 4 hardening: open. 5 resource: done. 6 program closure: open, with the residual below |
| Residuals | A rejecting falsifier for the NOR model (owner: this row; deciding gate: a mutation that shares a sector between images and must fail the model) and attended hardware flash validation |

Source: `memory-ownership-plan.md`, "Physical-page flash staging and
erase-aligned swap images".

### R7 -- kernel credential transitions (`sys/kern/kern_prot.c`, `kern_prot2.c`)

| Field | Content |
| --- | --- |
| Layer and profile | GNU17 freestanding kernel and the established BSD credential and syscall ABI; RP2040 kernel profile |
| Tier | T1: privilege-changing syscalls and the shared kernel privilege predicate |
| Native input | No |
| Contract | `setuid`, `seteuid`, `setgid`, `setegid`, and `setgroups` finish admission before publication; every refusal preserves all UID, GID, process-UID, and group-slot state; `suser()` admits effective UID zero and otherwise records `EPERM` |
| Invariants | Syscall numbers, argument counts, public layouts, 32-bit `uid_t`/`gid_t` representation, BSD real/saved-ID rules, effective GID in `u_groups[0]`, and `NOGROUP` termination remain unchanged; `setgroups` stages and validates the complete vector before publishing it |
| Falsifier | Fifteen independent mutations widen process groups, corrupt `getpgrp`, admit the group sentinel or a non-root caller, bypass each privileged setter, retain replaced saved IDs, leave `p_uid` stale, change the real GID through `setegid`, publish groups before validation, or retain the old tail; each must fail the 333-assertion credential fixture with status 1 |
| Gates | `check-kernel` (strict prototype diagnostics, behavior, mutation runner, address/undefined-behavior/alignment sanitizers), `check-kernel-ilp32` (signal and filesystem callers), `check-kernel-metadata`, `check-c17-kernel-inventory`, and exact PICO/PICO_UART production builds |
| Resources | Clean parent `4514cd81` and implementation `34df4b41` schema-2 receipts report zero delta for both kernels, all shipped a.out and packed-root surfaces, native artifacts, and every archive/member selection. Affected-object text moves 32 bytes from `ufs_fio.o` to `kern_prot2.o` with zero combined or linked delta; exact-command static frames remain unchanged. Runtime stack and heap peaks remain unmeasured |
| Evidence class | Host, sanitizer, ILP32 and exact target build; physical-board execution unrun |
| Ledger states | 1 language: done for both credential translation units and the focused fixture. 2 interface: done for the preserved syscall and privilege contracts. 3 native: n/a. 4 hardening: done for the exercised credential transition boundary. 5 resource: static constrained surfaces accepted with zero delta; runtime peaks remain open. 6 program closure: done for the bounded credential handlers, with the residuals below assigned separately |
| Residuals | Negative `getgroups` capacity, zero-length group-list semantics, `setreuid`, `setregid`, and `initgroups` policy are separate units. The broad kernel headers, remaining `ufs_fio.c` semantics, runtime resource peaks, MPU behavior and physical-board execution retain separate evidence states |

Source: `rp2040-credential-transition-contract.md` and `TESTING.md`
(`check-kernel`, `check-kernel-ilp32`).

### R8 -- syscall I/O buffer spans (`sys/kern/sys_generic.c`)

| Field | Content |
| --- | --- |
| Layer and profile | GNU17 freestanding kernel and the established BSD syscall/uio ABI; RP2040 kernel profile with the span predicate extracted under strict C17 |
| Tier | T1: direct syscall user-pointer boundary before filesystem, pipe, device or tty transfer |
| Native input | No |
| Contract | `read`, `write`, `readv` and `writev` admit every complete nonempty user span before `setjmp`, file-operation dispatch, offset movement or transfer publication; empty vectors retain their existing behavior |
| Invariants | Syscall numbers and arguments, `iovec`/`uio` layouts, sixteen-vector capacity, descriptor and access checks, `SSIZE_MAX` error precedence, short transfers and interruption behavior remain unchanged |
| Falsifier | Removing the admission pass or reducing it to a first-byte check must exit with the 226-assertion fixture's named refusal failure after the inherited source reaches the file-operation stub on invalid scalar and final-vector cases |
| Gates | `check-kernel`, required `check-kernel-ilp32`, `check-rp2040-usercopy-span`, `check-c17-kernel-inventory`, `check-kernel-metadata`, exact PICO/PICO_UART compilation and maintained STM32 compilation |
| Resources | Clean parent `6f7cce1a` and implementation `838270f0` schema-2 receipts report PICO flash +40 bytes and PICO_UART flash +56 bytes; all static SRAM regions, shipped a.out and packed-root surfaces, native artifacts and archive selections are unchanged. `rwuio` gains 44 text bytes and retains its 32-byte frame; PICO_UART's callable span service adds eight object-text bytes and changes the `copyin`/`copyout` static frames from 8 to 24 bytes. The bounded exception accepts these linked deltas for integration; runtime stack high-water remains open |
| Evidence class | Host, sanitizer, ILP32 and exact target build; physical-board execution unrun |
| Ledger states | 1 language: done for the five typed definitions and focused fixture; remaining `sys_generic.c` semantics stay migration-open. 2 interface: done for the unchanged read/write ABI and callable span service. 3 native: n/a. 4 hardening: done for the four syscall entries. 5 resource: measured and accepted with a bounded positive flash/frame exception; runtime peaks remain open. 6 program closure: done for the bounded syscall I/O seam |
| Residuals | Direct addresses in `ioctl`, `/dev/mem` and `ptrace` remain separate boundaries. `ptrace` first needs an ARM writable-register and PSR policy. Select/socket semantics, wider uio-origin tracking, broad kernel declarations, runtime peaks, MPU behavior and physical-board execution retain separate evidence states |

Source: `rp2040-syscall-io-span-admission.md` and `TESTING.md`
(`check-kernel`, `check-kernel-ilp32`, `check-rp2040-usercopy-span`).

### R9 -- file-owner control admission (`sys/kern/kern_descrip.c`, `sys_generic.c`)

| Field | Content |
| --- | --- |
| Layer and profile | GNU17 freestanding kernel and the established BSD file-control/ioctl ABI; RP2040 kernel profile |
| Tier | T1: caller-selected owner values and driver-produced process groups cross the syscall boundary |
| Native input | No |
| Contract | `F_SETOWN` and `FIOSETOWN` validate a PID or negative process-group representation before negation and dispatch; `F_GETOWN` and `FIOGETOWN` publish only a successful representable driver result; ioctl payloads cross through complete-span `copyin`/`copyout` |
| Invariants | Syscalls 54 and 92, command values, public layouts, descriptor/access precedence, positive-PID lookup, negative-group convention and driver errors remain unchanged |
| Falsifier | Restoring unconditional get-owner publication or direct ioctl user-pointer dereferences must fail the 36-check fixture with assertion status 1. Restoring unchecked negation must terminate with process status 1 or `SIGABRT` and emit the signed-negation diagnostic when the undefined-behavior sanitizer runtime is admitted; the gate records a skip otherwise |
| Gates | `check-kernel`, required `check-kernel-ilp32`, `check-c17-kernel-inventory`, `check-kernel-metadata`, exact PICO/PICO_UART compilation and maintained STM32 compilation |
| Resources | Clean parent `a740bc40` and implementation `290fd394` schema-2 receipts report +96 flash bytes in each kernel and zero delta for every static SRAM region, shipped a.out, packed-root surface, native artifact and archive/member selection. `kern_descrip.o` and `sys_generic.o` each gain 48 text bytes; `fgetown()` and `ioctl()` each gain eight static frame bytes. The bounded exception accepts those costs; runtime peaks remain open |
| Evidence class | Host, sanitizer, ILP32 and exact target build; physical-board execution unrun |
| Ledger states | 1 language: done for the five typed definitions and focused fixture; remaining translation-unit semantics stay migration-open. 2 interface: done for the four preserved owner-control entries. 3 native: n/a. 4 hardening: done for the exercised value, copy and publication boundary. 5 resource: measured and accepted with a bounded positive flash/frame exception; runtime peaks remain open. 6 program closure: done for the bounded owner-control seam |
| Residuals | General ioctl payloads, `FIONBIO`, `FIOASYNC`, `F_SETFL` rollback, descriptor duplication and locking, and conditional INET owner handling remain separate units. `/dev/mem`, `ptrace`, broad kernel declarations, runtime peaks, MPU behavior and physical-board execution retain separate evidence states |

Source: `rp2040-file-owner-control-admission.md` and `TESTING.md`
(`check-kernel`, `check-kernel-ilp32`).

## Open measurements this plan depends on

- Whether `qemu-arm` with an ARMv6-M CPU model faults on an unaligned access:
  a known-bad fixture under the configured emulator decides it.
- Smaller C acceptance of `_Static_assert`, designated initializers and
  compound literals: a native-suite compile, link and run decides each.
- Whether `-Wcast-align=strict` is clean across the RP2040 kernel
  configurations: an exact kernel compile with the option added decides it.

## Standards and continuation scope

The source review uses `8711f637e9cdefd7f1aa3628bd6262892673ebc0` as its base.
The review covers build ownership, the current documentation graph, fixed
resource accounts, directory interfaces and the stdio lifetime path. The
whole-tree syntax inventory remains discovery evidence; the review does not
establish semantic closure for every C file.

The [Issue 7 publication](https://pubs.opengroup.org/onlinepubs/9699919799/)
records POSIX.1-2017, following POSIX.1-2008. The
[Issue 8 publication](https://pubs.opengroup.org/onlinepubs/9799919799/)
records POSIX.1-2024. SUSv4 and SUSv5 work uses these explicit publication
boundaries, with XSI and option requirements recorded separately. A selected
interface passing its tests establishes that interface's exercised behavior;
a certification or whole-system conformance claim requires its own scope.

Preserve the Issue 7 shell judgments and add an Issue 8 delta column. A small
implementation can satisfy an interface with fewer states or allocations;
resource limits do not erase normative requirements. An unsupported option
needs an explicit disposition and truthful feature/limit reporting.

### Component walkthrough

| Surface | Connected mechanisms | Current deficiency or deciding boundary |
| --- | --- | --- |
| Maintained `.c` and `.h` | `share/mk/sys.mk`, warning profiles, `tools/c17`, public headers and callers | GNU17 selection alone leaves legacy warning overrides and old-style interfaces; header changes also reach Smaller C input |
| `.S`, linker scripts and generated headers | ARMv6-M entry/exception paths, `proc_asm.h`, Pico SDK register interfaces, boot ROM calls | Compiler acceptance leaves alignment, privilege and silicon behavior as separate obligations |
| libc members | ELF archive, converted a.out archive, selected board-native archive, multicall consumers | A smaller object benefits only consumers whose link includes it; an added dependency can outweigh local savings |
| `FILE` and `DIR` | Descriptors, buffer allocation, pushback/cookies, close/reopen and kernel I/O | Stream count differs from descriptor identity; directory APIs retain historical signature and failure-handling gaps |
| Kernel fixed tables | NBUF, compact inode fields, single UFS root, process window and swap | Existing reductions already land; a further capacity reduction needs workload high-water and failure evidence |
| Manifests and multicall templates | Installed names, shared text, mutually exclusive applet BSS, packed a.out | Source presence and utility names leave options, errors and shell semantics unproven |
| Makefiles, shell and Python gates | Root target, TESTING contract, execution inventory and CI invocation | A skipped optional branch establishes a skip; mutation/control execution must reach the claimed code |
| Research and evidence documents | `docs/INDEX.md`, this roadmap, retained audit ledgers and per-unit measurements | Historical surveys contain superseded layouts and availability lists; date/revision boundaries preserve their meaning |

### Ordered work queue

Each numbered item is one reviewable output. Implementation repeats the
listed compile, behavior and footprint checks for one bounded unit before
moving to another unit. Hardware-dependent rows stop at the documented opt-in.

1. **Pin the stdio ownership counterexample.** Exercise a legal descriptor
   above the static stream count at ILP32; retain the sanitizer report and
   the source revision. Output: a calibrated failing baseline.
2. **Repair unbuffered byte ownership.** Reuse `FILE._ub`, remove the global
   side allocation, retain the 24-byte ABI and exercise high descriptors,
   independent pushback, EOF/error, update mode, close/reopen and allocation
   failures. Output: `stdio-unbuffered-ownership.md` and `check-libc-ansi`.
3. **Accept the linked stdio footprint.** Clean libc and all consumers;
   compare object text/data/BSS, a.out extents, board archive, available
   kernel baselines and root blocks. Record PICO_UART build coverage
   separately when a paired baseline is unavailable. Record compile-time and runtime states
   separately. Output: the ownership document's measured table.
4. **Complete the utility-name ledger.** `sus-utility-ledger.json` resolves
   107 names against the selected manifest, multicall dispatch, Bourne-shell
   builtin table, source owners and named behavior gates. `posix-utilities.md`
   marks its 96 KB/840 KB/separate-binary baseline historical. A `csh`
   builtin never closes a Bourne-shell requirement. Output: the current
   name/owner ledger and its calibrated checker.
5. **Complete reproducible API discovery.** `bsd44-audit/posix_inventory.py`
   accepts explicit validated roots and rejects a missing root or required
   subtree. Paired fixtures distinguish declaration-only candidates from a
   definition under a different source basename. Declaration, source symbol,
   linkage and behavior states remain independent. Output: deterministic,
   path-independent candidate JSON for reviewed gap dispositions.
6. **Complete bounded standards contracts.** `sus-edition-contracts.md` pins
   selected Issue 7 and Issue 8 pages, option marks, edition deltas, owners,
   deciding gates and residuals. BSD extensions remain separate, and feature
   or limit values establish only the cases their gates exercise. Output:
   bounded edition-specific contracts.
7. **Close directory allocation/error contracts.** Split `scandir` from
   `opendir`/`closedir`: first test fstat/read/allocation failure cleanup,
   selected-entry capacity, compact record length and comparator types;
   then decide the historical close-result ABI and directory/CLOEXEC rules.
   The scanner unit supplies typed declarations, transactional publication,
   complete failure cleanup, selected-entry growth, compact self-describing
   records and per-call heap sorting. `check-scandir-contracts` and its cross
   gate calibrate those contracts; `libc-scandir-allocation-contract.md`
   records paired resources and remaining edition boundaries. The directory
   admission unit validates the opened descriptor, sets FD_CLOEXEC before
   allocation and preserves failure errno; `check-dirent-contracts` and its
   cross gate calibrate the unit. `libc-opendir-directory-cloexec.md` records
   its measured stack and linked costs. The close-result unit returns one
   close attempt, preserves errno through free and calibrates its result and
   ownership with four mutations. `libc-closedir-result-contract.md` records
   the source/ABI transition and paired resources. The separate `getwd`
   lifetime unit copies each borrowed name before stream release;
   `libc-getwd-entry-lifetime.md` records its calibrated regression and
   the independent historical bounds/metadata limitations.
   Output: one independently tested interface change per PR.
8. **Resolve case-folding semantics.** Compare all byte pairs under signed
   and unsigned `char`; record the inherited high-bit ASCII mapping and
   POSIX C-locale requirements before replacing `strcasecmp`'s 256-byte
   table. Measure login and other linked consumers. Output: a documented
   mapping decision and exhaustive finite oracle. The ASCII-only implementation
   passes both signedness variants, boundary cases and calibrated mutations;
   `libc-ascii-casefold-contract.md` records 240-byte load-image savings in
   each of 12 linked consumers and the high-byte compatibility decision.
9. **Audit formatted-output scratch ownership.** Establish return counts,
   final-flush failures and `_bufsiz` initialization in `fprintf`,
   `vfprintf` and `fputs`; measure their 1,024-byte temporary buffers.
   Evaluate a smaller chunk only with write-count and error-state evidence.
   Output: corrected wrapper contracts before a stack-size optimization.
   The wrapper unit centralizes fprintf through vfprintf, isolates borrowed
   descriptor batching in a temporary FILE, preserves original buffer
   ownership and propagates fputs failures. The selected board archive
   carries the new vfprintf dependency and a calibrated a.out link gate.
   The formatter unit checks count/width/precision arithmetic, stops real
   stream failures, counts truncated strings in bounded time and propagates
   printf/sprintf results. `stdio-formatter-count-contract.md` owns its
   calibrated gates and resource account. The private batch capacity becomes
   256 bytes after a 128/256/512/1024-byte comparison; wrapper frames fall
   from 1080 to 312 bytes with explicit write-count and chunk-failure gates.
   `stdio-output-batch-capacity.md` records the tradeoff and hardware residuals.
10. **Migrate one kernel interface family.** Select a caller-closed group
    from `tools/c17/kernel-ledger.txt`; compare PICO/PICO_UART diagnostics,
    public declarations, pointer widths and format arguments. Preserve
    Pico SDK register/ROM conventions and target attributes. Output: a
    reduced calibrated ledger and unchanged or explained linked footprint.
    The selected family is uiomove/ureadc/uwritec/uiofmove in kern_subr.c.
    Typed definitions match the public prototypes, preserve their bodies and
    carry an old-style-definition rejection gate.
    `kernel-uio-typed-interfaces.md` records caller preconditions and evidence.
11. **Validate native-header inputs.** Compile, link and execute a Smaller C
    stdio consumer; separately probe each proposed new header construct. The
    `t18_stdio` fixture now consumes expanded `stdio.h` and `sys/dir.h`, pins
    both target layouts, executes stdio ownership/scanning/bounds behavior and
    reaches the intended archive members. A copied-archive `filbuf.o` mutation
    calibrates rejection. The host scanner gate also closes the formerly
    declared-only `vscanf`, `vfscanf` and `vsscanf` symbols. The receipt starts
    after host preprocessing; on-device preprocessing and board execution stay
    open. Rerun the fixture after every admitted public-header change and once
    more after items 7 through 10. Output: native acceptance receipts,
    distinct from GNU17 cross builds. The final hosted rerun after item 10
    passes all 18 Smaller C programs and the damaged/restored archive control;
    `kernel-uio-typed-interfaces.md` records that acceptance boundary.
12. **Measure capacity and persistence on the board.** After explicit opt-in,
    select the exact image/configuration and collect process/kernel stack,
    table high-water, allocation failure and storage traffic under a named
    workload. Repeat recovery tests where flash behavior changes. Output:
    measured limits and falsifiers. The repaired shared-break image completes
    the named stdio workload, the 12-stream spillover probe and a quiet
    post-probe interval on the RP2040. All 43 checked workload commands return
    zero, the capacity and storage snapshots remain unsaturated, all fixed-table
    failure counters and checkpoint errors remain zero, and the shell remains
    responsive. `board-capacity-acceptance.md` records the image hashes,
    sampled extents, table high-water marks and storage deltas. The broader
    exhaustion, deliberate-fault and recovery corpus remains the deciding
    evidence for any limit or flash-policy change.

Items 1 through 3 form the bounded implementation in
`stdio-unbuffered-ownership.md`. Items 4 through 6 form the checked discovery
and edition-contract layer in `sus-utility-ledger.json`,
`posix-utilities.md`, `bsd44-audit/posix_inventory.py` and
`sus-edition-contracts.md`; `check-sus-discovery` calibrates that layer.
Item 7 has tested scanner, directory admission/CLOEXEC and close-result units.
The getwd lifetime unit closes the borrowed-entry defect; its independent
bounds and metadata limitations remain documented. Item 8 records the exhaustive mapping decision and paired resource reduction.
Item 9 completes wrapper, formatter and 256-byte batching units. Item 10
completes the four typed uio definitions; item 11 passes its final hosted
Smaller C rerun. Item 12 completes its named physical-board workload after the
account-offset and shell shared-break repairs. Operator login, wheel `su`, all
stdio extent probes, the 12-stream spillover probe and the post-probe quiet
window pass in one retained capture. The item measures one bounded workload;
it does not admit smaller fixed tables, a smaller kernel stack or a wear claim.
`board-capacity-acceptance.md` defines the sampled-process-stack probe,
its calibration, image boundaries, observed results and remaining capacity
decision corpus. The source classification program in
`c17-formatting-linting.md` owns the larger file-by-file denominator.
`check-c17-source-denominator` now verifies that every tracked C/header path
has exactly one row and every excluded row has a hashed quarantine or archive
record. Schema 2 assigns every row a deterministic source kind, build surface,
required dialect, extension boundary, risk family and lexical hazard-candidate
set. Those classifications preserve all 21 accepted, 85 migration-open, 543
excluded and 1,950 unreviewed language states. Candidate-free lexical output
is not semantic clearance. Tracked-path and classification accounting are
closed; language acceptance remains open and executable through the checker's
separate `--closure` mode.

`check-c17-compile-profiles` keeps strict portable compilation and exact GNU17
Cortex-M0+ kernel compilation in one explicitly invoked profile gate.
`check-c17-smaller-c` remains separate because native language acceptance does
not establish ISO C17 or the production cross-build result.
