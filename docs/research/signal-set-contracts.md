# Signal-set provider and header contracts

## Baseline and deciding experiment

The work branches from owned main `f602046341801b77feff61a2c3549c119ffb8d37`.
`lib/libc/gen/sigsetops.c` supplies five callable set operations and belongs
to `lib/libc/gen/Makefile`. `sys/sys/signal.h` supplies corresponding macros,
but its public declarations omit all five callable signatures. `sigset_t`
is unsigned long; the maintained ILP32 target represents it in 32 bits.

The callable `sigismember` tests `*set & ~sigmask(signo)`, which asks whether
any other bit is present. The macro tests the requested bit. Source search
uses `rg -n 'sigismember|sigsetops'` over the header, provider, and build files.
The governing membership invariant is `(set & (1U << (signo - 1))) != 0`
for admitted signal numbers 1 through 31. The callable operation requires a
const-qualified set pointer and the same answer as the public call form.

A bounded host GNU17/UBSan probe includes the actual provider with an explicit
`uint32_t` signal-set shim. It enumerates all 1,024 masks in the low ten bits
and all 31 valid signal numbers. The baseline reports 26,603 mismatches in
31,744 calls with exit status 1 and no sanitizer diagnostic. The shim models
the target representation; it does not qualify target headers, archive
membership, kernel behavior, or physical hardware. The provider SHA-256 is
`9359368cfa3db344eef72833927561b79f8ab4cb18d81d8e143fcbdf88f1cf26`.

The predicted repair reduces membership mismatches to zero on this exact
domain. Additional witnesses require empty/full sets, each individual bit,
all bits, argument single evaluation, callable/macro parity, exact signatures,
and invalid-signal errors before any shift. The current provider and macros
lack invalid-number admission. A valid-domain probe establishes no behavior
for those invalid inputs.

## Donor mechanisms

The inspected local DragonFlyBSD revision is
`94448f622a06cde90ba42d688e9cab2448ad9ab3`; its
`lib/libc/gen/sigsetops.c` SHA-256 is
`582715e1a1eb109f89db743c69143b2d385dde6a6d4db60a5061cd330ec803bc`.
It validates the signal number before deriving a word and bit in its multiword
set, returns `-1` with `EINVAL` for invalid numbers, and tests the selected bit.
The transferable invariant is admission before shifting; its multiword
representation does not replace the port's scalar ABI.

The inspected local OpenBSD revision is
`8849c5d46b9b299f5d887033706f1ddd60f8f966`; the same relative file has SHA-256
`347b32b34b233938c932435b74be0fbab31be4d361ff13afbf0cf685f7594883`.
Its scalar provider validates `0 < signo < NSIG` and tests the requested bit.
Both donors carry shared Berkeley ancestry; they supply alternative current
mechanisms at these retained local revisions, not independent proofs or a
claim that these revisions match live upstream.

## Implementation and qualification boundary

The repair preserves the scalar public representation and the kernel ABI.
The header and provider require one consistent validation policy, exact
prototypes, const membership input, and single evaluation. Empty and full
operations preserve their established bit representations. Invalid add/delete
operations leave the set unchanged and report `EINVAL`; invalid membership
reports `-1` with `EINVAL`. No invalid number reaches a shift.

The implementation choice must retain native Smaller C header acceptance.
Replacing macros with callable operations trades local expressions for archive
dependencies and therefore requires caller closure and matched resource
measurement. Inline alternatives require explicit native-compiler evidence;
they do not receive assumed acceptance. Remaining qualification includes
strict C17 mechanisms, GNU17 production, target header compilation, native
archive providers, host/ILP32/Cortex-M0 execution, calibrated mutants, clean
shipped-resource comparison, and the profile's exception process for growth.

The membership experiment establishes a defect and a falsifier. Implementation,
complete public-header contracts, resource admission, and integration remain
open. Physical-board execution retains its separate authorization boundary.

## Provider candidate and initial witnesses

The branch incorporates main `d01ef4a0`, which merges the native nm repair.
The first header fixture fails at baseline because address-taking exposes all
five absent declarations. The behavior fixture reports membership failures;
its first invalid add at `INT_MIN` triggers UBSan signed-overflow detection
in the baseline's `signo - 1`. These failures precede the provider edit.

The candidate declares the five callable interfaces, modernizes their function
definitions, and uses a const-qualified membership input. User-facing add,
delete, and membership resolve to validated providers. Kernel macros preserve
their trusted-input behavior; empty/full user macros retain their single-store
operation alongside callable providers. Each validated provider admits
`0 < signo < NSIG` before subtracting or shifting, uses an unsigned set bit,
and returns `-1` with `EINVAL` on rejection without mutating the set.

The strict-C17 host UBSan fixture passes 31,981 checks across bounded masks,
singleton/full sets, add/delete restoration, valid-call errno preservation,
and six invalid-number boundaries. The target-header fixture compiles with
strict C17, warnings as errors, real port headers, repeated signal-header
inclusion, and signal/select type coexistence. A Cortex-M0 QEMU invocation
links that fixture and the real-header provider objects with the established
newlib/Linux syscall support and exits zero. It covers exact function-pointer
types, callable/macro empty/full agreement, single evaluation, const membership,
invalid errors, and preservation of adjacent storage. Its support-code link
requires the repository's `-Os` setting to keep `r7` available for syscalls.

These are initial witnesses, not admission. Calibrated mutations, native
Smaller C input, full caller rebuild, registered gates, source-ledger refresh,
matched resources, review, and integration remain open. No physical board
executes this candidate.

## Mutation calibration and native input

`tests/libc_contracts/signal_set_calibrate.py` compiles the good provider and
seven warning-clean mutations under strict C17 with UBSan. Complemented
membership, incorrect empty/full results, wrong add/delete operators, incorrect
errno, and zero-signal admission each reach their named rejection. The good
control requires all 31,981 checks and empty stderr. The zero-admission mutant
requires a sanitizer diagnostic; a compile failure does not count as rejection.
Each invocation retains the generated source, commands, build/run logs, and
source/fixture hashes in its unique build-log directory.

The header witness uses a constant-size typedef assertion accepted by both
C17 and Smaller C. The host-built Thumb Smaller C compiler accepts the
cross-preprocessed real-header fixture; GNU assembler accepts the output for
Cortex-M0+. A malformed declaration fails with `Unexpected token {`, supplying
the negative control. The first invocation with absolute input/output paths
hits Smaller C's path-length check before parsing. Running from the fixture
directory with `input.i` and `output.s` reaches the actual header check and
passes. This evidence establishes preprocessing/compiler/assembler acceptance;
on-device preprocessing and native runtime remain separate.

Gate registration, production caller builds, source-ledger refresh, resource
qualification, and review remain required before this candidate is integrated.

## Registered-gate and caller-build qualification

The root Makefile registers `check-signal-sets` in the host tier,
`check-signal-sets-cross` in the cross tier, and `check-signal-sets-qemu` in
the QEMU tier. `sys/arch/rp2040/doc/TESTING.md` owns their execution claims;
the firmware workflow records their tier ownership. Missing required tools
fail the selected runner. Cross/native runs retain both valid input and
required prototype/syntax rejection controls.

A logged clean removes prior caller objects before `logged-build` compiles
and links the maintained programs with the validated provider calls. The
build succeeds in 86.522 seconds; its receipt is
`.build-logs/rp2040-build-local-20261114T212901Z-3861504-fadb785a/status.json`.
All three registered Makefile targets pass after that build. Separate strict
C17 UBSan invocations under GCC ILP32 and Clang each pass the good provider
and seven named mutation controls. The source ledger adds the three new
C/header fixtures as unreviewed and reports 153 accepted, 543 excluded,
62 migration-open, and 2,264 unreviewed paths. Its lexical classification
removes the provider's old-style-definition candidate and records the explicit
shift expressions; these labels remain review candidates rather than defects.

Matched source/resource captures, archive-provider closure, affected-caller
runtime checks, emulator image qualification, review, and integration remain
open. The build result establishes caller compilation/linkage, not zero
resource growth or physical-board behavior.

## Resource observation and fixed-signal caller hypothesis

Clean main `d01ef4a0` and candidate `ec6f9773` complete instrumented
distributions and shipped dependency captures. The retained
`signal-set-resources-d01ef4a0.json`, `signal-set-resources-ec6f9773.json`,
and paired comparison bind the observations to those sources. Eighteen
shipped programs acquire `sigsetops.o`; the nonpositive-growth comparator
rejects those additions pending measured review. The native archive grows
32 bytes, root data blocks grow from 746 to 747, and free blocks fall to 232.
Most affected programs gain 40 loaded bytes. These costs are unapproved.

Source inspection locates constant signal-set construction in `abort.c` and
`system.c`: the former removes known `SIGABRT` from a full set, and the latter
forms the singleton `SIGCHLD` set. Those internal values are compile-time
constants rather than untrusted signal-number inputs. Replacing their calls
with equivalent scalar mask construction can preserve the original generated
operations while retaining validation for public callers. The proposed
invariant requires compile-time admission of each constant within
`0 < signal < NSIG`, and identical syscall-visible masks and ordering.

The deciding experiment compares target object code and relocation identities
against main, then executes caller fixtures that observe masks, signal actions,
and cleanup. A fresh matched resource capture decides the actual savings.
The prediction is removal of repeated `sigsetops.o` dependencies attributable
solely to these fixed-signal constructions. A differing mask, reordered caller
effect, or unchanged dependency count refutes the corresponding prediction.
No resource savings enter the accepted budget before those checks pass.

The inspected Issue 8 function specifications at
`https://pubs.opengroup.org/onlinepubs/9799919799/functions/` require the stated
successful results. They permit `EINVAL` detection for invalid or unsupported
numbers in add, delete, and membership; empty/full define no errors. The port
chooses that permitted error detection as its stronger contract. The validation
repair does not represent optional error detection as a mandatory POSIX clause.

The fixed-signal target experiment compiles main and the proposed internal
mask construction with identical GNU17 Cortex-M0+ optimization flags. Both
text bytes and relocation entries match: 62 text bytes for `abort`, 172 for
`system`. The candidate applies those equivalent constructions with compile-time
signal-domain assertions. This supports the tested target code-generation
claim; fresh caller fixtures and resource captures still decide admission.
The signal-set manual describes callable interfaces, const membership, and
the selected invalid-number error contract without changing its provenance.

## Fixed-signal caller qualification

`signal_callers_test.c` links the real `abort.c` and `system.c` bodies with
instrumented process and signal operations. Six Cortex-M0 scenarios observe
masks, arguments, saved dispositions, restoration, and event ordering across
abort fallback, system parent completion, fork failure, child exec failure,
wait failure, and null command. All six pass. Three warning-clean mutations
must fail their named scenario: setting the abort bit, selecting SIGINT instead
of SIGCHLD, and omitting saved-mask restoration. All three controls reject.
The registered QEMU gate also executes the Smaller C-generated real-header
caller with the provider; its successful exit is distinct from the earlier
compile-and-assemble-only evidence. The fixtures model process/signal services
and do not establish kernel delivery or fork execution.

`tools/bin/rp2040/nm lib/libc_aout/libc.a` locates one text definition of each
of the five callable signal-set operations in the generated native archive.
Undefined references in consumer members remain distinct from those providers.
Fresh resource capture still decides the optimized candidate's actual costs.

The revised production build passes in 41.39 seconds,
with receipt `.build-logs/rp2040-build-local-20261114T214304Z-4090050-196e22e7/status.json`.
The registered `check-signal-sets-qemu` target passes after that build,
including the native-generated caller and all caller mutation controls.
Source accounting adds the caller fixture as unreviewed (2,265 unreviewed
paths); runtime witnesses remain separate from that accounting.

## Profile and image qualification

The profile records the five signal-set interfaces as partial with QEMU-user
witnesses. Host calibration remains a separate witness entry. The generated
profile hash and counts match the requirement rows; profile calibration rejects
all 53 mutations. The flash-image build completes successfully for candidate
`0abfb33d`. Fresh cross and QEMU signal-set gates pass, including the native
caller and caller mutation controls. Changed-comment and changed-text checks
pass over `d01ef4a0..0abfb33d`.

The local Renode model build uses pinned revision
`205a5e4b25440582008a4292074bb07f80a72328` and the repository patch series.
It completes with 27 warnings, including unavailable NuGet audit data,
assembly-version conflicts, and donor compiler diagnostics. This result
establishes model compilation with warnings. The confined boot invocation
exits before boot with a Renode configuration-lock exception. An invocation
outside filesystem confinement completes `check-renode` successfully. The
UART0 boot exercises process listing, memory-device refusal, fstat, image
identity, MPU faults, exec residue, post-2038 timestamps, native compilation,
and core-1 lifecycle. The warning classifier accepts its modeled warning
classes. This emulator result supplies no physical-board evidence.

`signal-set-resource-exception.md` and the optimized resource receipts record
the measured admission request. The owner approves its exact limits on
2026-11-14. Aggregate local qualification, hosted CI, and reviewed integration
remain open.

The passing PICO_UART run binds `unix.bin` SHA-256
`ec6c51e61e360f7e9447d5864baeea3eb8e6c901aadd9cd674258224c10429b7`
and `flash.bin` SHA-256
`db89d7949ba7524e72db4b4139c19f2e06aeccb2af7895ff0c187aff9090fa1a`.
Its captured gate output has SHA-256
`5c5a8391dde9a33b5e37c6c3d4553c7eb4db44e6da8508f09a08f39ba1a1d0ed`.
The classifier reports 201,579 observations across 30 permitted warning
classes and zero errors; the count is model diagnostic evidence, not a
warning-free peripheral implementation claim.
