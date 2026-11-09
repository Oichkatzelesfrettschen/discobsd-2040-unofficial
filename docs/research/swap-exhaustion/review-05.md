# Review 05: fork swap reservation landing

## CI root-cause analysis

The analysis covers firmware workflow runs `42564216329` at `5cd62ae3`,
`42565441356` at `c695ee0c`, and `42565715195` at `c138f456`. The first two
runs exposed defects already repaired by `c695ee0c` and `c138f456`; the third
run exposed the remaining stale host fixture. All three runs completed on
2026-11-09 UTC.

| Surface | Observed failure | Mechanism | Why local acceptance missed it | Gate class that catches it |
|---|---|---|---|---|
| macOS host tier at `5cd62ae3` | Clang reported `sys/kern/subr_rmap.c:45:1: error: a function definition without a prototype is deprecated` while building `fork_swap_rmap_compact.o`; the other K&R definitions failed under the same diagnostic. | The fork reservation gate used `-Werror` without the kernel tree's `-Wno-deprecated-non-prototype` allowance. Clang diagnosed the retained K&R definitions in `subr_rmap.c` and in extracted production code before the gate could execute. | The focused gate had been built only with the default Linux GCC. The explicit `HOST_CC=clang` variant and the complete acceptance had not run. | A host compile of the complete fork reservation gate with Clang and warnings as errors. Commit `c695ee0c` adds the same K&R allowance used by the kernel host gates. |
| RP2040 SwapRAM link verifier at `5cd62ae3` | Replaying the verifier from `5cd62ae3` against the built PICO objects reported `sys/arch/rp2040/compile/PICO/vm_swap.o: no relocation to swapram_out`. | `vm_swap.c` replaced the one-step `swapram_out()` entry with the reservation transaction's `swapram_reserve()` and `swapram_cancel()` entry points. The verifier still required the retired caller relocation, so the verifier's oracle described the former interface rather than the linked caller. | The focused `check-kernel` gate does not run the cross tier's `check-swapram`; the complete `logged-check` had not completed. The hosted runs also stopped in the host tier before reaching the cross tier, so the hosted failed-job logs contain no link-verifier line. | The cross link-oracle gate `check-swapram`, calibrated with the linked PICO kernel as the known-good input and an object with none of the required entry relocations as the known-bad input. Commit `c138f456` names the six current entry points. |
| Linux and macOS host tiers at `5cd62ae3` and `c695ee0c` | GCC reported `swapram.c:498:5: error: implicit declaration of function 'bzero'`; Clang reported `call to undeclared library function 'bzero'` at the same line. | `check-swapram-evac` compiles production `swapram.c` through its fixture shim. `swapram_cancel()` added a `bzero()` call, but the shim's replacement `sys/systm.h` omitted that declaration. Production kernel builds use the complete kernel header and therefore passed. | The focused fork reservation gate neither compiles the evacuation fixture nor substitutes its shim. The complete host tier had not completed locally. | The host fixture compile `check-swapram-evac` under both GCC and Clang with warnings as errors. Commit `c138f456` adds the fixture declaration. |
| Linux and macOS host tiers at `c138f456` | GCC and Clang reported `tests/kernel/itimer_test.c:62:6: error: conflicting types for 'swapout'`; `sys/sys/proc.h:260` showed the five-argument declaration. A direct local run of `tests/kernel/check_correctness.py` reproduced the GCC failure with exit 1. | The production declaration and implementation gained a `struct swap_image_reservation *` argument, while the interval-timer fixture retained its four-argument stub. The fixture includes `proc.h`, so the compiler rejects the incompatible definition before linking the extracted `kern_fork.c`. | The focused fork gate carried the new interface and passed. Earlier hosted host tiers stopped on the Clang and SwapRAM-fixture defects before this later parallel target became decisive. The local `logged-check` receipt records a completed build followed by an interrupted check stage (`signal_15`, exit 143), so it supplies no complete host-tier verdict. | The existing `check-storage-correctness` host compile against production headers under both GCC and Clang. The fix must update the stub and retain the unused reservation parameter explicitly. |

The local interrupted-run evidence is
`.build-logs/rp2040-check-local-20261109T051017Z-360512-97222b5e/status.json`.
Its ownership receipt reports `state: released`, with empty process-group and
pending-launch lists; recovery is unnecessary before the next logged target.

### Failed-log classification

The failed-job logs contain deliberate failure text that does not identify a
product defect. `run_logged_gate_test.py` emits labels such as `injected log
failure`, `injected ownership finalization failure`, and hard-timeout cleanup
records, then its Python suite reports success. The libtcl, crc32, dc/bc,
Dhara, and other mutation gates print the rejected mutant's `FAIL`, abort, or
timeout before printing a `PASS ... rejected` verdict. After the genuine host
compile error, parallel bmake recipes also report secondary `Error code 2` or
`Error code 6` stops. The genuine failures above are the first production or
fixture compile/link deviations; the injected and secondary lines do not
define additional fixes.

## Pre-fix whole-diff adversarial review

The review compared `origin/main...c138f456` with the commit 2 and commit 3
contracts in `review-02.md`. Source tracing covered every caller and state
transition, and the deterministic review checks ran `git diff --check`, Ruff
over both new Python tools, ShellCheck at error severity over the changed link
verifier, and complete symbol searches for the revised swap interfaces. Those
checks passed. Read-only syntax probes also passed for the changed kernel
translation units with SwapRAM enabled, SwapRAM disabled, and
`SWAP_IMAGE_ALIGN` undefined. The supported STM32 GCC build remains the
deciding portability check.

### SERIOUS findings

1. **Fork-copy I/O violates the diagnostic sleep precondition.**
   `sys/kern/kern_fork.c:179-187` makes the child the process named by
   `u.u_procp`, leaves `child->p_stat` at `SIDL` while `swapout()` runs, and
   changes the state to `SRUN` only after the copy. Production swap I/O can
   sleep for a buffer at `sys/kern/ufs_bio.c:238-242` or for completion at
   `sys/kern/vm_swp.c:37-39`; `sys/kern/kern_synch.c:150-152` panics under
   `DIAGNOSTIC` when the current process is outside `SRUN`. `origin/main`
   placed the `SRUN` assignment before `swapout()`. The fork fixture replaces
   both I/O and sleep and incorrectly requires `SIDL`, so its diagnostic build
   accepts the regression. The fix must restore the current-process sleep
   contract without exposing a child fork continuation before image commit,
   and a known-bad ordering mutation must fail a modeled I/O-wait control.

2. **The retained interval-timer gate has the retired swap interface.**
   `tests/kernel/itimer_test.c:62-63` defines the four-argument `swapout()`
   while `sys/sys/proc.h:260-261` declares five arguments. The fixture also
   calls production `newproc()` at `itimer_test.c:441`, so it needs a successful
   `swap_image_reserve()` boundary as well as the corrected `swapout()` stub.
   Both hosted compilers reject the current fixture before the timer oracle
   runs.

3. **The new release wakeup has no calibrated oracle.**
   `sys/kern/vm_swap.c:132-148` releases flash or SwapRAM capacity and then
   wakes `&runout`, but `tests/kernel/fork_swap_test.c:89` discards every
   channel. The existing lifecycle checks observe only eventual free capacity.
   Deleting the wakeup or moving it before the free therefore passes. The gate
   must observe the channel and capacity at notification time, exercise the
   nonzero-`runout` commit branch at `vm_swap.c:310-312`, and reject omitted and
   prematurely ordered wakeups.

4. **Reservation guard failures lack calibrated rejection controls.**
   The ordinary lifecycle reaches `RESERVED -> COPIED -> COMMITTED` and
   `RESERVED -> EMPTY`, but it never directly rejects wrong-owner copy or
   commit, copy or commit from the wrong state, or cancellation after copy.
   The guards at `sys/kern/vm_swap.c:108-111,227-229,291-293` can be removed
   without moving any current assertion. The gate must exercise the valid
   transitions and exact defensive panics, then reject one known-bad mutation
   for each owner or state guard.

### MINOR findings

1. `tools/verify_rp2040_swapram_link.sh:62-64,83-85,89-91` feeds large symbol
   and relocation strings to `grep -q`. The early reader exit makes the shell's
   `printf` report `Broken pipe` while a valid PICO or PICO_UART image still
   exits zero. A consuming search removes the false error text. The calibrated
   bad input substitutes `swapram_pool.o` for `vm_swap.o` and must still reject
   all six missing caller relocations.
2. The fork gate's four descriptor shapes all inherit RP2040 alignment and
   SwapRAM. They do not execute the three-extent `malloc3()` cancellation and
   release arms at `sys/kern/vm_swap.c:94-96,121-123,143-145`. Source arithmetic
   is symmetric, `mfree()` explicitly accepts zero size at
   `sys/kern/subr_rmap.c:92-93`, and current STM32 CI at `c138f456` compiled and
   linked all kernels. Runtime behavior for that path remains outside the host
   oracle.
3. Successful fork accounting has only a `ru_nswap` assertion. The fixture
   does not independently assert `forkstat`, `cnt`, SwapRAM attempt/admission
   counters, or that the copied u-area contains the increment made before its
   write at `sys/kern/vm_swap.c:263-281`. Strengthening the gate will bind the
   statistics review to executed evidence.

### SOUND findings

- `sys/kern/kern_fork.c:37-44` reserves against the candidate child slot while
  the parent supplies the bytes. PID selection, free-slot removal, LARGE
  inheritance, list/hash publication, and shared-reference increments begin
  only at lines 52, 90, 102, 137, 148, and 157. Refusal returns `ENOMEM` through
  `fork1()` at lines 251-254 with zero child publication and zero PID
  consumption.
- SwapRAM indexes ownership by `p - proc` in
  `sys/arch/rp2040/rp2040/swapram.c:248-254`; reservation precedes
  `swapram_inherit()` and inheritance changes only the LARGE flag and holder
  count at lines 823-830. The reservation metadata therefore remains keyed to
  the child.
- The state machine initializes or refuses as `EMPTY`, acquires backing before
  publishing `RESERVED`, accepts idempotent cancellation only from
  `RESERVED`, marks bytes `COPIED`, and publishes process addresses before
  `COMMITTED`. Hard media failure remains a panic in `vm_swp.c`; the branch
  does not convert media failure to `ENOMEM`.
- `fork()` and `vfork()` share the negative result path. Process 1 treats a
  negative result as a boot panic at `sys/kern/init_main.c:185-192`, distinct
  from the parent and child branches. The successful vfork handoff and physical
  board refusal remain separate evidence obligations.
- `swap_image_release()` publishes free capacity before its wakeup. The
  scheduler's unreserved path deliberately retains its fatal exhaustion policy;
  recoverable victim restoration belongs to the later scheduler transaction
  described in `review-02.md`, outside the fork admission change.
- Fork and vfork statistics move only after successful reservation. SwapRAM
  attempts and admissions move only after complete backing admission, as the
  explicit diagnostic-preservation contract in
  `sys/arch/rp2040/include/swapram.h:113-120` requires. Refused fork admission
  therefore preserves the recorded counters.

The strongest supported conclusion is narrow: the branch acquires child-keyed
backing before fallible fork publication and preserves the parent on capacity
refusal. The sleep-state regression and the two missing ownership oracles must
be repaired before that transaction can land. Real storage interleavings,
successful vfork completion on target, and physical-board exhaustion remain
outside host evidence.

## Fixes and commits

- `5dd26c5a5c7d249899ebbebabce7354cd83d16d5` --
  `fix(fork): keep child copy sleepable`. The child enters `SRUN` before the
  copy continuation can block, while the owned reservation still commits
  before the fork continuation returns. The interval-timer fixture implements
  the new reservation and swap boundaries. The fork gate records modeled I/O
  waits, release notification order, committed u-area accounting, successful
  statistics, and invalid owner/state transitions. Eight added mutants cover
  the sleep ordering, wakeup omission/order, and five reservation guards.
- `ae2b0680e976a1a747eaf7cca1490c16963f8ea8` --
  `test(rp2040): consume link verifier searches`. Symbol and relocation
  searches consume their complete inputs, so valid PICO and PICO_UART images
  emit zero broken-pipe diagnostics while a caller-free object still reports
  all six missing SwapRAM relocations and exits 1.
- `13c46178d99db2385be7960fe243fe2a4de4b9cc` --
  `test(c17): record fork gate sizeof hazard`. The fork gate's wake-channel
  count uses `sizeof(array) / sizeof(array[0])`, so the C17 source denominator
  records the resulting `width-sensitive-sizeof` classification. The live
  denominator reports 141 accepted, 543 excluded, 58 migration-open and 2,047
  unreviewed files, and all 21 denominator mutations are rejected.
- `38cf20d6efb3a9db2acb9dfe9dbbd9697421d8dc` --
  `test(c17): remove prototyped swap span entry`. The reservation transaction
  replaced the K&R `swapimagespan` definition with the prototyped
  `swap_image_span` helper, so the committed K&R ledger removes the obsolete
  row. Regeneration with the PICO and PICO_UART compiler oracles changed only
  that row; the focused inventory gate passed with 162 definitions and 2
  declarations in 24 files, with compiler coverage of 51 of 123 sources.

Focused pre-acceptance controls passed. The fork gate reported 45,990 checks
for each compact, wide and diagnostic configuration and 45,996 for the
descriptor-full configuration. All 13 mutants exited 1 at their named oracle.
`check-storage-correctness` passed the 2,455-check interval-timer baseline and
all 51 other baseline or calibrated-mutation cases. Ruff, ShellCheck at error
severity, `git diff --check`, and both link-verifier controls passed.

## Local acceptance progress

The first post-repair acceptance attempt used
`rp2040-build-local-20261109T054854Z-31-1fff98aa` and
`rp2040-check-local-20261109T055148Z-32716-d920da3e`. The build exited 0. The
check exited 1 because `tests/kernel/fork_swap_test.c` acquired the
`width-sensitive-sizeof` classification while the denominator ledger retained
its earlier classification. A complete temporary classifier refresh changed
only that field, and commit `13c46178` records the classification. Direct
denominator verification then passed the 2,789-file accounting and rejected
all 21 calibration mutations.

The next exact acceptance sequence started with
`rp2040-build-local-20261109T060011Z-31-ab43a344`, which exited 0 after
123.184 seconds. `rp2040-check-local-20261109T060214Z-32716-6bfb77cc` completed
its internal build with exit 0 after 125.134 seconds and ran the check stage
for 2,172.363 seconds. The check reached `shell.posix.ilp32`, where the compiled
ELF32 i386 capability probe terminated with `SIGSYS` and the required POSIX
shell tier reported an error. A separate diagnostic compiled another ELF32
i386 executable successfully; direct execution under the review sandbox
returned 159, while the installed `qemu-i386` interpreter ran the identical
file with exit 0. The failure therefore identifies the sandbox's native i386
syscall boundary rather than missing multilib headers, libraries or loader.
`TESTING.md` requires native ILP32 execution for this tier, so emulation or a
skip cannot satisfy the local acceptance contract.

The retained build and check logs contain zero `warning:` diagnostics. The two
successful build stages each emitted a nonfatal `games/pzcommon` cleanup
message because `rmdir` found no `tests/build` directory; both receipts record
exit 0. The separate kernel-headroom command passed for PICO (34,540 bytes),
PICO_UART (44,500 bytes), PICOCALC (16,840 bytes) and PICOCALC_PSRAM (14,732
bytes), each against the 1,024-byte margin. The separate Clang fork gate passed
45,990 checks in each compact, wide and diagnostic configuration, 45,996
checks in the descriptor-full configuration, and all 13 mutation controls.

A direct `check-cross` diagnostic after the sandbox-limited aggregate attempt
reached `check-c17-kernel-inventory` and rejected the obsolete
`swapimagespan` K&R ledger row. The complete two-configuration temporary
regeneration produced exactly that one-line deletion. Commit `38cf20d6`
records the repair, and the focused `check-c17-kernel-inventory` gate passes.

The final post-fix build,
`rp2040-build-local-20261109T065313Z-30-eac4465f`, exited 0 after 144.222
seconds. Its log contains zero `warning:` and zero `error:` diagnostics. The
final aggregate attempt,
`rp2040-check-local-20261109T065617Z-30-ca4c0774`, completed its internal build
with exit 0 after 164.713 seconds. Before the wrap-up order stopped the check
stage after 1,470.048 seconds, ShellCheck had passed 168 scripts, Ruff had
passed 234 files, architecture isolation had passed 143 assertions, and the
fork reservation gate had passed its four configurations and rejected all 13
named mutants. The interruption occurred during the Fourfall play-mutant gate.

The interrupted aggregate has no terminal receipt. Its aggregate and check
status files still report `running`, and
`.build-logs/rp2040-products.lock.ownership.json` still reports `active` with
process groups 32691, 94832 and 94847. A process-table probe found all three
groups absent. `recover_logged_execution.py` requires a terminal receipt, so
the stale receipt and ownership barrier cannot be closed truthfully through
that recovery interface. The wrap-up order prohibits restarting acceptance.
The branch through `38cf20d6` is pushed to
`origin/kern/fork-swap-reservation`; PR checks after that push were not watched.

## Final requested-tree adversarial review

The requested tree is a systems, concurrency, memory-ownership and build-
reproducibility change. The review covered the complete
`origin/main...38cf20d6` diff: 19 files, 1,270 insertions and 127 deletions.
`git diff --check origin/main...38cf20d6` passed. The final tree has no FATAL or
SERIOUS finding.

### MINOR findings

1. **The RAM release oracle bypasses the production swap-in route.** The host
   gate calls `swap_image_release()` directly at
   `tests/kernel/fork_swap_test.c:504-507`, which observes a `runout` wakeup.
   Production `swapin()` instead routes a RAM image through `swapram_in()` at
   `sys/kern/vm_swap.c:186-189`; that function frees pool capacity at
   `sys/arch/rp2040/rp2040/swapram.c:556-565` without calling
   `swap_image_release()`. The current tree gives `runout` only the swapper
   waiter at `sys/kern/vm_sched.c:84-87`, so the review establishes no current
   missed waiter. A future RAM-capacity waiter could remain asleep after space
   becomes available while the present gate still passes. Add a production
   `swapin()` integration control or route both tiers through one shared
   release-notification mechanism.
2. **The RP2040 host matrix excludes the non-aligned three-extent path.** Every
   fork fixture enables SwapRAM and inherits RP2040 alignment through
   `tests/kernel/Makefile:3054-3055`; descriptor width changes the resource-map
   representation, not the allocation policy. The `malloc3()` allocation,
   cancellation and release arms at `sys/kern/vm_swap.c:94-96,121-123,143-145`
   therefore lack host execution. Omitting one segment free could leave a leak
   while all current gates pass. Add a SwapRAM-disabled, non-aligned fixture
   that exercises fragmented success, partial refusal, cancellation and
   release, calibrated by a missing-segment-free mutant.
3. **The saved-context child and successful process-1 paths remain
   unexecuted.** `child_resume` is declared, returned and reset to zero at
   `tests/kernel/fork_swap_test.c:35,112,226`, so the child return from
   `newproc()` never executes. The boot control at lines 623 and 658 checks
   reservation refusal only, while the successful process-1 geometry begins at
   `sys/kern/init_main.c:185-199`. A wrong child result or initial-process image
   geometry could pass the current gate. Add explicit parent, child and refusal
   result controls plus a successful initial-process reservation control.

### SOUND findings

- **Reservation precedes publication.** `sys/kern/kern_fork.c:37-44` acquires
  child-owned backing before PID selection, free-list removal, PID/hash/list
  publication and reference increments at lines 52-165. `swapram_inherit()` at
  line 102 follows the reservation and preserves the child-keyed slot.
- **The blocking-copy sleep contract now holds.** `sys/kern/kern_fork.c:179-194`
  makes the child current and `SRUN` while copy I/O may sleep, matching the
  `tsleep()` precondition and state transition at
  `sys/kern/kern_synch.c:127-190`. Wakeup restores runnable state at
  `kern_synch.c:291-316`, while `sys/kern/vm_swap.c:291-305` commits owned bytes
  before the child continuation can return.
- **Maintained callers use the current interface.** A complete source search
  found production `swapout()` calls only in `sys/kern/kern_fork.c:191` and
  `sys/kern/vm_sched.c:57`, both with the reservation argument. The declaration
  at `sys/sys/proc.h:260-261` and retained fixtures use the same interface.
- **Compiler inventory movement matches the source change.** The
  `tools/c17/kernel-ledger.txt` diff removes only the obsolete K&R
  `swapimagespan` row; `tools/c17/source-denominator.json` records the new fork
  fixture and its `width-sensitive-sizeof` hazard.

### Steelman and verdict

The branch's strongest defensible claim is that fork backing admission now
forms a child-owned reservation transaction before fallible process
publication, then copies and commits the image while preserving the kernel's
sleep-state contract. The source order, calibrated fork mutants, updated
callers and warning-free build support that claim. The three MINOR gaps limit
portability, notification and continuation coverage; they do not demonstrate a
current transaction failure. Acceptance remains incomplete because the native
i386 tier is blocked by the sandbox and the final aggregate was interrupted by
the controlling wrap-up order.

## Handoff
- Requests 01-04 became `5dd26c5a5c7d249899ebbebabce7354cd83d16d5`, `ae2b0680e976a1a747eaf7cca1490c16963f8ea8`, `13c46178d99db2385be7960fe243fe2a4de4b9cc` and `38cf20d6efb3a9db2acb9dfe9dbbd9697421d8dc`; all four are pushed on `origin/kern/fork-swap-reservation`.
- Request 05 contains `brief-05.md` and `review-05.md`; its orchestrator hash is pending.
- Current-tree findings rank as 0 FATAL, 0 SERIOUS, 3 MINOR and 4 SOUND.
- MINOR: the RAM release oracle bypasses production `swapram_in()`; add swap-in integration coverage or shared notification.
- MINOR: the non-aligned three-extent path lacks host execution; add a SwapRAM-disabled fixture.
- MINOR: the saved-context child and successful process-1 paths lack execution controls.
- `rp2040-build-local-20261109T065313Z-30-eac4465f` passed with exit 0 and zero warning/error diagnostics.
- `rp2040-check-local-20261109T065617Z-30-ca4c0774` passed its internal build, lint, architecture isolation, fork gate and all 13 fork mutants before interruption during Fourfall play mutants.
- The aggregate has no terminal verdict; its receipts remain stale `running` and product ownership remains `active` although the recorded process groups are absent.
- Native i386 execution remains blocked by sandbox `SIGSYS`; PR checks after `38cf20d6` were not watched under the wrap-up order.
- A board run still needs fork/vfork capacity refusal, successful child/vfork resumption and SwapRAM/flash release behavior on measured RP2040 silicon.
