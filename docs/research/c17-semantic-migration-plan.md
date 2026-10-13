# C17 semantic migration plan

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
`e3aa6d38`) and 142 `WARNLEVEL=legacy` Makefiles (140 outside `tests/`).

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

The six rows below are migrations already on main. They record what a
finished row looks like and which of its ledgers remain open.

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
| Ledgers | Language done (strict C17 host, `WARNLEVEL=full`); hardening done for the decoder; resource done |
| Residuals | Descriptor lifecycle: `stat`, `freopen`, then pathname `chmod`/`chown`/`utimes`/`unlink` admit rename and symlink races. Owner: the P1 frontier row; deciding gate: a competing-rename and symlink harness with failure injection |

Source: `security-c17-frontier.md`, "LZW memory-safety repair" through "Size,
RAM and runtime evidence".

### R2 -- formatted input scanner (`lib/libc/stdio/doscan.c`, `doscan_float.c`, `ungetc.c`)

| Field | Content |
| --- | --- |
| Layer and profile | ISO C17 library semantics (7.21.6.2) and the BSD/2.11 ABI (`FILE` unchanged); modernized cross userland |
| Tier | T1: `sysctl -w kern.hostid=<digits>` reaches `%ld` conversion with caller-written input |
| Native input | The libc members are cross-built; `include/stdio.h` is native input when a natively compiled program includes it. Native acceptance of the changed header is not recorded |
| Contract | The project defines integer-conversion saturation at the destination limit as an extension; C17 does not generally define an unrepresentable scanf conversion result. Separately, `%X` and uppercase floating conversions follow C17 lowercase semantics; `%D` and `%O` remain as documented extensions; `%n`, `%i`, `%p` and the C99 length modifiers are supported |
| Invariants | No staging buffer on the integer path; a 32-byte automatic scanset bitmap with nothing carried between directives; float staging bounded by `MANT_DIGITS` with `_Static_assert` ties to `DBL_DIG` |
| Falsifier | The replaced scanner aborts under the sanitizer, first on `%X` storing a long through `unsigned int *`, then on a digit run overrunning the 64-byte `_innum` staging buffer |
| Gates | `check-libc-scanf` (host width, ILP32 and AddressSanitizer) |
| Resources | `doscan.o` text +176, data -256; frame 128 to 120 bytes; over 244 programs built both ways, 42 changed and the sum is -1,096 bytes; `trek` and `primes` gain about 1.3 KiB by linking the float scanner |
| Evidence class | Host and cross |
| Ledgers | Language, interface, hardening and resource done for the scanner |
| Residuals | Native acceptance of the changed public header: owner, the native-toolchain ledger; deciding gate, a Smaller C compile of a `scanf` consumer. The pushback slot moved to the stdio core unit (`stdio-core-c17-unit.md`) |

Source: `211bsd-patch-scope.md`, "The first migration unit: `_doscan`".

### R3 -- directory streams (the five `DIR` members)

| Field | Content |
| --- | --- |
| Layer and profile | ISO C17, POSIX directory interfaces and the BSD/2.11 ABI; modernized cross userland |
| Tier | T1: parses directory records read from the filesystem |
| Native input | `DIR` is laid out in the public header `sys/sys/dir.h`; native acceptance of that header is unrecorded |
| Contract | Allocation failure preserves `errno`; free entries are skipped; cookies need no allocation; a failed seek leaves the stream unchanged; close owns the buffer |
| Invariants | A 1 KiB directory block held intact; record lengths and names bounded before use; `telldir` is arithmetic only |
| Falsifier | The pre-change source fails strict C17 at its K&R definitions before the layout fixture rejects the 1,108-byte stream |
| Gates | `check-dirent-contracts`, `check-dirent-contracts-cross` |
| Resources | `sizeof(DIR)` 1,108 to 1,040 (-68 heap bytes per open directory); five-member text +80 |
| Evidence class | Host and cross; board enumeration unmeasured |
| Ledgers | Language done; interface done except below; resource done |
| Residuals | The recorded falsifier is syntactic and layout-based; the six malformed-record classes are not recorded as calibrated against the pre-change reader. Owner: this row; deciding gate: a pre-change behavioral run of those cases. Board cross-block cookie replay: owner, the hardware gate. The scanner, directory admission/CLOEXEC and integer close-result units below supply separate calibrated interface evidence |

Source: `bsd-workspace-directory-stream-c17.md`.

### R4 -- syscall pathname copy (`sys/kern/ufs_namei.c`)

| Field | Content |
| --- | --- |
| Layer and profile | GNU17 freestanding kernel and the BSD/2.11 errno ABI; RP2040 kernel profile, with the copy helper compiled strictly on the host seam |
| Tier | T1: syscall user-pointer boundary |
| Native input | No |
| Contract | User paths use `copyin`; kernel-owned paths keep `copystr`; path origin is preserved through `vn_open`, `link` and `rename` |
| Invariants | An invalid pointer or a missing NUL at the process-window boundary returns `EFAULT`; an in-range terminator at the boundary succeeds; no terminator within `MAXPATHLEN` returns `ENOENT` |
| Falsifier | A direct-copy mutation fails the boundary oracle |
| Gates | `check-namei-user-path` (address and undefined-behavior sanitizers) |
| Resources | Unmeasured in the change record (commit `98ae3f2b`) |
| Evidence class | Host and exact kernel build |
| Ledgers | Language and hardening done for the helper; resource open |
| Residuals | Kernel text and frame cost: owner, this row; deciding gate, before-and-after `ufs_namei.o` and kernel size. Protection beyond software bounds checks needs MPU fault evidence (`MPU.md`); owner, the executable-loading P1 row |

Source: `TESTING.md` (`check-namei-user-path`) and `security-c17-frontier.md`,
"Ranked residual frontier".

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
| Ledgers | Hardening done for the reader and predicate; resource done at compile time |
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
| Ledgers | Resource done; hardening open |
| Residuals | A rejecting falsifier for the NOR model (owner: this row; deciding gate: a mutation that shares a sector between images and must fail the model) and attended hardware flash validation |

Source: `memory-ownership-plan.md`, "Physical-page flash staging and
erase-aligned swap images".

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
   calibrated gates and resource account. Scratch-size evaluation remains
   the next ordered subitem.
10. **Migrate one kernel interface family.** Select a caller-closed group
    from `tools/c17/kernel-ledger.txt`; compare PICO/PICO_UART diagnostics,
    public declarations, pointer widths and format arguments. Preserve
    Pico SDK register/ROM conventions and target attributes. Output: a
    reduced calibrated ledger and unchanged or explained linked footprint.
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
    distinct from GNU17 cross builds.
12. **Measure capacity and persistence on the board.** After explicit opt-in,
    select the exact image/configuration and collect process/kernel stack,
    table high-water, allocation failure and storage traffic under a named
    workload. Repeat recovery tests where flash behavior changes. Output:
    measured limits and falsifiers; until execution, runtime/wear rows stay
    open.

Items 1 through 3 form the bounded implementation in
`stdio-unbuffered-ownership.md`. Items 4 through 6 form the checked discovery
and edition-contract layer in `sus-utility-ledger.json`,
`posix-utilities.md`, `bsd44-audit/posix_inventory.py` and
`sus-edition-contracts.md`; `check-sus-discovery` calibrates that layer.
Item 7 has tested scanner, directory admission/CLOEXEC and close-result units.
The getwd lifetime unit closes the borrowed-entry defect; its independent
bounds and metadata limitations remain documented. Item 8 records the exhaustive mapping decision and paired resource reduction.
Items 9, 10 and 12 remain the ordered
continuation frontier. Item 11
has a calibrated baseline and retains a final rerun after those interface
changes. The source
classification/quarantine program in `c17-formatting-linting.md` still owns
the larger file-by-file denominator.
