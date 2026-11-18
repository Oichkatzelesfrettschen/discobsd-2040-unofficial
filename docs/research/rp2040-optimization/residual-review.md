# PR #435 successor review

The archived branch is `7aed0038`. PR #435 closed after #671 reconciled its
work on main. This review checks its ledger against main at `0a211d15`.
The historical measurements remain useful controls for their named trees.
They cannot be added to the current measurements or treated as current
product margins.

Three software gaps have direct corrections. A project assembly rule selects
ISO preprocessing even when the installed BSD make defaults to traditional
CPP. The native a.out libc also confines its object outputs to its own
directory. The Dhara interruption gate now rejects a mixed sector in the
partial-prefix matrix and checks that removing checkpoint tree validation
reproduces the original failure. The single-cut journal repair itself is already on main. In a repeated-cut
host model the ninth interrupted checkpoint exhausts the existing scan
bound. The driver now refuses to open a recognized journal whose roots all
fail validation, preserving the media for recovery.

The first release question is which combinations can actually exhaust a
2,048-byte kernel stack. The retained 3,164 to 3,548-byte static sums across
six configurations exceed the budget even before the swap and panic
overlays. The 1,828-byte Pico watermark is an observation from a different,
narrower workload. Resolve the discrepancy by exercising reachable chains
and measuring stack-pointer minima while preserving the exception and
priority conditions that make each chain possible.

The second question is what persistence boundary can be promised for the
board and flash chip in the release. The current model establishes old/new
sector behavior for its modeled cuts. Repeated loss before a replacement
checkpoint and electrical power loss remain separate admission work.

The line of evidence for initial integration is the
[PR #435 closeout](../kernel-footprint-pr435-closeout.md). The current
short firmware workflow built the six kernels, built and validated the UF2
images and booted the UART0 model on `0a211d15`. The broader jobs in that
workflow run were skipped. [Firmware run](https://github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial/actions/runs/44203059224)

## 32 decisions and checks

| # | Question | Evidence, disposition and deciding step |
| ---: | --- | --- |
| 1 | Which branch owns the result? | **Resolved.** #435 is closed and its head is archived. Start follow-ups on main. Review the closeout by mechanism since several main implementations differ from the branch. |
| 2 | Does the evidence match the code under review? | **Partly resolved.** Use exact revision, target configuration, toolchain, command and artifact hash for each receipt. Keep old resource receipts for comparison. Rebuild the exact candidate before a release decision. |
| 3 | Which jobs actually ran? | **Resolved for the audited workflow.** Main's short workflow passed build, image, lint and Renode UART0. Host, cross, QEMU, ILP32 and other Renode stages have different owners or triggers. Check step conclusions before claiming coverage. |
| 4 | Can a normal build depend on installed make defaults? | **Remediated.** Debian bmake 20200710 leaves `##` in ROM assembly with its inherited traditional CPP rule. A project rule selects ISO preprocessing. The gate compares bytes and relocations against GNU as and rejects the traditional input. |
| 5 | Can an incremental native build reuse an object of another format? | **Remediated.** Source lookup can find a prebuilt ELF object with an a.out target's name. Limit object-target lookup to the native output directory. The assembler tier rebuilds the archive after an ELF build and links from it. |
| 6 | Which C17 boundaries are already enforced? | **Resolved for active scope.** Both ARM file lists attach strict prototype flags to process, resource, clist and time units. Typed controller and device slots on main supersede the archived interface design. Scope future conversion by caller and configuration. |
| 7 | What do dormant definitions mean for closure? | **Inventory open.** Two time helpers still use identifier lists inside `NOT_CURRENTLY_IN_USE`. They contribute no active object code. Decide whether to retire the dormant API or test and convert it as its own unit. Track it in the C17 ledger. |
| 8 | Have both user ABIs been covered? | **Scoped.** Err forwarding and smaller C previously passed in their named fixtures. Main now has separate time32/time64 archive paths and an independent native board archive selection. Any ABI change needs exact consumer and archive checks on each affected view. |
| 9 | Can production test images regress through stale artifacts? | **Guarded.** Image, source-date and generated-config owners bind current inputs. Validate published format and exact revision after a clean rebuild, including native libc. Reserve release claims for receipts of the build that produced the artifacts. |
| 10 | What is the release flash budget? | **Measure the tightest profile.** The linker and headroom checks measure resident ranges. Use the current PICOCALC and lean profiles and report payload, address span and margin separately. Board-descriptor alignment can turn a small source gain into a whole-page span change, or no span change. |
| 11 | Did section collection already buy the savings? | **Resolved.** Main already collects sections and omits unused trace names from lean links. The archive's GC experiment, later trace closure and today's footprint changes overlap. Recalculate only against a common same-tree control. |
| 12 | Should alignment sorting expand? | **Scoped sort landed.** Main sorts ordinary rodata and BSS with 8-byte boundaries. Wider text sorting saved some payload in private links without improving the adopted flash-span result. Keep the measured hot layout as the control. [Handoff](../kernel-footprint-handoff.md) |
| 13 | Does compact dispatch earn its complexity? | **Decision open.** The original six-image prototype saved 256 span bytes in each profile. This review measures 256 bytes in five profiles and 512 in PICOCALC_LEAN. The measured path grows from seven to ten instructions and adds a flash literal load. Establish cycle limits and a size trigger before adding a second production representation. [Handoff](../kernel-footprint-handoff.md) |
| 14 | Can correct metadata yield an incorrect linked table? | **Gate candidate.** Host generation and the private image probe cover different boundaries. A production verifier should compare selected master rows to the ordinary linked table, including Thumb metadata, with wrong-slot and non-Thumb controls. It can be admitted independently of compact dispatch. |
| 15 | What must dispatch execution include? | **Open for compact production admission.** The probe starts after `setjmp` and checks indirect targets. Full-path evidence includes PendSV, fifth and sixth stack arguments, rejection and carry reporting, signal interruption, unavailable calls and reschedule. Use the existing exact-source argument-admission and Renode stages as controls. |
| 16 | Are RAM totals measuring placement or usable space? | **Separate metrics.** Account by bank and owner, including u areas, stacks, reserves, USB buffers and overlay lifetimes. Ordinary BSS, total address span and allocatable RAM differ. Link the worst configuration each time. [Memory map](memory-map.md) |
| 17 | Will a larger SwapRAM pool reduce flash traffic? | **Decision open.** A historical 32 KiB PICO link used nearly all its extra RAM margin, and a PicoCalc control failed its reserve. Compare resident bytes, compression rejection, flash fallback and latency with telemetry. Recalculate capacity after the reserve changes on main. |
| 18 | Does relocating UART scratch save power? | **Unmeasured.** Packing places all flash work in SRAM4 and could free SRAM5 for a later power-state experiment. It saves no linked bytes and placement alone leaves the bank powered. Pair UART boot, reboot and flash operations with supply-current readings before changing the layout. |
| 19 | Is a smaller USB ring safe under backpressure? | **Unmeasured for admission.** Peak and drop counters exist. Measure boot, consecutive bursts, disconnect, slow host, reattach and prolonged console load. Record drops, blocked time and tail latency against the current configuration, including PicoCalc's different scratch owners. |
| 20 | Does a watermark reveal stack-pointer minima? | **No.** Paint measures touched words, so reserved unwritten frames and sentinel coincidences can hide depth. Pair overwrite watermarks with minimum-SP captures and verify the sampling and fault code's own stack cost. [Capacity](../../../sys/arch/rp2040/doc/CAPACITY.md) |
| 21 | Which static stack chain can happen in one execution? | **Highest priority.** Drive traced script exec, close-on-exec terminal drain, interruption, last close of an unlinked file with indirect blocks and Dhara collection. Count how often each prerequisite actually occurs. A passing stdio run cannot establish coverage of this chain. [Stack model](../kernel-size-flags/README.md) |
| 22 | Can interrupt and swap overlays coexist at the worst point? | **Open.** Measure actual masking and nesting there, with USB/UART, clock, PicoCalc input and swap-buffer pressure. Log the call-chain waypoint and interrupt state so a lower peak can be explained rather than generalized. |
| 23 | Can the failure reporter run on the exhausted stack? | **Open.** The retained model has a possible panic re-entry through console input, leaves deeper recursion unbounded and omits combined panic/swap and renderer fault cases. Prove recursion bounds or isolate bounded panic output before using it as the observation channel. |
| 24 | Are ROM and loader stacks pinned to actual board bytes? | **Open on silicon.** The B2 ROM model is hash-bound. A version-byte match alone leaves identity unproven, and the PicoCalc loader's second stage differs. Capture ROM identity and measure the actual loader separately. |
| 25 | Where does the storage operation become durable? | **Model guarded.** The full sentinel map checks all 464 complete-operation boundaries. Early acknowledgement, a changed programmed byte and omitted metadata each fail. Keep acknowledged-new, unacknowledged-old-or-new and untouched-sentinel requirements explicit. |
| 26 | Does the old partial-write counterexample still happen? | **Remediated on main and guarded here.** The repaired journal passes all 464 half-program and half-erase cuts. The bypass control reproduces mixed sector 0 at cut 26 before acknowledgement. The gate now fails if production accepts that result. |
| 27 | What if the next cut occurs before a good replacement checkpoint? | **Observed and contained.** The full-map model passes eight consecutive half-checkpoint cuts and fails resume on the ninth. The driver now returns EIO for recognized journals with no valid root. Test wrap and repeated erase cuts before changing the search bound or claiming recovery. |
| 28 | Does a controlled reset represent a real power cut? | **Separate tests.** Use the existing command reset hook to capture phase, offset and before/after bytes. Then use a repeatable external supply cut on a disposable image. Require reopen, filesystem check, file and unrelated sentinels, and rollback proof. [Wear recovery](wear-recovery.md) |
| 29 | Can wear telemetry support a policy decision? | **Open.** Counters establish operations and amplification. Physical erase-address distribution, retention and timing require separate evidence. Compare equal workloads at fixed occupancy and sync policy before admitting a batching or eviction change. |
| 30 | Is the profile matrix representative of shipped hardware? | **Partly resolved.** Six tracked kernel links and a PICO_UART model boot cover distinct steps. On hardware pin model, loader, clock, flash part and image pair, then sample the applicable UART, USB and PicoCalc workloads. Treat optional PSRAM as its own configuration. |
| 31 | What is the failure and rollback procedure? | **Open for the new hardware workloads.** Capture a known-good flash image, verify its identity, use a disposable root, retain raw serial and power traces, then verify restoration. Select each physical board by its USB port when multiple boards are present. |
| 32 | What would change the release decision? | **Order established.** Reproduce the stack-chain hypotheses and storage contract on the target before spending risk on marginal savings. For each candidate set pass thresholds for margin, latency, recovery, diagnostic coverage and compatibility before measurement. Keep the existing policy when a threshold lacks evidence. |

## Measurements on the successor tree

The local logged build used GCC 15.3.1, GNU binutils 2.44 and Debian bmake
20200710. The kernel sources and link inputs are those of `0a211d15`.
`check-kernel-headroom` requires 1,024 bytes free against the 130,816-byte
flash limit. The compact column comes from private relinks and all 163
target comparisons per configuration through
`tools/rp2040_compact_dispatch_probe.py`. The bad-target control fails in
each configuration. Neither relink executes an entire kernel system call.

| Configuration | Ordinary loaded span | Ordinary headroom | Compact loaded span | Span saved |
| --- | ---: | ---: | ---: | ---: |
| PICO | 98,624 | 32,192 | 98,368 | 256 |
| PICO_DIAG | 101,184 | 29,632 | 100,928 | 256 |
| PICO_UART | 90,424 | 40,392 | 90,168 | 256 |
| PICOCALC | 128,912 | 1,904 | 128,656 | 256 |
| PICOCALC_LEAN | 120,976 | 9,840 | 120,464 | 512 |
| PICOCALC_LEAN_DIAG | 123,280 | 7,536 | 123,024 | 256 |

PICOCALC has 880 bytes over the gate's minimum. Its compact prototype
adds 256 bytes of margin. That trade is useful near a known threshold,
but compiler and board-descriptor placement can change the result. The
current experiment preserves initialized data and ordinary BSS across
all six links. The linked measurement includes size and placement, while
the executed comparison separately checks target selection. This matrix
predates the corrupt-checkpoint guard. Adding that guard increases the
PICO_UART and PICOCALC_LEAN_DIAG spans by 256 bytes each in this toolchain.
PICOCALC still has 1,904 bytes free and all six pass the headroom gate.

The stack and ROM tools passed their known-good self-tests and rejected
24 and six malformed inputs respectively. The retained six-configuration
stack sums are from `793d3432` with GCC 16.2.0. This review rebuilt current
PICOCALC in a private worktree with GCC 15.3.1, adding
`-fstack-usage -fcallgraph-info=su` to the normal debug flags. The resulting
ELF has the same section sizes as the ordinary build. Running
`stack_chain.py` on that ELF and call graph gives these bounds.

| PICOCALC modeled path | Bytes | Stack budget |
| --- | ---: | ---: |
| Deepest syscall chain alone | 2,424 | 2,048 |
| U-area sum with modeled interrupt nesting | 3,516 | 2,048 |
| U-area sum with swap-buffer overlay | 3,972 | 2,048 |
| U-area sum with panic overlay | 6,940 | 2,048 |
| Render stack with modeled interrupt nesting | 1,464 | 2,048 |
| Render stack with panic overlay | 4,936 | 2,048 |

The current call graph still points at script exec, terminal drain,
traced exit, final inode release and a flash collection erase. Its
sum assumes the listed interrupt levels can occur on top and reports
panic and swap separately. A reachable-path test should record the
minimum SP, each relevant event, interrupt priorities and masking
before treating a shorter real trace as a tighter bound.

## Repeated checkpoint cuts

A sanitized host experiment reused the 989-sector sentinel workload.
It interrupted the first checkpoint program after the first 512 bytes.
After each reset it discarded all Dhara state, checked every logical
sector, and retried on the same flash bytes without an intervening
successful checkpoint. It recovered all expected sectors through eight
attempts, spanning erase blocks 142 to 149. On the ninth attempt,
in block 150, the eight-block backward scan could no longer reach the
last whole checkpoint. Before the guard, the journal returned
`DHARA_E_TOO_BAD` and `fl_setup()` ignored it. The same media now yields
`DHARA_E_CORRUPT_MAP` and the flash driver returns `EIO` before
publishing root capacity or a writable empty map.

The driver oracle tears a real first checkpoint in its NOR model.
The old driver falsely opens it and publishes capacity. The repaired
driver rejects it without an erase or program, refuses a direct buffer write, still opens raw swap, and
accepts and reads the checkpoint when the complete bytes are restored.
The existing blank-media provisioning path still works. A control that
ignores the corruption status fails the same assertions.

Changing only the backward search in a private journal copy to 16 blocks
lets this workload pass all 12 attempted cuts. This narrows the immediate
cause. It leaves recovery across wrap, older-root safety after possible
page reuse, worst-case boot time and behavior after the new bound
unsettled. It also leaves the separate case of nonblank media whose
checkpoint header is unrecognizable. Changing the shared
`DHARA_MAX_RETRIES` would affect capacity and other retry paths as well.
Choose a recovery bound and an explicit blank-media policy separately
from allowing writes on a damaged root.

## Execution order

1. Keep the three regression repairs in separate reviewable changes from
   layout and storage policy. Rebuild with the affected make implementation
   and run the assembler and recovery suites that own the failures.
2. Keep linked dispatch verification independent from the representation
   choice. Use the private probe's exact-master comparison to evaluate the
   added coverage over the generated-source gate.
3. Stage stack and storage tests on disposable images. Capture stack-pointer
   minima, overwrite marks and which chain waypoints were reached. For
   storage, start with command-level resets and advance to repeated cuts
   before electrical interruptions.
4. Recompute current margins and operation counts after those results.
   Compare compact dispatch, SRAM5 packing and pool-size alternatives against
   the same built tree, production workloads and agreed limits.

A modeled stack sum above budget is an unresolved bound, and the observed
220-byte watermark margin applies to its workload only. No matching board is
attached to this run. Physical results need an identified board and an
explicit test opt-in. The experiments that change timing, layout or
persistence remain separate decisions with the thresholds above.
