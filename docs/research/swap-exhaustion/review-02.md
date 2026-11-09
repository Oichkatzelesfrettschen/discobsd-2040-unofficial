# Adversarial review and LARGE-fork design

This review examines source revision
`958c0c0843eb86752710cba217e1a07ebf6b1457`, the unchanged swap-related source
used by `audit-01.md` at `2d461c64c3fd4c08ca8e236afe837df5a781d4a5`,
and the LARGE-bonus build at
`77676485aaf7db07ffeafb40d5fe233e6ce3d835`. Source, linked binaries, host
execution, and emulation are separate evidence classes below. The review did
not use a board or network access.

## 1. Claim-by-claim verdict on `audit-01.md`

### Execution record and evidence limits

The following commands and outputs provide the measured evidence referenced as
`E1` through `E10` in the claim ledger.

- **E1 -- revision and build.** `git rev-parse HEAD` printed
  `958c0c0843eb86752710cba217e1a07ebf6b1457`. A targeted `git diff --quiet
  2d461c64c3fd4c08ca8e236afe837df5a781d4a5 HEAD -- sys/kern/kern_fork.c
  sys/kern/vm_sched.c sys/kern/vm_swap.c sys/kern/vm_swp.c sys/kern/subr_rmap.c
  sys/dev/swap.c sys/arch/rp2040/rp2040/swapram.c` exited 0. In a throwaway
  source copy, `PYTHON=/usr/bin/python3; export PYTHON; bmake MACHINE=rp2040
  build` exited 0 after 581.7 seconds. This build establishes compilation, not
  board behavior.
- **E2 -- production allocator and temporary-device fixtures.** The commands
  `bmake -C tests/kernel rmap_test_compact rmap_test_wide
  rmap_test_diagnostic swap_ioctl_test` followed by execution of those four
  binaries reported 139, 137, 142, and 56 passing checks, respectively. The
  temporary-device mutant loop rejected
  `swap_ioctl_media-size-fallthrough`, `swap_ioctl_release-fallthrough`,
  `swap_ioctl_shortage-output-suppression`,
  `swap_ioctl_stale-released-size`, and `swap_ioctl_unrounded-free`. The
  sanitizer probe exited with `Bad system call`, so this review records the
  sanitizer variant as **UNPROVEN**, rather than passed. The existing sources
  define the exercised allocator cases at `tests/kernel/rmap_test.c:347-494`
  and the temporary-device oracle at `tests/kernel/swap_ioctl_test.c:90-194`.
- **E3 -- existing RP2040 host fixtures.** The exec-spool fixture and NOR
  flash-swap fixture exited 0. The production evacuation fixture compiled with
  `SWAPRAM_KB=64` exited 0. Recompiling the same fixture with
  `SWAPRAM_KB=16` produced 36 failed assertions and terminated at
  `panic: swapram: no image`; its `roundtrip()` still constructs eight images
  and expects all eight to fit (`sys/arch/rp2040/test/swapram/swapram_evac_test.c:360-397`),
  while the Makefile fixes the host fixture at 64 KiB
  (`sys/arch/rp2040/test/swapram/Makefile:36-45`). The 16 KiB result rejects the
  proposition that the current fixture already supplies a valid size matrix;
  it does not reject the kernel evacuation mechanism.
- **E4 -- call graph and source-pattern sweeps.** GNU `cflow`, `cppcheck
  2.22.0`, Semgrep `1.164.0` with metrics and version checks disabled, Weggli,
  and ast-grep were run over the fork, scheduler, swap, allocator, exec-spool,
  temporary-device, and SwapRAM sources. Semgrep found 12 panic calls but
  could not parse every K&R definition. Weggli matched modern definitions more
  reliably than K&R definitions, and ast-grep produced no useful K&R matches.
  Cppcheck's reports after `panic("no procs")` were unreachable-code modeling
  artifacts. These parser limitations make the sweeps corroboration rather
  than proof of absence.
- **E5 -- linked call graph.** `rizin -q -c 'aaa; axt sym.swapout; q'
  sys/arch/rp2040/compile/PICO/unix.elf` found linked calls from `newproc` at
  `0x10002920` and `sched+0x24` at `0x1000e120`, and no third linked caller.
  `arm-none-eabi-objdump -dr` supplied the corresponding call-site and routine
  disassembly. This result agrees with the two source calls at
  `sys/kern/kern_fork.c:173-179` and `sys/kern/vm_sched.c:50-59`.
- **E6 -- exact executable and allocator emulation.** With `PYTHON` selected,
  `"$PYTHON" docs/research/swap-exhaustion/scratch-02/emulate_swap_allocator.py
  ../../mpu/large-bonus-region/sys/arch/rp2040/compile/PICO/unix.elf
  ../../mpu/large-bonus-region/tests/rp2040/swapram_large/bigtest` printed:

      a.out header: midmag=0x00000107 text=8040 data=200 bss=150156
      observed image: data_after_text=151704 blocks=149+3+3
      aligned_blocks=156 sectors=39 program_pages=614 exchange_peak=528
      capstone: address=0x10005f74 bytes=420 instructions=205
      unicorn recorded map: failed=95/95 state_changes_on_failure=0
      unicorn positive map: result=156 cursor=160
      outputs=(4, 153, 156) map=[(8, 160)] instructions=139

  The script uses pyelftools for ELF symbols and bytes, Capstone for Thumb
  disassembly, and Unicorn for instruction execution. `exec_aout_check()` saves
  the original 8,040-byte OMAGIC text length in
  `p_tsize` (`sys/kern/exec_aout.c:149-153,221-225`), and `swapout()` subtracts
  that clean text (`sys/kern/vm_swap.c:26-33,147-169`). The executable's Thumb
  symbol bit was cleared before emulation. The named artifact therefore has an
  exact 156-block image span. The audit's 164-block value remains valid only
  for its stated `p_tsize == 0` condition.
- **E7 -- binary size and structure measurements.** `arm-none-eabi-size` on
  the LARGE-bonus `unix.elf` printed 95,956 bytes of text, 256 bytes of data,
  and 39,856 bytes of BSS. `arm-none-eabi-nm -S --size-sort` and
  `arm-none-eabi-readelf -S -s` showed a 16,384-byte `.swapram_pool`, an
  8,192-byte scratch region, and 34,860 bytes of kernel-flash headroom above
  the required 1,024-byte margin. The linker symbols place the user window at
  `0x20000000..0x20024000`, SwapRAM at `0x20024000..0x20028000`, data at
  `0x20028000`, BSS through `0x2002bdf0`, process 0's user area at
  `0x2003e800`, and scratch at `0x20040000..0x20042000`; the measured static
  RAM gap is 76,304 bytes. Bloaty corroborated the ELF section totals. Pahole
  measured `struct proc` as 104 bytes, `struct map` as 12, `struct mapent` as
  4, `struct exec_params` as 284, and `struct user` as 972 bytes inside the
  3,072-byte `USIZE`. Linked function sizes were 508 bytes for
  `swapram_evacuate`, 448 for `swapout`, 420 for
  `malloc3_contiguous_next`, 784 for `newproc`, 276 for `sched`, 232 for
  `hsx_expand`, 284 for `hsx_dec_run`, and 192 for `swapram_in`. These are
  sizes of the existing build, not estimates of unimplemented designs.
- **E8 -- compression model.** Running `./swapram_test <a.out> 4096` for the
  built init, update daemon, shell, second shell, and su images measured raw to
  compressed sizes of 8,492 to 706, 7,252 to 473, 16,776 to 2,001, 16,776 to
  2,001, and 9,840 to 734 bytes. The aggregate was 59,136 to 5,915 bytes. The
  fixture reads executable data and zero BSS, but synthesizes stack and user
  bytes (`sys/arch/rp2040/test/swapram/swapram_test.c:372-425`), so those ratios
  are estimates rather than captures of the board occupants. A source-derived
  bound is stronger: every image selected for evacuation already coexists in
  the 16 KiB compressed pool selected by the PICO build, so a packed flash
  representation of those compressed streams requires at most 16 1-KiB blocks.
- **E9 -- root/swap geometry prototype.** A throwaway build changed raw swap
  from 384 KiB to 624 KiB and the Dhara region from 1,536 KiB to 1,296 KiB.
  `tools/bin/flashimg -c` printed `region 1296 kbytes, capacity 827 sectors of
  1024 bytes, 827 kbytes`. Rebuilding the complete root with `FS_KBYTES=826`
  printed `96 files 739 blocks 78 free` and `Volume size: 826 blocks`; the
  repository account-image check exited 0, and `flashimg` wrote all 827 sectors
  into the modeled 1,296-KiB region. The current partition constants and root
  sizing rule are `sys/arch/rp2040/dev/flash.h:46-101` and
  `distrib/rp2040/Makefile.inc:1-9`. This result proves host-image fit for that
  source tree, not flash behavior or boot on a board.
- **E10 -- unavailable and prohibited evidence.** The structural Graft query
  surface requested an approval which the session policy could not grant, so
  source search, object inspection, and disassembly supplied the navigation
  evidence. The review did not open a serial device, run a flashing tool, or
  execute a board gate. Every board-only claim remains explicitly pending.

### Granular claim ledger

Each row covers the claim or tightly coupled claims in the cited audit range.
`CONFIRMED` means the cited source or an executed instrument establishes the
bounded statement. `REFUTED` means the source, binary, or execution supplies a
counterexample. `UNPROVEN` means the available evidence cannot decide the
claim.

| Audit claim | Verdict | Adversarial evidence |
| --- | --- | --- |
| `audit-01.md:3-7` identifies the audit revision and says the first audit ran no build, mutant, board, or network query. | **CONFIRMED.** | The file records its own provenance. E1 confirms that the relevant implementation is unchanged at the reviewed HEAD; E1--E9 add the execution evidence absent from the first audit. |
| `audit-01.md:11-19` calls the panic a correctness and denial-of-service defect, identifies one `panic("out of swap space")` site reached by fork and the swapper, and says the other named consumers refuse shortage without that panic. | **CONFIRMED.** | The panic and allocation path are at `sys/kern/vm_swap.c:147-181`; E5 finds only the linked `newproc` and `sched` callers. Exec-spool cleanup is at `sys/kern/exec_subr.c:31-62,142-200`, temporary allocation returns a zero size at `sys/dev/swap.c:248-291`, and evacuation rolls reservations back at `sys/arch/rp2040/rp2040/swapram.c:561-630`. |
| `audit-01.md:21-31` says the recorded map has 164 free blocks, the image needs at most 164 when `p_tsize == 0`, placement repair can fit the first copy, and the two-image peak is conditionally 544. | **CONFIRMED for the map, the explicit zero-text condition, and first-copy total capacity; REFUTED as the demand of the named built artifact.** | The four transcript runs total 164 free blocks (`brief-01.md:16-25`; `audit-01.md:680-686`). E6 rejects a contiguous exact allocation at all 95 recorded-map cursor positions but accepts a 156-block positive-control run, so split placement or compaction can use the available total for the first copy while the current run layout cannot. E6 proves `p_tsize == 8040`, an exact 156-block span, and an exact `216 + 156 + 156 = 528` peak. Both 528 and conditional 544 exceed the 380 allocatable blocks initialized at `sys/kern/init_main.c:145-166`. |
| `audit-01.md:33-46` recommends transactional reservation, prepublication `ENOMEM`, victim recovery, and protected exchange capacity; it says the combination does not itself make the recorded fork succeed. | **CONFIRMED for Layers B and C; CONFIRMED that current geometry still refuses; REFUTED if read as excluding a backing-capacity redesign.** | `newproc()` publishes before `swapout()` (`sys/kern/kern_fork.c:76-178`), and `sched()` writes the resident first (`sys/kern/vm_sched.c:50-62`). E9 supplies the separately proven larger-backing direction the audit itself allowed at line 46. |
| `audit-01.md:50-55` separates retained board observation from source and says the audit has no owner trace. | **CONFIRMED.** | The transcripts are quoted at `brief-01.md:7-25`. Neither those lines nor the free-map diagnostic at `sys/kern/vm_swap.c:174-181` records owners. E10 adds no board capture. |
| `audit-01.md:57-63` defines image span and exchange headroom, and distinguishes a reservation from a count or preflight. | **CONFIRMED.** | `swapimagespan()` rounds data-after-clean-text, stack, and `USIZE` at `sys/kern/vm_swap.c:38-46`; `malloc3_contiguous_next()` removes the run only on success at `sys/kern/subr_rmap.c:289-374`. E6 supplies the exact span. |
| `audit-01.md:65-68` limits the historical comparison to the three local snapshots. | **CONFIRMED.** | The cited trees and exact implementations are the bounded evidence at `audit-01.md:231-259`; no network or broader historical survey occurred (E10). |
| `audit-01.md:74-78` says one flash-allocation refusal panics and `swapout()` has no error contract. | **CONFIRMED.** | `swapout()` returns `void` and panics at `sys/kern/vm_swap.c:135-181`; E5 shows both linked callers consume that void contract. |
| `audit-01.md:80-85` says `newproc()` has already acquired and published child state when `swapout()` can panic. | **CONFIRMED.** | The child leaves `freeproc`, enters `pidhash` and `allproc`, inherits references and SwapRAM state, becomes `SRUN`, and then reaches `swapout()` at `sys/kern/kern_fork.c:76-178`. |
| `audit-01.md:87-90` says `sched()` clears `SLOAD` and may remove the victim from the run queue before the fallible operation. | **CONFIRMED.** | Victim mutation occurs at `sys/kern/vm_sched.c:95-122`; the next loop performs `swapout()` at lines 50-59. |
| `audit-01.md:92-96` says boot calls `newproc(0)` directly and a tri-state return needs an explicit boot policy. | **CONFIRMED.** | Process 1 creation is at `sys/kern/init_main.c:180-196`; `fork1()` currently interprets every nonzero `newproc()` result as the child branch at `sys/kern/kern_fork.c:241-249`. |
| `audit-01.md:100-106` calls exec-spool shortage transactional. | **CONFIRMED for the exercised and source-visible paths.** | Acquisition, migration, and discard are at `sys/kern/exec_subr.c:31-62,142-200,730-776`; E3's exec-spool fixture passed. Concurrency with a sleeping swap I/O operation remains a proposed interleaving gate rather than executed proof. |
| `audit-01.md:108-113` says `TFALLOC` reports shortage cleanly and retains its extent across close. | **CONFIRMED.** | Allocation and zero-size shortage are at `sys/dev/swap.c:248-291`; `swcclose()` does not free the extent at `sys/dev/swap.c:105-125`. E2 passed the fixture and rejected all five existing mutants. |
| `audit-01.md:115-120` says SwapRAM evacuation reserves all destinations before writes and fully rolls back reservation failure. | **CONFIRMED at 64 KiB; UNPROVEN at the PICO 16-KiB size with the current fixture.** | The transaction is at `sys/arch/rp2040/rp2040/swapram.c:561-668`. E3 passed the 64-KiB fixture, while its structurally invalid 16-KiB invocation failed before supplying a calibrated verdict. |
| `audit-01.md:122-127` says successful evacuation protects no extent for the immediately following resident eviction. | **CONFIRMED.** | `swapram_service()` precedes `swapout(in_core)` at `sys/kern/vm_sched.c:50-59`; LARGE service closes and evacuates the pool at `sys/arch/rp2040/rp2040/swapram.c:688-699` without reserving the resident image's flash span. |
| `audit-01.md:131-138` recommends a prepublication owned reservation and rejects check-then-allocate. | **CONFIRMED.** | The allocator searches before mutation and removes the selected run on success (`sys/kern/subr_rmap.c:289-374`). Swap I/O may sleep (`sys/kern/vm_swp.c:29-60`), so an unowned preflight has a real interleaving window. |
| `audit-01.md:140-158` enumerates the late unwind and rejects `exit()` as a substitute. | **CONFIRMED.** | The acquisition surfaces are at `sys/kern/kern_fork.c:76-178` and `sys/arch/rp2040/rp2040/swapram.c:784-804`; `exit()` destroys the current process context and creates a zombie at `sys/kern/kern_exit.c:57-119`. Moving reservation ahead of publication avoids most of this reverse transaction. |
| `audit-01.md:162-167` requires victim restoration, pending-selection cancellation, a capacity wait, and release wakeups. | **CONFIRMED as a required failure invariant, not as existing behavior.** | The state requiring restoration is mutated at `sys/kern/vm_sched.c:95-129`. Existing `swapout()` cannot refuse (`sys/kern/vm_swap.c:135-181`), so the required path is unimplemented and unrun. |
| `audit-01.md:169-184` says sleeping does not establish progress and requires either a protected hole or bounded exchange. | **CONFIRMED.** | The resident is written before the incoming image is read and freed (`sys/kern/vm_sched.c:56-59`, `sys/kern/vm_swap.c:92-109`), and the machine provides one flat process window (`sys/arch/rp2040/include/machparam.h:115-128`). E6 shows the exact current capacity shortfall. |
| `audit-01.md:186-191` says heap and stack growth can increase image demand without reserving swap. | **CONFIRMED.** | Heap admission checks the user window at `sys/kern/kern_mman.c:23-58`; syscall and signal entry update stack accounting at `sys/arch/rp2040/rp2040/syscall.c:188-200` and `sys/arch/rp2040/rp2040/sig_machdep.c:77-90`. None calls a swap-reservation function because none exists. |
| `audit-01.md:195-199` says the extent must remain owned across sleeping swap I/O and cursor publication cannot provide ownership. | **CONFIRMED.** | Buffer acquisition and completion waits are at `sys/kern/vm_swp.c:29-60` and `sys/kern/ufs_bio.c:227-249,333-349`; retained-cursor publication is only an index write at `sys/arch/rp2040/rp2040/machdep.c:242-259`. |
| `audit-01.md:201-206` says the 29-entry map bound must be recomputed for persistent or multi-extent reservations and map overflow loses the freed run. | **CONFIRMED.** | The stated live-extent/free-run bound is at `sys/arch/rp2040/include/machparam.h:87-94`, and the overflow diagnostic returns without recording the run at `sys/kern/subr_rmap.c:150-165`. E7 confirms that each map entry is four bytes but does not prove the old count sufficient after a redesign. |
| `audit-01.md:212-217` chooses `ENOMEM` for swap shortage and retains `EAGAIN` for process limits. | **CONFIRMED.** | The shipped manual distinguishes the errors at `share/man/man2/fork.2:46-67`; current process-limit branches set `EAGAIN` at `sys/kern/kern_fork.c:217-240`. |
| `audit-01.md:219-223` declines to make an independent POSIX claim. | **CONFIRMED as an evidence boundary.** | The first brief makes the POSIX assertion at `brief-01.md:44-48`, but E10 records no network standards lookup and the tree has no normative POSIX text. |
| `audit-01.md:225-229` says ARM syscall and libc paths preserve `ENOMEM`. | **CONFIRMED.** | The kernel returns `u.u_error` in `r0` with carry at `sys/arch/rp2040/rp2040/syscall.c:266-279`; the libc stub stores the value in `errno` and returns `-1` at `lib/libc/arm/sys/SYS.h:30-39`. |
| `audit-01.md:233-240` describes the local 2.11BSD snapshot as eager-copy, panic-on-shortage ancestry despite its manual's `ENOMEM`. | **CONFIRMED for that snapshot.** | The cited paths are `BSD_Workspace/211bsd/sys/sys/kern_fork.c:65-85,238-258`, `BSD_Workspace/211bsd/sys/sys/vm_swap.c:101-107`, and `BSD_Workspace/211bsd/man/man2/fork.2:55-67`. This bounded comparison establishes no broader release history. |
| `audit-01.md:242-248` makes the corresponding RetroBSD claim. | **CONFIRMED for that snapshot.** | The eager child copy and panic are at `BSD_Workspace/retrobsd/sys/kernel/kern_fork.c:155-166` and `BSD_Workspace/retrobsd/sys/kernel/vm_swap.c:66-72`; the admission and manual evidence are at `BSD_Workspace/retrobsd/sys/kernel/kern_fork.c:216-234` and `BSD_Workspace/retrobsd/src/man/man2/fork.2:55-67`. |
| `audit-01.md:250-259` says the local 4.4BSD-Lite2 snapshot uses VM copy-on-write and retryable pager failure, which is not a drop-in flat-window solution. | **CONFIRMED.** | Map cloning is at `BSD_Workspace/4.4BSD-Lite2/sys/vm/vm_map.c:1754-1781,2217-2244`; pager failure and page reactivation are at `BSD_Workspace/4.4BSD-Lite2/sys/vm/swap_pager.c:619-637` and `BSD_Workspace/4.4BSD-Lite2/sys/vm/vm_pageout.c:331-340,474-484`. The RP2040 has one untranslated user window (`sys/arch/rp2040/include/machparam.h:115-128`). |
| `audit-01.md:263-267` says the shell distinguishes `ENOMEM`, retries only twice, and carries a stale 32-second comment. | **CONFIRMED.** | The retry and messages are at `bin/sh/xec.c:1074-1098`, `bin/sh/msg.c:23-24`, and `bin/sh/defs.h:506`. |
| `audit-01.md:269-278` says init mishandles failed single-user and startup-script forks, including an indeterminate or stale status. | **CONFIRMED.** | The missing negative-PID branches and status reads are at `sbin/init/init.c:278-335`; `wait()` returns `ECHILD` without a status at `sys/kern/kern_exit.c:258-267`. |
| `audit-01.md:280-283` says init forks login/window children while getty execs login, and init lacks a direct negative-result branch. | **CONFIRMED.** | Getty uses `execle()` at `libexec/getty/main.c:280-281`; init records the fork results at `sbin/init/init.c:424-472`. |
| `audit-01.md:289-309` reconstructs the 384-block partition, four-block scratch reservation, four free runs totaling 164, and 216 allocated blocks whose three intervals do not identify three owners. | **CONFIRMED.** | Geometry is at `sys/arch/rp2040/dev/flash.h:87-101` and map initialization at `sys/kern/init_main.c:145-166`. Arithmetic over the transcript in `brief-01.md:16-25` gives 164 free and 216 allocated; the map representation coalesces adjacent free runs at `sys/kern/subr_rmap.c:104-165` and cannot identify allocated owners. |
| `audit-01.md:311-317` calls 164 only an upper bound because the diagnostic omits `p_tsize`. | **REFUTED for the named built artifact; CONFIRMED for the diagnostic alone.** | E6 reads the executable and linked routine and proves `p_tsize == 8040`, data-after-text 151,704 bytes, and an exact 156-block span. The diagnostic indeed omits `p_tsize`, owner records, and `swapnext` at `sys/kern/vm_swap.c:174-181`; the missing diagnostic field does not make the binary unknowable offline. |
| `audit-01.md:319-323` says the test declares a 150,000-byte BSS and that three data/stack/user extents cannot place the specimen in the recorded holes. | **CONFIRMED for the source array and placement conclusion; corrected for the full executable.** | The array is at `tests/rp2040/swapram_large/bigtest.c:18-22`. E6 measures total executable BSS as 150,156 and data-after-clean-text as 151,704 bytes, or 149 data blocks. The largest recorded hole is 120 blocks, so a data extent alone cannot fit. |
| `audit-01.md:327-333` describes next-fit search, wrap, success-only cursor advancement, descriptor-pressure boundary placement, and retained cursor seeding. | **CONFIRMED.** | The allocator behavior is at `sys/kern/subr_rmap.c:289-374`, the retained cursor at `sys/arch/rp2040/rp2040/machdep.c:209-259`, and E2/E6 exercise failure preservation, wrap, and a positive allocation. |
| `audit-01.md:335-341` says alignment costs zero through three blocks and the frequency or causation of the observed layout is unknown. | **CONFIRMED.** | Alignment arithmetic is at `sys/kern/subr_rmap.c:293-299` and `sys/kern/vm_swap.c:167-169`. The three transcripts show different placement but provide no chronological allocation/free trace (`audit-01.md:680-686`), so any frequency claim remains **UNPROVEN**. |
| `audit-01.md:343-349` lists the records required to attribute every occupied block. | **CONFIRMED as the minimum source-visible ownership set.** | Processes, evacuation, exec spool, and temporary devices all share `swapmap` through the cited paths. No single existing diagnostic emits those owners atomically (`sys/kern/vm_swap.c:174-181`). |
| `audit-01.md:353-364` gives a conditional 544-block peak and says placement-only success does not prove one parent/child alternation. | **REFUTED numerically for the built specimen; CONFIRMED mechanistically.** | E6 gives an exact 528-block peak, still above 380. The resident write precedes incoming release at `sys/kern/vm_sched.c:56-59` and `sys/kern/vm_swap.c:92-109`, so a successful fork-return oracle remains insufficient. |
| `audit-01.md:368-378` describes 4-KiB erase, 256-byte program, append, rewrite, interrupt masking, and XIP-off behavior. | **CONFIRMED.** | Constants are at `sys/arch/rp2040/dev/flash.h:46-53`; append and rewrite paths are at `sys/arch/rp2040/dev/flash_swap.c:21-116` and `sys/arch/rp2040/dev/flash.c:294-348,643-675`. E3's NOR fixture exercised alignment and one-to-zero programming, but it cannot measure silicon latency. |
| `audit-01.md:380-384` gives 41 erases and 645 programs for the conditional zero-text 164-block image. | **CONFIRMED for that explicit condition; REFUTED for the named built specimen.** | The conditional arithmetic is internally consistent. E6 measures 39 erase callbacks and 614 program callbacks for 151,704 data bytes, 2,244 stack bytes, and 3,072 user bytes. |
| `audit-01.md:386-391` labels the 2.103-second result as a stale estimate and requires a current board measurement. | **CONFIRMED.** | The old constants are quoted at `docs/research/ram-compression.md:3-8`. The only defensible current formula is `T = E*t_erase + P*t_program + overhead`; E10 records that no board timing ran. |
| `audit-01.md:393-400` says reservation/refusal and placement add no direct flash callbacks, while multi-extent layouts need ownership metadata. | **CONFIRMED.** | A removed map extent is metadata until `swap()` writes it (`sys/kern/subr_rmap.c:289-374`, `sys/kern/vm_swap.c:189-235`). Erase-sector ownership follows the image-write behavior at `sys/arch/rp2040/dev/flash.c:643-675`. |
| `audit-01.md:401-404` says relocating all 216 allocated blocks necessarily adds at least 54 erases and 864 program callbacks. | **UNPROVEN.** | Fifty-four sectors follows only if every allocated block is copied. The transcripts do not distinguish data-bearing pages from alignment padding or identify image lengths. The append driver programs the requested byte stream, not every map-padding byte (`sys/arch/rp2040/dev/flash_swap.c:21-52`), so 864 callbacks needs an owner/length trace. Scratch-mediated rewrites can add a second erase per touched sector (`sys/arch/rp2040/dev/flash_swap.c:55-116`), but the touched-sector count is likewise unknown. |
| `audit-01.md:405-407` says swapping the parent first has similar traffic and does not prove later progress without exchange. | **CONFIRMED under current geometry.** | Parent and child initially share the same sizes (`sys/kern/kern_fork.c:157-178`), and E6 shows that holding one exact image leaves only eight free blocks in the recorded occupancy. The two-slot geometry in E9 changes the capacity premise rather than invalidating the current-geometry statement. |
| `audit-01.md:411-421` calls read-only, unprivileged `TFALLOC` a persistent shared-swap reservation path. | **CONFIRMED.** | Generic ioctl admits `FREAD` or `FWRITE` (`sys/kern/sys_generic.c:232-239,300-351`), `swcioctl()` ignores the mode and credentials (`sys/dev/swap.c:228-291`), the manifest gives group-readable temporary nodes (`distrib/rp2040/mi.rp2040:18-22,124-136`), and close retains the extent (`sys/dev/swap.c:105-125`). The shipped `talloc` utility itself opens read-only (`usr.sbin/talloc/talloc.c:23-31`). |
| `audit-01.md:423-428` requires kernel authorization and a defined lifetime, rather than a mode-only manifest change. | **CONFIRMED.** | The mutation occurs in `swcioctl()` after generic descriptor admission; protecting only today's node mode would leave the kernel API callable through any later permissive node (`sys/kern/sys_generic.c:232-239`, `sys/dev/swap.c:228-302`). |
| `audit-01.md:430-437` says an unprivileged fork can reach panic after both storage tiers refuse and ordinary growth can increase demand. | **CONFIRMED as source reachability; UNPROVEN as a separately executed exploit.** | `fork1()` performs process-count but no swap admission (`sys/kern/kern_fork.c:217-242`); growth paths are `sys/kern/kern_mman.c:23-58` and `sys/arch/rp2040/rp2040/syscall.c:188-200`; the terminal panic is `sys/kern/vm_swap.c:167-181`. E10 records no board exploit run. |
| `audit-01.md:439-445` says exec spools can add transient pressure but does not claim a reachable multi-spool exploit. | **CONFIRMED for one spool; UNPROVEN for concurrent multi-spool pressure.** | The allocation can precede a sleeping buffer acquisition at `sys/kern/exec_subr.c:31-59` and `sys/kern/ufs_bio.c:227-249,333-349`. E3 passes single-spool refusal; no interleaving fixture ran. |
| `audit-01.md:449-460` describes the existing kernel, allocator, temporary-device, and mutant surfaces. | **CONFIRMED and executed.** | Target ownership is at `Makefile:367-370` and `tests/kernel/Makefile:473-484,1003-1042`; E2 supplies measured check counts and mutant rejection. The sanitizer subvariant remains **UNPROVEN** because the sandbox rejected its runtime probe. |
| `audit-01.md:462-467` says the existing fork and scheduler fixtures do not exercise exhaustion. | **CONFIRMED.** | The interval-timer fixture stubs `swapout()` at `tests/kernel/itimer_test.c:62-67`; the scheduler fixture's main covers sleep and meters at `tests/kernel/synch_meter_test.c:119-124`. E2 provides no contrary gate. |
| `audit-01.md:469-477` says exec-spool and evacuation tests cover their local transactions, while evacuation needs 16- and 64-KiB variants. | **CONFIRMED, with a stronger defect found in the fixture.** | The source oracles are `tests/rp2040/exec_spool/exec_spool_test.c:400-472` and `sys/arch/rp2040/test/swapram/swapram_evac_test.c:400-442`. E3 passes the former and the 64-KiB latter, but proves that changing only `SWAPRAM_KB` does not produce a valid 16-KiB variant because `roundtrip()` is fixed to eight images. |
| `audit-01.md:479-483` says the NOR model covers media semantics but lacks calibrated source mutants. | **CONFIRMED.** | The model and cases are at `tests/rp2040/flash_swap/flash_swap_test.c:46-87,150-274`; its Makefile has only build/test rules at `tests/rp2040/flash_swap/Makefile:7-11`. E3 passed the positive fixture but ran no flash-source mutant. |
| `audit-01.md:487-519` ranks transactional reservation plus exchange admission first and states its invariant. | **CONFIRMED as the correct Layers B/C repair; incomplete as a LARGE-fork success design.** | The named functions are the actual shared surfaces (`sys/kern/vm_swap.c:125-255`, `sys/kern/kern_fork.c:76-269`, `sys/kern/vm_sched.c:50-129`). E6 proves current capacity would reject the fork; E9 supplies the additional geometry needed to meet brief 02's success requirement. |
| `audit-01.md:521-527` names init, map pressure, starvation, and hard media errors as residuals. | **CONFIRMED.** | Init's failed-fork paths are at `sbin/init/init.c:278-335`; map overflow is at `sys/kern/subr_rmap.c:150-165`; a reservation wait has no current progress proof; hard swap I/O panics at `sys/kern/vm_swp.c:29-42`. Layer B must not relabel hard I/O corruption as capacity refusal. |
| `audit-01.md:529-539` says in-place exchange is the only listed way to make the recorded case progress without two full images and ranks it below refusal. | **CONFIRMED under the existing 384-KiB capacity premise; REFUTED as an exclusive redesign claim.** | E9 provides 620 allocatable blocks and two fixed slots, enough for both 164-block conditional images beside 216 general blocks. Direct use of the overlapped SwapRAM bytes remains invalid during LARGE (`sys/arch/rp2040/rp2040/swapram.c:721-745`). |
| `audit-01.md:541-551` says placement or arbitrary extents alone cannot solve the current conditional 544 peak and that scatter/gather needs new metadata and sector ownership. | **CONFIRMED under current geometry.** | E6 strengthens the peak to an exact 528, still above 380. The old three-extent note and current single-run requirement are at `docs/research/memory-ownership-plan.md:304-305,485-500`; erase ownership is enforced at `sys/arch/rp2040/dev/flash.c:643-675`. Expanded capacity plus fixed placement is a compound redesign, not placement alone. |
| `audit-01.md:553-559` says compaction creates a run but not capacity and carries publication/recovery cost. | **CONFIRMED.** | Compaction cannot change the 380-block total. The exact peak is 528 by E6. The audit's specific 864-program lower bound remains **UNPROVEN** for the reason recorded above. |
| `audit-01.md:561-572` says LARGE-only admission and observed-fork-only refusal are insufficient at a shared primitive. | **CONFIRMED.** | The named consumers reach `swapmap` at `sys/kern/vm_swap.c:167-169`, `sys/kern/exec_subr.c:31-59`, `sys/dev/swap.c:248-291`, and `sys/arch/rp2040/rp2040/swapram.c:588-635`; `sched()` independently calls `swapout()` (E5). Admission-only remains useful as a guard but cannot meet the requested success outcome. |
| `audit-01.md:574-577` labels all ten proposed gates unrun. | **CONFIRMED.** | E2 and E3 execute preexisting component fixtures only. No target named in `audit-01.md:579-676` exists as a completed calibrated gate at this revision. |
| `audit-01.md:579-587` proposes `check-swap-reservation`. | **CONFIRMED as a necessary gate specification, with 156 as the exact primary specimen.** | E6 already exercises all 95 recorded-map cursor positions and proves failure preservation plus a positive 156-block map, but it does not exercise an implemented reservation object, ownership ledger, descriptor variants, or the named mutants. |
| `audit-01.md:589-599` proposes `check-fork-swap-refusal`. | **CONFIRMED as necessary and entirely unimplemented.** | The publication and ownership surfaces are at `sys/kern/kern_fork.c:76-178`; no current fixture links production `newproc()` to a fallible reservation, because that interface does not exist. |
| `audit-01.md:601-610` proposes `check-scheduler-swap-refusal`. | **CONFIRMED as necessary and entirely unimplemented.** | The mutation ordering is `sys/kern/vm_sched.c:50-129`. The gate must additionally include a state with no independent releaser, proving bounded refusal instead of an eternal capacity sleep. |
| `audit-01.md:612-619` proposes `check-swap-exchange-headroom`. | **CONFIRMED for current-geometry refusal; requires a success variant for the chosen design.** | E6 supplies exact 216+156+156 arithmetic. The 164-block case remains a conservative conditional mutant. E9 changes the chosen oracle from refusal to repeated alternation in the two fixed slots. |
| `audit-01.md:621-628` proposes `check-swap-reservation-interleavings`. | **CONFIRMED as necessary and unimplemented.** | The actual sleep sites are `sys/kern/vm_swp.c:29-60` and `sys/kern/ufs_bio.c:227-249,333-349`; E3's sequential fixtures do not cover competing ownership. |
| `audit-01.md:630-637` proposes a 16-/64-KiB evacuation matrix. | **CONFIRMED and strengthened.** | E3 proves the 16-KiB fixture must parameterize image count and pool occupancy rather than merely change `SWAPRAM_KB`; the existing all-or-none oracle at `sys/arch/rp2040/test/swapram/swapram_evac_test.c:400-442` remains valid after that repair. |
| `audit-01.md:639-647` proposes `check-tfalloc-admission`. | **CONFIRMED as necessary and unimplemented.** | E2 covers shortage/release arithmetic only. Descriptor mode, credentials, owner lifetime, capacity wakeup, and protected-headroom mutants require new fixture shims around `sys/dev/swap.c:105-125,228-302`. |
| `audit-01.md:649-654` proposes `check-init-fork-refusal`. | **CONFIRMED as necessary and unimplemented.** | The single-user, startup, login, and window paths at `sbin/init/init.c:278-335,424-472` have no injected-failure fixture. |
| `audit-01.md:656-663` proposes 41/645 as the flash-operation oracle. | **CONFIRMED only for the explicit `p_tsize == 0` 164-block mutant; REFUTED for the named executable.** | E6 establishes the exact primary oracle as 39 erases and 614 program callbacks. Both cases need callback-position failure mutants and adjacent-image preservation in the NOR model. |
| `audit-01.md:665-676` proposes an authorized board gate expecting clean refusal under the earlier recommendation. | **CONFIRMED as the Layers A--C refusal gate, but insufficient for brief 02.** | Board execution remains pending by E10. The chosen two-slot design needs a second authorized gate whose expected result is successful fork, repeated parent/child alternation, exit, wait, owner-ledger cleanup, and zero panic. |
| `audit-01.md:680-686` says all three retained layouts have 216 occupied and 164 free blocks while largest-run placement varies. | **CONFIRMED for totals and variation; UNPROVEN for ownership and future stability.** | Direct arithmetic over the three printed maps gives identical totals and largest runs of 120, 100, and 80. The transcripts contain no owner ledger, epoch-wide trace, or falsifier for a later occupancy change. |

The ledger changes four quantitative claims from the first audit: the named
image is 156 rather than 164 blocks; its exchange peak is 528 rather than 544;
its append cost is 39 erases and 614 programs rather than 41 and 645; and the
864-program compaction lower bound lacks the owner and padding evidence needed
to call it necessary. The panic, publication, scheduler-ordering, authorization,
and progress findings survive those corrections.

## 2. Verdicts on J1--J5

### J1 -- order A, then B, then C

**REFUTED as stated; the implementation order remains defensible.** Layer A is
small and independent because `TFALLOC` authorization is confined to generic
ioctl admission and `swcioctl()` (`sys/kern/sys_generic.c:232-239,300-351`,
`sys/kern/sys_inode.c:84-94`, `sys/dev/swap.c:228-302`). Boot invokes `fsck -p`
before leaving the root-controlled startup path (`etc/rc:17-28`), and fsck opens
`/dev/temp0` with `O_RDWR` before `TFALLOC` (`sbin/fsck/setup.c:112-130`), so a
root-plus-write policy preserves that privileged consumer.

The exclusivity claim is false. A user process can become an interruptible
sleeper, be selected as the resident victim, and retain its flash image while it
remains asleep: `sched()` accepts `SSLEEP|P_SINTR` as an outgoing victim but
selects only swapped `SRUN` processes for incoming execution
(`sys/kern/vm_sched.c:64-89,95-123`). That path is separate from `TFALLOC` and
can hold process-image swap for an unbounded sleep. The shipped `talloc` utility
also opens its target read-only (`usr.sbin/talloc/talloc.c:23-31`), so Layer A
must either change that utility or intentionally revoke its allocation ability.
Layer A closes the direct retained-temporary-extent vulnerability; Layer B and
Layer C remain necessary to make ordinary process ownership safe.

### J2 -- B before C

**REFUTED.** E5 confirms that only `newproc()` and `sched()` call the linked
`swapout()`, which supports the proposed reservation primitive. Layer B can
remove the single capacity panic at `sys/kern/vm_swap.c:167-181`, but it cannot
truthfully convert every panic into refusal or sleep. Hard swap I/O still
panics at `sys/kern/vm_swp.c:29-42`, and SwapRAM retains invariant panics outside
the capacity-refusal contract. More importantly, sleeping the swapper after an
outgoing reservation refusal can deadlock: the incoming extent is released only
after `swapin()` (`sys/kern/vm_swap.c:92-109`), but the scheduler writes the
resident first (`sys/kern/vm_sched.c:56-59`). When that incoming extent is the
only event capable of creating capacity, no external wakeup establishes
progress. Layer B may land before C only with a bounded safe refusal state and a
gate that rejects an eternal sleep; it does not independently establish the
claimed behavior.

### J3 -- return `ENOMEM`

**CONFIRMED, with a mandatory init companion fix.** The shipped fork manual
assigns `ENOMEM` to insufficient swap and `EAGAIN` to process-count limits
(`share/man/man2/fork.2:46-67`); the kernel already uses `EAGAIN` for the latter
(`sys/kern/kern_fork.c:217-240`). The ARM syscall and libc paths preserve the
error value (`sys/arch/rp2040/rp2040/syscall.c:266-279`,
`lib/libc/arm/sys/SYS.h:30-39`). The shell retries and prints `cannot fork: no
swap space` for `ENOMEM` (`bin/sh/xec.c:1074-1098`, `bin/sh/msg.c:23-24`).

Shipped callers generally treat every negative fork result as one failure
class, so no evidence favors `EAGAIN`. Init is the exception because it lacks
negative-PID branches, can read an uninitialized or stale status, retries the
single-user path without delay, and can record `-1` as a login/window child
(`sbin/init/init.c:278-335,424-472`). A recoverable kernel result therefore
requires the init fix and its injected-failure gate in the same deployable
series.

### J4 -- the 544-block exchange peak

**REFUTED numerically and CONFIRMED mechanistically.** E6 proves that the built
OMAGIC executable has 8,040 bytes of clean text retained in `p_tsize`, so
`swapout()` writes 151,704 data bytes, 2,244 stack bytes, and the 3,072-byte user
area. The aligned image is 156 blocks and the exact recorded-state peak is
`216 + 156 + 156 = 528`, not 544. The 164/544 arithmetic remains a valid
explicit `p_tsize == 0` stress case.

The ordering claim survives. `sched()` calls `swapout(in_core)` before
`swapin(swapped_out)` (`sys/kern/vm_sched.c:56-59`), and `swapin()` frees the
incoming extent only after all reads (`sys/kern/vm_swap.c:92-109`). The first
switch therefore needs both complete images unless a proven exchange algorithm
or separate second slot changes that mechanism. The measured clean text does
not make 528 fit in 380 blocks.

### J5 -- stable 216-block occupancy and its owners

**UNPROVEN.** The three retained maps establish a stable total of 216 allocated
blocks and 164 free blocks while placement changes (`audit-01.md:680-686`). They
do not attribute any occupied block. The free-map printer emits only free run
size and address (`sys/kern/vm_swap.c:174-181`), while process images,
temporary-device extents, exec spools, and evacuated SwapRAM images all use the
same resource map (`sys/kern/vm_swap.c:167-169`, `sys/dev/swap.c:248-291`,
`sys/kern/exec_subr.c:31-59`,
`sys/arch/rp2040/rp2040/swapram.c:588-668`).

E8 makes the proposed small-process attribution plausible but cannot prove it:
the fixture's stacks and user areas are synthetic
(`sys/arch/rp2040/test/swapram/swapram_test.c:383-425`). Only an authorized,
atomic owner-ledger capture can confirm or refute the named owners, alignment
padding, retained temporary extents, stale spools, or leaked images.

## 3. Ranked LARGE-fork designs, costs, and falsifiers

All ranks assume the exact built specimen unless a row says `conditional
maximum`. Flash time uses `T = E*t_erase + P*t_program + overhead`; the old
45-ms/0.4-ms constants are not current measurements
(`docs/research/ram-compression.md:3-8`). Code-size entries distinguish measured
existing headroom from unimplemented code. The LARGE-bonus kernel has 34,860
bytes of measured flash headroom above its required margin and a 76,304-byte
static RAM gap (E7), but every implementation must rerun both gates.

The incremental text figures below are preimplementation budgets, not measured
sizes. They use the E7 sizes of `malloc3_contiguous_next` (420 bytes),
`swapout` (448), and `swapram_evacuate` (508) as scale references. A post-link
measurement above a budget falsifies the estimate and requires a design review,
not a silent budget increase.

| Rank and mechanism | Incremental RAM estimate | Flash-space and I/O estimate | Incremental text budget | Time model |
| --- | ---: | --- | ---: | --- |
| 1. Two fixed LARGE slots | At most 64 bytes for two global slot records | 624 raw blocks total: 292 general, 328 LARGE slots, four scratch; one exact outgoing image costs 39 erases and 614 programs | At most 1,024 bytes | `39*t_erase + 614*t_program + read(156 KiB) + overhead` per exact switch |
| 2. Packed compressed evacuation | At most 512 bytes for a directory covering `NPROC == 25` (`sys/arch/rp2040/include/machparam.h:67`) beyond already linked codec workspaces | At most 16 packed blocks, four erases, and 64 programs for the complete evacuated pool; modeled five-image case uses six blocks and at most 24 contiguous programs | At most 2,048 bytes beyond the existing codec | `E*t_erase + P*t_program + encode + decode + overhead`, with `E <= 4`, `P <= 64` |
| 3. RAM-bounce exchange | 4,096-byte bounce plus at most 64 bytes of state | One exact 156-block incoming image beside existing occupants; 39 erases, 614 programs, and 156 KiB of reads per exchange | At most 2,048 bytes | Same callback formula as rank 1 plus the full-image read and sector-copy overhead |
| 4. True vfork/spawn | At most 64 bytes of suspended-parent state per active handoff | Zero child-copy blocks and callbacks before exec; exec has its existing spool/image cost | At most 2,048 bytes | No flash-copy time before exec; handoff plus exec time remains to be measured |
| 5. Admission only | At most 512 bytes for global reservation/headroom ownership across 25 process slots | Zero callbacks on refusal and no added capacity | At most 1,536 bytes | Bounded map search and bookkeeping only on refusal |
| 6. Copy-on-swap | At least a 36-bit 4-KiB-page dirty map, but worst-case preservation grows to a full 144-KiB user window | Initially one 156-block snapshot; worst-case divergence needs a second full image and returns to the 528-block peak | No credible bound; a 4,096-byte review threshold is already larger than the other designs | Per-write fault cost plus worst-case 39/614 full-copy cost; no bounded current model |

### Rank 1 -- 624-KiB raw swap with two fixed 164-block LARGE slots

This design changes the flash partition at first principles rather than trying
to manufacture capacity inside 384 KiB. The 2-MiB chip retains the 128-KiB
kernel region, uses 1,296 KiB for Dhara root, and uses the top 624 KiB as raw
swap (`sys/arch/rp2040/dev/flash.h:46-101`). Raw swap is partitioned as:

| Purpose | Half-open block interval | Blocks |
| --- | --- | ---: |
| Rewrite scratch | `[0,4)` | 4 |
| General allocator | `[4,296)` | 292 |
| LARGE slot 0 | `[296,460)` | 164 |
| LARGE slot 1 | `[460,624)` | 164 |

The recorded general occupants consume 216 blocks, leaving 76 general blocks.
The two slots hold the conditional maximum pair of `2 * 164 = 328` blocks, so
the 620 allocatable blocks leave `620 - 216 - 328 = 76`. The exact named pair
uses 312 blocks and leaves 92 physically unoccupied: 76 blocks in the general
arena and 16 blocks of internal tail slack across the fixed slots. General
consumers cannot allocate that slot tail slack. One slot holds the swapped
incoming member while the other remains the protected route for the resident
member. After exchange, the freed incoming slot becomes the next protected
route. General allocations cannot fragment either slot.

The flash-capacity cost is 240 additional raw blocks and a reduction of modeled
Dhara logical capacity from 989 to 827 1-KiB sectors. E9 proves that the complete
root fits in an 826-block volume with 78 blocks free and that the host image
writer accepts the 1,296-KiB region. Geometry constants add no instructions.
Two small global slot-owner records avoid adding fields to every 104-byte
`struct proc`; the exact metadata and selector code sizes remain unmeasured.
One exact image append costs 39 erases and 614 program callbacks; a first
parent-to-child switch writes one such outgoing image, reads the incoming one,
and frees its slot. The conditional maximum costs 41/645. Nested LARGE forks or
an independent second LARGE lineage must receive `ENOMEM` when both slots are
committed because one free slot is the exchange invariant, not spare capacity.

**Falsifiers:** the production root profile does not fit with the required free
margin; Dhara's compiled geometry or account-image gate disagrees with E9; the
general owner ledger exceeds 292 blocks; a slot can be allocated by a general
consumer; a successful alternation ever has zero free LARGE slots before the
incoming extent is released; injected media failure leaves two apparent owners;
the code or RAM headroom gate fails; or an authorized board cannot mount,
modify, fsck, reboot, and remount the reduced root while completing repeated
LARGE parent/child alternation.

### Rank 2 -- retain evacuated SwapRAM streams compressed in a packed flash arena

Every evacuated image already fits with the others in the PICO kernel's 16-KiB
compressed pool. Copying the compressed streams and their segment lengths into
a packed flash arena therefore uses at most 16 1-KiB blocks, versus the observed
216 raw allocated blocks whose ownership remains unproven. E8's five modeled
images occupy 5,915 compressed bytes, or six 1-KiB blocks when packed, but their
synthetic stack and user bytes prohibit using that ratio as a board claim.

The source-derived worst case costs at most four 4-KiB erases and 64 256-byte
program pages for one complete 16-KiB packed arena. The modeled five-image set
would fit in two sectors and at most 24 contiguous program pages. Existing
heatshrink encode/decode code already contributes to the E7 kernel; new costs
are a packed-arena directory, streaming flash writer/reader, ownership state,
and failure recovery. A streaming implementation need not allocate another
16-KiB RAM buffer, but its exact workspace and code size are unmeasured.

This design creates enough current geometry for one exact pair if it actually
reduces the other occupants below `380 - 312 = 68` blocks. It ranks below fixed
slots because J5 has not established that all 216 blocks are evacuated images,
and packed streams add new integrity and random-access obligations.

**Falsifiers:** an atomic owner ledger attributes more than 68 blocks to
non-evacuation consumers after compression; real compressed streams plus
metadata exceed 16 KiB; streaming decode cannot restore data, stack, and user
segments independently; callback-position injection corrupts an adjacent
stream or loses the old arena; code/RAM headroom fails; or a board capture shows
latency or wear beyond the accepted budget.

### Rank 3 -- 4-KiB RAM-bounce in-place sector exchange

With one incoming 156-block image in flash and its process resident in RAM, a
4-KiB bounce buffer can read one incoming sector, erase and program that same
sector with the outgoing RAM bytes, and then install the saved incoming bytes
in RAM. After all sectors and the user area are exchanged, metadata can switch
owners atomically. The peak becomes `216 + 156 = 372` blocks for the exact
specimen plus 4 KiB of RAM, rather than two images. Each exact exchange reads
156 KiB and costs 39 erases plus 614 programs; the conditional maximum costs
41/645. The 4-KiB buffer fits the E7 static RAM gap, but competes with kernel
stacks and other future allocations.

The recorded free map has no 156-block contiguous run, so exchange alone cannot
create the initial child image. It needs a dedicated slot, scatter mapping, or
compaction first. It also creates a long sequence in which flash contains a
mixture of two process images. Metadata publication, callback failure, reset,
interrupt masking, XIP-off latency, and `p_tsize`-dependent segment boundaries
all require explicit recovery rules. That complexity ranks it below a capacity
layout that preserves ordinary whole-image semantics.

**Falsifiers:** no initial contiguous or safely scattered incoming image can be
reserved; any callback failure loses the only valid copy of a sector; an
interrupt executes XIP code during the ROM operation; metadata becomes visible
before all sectors switch; a 4-KiB static buffer violates stack/RAM headroom; or
the board-measured masked-interrupt interval violates console or timer bounds.

### Rank 4 -- true `vfork()` or spawn for exec-heavy LARGE callers

A true address-space-sharing `vfork()` followed immediately by exec can avoid a
child copy and add zero image erases/programs before exec. A purpose-built spawn
can similarly combine child creation and exec admission. The current `vfork()`
does not provide that behavior: `fork1(1)` still reaches the same eager
`newproc()` copy before applying its parent/child handoff
(`sys/kern/kern_fork.c:173-205,241-269`). A correct redesign must prevent the
child from corrupting the suspended parent's kernel and user state and must
make exec/exit the only release points. Code and RAM costs are unmeasured.

This optimization cannot satisfy the requested semantics of `bigtest`, whose
child verifies copied BSS after ordinary `fork()`
(`tests/rp2040/swapram_large/bigtest.c:48-77`). It reduces pressure for the
shell and other fork/exec callers but is not the LARGE-fork solution.

**Falsifiers:** any child return, signal, failed exec, descriptor action, or
stack write changes parent-visible state outside the specified `vfork`
contract; parent wakeup can deadlock the sole process window; existing callers
depend on ordinary fork separation; or the ordinary LARGE fork still fails the
success gate.

### Rank 5 -- exact exchange-headroom admission without a success mechanism

Admission computes the image span at exec and fork, reserves backing plus the
resident escape route, and returns `ENOMEM` before publication when the
invariant cannot hold. Refusal costs zero erase/program callbacks and only
reservation metadata. Layer C requires this shared policy regardless of the
success design.

Admission alone deliberately rejects the recorded case because 528 exceeds
380. It ranks below every design that can make the fork succeed and serves as a
safety gate, not the requested outcome.

**Falsifiers:** any consumer reduces the protected route after admission;
legitimate heap or stack growth exceeds the recorded demand; refusal occurs
after child publication or flash writes; a waiter sleeps with no possible
capacity event; or an admitted alternation cannot complete.

### Rank 6 -- copy-on-swap sharing without an MMU

At fork, parent and child could nominally refer to one flash snapshot until one
writes. RP2040 supplies MPU permissions but no address translation, dirty-page
mapping, or second process window (`sys/arch/rp2040/include/machparam.h:115-128`).
The running process writes directly throughout the 144-KiB window. Detecting
the first write would require coarse MPU write faults, instruction-aware write
emulation or a full copy/diff log, and enough backing to preserve the other
process's old bytes. The mechanism therefore recreates the storage and recovery
problem it is intended to avoid. Its code size, fault latency, and worst-case
log are unbounded by current evidence.

**Falsifiers of feasibility:** an implementation cannot prove complete write
coverage for stores, exception frames, DMA, and kernel copyout; a coarse-region
fault cannot identify and preserve the modified bytes; the worst-case log
reaches one full image; or fault handling violates the MPU privilege and
deliberate-fault evidence requirements. No current source or execution result
answers those obligations.

## 4. Ordered build plan

The plan keeps every commit independently buildable and bisectable. Every gate
requires a known-good control and every named mutant before its verdict counts.
Host, cross-build, Renode, and authorized-board evidence remain separate. The
plan uses the exact 156-block executable as its primary specimen and retains
164 blocks as the `p_tsize == 0` maximum-stress mutant.

### Commit 1 -- `fix(swap): authorize temporary extent mutation`

**Functions and files:** Keep the existing `fp->f_flag` propagation through
`ino_ioctl()` (`sys/kern/sys_inode.c:84-94`), consume that argument instead of
marking it unused in `swcioctl()`, and change the chosen extent-lifetime path in
`sys/dev/swap.c:105-125,228-302`. Require `FWRITE` and the production `suser()`
predicate (`sys/kern/kern_prot2.c:43-54`) for every `TFALLOC` mutation,
including replacement and zero-size release. Change
`usr.sbin/talloc/talloc.c:23-31` to open read/write or deliberately report
authorization failure. Preserve fsck's root `O_RDWR` path at
`sbin/fsck/setup.c:112-130`.

**Invariant:** Only a root caller holding a write-open descriptor can mutate a
temporary swap extent. Shortage still returns success with output size zero.
The documented lifetime releases exactly one rounded extent and wakes a later
capacity waiter; close behavior must match the selected explicit lifetime.

**Gate and mutants:** Add `check-tfalloc-admission` around production
`swcioctl()` and allocator code. Exercise root/read-write, root/read-only,
nonroot/read-write, nonroot/read-only, replacement, shortage, release, close,
and fsck-shaped calls. Retain E2's five mutants and add
`ignore-descriptor-mode`, `omit-root-check`, `retain-after-owner-lifetime`,
`free-wrong-owner`, `omit-capacity-wakeup`, and
`consume-protected-headroom`. Require the sanitizer variant when the execution
environment permits it; until then, record that variant as blocked.

**Board-only evidence:** None for merge of the authorization logic. An eventual
authorized system gate must show root fsck scratch allocation still succeeds,
an ordinary user receives the selected error, the extent ledger changes only
for the authorized call, and normal root boot continues.

### Commit 2 -- `refactor(swap): add owned image reservations`

**Functions and files:** In `sys/kern/vm_swap.c:38-255` and the appropriate
declaration header, replace the allocation embedded in `swapout()` with named
functions `swap_image_span()`, `swap_image_reserve()`,
`swap_image_copy_out()`, `swap_image_commit()`,
`swap_image_cancel()`, and `swap_image_release()`. A reservation records tier,
aligned span, segment addresses, owner, and state. Keep hard media failure in
`swap()`/`swap_with_buf()` separate from `ENOMEM`. Make cancellation idempotent
and publish `swapnext` only after successful ownership acquisition.

**Invariant:** Exactly one owner holds every removed extent from reservation
through commit or cancellation. Failure preserves the map, cursor, process
addresses, and media byte for byte. Copy consumes the supplied reservation and
never allocates a second time. Release makes capacity visible before its
wakeup.

**Gate and mutants:** Add `check-swap-reservation` in compact, wide,
diagnostic, and descriptor-full configurations. Seed all three recorded maps,
try all 95 aligned cursor positions, and request exact 156-, intermediate
160-, and conditional 164-block spans. Add SwapRAM success/refusal controls.
Reject `advance-cursor-on-failure`, `omit-wrap`, `omit-span-rounding`,
`split-full-map-interior`, `partial-allocation`, `allocate-again-on-copy`,
`publish-before-ownership`, `double-cancel`, `wrong-owner-release`, and
`capacity-error-on-media-failure`. Rerun E2's allocator gates.

**Board-only evidence:** None for the reservation state machine. Later board
evidence must distinguish capacity refusal counters from hard flash-I/O failure
and show that the owner ledger and map agree at a protected observation point.

### Commit 3 -- `fix(fork): reserve child backing before publication`

**Functions and files:** Change `newproc()` and `fork1()` in
`sys/kern/kern_fork.c:23-34,76-269` to an explicit parent/child/error result.
Compute and reserve the child's backing while the parent remains authoritative,
then acquire the process slot and publish the fully owned child. Define the
process-1 policy at `sys/kern/init_main.c:180-196` rather than interpreting an
error as the child branch. Cover both `fork()` and the current `vfork()` entry.
Keep PID consumption policy explicit.

**Invariant:** `ENOMEM` creates zero child. On refusal, the parent image,
`u.u_procp`, parent status, PID hash, `allproc`, `freeproc`, file/inode/text
reference counts, LARGE-holder count, reservation ledger, cursor, and flash
bytes equal their measured pre-call values except for an explicitly permitted
consumed PID. No child becomes runnable before its backing owner is committed.

**Gate and mutants:** Add `check-fork-swap-refusal`, linking production
`newproc()`, reservation/copy code, SwapRAM inheritance, and allocator. Inject
failure before each ownership transition and after every reversible acquisition.
Reject `ignore-reservation-refusal`, `error-enters-child-branch`,
`publish-pidhash-early`, `publish-allproc-early`, `omit-text-release`,
`omit-file-release`, `omit-directory-release`, `omit-swapram-leave-large`,
`retain-parent-SIDL`, `leak-proc-slot`, `copy-before-commit`, and
`boot-error-as-child`. Require `fork() == -1`, `errno == ENOMEM`, zero writes,
and a live unchanged parent.

**Board-only evidence:** A later authorized capacity-refusal run must show the
parent and shell remain live, the child PID is absent from every process list,
the owner ledger/map are unchanged, and the kernel does not reboot. Host gates
must decide transaction correctness before that run.

### Commit 4 -- `fix(swapper): restore victims after capacity refusal`

**Functions and files:** Make `sched()` in `sys/kern/vm_sched.c:43-130`
reserve outgoing backing before clearing `SLOAD` or removing `SRUN` membership,
or restore those exact fields on refusal. Add a dedicated capacity wait channel
and wake it from `swap_image_release()`, reservation cancellation, temporary
extent release, exec-spool discard, and evacuation rollback. Clear both pending
selections on refusal. Retain the existing hard-I/O integrity policy separately.

**Invariant:** Capacity refusal leaves the resident bytes and addresses,
`SLOAD`, run-queue membership, incoming image, and pending selections unchanged.
The swapper either has a source-visible future capacity event and sleeps once,
or records a bounded refusal and returns to eligible work; it never spins and
never sleeps waiting for an extent that only the blocked incoming swap-in could
release.

**Gate and mutants:** Add `check-scheduler-swap-refusal` with successful
release/retry, fragmented total-free, and no-independent-releaser cases. Reject
`retain-cleared-SLOAD`, `omit-runq-restore`, `swapin-after-out-refusal`,
`spin-on-refusal`, `sleep-without-releaser`, `omit-release-wakeup`,
`stale-pending-incoming`, `stale-pending-victim`, and
`accept-total-not-aligned-run`. Add `check-swap-reservation-interleavings` at
the `geteblk()` and I/O sleep sites (`sys/kern/vm_swp.c:29-60`,
`sys/kern/ufs_bio.c:227-249,333-349`) against competing fork, spool,
evacuation, and temporary allocations.

**Board-only evidence:** A later authorized run must record bounded continued
scheduling after refusal, capacity wakeup after a real release, stable process
liveness, and zero panic. Host-controlled interleavings remain the deciding
state-restoration evidence.

### Commit 5 -- `fix(init): handle child creation refusal`

**Functions and files:** Add explicit negative-PID branches, initialized status,
bounded delayed retry, and diagnostics to single-user, startup-script, login,
and window child creation in `sbin/init/init.c:278-335,424-472`. Never call
`wait()` for `pid == -1` and never store `-1` as a live child.

**Invariant:** Failed child creation preserves init's control-flow state,
consumes no indeterminate wait status, retries with a bound and delay, and
continues supervising existing children. The diagnostic names `ENOMEM` as swap
shortage without converting it to a fatal init exit.

**Gate and mutants:** Add `check-init-fork-refusal` with injected `ENOMEM` for
all four paths and successful retry controls. Reject
`wait-after-negative-pid`, `read-uninitialized-status`, `retry-without-delay`,
`negative-pid-enters-child`, `record-negative-login-child`,
`record-negative-window-child`, and `fatal-enomem`. Build the shipped init from
clean so the gate exercises the changed object.

**Board-only evidence:** An authorized exhaustion run must show a diagnostic,
bounded retry, continued console access, existing-child supervision, and
successful recovery after capacity returns.

### Commit 6 -- `fix(swap): enforce shared exchange-headroom admission`

**Functions and files:** Add a shared headroom policy to
`swap_image_reserve()` and apply it at exec spool acquisition
(`sys/kern/exec_subr.c:31-62,435-486`), `TFALLOC`
(`sys/dev/swap.c:248-291`), evacuation
(`sys/arch/rp2040/rp2040/swapram.c:561-699`), heap growth
(`sys/kern/kern_mman.c:23-58`), and RP2040 stack-growth accounting
(`sys/arch/rp2040/rp2040/syscall.c:188-200`,
`sys/arch/rp2040/rp2040/sig_machdep.c:77-90`). Recompute the compact-map bound
at `sys/arch/rp2040/include/machparam.h:87-94` for the final persistent-owner
count.

**Invariant:** Every published runnable swapped image owns its backing and the
system retains an aligned route large enough for the sole resident image to
leave the process window. An operation that would consume that route fails
before publication or growth. The invariant accounts for alignment and actual
largest-run geometry, rather than total free blocks.

**Gate and mutants:** Add `check-swap-exchange-headroom` and
`check-swapram-evac-headroom`. Parameterize the evacuation fixture so both 16-
and 64-KiB pools reach valid exact-capacity and rollback cases; never reuse the
invalid eight-image 16-KiB invocation from E3. Reproduce 216 general blocks and
two exact 156-block images, plus the conditional 164-block mutant. Reject
`tfalloc-consumes-headroom`, `spool-consumes-headroom`,
`evac-consumes-headroom`, `heap-grows-past-headroom`,
`stack-grows-past-headroom`, `release-headroom-early`,
`count-free-not-largest-run`, `omit-evac-rollback-free`,
`write-before-all-evac-reservations`, and `reopen-pool-after-failed-large`.

**Board-only evidence:** Before the partition/slot commits, the exact recorded
case must refuse cleanly with `ENOMEM`; an admitted smaller case must alternate,
exit, and wait. After the slot commits, the same exact case changes expected
movement from refusal to success. Record both configurations separately.

### Commit 7 -- `feat(rp2040): expand raw swap while preserving root capacity`

**Functions and files:** Change `FLASH_SWAP_BYTES` and derived filesystem
geometry in `sys/arch/rp2040/dev/flash.h:76-101`, set the full root volume to
the host-proven value in `distrib/rp2040/Makefile.inc:1-9`, and update the
authoritative storage documentation and image-account manifest entries that
cite those constants. Keep the 128-KiB kernel region and four-block rewrite
scratch invariant unchanged. Rebuild the PICO and PICO_UART generated
Makefiles only if their generation inputs actually encode the geometry.

**Invariant:** The chip layout is exactly 128 KiB kernel, 1,296 KiB Dhara root,
and 624 KiB raw swap with no overlap or gap. The complete shipped root fits a
826-block volume with the documented free-margin threshold. Host image tooling
and kernel constants calculate the same offsets and sizes.

**Gate and mutants:** Add a geometry/account-image gate that runs `flashimg -c`,
builds every root profile, checks all region endpoints, validates the full
image, and asserts a minimum root free margin. Reject
`old-root-new-swap-overlap`, `new-root-old-swap-offset`,
`omit-partition-table-sectors`, `volume-exceeds-dhara-capacity`,
`profile-exceeds-root`, and `kernel-region-growth-overlap`. Rerun clean
`logged-build`, host image-account checks, cross-builds for PICO and PICO_UART,
and the kernel flash-headroom gate. E9 is the positive prototype, not the final
gate result.

**Board-only evidence:** This commit requires an explicitly authorized flash
cycle after all host gates pass. Capture JEDEC/geometry identity, first boot,
root mount, free blocks, representative writes, `fsck`, reboot, remount, and
raw-swap endpoint probes that never cross into root. Host image success alone
cannot establish silicon writes or retained filesystem integrity.

### Commit 8 -- `feat(rp2040): reserve two LARGE swap slots`

**Functions and files:** Initialize only `[4,296)` in the general `swapmap` at
`sys/kern/init_main.c:145-166`. Add two global slot records for `[296,460)` and
`[460,624)` to the reservation layer, with named functions
`large_slot_reserve_incoming()`, `large_slot_reserve_outgoing()`,
`large_slot_commit()`, `large_slot_cancel()`, and `large_slot_release()`.
Route LARGE child-copy and LARGE resident-eviction reservations through those
functions from `newproc()` and `sched()`; retain ordinary images and evacuated
small images in the 292-block general arena. Reject a nested or independent
LARGE fork unless one slot can hold the incoming image and the other remains
the resident's protected route.

**Invariant:** General consumers can never own a LARGE-slot block. At most one
slot holds the incoming/swapped lineage member while the other is free for the
resident; after the resident is written and the incoming image is restored, the
freed incoming slot becomes the protected route. Slot state and process
addresses publish together. Cancellation or exit leaves each slot either
wholly owned by one process image or wholly free. Exact 156- and conditional
164-block images both fit.

**Gate and mutants:** Extend `check-swap-reservation` with fixed endpoints and
state transitions. Change `check-swap-exchange-headroom` so the 216-block
general occupancy plus an exact LARGE parent/child pair must complete repeated
alternation, exit, and wait; retain the conditional 164-block pair as a maximum
case. Require 76 generally allocatable residual blocks in both cases; the exact
case also has 16 blocks of inaccessible fixed-slot tail slack, for 92 physically
unoccupied blocks. Add `check-flash-swap-operation-counts` with exact 39/614 and
conditional 41/645 oracles, adjacent-slot preservation, and callback failure at
every position. Reject `general-allocator-enters-slot`,
`large-allocator-enters-general`, `same-slot-double-owner`,
`admit-without-free-route`, `release-incoming-before-resident-commit`,
`publish-slot-owner-before-copy`, `omit-slot-cancel`,
`nested-large-overcommit`, `independent-lineage-overcommit`,
`erase-per-kibibyte`, `omit-boundary-erase`, and `clear-B_SWAPIMAGE`.

Rerun the complete clean build, E2/E3 gates, both evacuation sizes, reservation
interleavings, init refusal, root-image geometry, code-size/headroom, RAM layout,
Renode boot, and all calibrated mutants. Treat every warning as a failure.

**Board-only evidence:** With explicit authorization, stage only the test image
and reproduce the named LARGE executable and resident-process set. Capture
source and image revisions, owner ledger, map, cursor, both slot records,
storage counters, `p_tsize`, fork return/errno, shell output, parent and child
BSS checks, at least two complete parent/child alternations, child exit, parent
wait, released slots, continued shell/init liveness, elapsed erase/program
timing, and reboot-free operation. A separate maximum `p_tsize == 0` specimen
must exercise 164-block slots. A power-loss or injected hard-I/O campaign is a
distinct integrity gate and must not be inferred from a successful capacity
run.

## Addendum

Checks of this review's decisive claims against source and retained board
transcripts:

- `usr.sbin/talloc/talloc.c:23-26` opens its target `O_RDONLY` and tests
  `!fd`, which accepts the `-1` failure value and rejects descriptor 0. The
  temporary-extent authorization commit fixes both.
- fsck releases its `/dev/temp0` extent in `ckfini()`
  (`sbin/fsck/utilities.c:183-184`), but `swcclose()` releases nothing
  (`sys/dev/swap.c:118-126`), so an fsck that exits through `errexit()`
  after `TFALLOC` leaves its extent allocated until the next `TFALLOC` on that
  unit. A clean boot-time `fsck -p` of the shipped root does not reach the
  scratch-file path, so this leak is unlikely to be inside the recorded 216
  blocks; the descriptor-lifetime decision belongs to the authorization commit.
- J5 attribution, decided by a board capture. A PICO kernel built from the
  LARGE-bonus branch with a temporary dump of every process's swap fields at
  the shortage panic ran `bigtest` on the bare Pico and printed:

      swapout: pid 17 dsize 159744 ssize 2244, free: 40@4 4@64 108@244 12@372
      owner: pid 18 stat 3 flag 48 d 159744@88 s 2244@237 u@240 t 8040
      owner: pid 17 stat 1 flag c0 d 159744@536870912 s 2244@537032508 u@537126912 t 8040
      owner: pid 14 stat 1 flag 80 d 45508@44 s 2232@56 u@59 t 33852
      owner: pid 12 stat 1 flag 80 d 45508@352 s 2188@364 u@367 t 33852
      owner: pid 10 stat 1 flag 80 d 1440@80 s 2128@81 u@84 t 1356
      owner: pid 1 stat 1 flag 80 d 17408@68 s 2080@72 u@75 t 14048

  The panicking `swapout()` is the scheduler's eviction of the sleeping LARGE
  parent (pid 17, `SLOAD`, addresses in RAM), not the fork copy: the child
  (pid 18, runnable and swapped) already owns its exact 156-block image at
  blocks 88-243. The small processes own 60 blocks, because clean text stays
  out of swap: init 12 at 68, pid 10 8 at 80, `-sh` 20 at 352 and `su` 20 at
  44. The 216 occupied blocks are therefore 60 small-process blocks plus the
  child, and the exchange needs 60 + 156 + 156 = 372 of 380 allocatable
  blocks: it fails on fragmentation (largest free run 108 of 164 free), not on
  total capacity. No temporary extent or exec spool appears; the four owners
  plus the child account for all 216 blocks. This refutes the evacuated-raw
  attribution of J5 and the premise that compressed evacuation recovers the
  needed space, since it can recover at most the 60 small-process blocks.
