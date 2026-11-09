# Audit: RP2040 `panic("out of swap space")`

Assistant-drafted (OpenAI Codex CLI 0.167.0, model gpt-daybreak-blue-latest
at high reasoning effort) as a read-only audit of source revision
`2d461c64c3fd4c08ca8e236afe837df5a781d4a5`. The audit used local source,
history, and sibling BSD source snapshots. The audit did not run builds,
mutants, a board, or network queries.

## Executive finding

The panic is a correctness defect and a denial-of-service boundary, but
fragmentation is only the observed trigger. `swapout()` contains the single
`panic("out of swap space")` site and receives calls from child creation and
the swapper (`sys/kern/vm_swap.c:125-181`, `sys/kern/kern_fork.c:173-179`,
`sys/kern/vm_sched.c:50-59`). Exec argument spooling, temporary swap devices,
and SwapRAM evacuation use the same resource map but already report shortage
without calling that panic (`sys/kern/exec_subr.c:31-62`,
`sys/dev/swap.c:248-291`,
`sys/arch/rp2040/rp2040/swapram.c:588-630`).

The recorded map contains 164 free allocatable blocks, while the reported
process needs at most 164 blocks when `p_tsize == 0`. A contiguous placement,
split allocation, or compaction can make that first copy fit, but the existing
scheduler writes the outgoing resident before reading and freeing the incoming
image (`sys/kern/vm_sched.c:56-59`, `sys/kern/vm_swap.c:92-109`). With the
recorded 216 allocated blocks unchanged, two 164-block images require
`216 + 164 + 164 = 544` allocatable blocks; RP2040 provides 380. This capacity
argument is conditional on the unknown clean-text size and stable occupancy,
but it proves that fragmentation-only repairs cannot establish progress for the
reported worst case (`sys/kern/vm_swap.c:26-33,167-181`,
`sys/kern/init_main.c:145-166`).

The recommended correction combines three mechanisms:

1. Make swap-image reservation and `swapout()` failure explicit and
   transactional.
2. Return `ENOMEM` from fork before publishing a child when backing plus the
   scheduler's exchange headroom cannot be reserved.
3. Make the swapper recover its victim state on refusal, and prevent every swap
   consumer from consuming protected exchange capacity.

That combination removes the panic, preserves the documented zero-child fork
contract, and states the progress invariant honestly. It does not promise that
the observed LARGE fork succeeds. Making that fork succeed under the recorded
occupancy requires a separately proven in-place process-image exchange or more
backing capacity.

## Evidence boundary and terminology

The board transcript in the brief is retained observation rather than a result
reproduced by this audit (`docs/research/swap-exhaustion/brief-01.md:7-25`). The
source establishes control flow, allocation geometry, and interfaces. The
capacity and flash-operation counts below are calculations from those sources
and the transcript. The audit has no allocation trace that identifies the
owners of the recorded occupied intervals.

An **image span** means the aligned flash run containing block-rounded data
after clean text, stack, and the 3,072-byte user area
(`sys/kern/vm_swap.c:38-46,147-169`,
`sys/arch/rp2040/include/machparam.h:100-113,135-144`). **Exchange headroom**
means an actually reserved aligned extent large enough to write the resident
image while the incoming image still owns its extent. A free-block count or
largest-run preflight is not a reservation.

The local comparison roots named `BSD_Workspace/211bsd`,
`BSD_Workspace/4.4BSD-Lite2`, and `BSD_Workspace/retrobsd` are sibling source
snapshots. They establish the behavior of those snapshots, not an exhaustive
claim about every historical release.

## 1. Correctness audit

### 1.1 Panic reachability

**SERIOUS -- one allocation refusal terminates the system.** `swapout()` first
tries `swapram_out()`, then asks `malloc3_contiguous_next()` for one aligned
flash run, prints the free map on failure, and panics
(`sys/kern/vm_swap.c:147-181`). The function returns `void`, so neither caller
has an error contract (`sys/kern/vm_swap.c:135-140`).

`newproc()` reaches `swapout()` after it has consumed a process slot, inherited
the LARGE flag, inserted the child into the PID hash and `allproc`, incremented
file and directory references, incremented the executable-text reference,
changed `u.u_procp`, marked the parent `SIDL`, and marked the child runnable
(`sys/kern/kern_fork.c:76-94,118-145,153-178`). The child is therefore already
published at the current failure point.

`sched()` selects a resident non-system victim, clears `SLOAD`, and removes a
runnable victim from the run queue before the next loop iteration calls
`swapout()` (`sys/kern/vm_sched.c:95-122,50-59`). A fallible `swapout()` alone
would strand the victim unless the caller restores those exact state changes.

Boot creates process 1 by calling `newproc(0)` directly
(`sys/kern/init_main.c:180-196`). Any tri-state `newproc()` interface therefore
needs an explicit boot policy as well as `fork1()` handling; treating a negative
return as true would enter the existing child branch
(`sys/kern/kern_fork.c:241-249`).

### 1.2 The other swap consumers

**SOUND -- exec spool shortage is already transactional.** Exec first tries the
SwapRAM spool, then reserves an aligned flash run; flash shortage returns
`ENOMEM`, buffer shortage releases the run, migration refusal preserves the RAM
spool, and common discard frees either backing
(`sys/kern/exec_subr.c:31-62,142-200`). Argument serialization propagates the
allocation error and discards partial state on later failure
(`sys/kern/exec_subr.c:730-776`).

**SOUND with a security defect -- `TFALLOC` reports shortage without a panic.**
The temporary-device ioctl releases an old extent, tries an aligned allocation,
and returns success with output size zero when the allocation fails
(`sys/dev/swap.c:248-291`). The temporary extent persists across device close
because `swcclose()` only validates the unit and returns
(`sys/dev/swap.c:105-125`).

**SOUND within its transaction -- SwapRAM evacuation reserves every image
before writing.** One failed reservation frees every earlier reservation,
clears provisional process addresses, performs zero image writes, and returns
`-1` (`sys/arch/rp2040/rp2040/swapram.c:561-630`). A successful reservation
phase publishes the cursor and then expands each image before freeing its pool
entry (`sys/arch/rp2040/rp2040/swapram.c:632-668`).

**SERIOUS -- evacuation does not protect the next resident eviction.** The
swapper services a LARGE request before it writes the already selected resident
victim (`sys/kern/vm_sched.c:50-59`). LARGE service closes pool admission and
evacuates current pool images, but reserves nothing for the resident image that
may be written next (`sys/arch/rp2040/rp2040/swapram.c:688-699`). Successful
evacuation therefore proves only that evacuated images fit.

### 1.3 Required fork transaction and unwind

The cleanest failure boundary precedes child publication. A reservation API
should compute the child image span from the parent's data, stack, clean text,
and user-area sizes, remove a real extent from `swapmap`, and hand that extent
to a copy operation without allocating it again. The allocator performs its
search before modifying the map and removes the chosen extent only on success
(`sys/kern/subr_rmap.c:289-345,348-374`). Holding the removed extent across
sleeping flash I/O excludes competing consumers; a check followed by a later
allocation leaves a time-of-check/time-of-use window.

If the implementation retains late allocation after child publication, the
failure path must reverse all of the following source-visible effects:

- restore `parent->p_stat` and `u.u_procp`
  (`sys/kern/kern_fork.c:170-181`);
- remove the child from `pidhash` and `allproc`, then return the slot to
  `freeproc` (`sys/kern/kern_fork.c:118-134`,
  `sys/kern/kern_exit.c:225-244`);
- release every incremented file, current-directory, root-directory, and text
  inode reference (`sys/kern/kern_fork.c:136-164`,
  `sys/kern/kern_descrip.c:370-384`, `sys/kern/ufs_inode.c:312-348`);
- undo `swapram_inherit()`, whose LARGE path increments `sr_nlarge`
  (`sys/kern/kern_fork.c:91-94`,
  `sys/arch/rp2040/rp2040/swapram.c:784-804`);
- free any reserved extent without publishing a child or cursor state.

Calling `exit()` is not an unwind substitute: it closes the current `u` area's
descriptors, releases current directories and text, and moves the current
process to the zombie list (`sys/kern/kern_exit.c:57-119`).

### 1.4 Scheduler recovery and progress

A failed outgoing reservation must leave the incoming image untouched, restore
the selected victim's `SLOAD`, restore its run-queue membership if `sched()`
removed it, clear both pending selections, and sleep on a swap-capacity event
(`sys/kern/vm_sched.c:56-62,95-129`). Every release capable of satisfying the
request must wake that event. A loop that immediately retries the same request
would convert the panic into a spin.

Sleeping alone does not prove progress. The incoming extent is released only
after `swapin()` reads data, stack, and the user area
(`sys/kern/vm_swap.c:92-109`), while the outgoing process must be written first
(`sys/kern/vm_sched.c:56-59`). With one flat resident process window, the source
offers neither a second resident victim nor a place to read the incoming image
before preserving the outgoing one
(`sys/arch/rp2040/include/machparam.h:115-128`).

The system therefore needs one of two explicit invariants:

1. **Reserved-hole invariant:** whenever a runnable swapped image exists, an
   aligned extent large enough for the resident image remains reserved from
   fork, exec spool, evacuation, `TFALLOC`, heap growth, and stack growth.
2. **Exchange invariant:** a proven algorithm exchanges the resident and
   incoming images using bounded scratch without requiring two simultaneous
   full image extents.

Heap growth checks only the process window and current stack position, while
syscall and signal entry can enlarge the recorded stack size; none of those
paths reserves flash (`sys/kern/kern_mman.c:23-58`,
`sys/arch/rp2040/rp2040/syscall.c:188-200`,
`sys/arch/rp2040/rp2040/sig_machdep.c:77-90`). Fork-only admission would leave
scheduler exhaustion reachable after legitimate growth.

### 1.5 Reservation races and map capacity

Swap I/O can sleep while acquiring a buffer and while waiting for completion
(`sys/kern/vm_swp.c:29-60`, `sys/kern/ufs_bio.c:227-249,333-349`). The map
extent must therefore remain owned across those sleeps. Cursor publication
should follow successful reservation and must not stand in for ownership
(`sys/arch/rp2040/rp2040/machdep.c:242-259`).

The configured 29-entry compact map derives from a stated bound of 27 live
extents and 28 free runs (`sys/arch/rp2040/include/machparam.h:87-94`). Any new
persistent reservation, multi-extent image, or multiple concurrent exec spool
model must recompute that bound. `mfree()` prints and loses a freed run when the
descriptor array overflows, which would turn a bookkeeping underestimate into
permanent capacity loss (`sys/kern/subr_rmap.c:150-165`).

## 2. Fork semantics and historical comparison

### 2.1 Errno recommendation

The shipped interface promises `-1`, zero child creation, and an errno on
failure. It assigns `EAGAIN` to process-count limits and `ENOMEM` to
insufficient swap (`share/man/man2/fork.2:46-67`). The current kernel already
uses `EAGAIN` for process-slot and per-user limits
(`sys/kern/kern_fork.c:217-240`). The correct shortage result is therefore
`ENOMEM`; using `EAGAIN` would erase the interface's meaningful distinction.

The brief states that POSIX lists both errors
(`docs/research/swap-exhaustion/brief-01.md:44-47`). The repository contains no
normative POSIX fork text, and the audit's network boundary prevented an
independent standards lookup. The recommendation rests on the shipped
DiscoBSD interface and caller behavior, not an unverified clause number.

The RP2040 syscall return path copies `u.u_error` into `r0` and sets carry, and
the ARM libc stub stores that value in `errno` and returns `-1`
(`sys/arch/rp2040/rp2040/syscall.c:266-279`,
`lib/libc/arm/sys/SYS.h:30-39`). Setting `u.u_error = ENOMEM` in the parent is
therefore sufficient at the ABI boundary after a complete kernel unwind.

### 2.2 2.11BSD, RetroBSD, and 4.4BSD-Lite2

The local 2.11BSD snapshot uses `EAGAIN` for process admission, tries core
allocation for the child, and falls back to `swapout()`
(`BSD_Workspace/211bsd/sys/sys/kern_fork.c:65-85,238-258`). Its `swapout()`
panics when `malloc3()` cannot allocate swap
(`BSD_Workspace/211bsd/sys/sys/vm_swap.c:101-107`), even though its manual
documents `ENOMEM` for insufficient swap
(`BSD_Workspace/211bsd/man/man2/fork.2:55-67`). The snapshot supplies ancestry
for the defect, not a recoverable implementation.

The local RetroBSD snapshot always creates the child copy through `swapout()`
and panics when the three-part swap allocation fails
(`BSD_Workspace/retrobsd/sys/kernel/kern_fork.c:155-166`,
`BSD_Workspace/retrobsd/sys/kernel/vm_swap.c:66-72`). Its process-admission path
returns only `EAGAIN`, while its manual also documents `ENOMEM` for swap
shortage (`BSD_Workspace/retrobsd/sys/kernel/kern_fork.c:216-234`,
`BSD_Workspace/retrobsd/src/man/man2/fork.2:55-67`).

The local 4.4BSD-Lite2 snapshot clones inheritable VM map entries and uses
copy-on-write objects rather than allocating one eager contiguous child image
(`BSD_Workspace/4.4BSD-Lite2/sys/vm/vm_map.c:1754-1781,2217-2244`). Later swap
pager allocation failure returns `VM_PAGER_FAIL`, and pageout reactivates pages
after pager failure (`BSD_Workspace/4.4BSD-Lite2/sys/vm/swap_pager.c:619-637`,
`BSD_Workspace/4.4BSD-Lite2/sys/vm/vm_pageout.c:331-340,474-484`). That design
shows a retryable pager failure model, but its virtual-memory machinery is not a
drop-in solution for one flat RP2040 process window. Its fork manual still
documents `ENOMEM` for insufficient swap
(`BSD_Workspace/4.4BSD-Lite2/lib/libc/sys/fork.2:74-99`).

### 2.3 Current caller behavior

The shell retries fork after an alarm pause, then distinguishes `ENOMEM` as
`"cannot fork: no swap space"` from `EAGAIN` as a process-limit failure
(`bin/sh/xec.c:1074-1098`, `bin/sh/msg.c:23-24`). `FORKLIM` is 2, so the current
comment describing retries through 32 seconds is stale
(`bin/sh/defs.h:506`, `bin/sh/xec.c:1074-1083`).

Init's single-user loop has no explicit negative-PID branch; a failed fork
reaches `wait()`, then immediately retries after `ECHILD`
(`sbin/init/init.c:278-309`). Init's startup-script path also lacks a negative
branch; `pid == -1` lets `wait(&status) == -1` terminate the loop and then reads
`status` (`sbin/init/init.c:312-335`). The kernel returns `ECHILD` without
writing a status when it finds no child (`sys/kern/kern_exit.c:258-267`).
Therefore, a newly recoverable fork failure exposes an init defect: the first
failed wait can leave `status` indeterminate, and an earlier reaped child can
leave an unrelated status. A fork-exhaustion gate must cover init rather than
assuming that a kernel return is the complete user-visible repair.

Init, rather than getty, forks login sessions. The getty program transitions to
login with `execle()` (`libexec/getty/main.c:280-281`), while init records fork
results for login and window children without a direct negative-result branch
(`sbin/init/init.c:424-472`).

## 3. Capacity and fragmentation

### 3.1 Recorded layout

The raw swap partition contains 384 KiB, and initialization reserves its first
four 1 KiB blocks as the rewrite scratch sector, leaving blocks `[4,384)` in
`swapmap` (`sys/arch/rp2040/dev/flash.h:87-101`,
`sys/kern/init_main.c:145-166`). The transcript's free map partitions the device
as follows (`docs/research/swap-exhaustion/brief-01.md:13-25`):

| Interval | Blocks | Audit classification |
| --- | ---: | --- |
| `[0,4)` | 4 | reserved rewrite scratch |
| `[4,124)` | 120 | free |
| `[124,144)` | 20 | allocated |
| `[144,148)` | 4 | free |
| `[148,324)` | 176 | allocated |
| `[324,352)` | 28 | free |
| `[352,372)` | 20 | allocated |
| `[372,384)` | 12 | free |

The four free runs sum to 164 blocks. The complement inside the 380-block
allocatable region sums to 216 blocks. Adjacent allocations merge into one
occupied interval in this reconstruction, so the three occupied intervals do
not imply three owners.

For `p_tsize == 0`, the request is
`round4(ceil(159744/1024) + ceil(2244/1024) + ceil(3072/1024)) =
round4(156 + 3 + 3) = 164` blocks. `swaptext()` can subtract clean executable
text, but the panic diagnostic omits `p_tsize` and `p_tip`; it also omits owner
records and `swapnext` (`sys/kern/vm_swap.c:26-33,167-181`). The transcript
therefore establishes a 164-block upper bound under the source's clean-text
rule, not the exact child demand.

The test declares a 150,000-byte BSS by itself
(`tests/rp2040/swapram_large/bigtest.c:18-22`). That BSS needs at least 147 data
blocks, which exceeds the 120-block largest hole. Allocating data, stack, and
user area as only three extents cannot fit this specimen; a successful split
design would have to chunk the data segment too.

### 3.2 What caused the layout, and how often

The next-fit allocator searches at and above the cursor, wraps below it, and
advances the cursor only after success
(`sys/kern/subr_rmap.c:307-345,348-374`). Under descriptor pressure it chooses a
fitting run boundary instead of manufacturing a false shortage
(`sys/kern/subr_rmap.c:332-367`). The cursor persists in watchdog scratch across
software reset and uses bounded ROSC sampling after an invalid retained value
(`sys/arch/rp2040/rp2040/machdep.c:209-239`).

Alignment charges zero through three padding blocks per image after the three
segment sizes have each been rounded to blocks
(`sys/kern/subr_rmap.c:293-299`, `sys/kern/vm_swap.c:167-169`). Variable sizes,
lifetimes, and a rotating cursor can produce the recorded holes. The transcript
contains neither the allocation/free trace nor coherent owner records, so the
frequency of this layout is unknown. A claim that next-fit "often" produces it
would exceed the evidence.

An attribution capture needs every process's flags, data/stack/user swap
addresses and sizes, text backing and `p_tsize`; all three temporary-device
records; live exec spool reservations; evacuation state; `swapnext`; and one
map dump at a single protected observation boundary. The shared consumers are
visible at `sys/kern/vm_swap.c:167-169`,
`sys/arch/rp2040/rp2040/swapram.c:588-635`,
`sys/kern/exec_subr.c:31-59`, and `sys/dev/swap.c:248-279`.

### 3.3 Total capacity, not just the largest hole

If a placement repair consumes all 164 free blocks for the child while the 216
other blocks remain allocated, no free block remains for the resident parent.
The first parent-to-child switch needs the parent image before it can read and
release the child image (`sys/kern/vm_sched.c:56-59`,
`sys/kern/vm_swap.c:92-109`). The conditional peak is 544 blocks against 380.

This result has two consequences. First, best-fit, a dedicated LARGE arena,
arbitrary splitting, and compaction can improve placement success but cannot
prove scheduler progress. Second, reserving only the child's extent can convert
the fork-time panic into a later scheduler refusal. The acceptance oracle must
exercise at least one complete parent/child alternation, not merely successful
fork return.

## 4. Flash time and wear

The raw driver uses a 4,096-byte erase sector and 256-byte program page
(`sys/arch/rp2040/dev/flash.h:46-53`). A `B_SWAPIMAGE` append erases each sector
when ascending writes first reach its boundary and programs each page once,
padding a short final page with erased bytes
(`sys/arch/rp2040/dev/flash_swap.c:21-52`,
`sys/arch/rp2040/dev/flash.c:643-675`). An ordinary rewrite stages a whole
sector through the reserved scratch sector, so each affected destination sector
causes two erase callbacks and 32 program callbacks
(`sys/arch/rp2040/dev/flash_swap.c:55-116`). Erase and program operations mask
interrupts and leave XIP for the ROM operation
(`sys/arch/rp2040/dev/flash.c:294-348`).

For the conditional 164-block, `p_tsize == 0` image, append reaches 41 sectors
and programs 645 physical pages: 624 for 159,744 data bytes, 9 for 2,244 stack
bytes, and 12 for the 3,072-byte user area. The map holds 164 KiB; page-tail
padding makes the callback byte count 165,120 bytes. These are source-level
callback counts, not measured board latency or chip-internal wear.

The historical note quotes about 45 ms per erase and 0.4 ms per program page
(`docs/research/ram-compression.md:3-8`). Applying those old estimates gives
`41*45 ms + 645*0.4 ms = 2.103 s` plus software overhead. The note's platform
state predates current capacity work, so the audit treats that value as a stale
estimate. A current claim should report `T = E*t_erase + P*t_program + overhead`
with board-measured `t_erase` and `t_program`.

Candidate flash costs follow directly:

- Reservation and clean refusal add zero erase or program callbacks. A
  successful copy retains ordinary image traffic.
- Best-fit and dedicated placement add zero callbacks directly, but repeatedly
  choosing the same low-waste region can weaken the cursor's wear rotation.
- Independent aligned extents add zero relocation traffic but can add padding
  and sector touches; their metadata must preserve exclusive sector ownership.
- Compaction adds a fresh destination write for every relocated sector. An
  ideal append relocation of all 216 occupied blocks would add at least 54
  erase and 864 program callbacks; scratch-mediated rewrite can double the
  sector erase count.
- Swapping the parent instead of the child has approximately the same initial
  image traffic. Subsequent progress remains unproved unless an exchange
  algorithm reuses the incoming extent safely.

## 5. Security audit

**FATAL, high confidence -- an ordinary user can reserve shared swap through
`TFALLOC`.** Generic ioctl accepts a descriptor with either `FREAD` or `FWRITE`,
the character-device path forwards the descriptor flags, and `swcioctl()`
ignores those flags and performs no credential check
(`sys/kern/sys_generic.c:232-239,300-351`,
`sys/kern/sys_inode.c:84-94`, `sys/dev/swap.c:228-291`). The RP2040 manifest
defaults device nodes to root:root mode 0664 and gives `/dev/temp0` through
`/dev/temp2` no narrower override (`distrib/rp2040/mi.rp2040:18-22,124-136`,
`distrib/rp2040/mkmanifest.py:475-493`). Read-only access is therefore enough to
reserve an extent, and close does not release it
(`sys/dev/swap.c:105-125`).

Direct `TFALLOC` shortage remains clean, but a retained temporary extent can
make later fork or scheduler `swapout()` hit the panic. The immediate remedy is
to require write authorization and an explicit privilege or device-ownership
policy for allocation-changing ioctls, and to define lifetime as descriptor
owned or privileged global state. A mode-only fix would leave the kernel API
incorrect for any future permissive node.

**SERIOUS, high confidence -- unprivileged fork can reach the panic once both
tiers refuse the child image.** `fork1()` applies a per-user process count but
performs no swap admission (`sys/kern/kern_fork.c:217-242`). A normal process can
also enlarge its future swap demand through bounded heap and stack growth
without reserving backing (`sys/kern/kern_mman.c:23-58`,
`sys/arch/rp2040/rp2040/syscall.c:188-200`). The denial-of-service claim is
conditional on creating sufficient shared-swap pressure; the `TFALLOC` path
supplies a source-visible way to retain that pressure.

**SERIOUS, medium confidence -- exec spools can contribute transient pressure.**
Unprivileged exec argument serialization can reserve flash before `geteblk()`,
which can sleep (`sys/kern/exec_subr.c:31-59`,
`sys/kern/ufs_bio.c:227-249,333-349`). The source-derived map bound assumes one
exec spool (`sys/arch/rp2040/include/machparam.h:87-94`), but the audit did not
construct a reachable multi-spool schedule. Treat that bound as an obligation
for a concurrency gate rather than as a confirmed exploit.

## 6. Complexity and existing test surfaces

The root `check-kernel` target runs the host kernel harness
(`Makefile:367-370`). The production resource-map allocator already runs in
compact, wide, and diagnostic descriptor forms, with cases for aligned
allocation, cursor rotation and wrap, failure preservation, descriptor-full
boundary selection, and coalescing (`tests/kernel/rmap_test.c:347-494`,
`tests/kernel/Makefile:473-484`).

The temporary-swap fixture links the production allocator and checks shortage,
replacement shortage, rounded release, cursor preservation, and zero shortage
output (`tests/kernel/swap_ioctl_test.c:90-194`). Its calibrated mutants cover
return fallthrough, shortage-output suppression, unrounded free, and stale
released size (`tests/kernel/Makefile:297-298,1003-1042`).

The current fork-related interval-timer fixture stubs `swapout()` to an empty
function and forces the parent setjmp return, so it establishes no exhaustion
behavior (`tests/kernel/itimer_test.c:62-67`). The existing scheduler fixture's
main exercises sleep and meter behavior rather than image exchange
(`tests/kernel/synch_meter_test.c:119-124`). New fork and scheduler fixtures are
therefore required.

The exec-spool gate checks flash allocation refusal and SwapRAM-to-flash
migration refusal while preserving ownership
(`tests/rp2040/exec_spool/exec_spool_test.c:400-472`). The evacuation fixture
checks five shortage maps, zero writes, exact process/map restoration, readable
RAM images, and exact-capacity success
(`sys/arch/rp2040/test/swapram/swapram_evac_test.c:400-442`). Its build uses a
64 KiB pool while the PICO kernel selects 16 KiB, so both sizes belong in the
new matrix (`sys/arch/rp2040/test/swapram/Makefile:36-45`,
`sys/arch/rp2040/compile/PICO/Makefile:6`).

The NOR host model enforces erase/program alignment and one-to-zero programming,
and exercises adjacent images, reuse, rewrite, and injected callback failure
(`tests/rp2040/flash_swap/flash_swap_test.c:46-87,150-274`). Its Makefile has a
test target but no source-mutant calibration
(`tests/rp2040/flash_swap/Makefile:7-11`).

## 7. Ranked recommendation

### Rank 1 -- combine candidates 1, 2, and 5: fallible transaction plus exchange admission

Change these exact surfaces:

1. In `sys/kern/vm_swap.c`, split allocation from copying. Add a reservation
   object containing tier, aligned span, three segment addresses, and ownership
   state. Make reservation return `ENOMEM`; make copy consume exactly that
   reservation; make cancellation idempotently return it. Keep media I/O errors
   separate from capacity refusal (`sys/kern/vm_swap.c:125-245`).
2. In `sys/kern/kern_fork.c`, reserve before child publication, change
   `newproc()` to an explicit parent/child/error result, return `ENOMEM` through
   `fork1()`, and cover both fork and vfork (`sys/kern/kern_fork.c:23-34,76-178,210-269`).
3. In `sys/kern/vm_sched.c`, restore victim and run-queue state after reservation
   refusal, cancel the pending incoming selection, and sleep on a swap-capacity
   event (`sys/kern/vm_sched.c:50-129`).
4. In `sys/kern/exec_subr.c`, `sys/dev/swap.c`,
   `sys/arch/rp2040/rp2040/swapram.c`, `sys/kern/kern_mman.c`, and the RP2040
   stack-growth paths, enforce the same exchange-headroom admission before a
   consumer can reduce the protected hole
   (`sys/kern/exec_subr.c:31-62,435-486`, `sys/dev/swap.c:248-291`,
   `sys/arch/rp2040/rp2040/swapram.c:588-699`,
   `sys/kern/kern_mman.c:23-58`,
   `sys/arch/rp2040/rp2040/syscall.c:188-200`).
5. In `sys/dev/swap.c` and the ioctl admission layer, require write authority
   plus an explicit allocation privilege, bind or deliberately document the
   temporary extent lifetime, and wake the capacity event on release
   (`sys/dev/swap.c:105-125,228-302`, `sys/kern/sys_generic.c:232-239`).

The invariant is: **every published swapped runnable image has both its own
backing and a protected aligned route for the sole resident image to leave the
process window, or the operation that would violate that condition fails before
publication.** In the recorded state, that invariant should reject the LARGE
fork with `ENOMEM`; rejecting it is a correct result, not a partial fix.

Residual risks are init's weak failed-fork handling, resource-map descriptor
pressure from new reservation metadata, starvation when capacity never becomes
available, and flash I/O failure after a successful capacity reservation. Init
needs a companion userland fix or a recorded accepted failure policy
(`sbin/init/init.c:278-335`). Media error remains a distinct kernel integrity
policy because `swap_with_buf()` currently panics on hard I/O error
(`sys/kern/vm_swp.c:29-42`).

### Rank 2 -- candidate 6: a bounded in-place process-image exchange

An exchange algorithm is the only listed direction that can make the recorded
parent/child case progress without holding two full LARGE images. The design
must preserve each outgoing RAM chunk before overwriting it with the incoming
chunk, publish process metadata only after the complete exchange, survive
interrupt-masked flash operations, and recover from callback failure. Directly
using SwapRAM is invalid during LARGE because the pool bytes are the resident
process's bonus window (`sys/arch/rp2040/rp2040/swapram.c:721-745`). Rank this
below graceful refusal because the metadata, crash-consistency, and wear proof
is substantially larger.

### Rank 3 -- candidate 3: placement policy or arbitrary aligned extents

Best-fit or a dedicated LARGE arena may reduce fragmentation frequency, and
arbitrary aligned extents can assemble 164 free blocks. Neither mechanism
solves the 544-block exchange peak. The deferred note names "three-extent
SwapRAM," while the later flash design deliberately requires one complete
sector-owned run (`docs/research/memory-ownership-plan.md:304-305,485-500`). A
flash scatter/gather design needs new metadata, swapin/free logic, and exclusive
erase-sector ownership; restoring old three-piece allocation is unsafe
(`sys/arch/rp2040/include/machparam.h:108-113`,
`sys/arch/rp2040/dev/flash.c:643-675`).

### Rank 4 -- candidate 4: compaction

Compaction can convert total free capacity into one run, but it cannot create
the missing exchange capacity. It also adds relocation traffic, atomic metadata
publication, overlap ordering, and recovery obligations. Use compaction only
after exhaustion is recoverable and traces show that fragmentation, rather than
the scheduler invariant, causes material refusals.

### Rank 5 -- candidate 5 alone: LARGE-only admission control

LARGE exec admission is useful but insufficient alone. Normal-size processes,
temporary devices, exec spools, evacuation, heap growth, and stack growth share
the same map. The invariant belongs at the shared allocation policy, with LARGE
as one caller.

### Rank 6 -- candidate 1 alone: fail only the observed fork

Failing this fork removes the demonstrated trigger but leaves the scheduler's
identical panic reachable. A defect at a shared primitive requires a shared
failure contract.

## 8. Gates and mutants required to prove rank 1

The following gates are proposed and unrun. Each gate needs a known-good case
and every named known-bad mutant before its verdict counts.

1. **`check-swap-reservation` -- allocation geometry and ownership.** Seed the
   four recorded free runs, try all 95 aligned cursor positions, and request 156,
   160, and 164 blocks. Failure must preserve the map, cursor, address outputs,
   and ownership ledger byte for byte; a 164-block run is the positive control.
   Run compact, wide, diagnostic, and descriptor-full maps. Reject mutants that
   advance the cursor on failure, omit wrap, omit span rounding, split an
   interior run with a full descriptor table, partially allocate on failure, or
   allocate again instead of consuming the reservation. The existing allocator
   geometry lives at `tests/kernel/rmap_test.c:347-494`.

2. **`check-fork-swap-refusal` -- zero-child transaction.** Link production
   `newproc()`, the reservation/copy code, SwapRAM inheritance, and the
   production allocator. Inject refusal before publication and after each
   acquired ownership surface. Require `ENOMEM`, an unchanged parent image and
   execution state, unchanged PID hash and process lists except permitted PID
   consumption, restored free-list membership and reference counts, restored
   LARGE-holder count, released reservations, preserved cursor on refusal, and
   zero flash writes. Reject mutants that ignore refusal, return through the
   child branch, omit either list unlink, omit text/file/directory release, omit
   `swapram_leave_large()`, retain `SIDL`, or leak the process slot. The current
   publication sequence is `sys/kern/kern_fork.c:76-178`.

3. **`check-scheduler-swap-refusal` -- state restoration and wakeup.** Drive the
   actual victim selection into a failed outgoing reservation. Require the
   resident's original addresses and bytes, correct `SLOAD` and run-queue state,
   intact incoming image, one bounded sleep, and a matching wakeup after every
   qualifying extent release. The positive control releases a sufficient run
   and completes the swap-out/swap-in. Reject mutants that retain the pre-clear
   of `SLOAD`, omit run-queue restoration, call `swapin()` after failed
   `swapout()`, spin without sleeping, omit a release wakeup, or accept total
   free blocks while the largest aligned run is too small. The current ordering
   is `sys/kern/vm_sched.c:50-129`.

4. **`check-swap-exchange-headroom` -- end-to-end progress oracle.** Reproduce
   the 216-block other-consumer occupancy and two conditional 164-block LARGE
   images. Rank-1 admission must reject child-only capacity with `ENOMEM` and
   preserve the parent. An admitted smaller case must complete repeated
   parent/child alternation, exit, and wait within a fixed bound. Reject mutants
   that remove the protected extent, let `TFALLOC`, exec spool, or evacuation
   consume it, admit growth beyond it, or release it before the incoming extent
   becomes reusable.

5. **`check-swap-reservation-interleavings` -- sleep boundary.** Insert
   controlled callbacks at `geteblk()` and swap-I/O sleeps, then attempt
   competing fork, spool, evacuation, and temporary allocations. Require
   disjoint ownership and exact cancellation. Reject mutants that perform only
   a free-space preflight, free the reservation before blocking I/O, publish
   process addresses before commit, or publish a shared cursor before a
   multi-image reservation phase completes. The sleep sites are
   `sys/kern/vm_swp.c:29-60` and `sys/kern/ufs_bio.c:227-249,333-349`.

6. **`check-swapram-evac-headroom` -- evacuation plus scheduler capacity.** Run
   the production evacuation fixture at 16 KiB and 64 KiB pool sizes. Retain
   the existing all-or-none shortage assertions, then require evacuation to
   preserve protected resident headroom. Reject mutants that omit one rollback
   free, leave provisional addresses, write before all reservations succeed,
   consume protected headroom, or reopen admission after a failed LARGE
   transition. The existing shortage oracle is
   `sys/arch/rp2040/test/swapram/swapram_evac_test.c:400-442`.

7. **`check-tfalloc-admission` -- authorization, lifetime, and capacity.**
   Require `TFALLOC` mutation to fail through read-only and unprivileged
   descriptors, preserve the existing zero-on-shortage ABI for authorized
   callers, release according to the chosen lifetime, and wake blocked capacity
   waiters. Reject mutants that ignore descriptor mode, omit the privilege
   check, retain an extent after its owner lifetime, free an unrounded extent,
   suppress wakeup, or consume protected headroom. Extend the existing fixture
   at `tests/kernel/swap_ioctl_test.c:90-194` and its calibrated mutant loop at
   `tests/kernel/Makefile:1003-1042`.

8. **`check-init-fork-refusal` -- user-visible recovery.** Inject `ENOMEM` into
   single-user, startup-script, and getty-spawn forks. Require initialized
   status handling, bounded retry with a diagnostic, and continued init control
   flow. Reject mutants that read status after `wait() == -1`, retry without
   delay, treat `pid == -1` as a child, or record `-1` as a live login child.
   The exposed paths are `sbin/init/init.c:278-335,424-472`.

9. **`check-flash-swap-operation-counts` -- flash regression and calibration.**
   For the conditional 164-block image require 41 erase callbacks and 645
   program callbacks, adjacent-image preservation, and injected failure at
   every callback position. Reject mutants that erase per 1 KiB block, omit a
   sector-boundary erase, clear `B_SWAPIMAGE`, free an unrounded span, publish
   relocated metadata before data copy, or use the LARGE-overlapped SwapRAM pool
   without saving its resident bytes. Extend the NOR oracle at
   `tests/rp2040/flash_swap/flash_swap_test.c:46-87,150-274`.

10. **Authorized board gate -- observed case and residuals.** After host,
    mutation, cross-build, and Renode gates pass, stage `bigtest` only in a test
    image and reproduce the stated resident-process set. Capture the complete
    owner ledger, map, cursor, storage counters, fork errno, shell diagnostic,
    epoch, process liveness, and reboot-free continued operation. The expected
    rank-1 result under the recorded 216-block occupancy is a bounded
    `fork() == -1` with `errno == ENOMEM`, a live parent and shell, and zero
    panic. A separate admitted smaller specimen must fork, alternate, and exit
    successfully. The existing test already treats negative fork as a test
    failure and verifies the child's copied BSS
    (`tests/rp2040/swapram_large/bigtest.c:48-77`); the new gate must distinguish
    deliberate capacity refusal from copy corruption.

## Addendum: repeat board layouts (orchestrator, not the audit)

Two later runs of the same gate on the same kernel lineage, rebased onto
main `2d461c64`, panicked at bigtest's fork with
`free: 60@24 4@104 100@284` and `free: 20@4 60@44 4@124 80@304`. All three
transcripts show 164 free of 380 allocatable blocks, so the 216-block
occupancy held. The largest run varied (120, 100, 80) with the cursor. The
repeats support the audit's capacity argument: the shortage is in the total,
not the placement.
