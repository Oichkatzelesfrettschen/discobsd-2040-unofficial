# Review: routeless-resident in-place swap exchange

## 1. Eviction-path falsifier

The selected invariant survives the path inventory. Only the scheduler and
fork creation copy a resident image out of the process window. The scheduler
can use the SwapRAM pool when that tier admits the outgoing image, the
incoming flash image's extent plus a preallocated tail, or one fresh extent
for the whole outgoing image. Fork creation reserves
backing owned by the child before it mutates process state. The remaining
listed paths consume independent temporary or swapped-image backing, change
resident geometry without copying an image, or release ownership.

| Path | Operation and destination under the routeless-resident invariant | Falsifier result |
| --- | --- | --- |
| `sched()` flash-to-flash replacement | The outgoing resident consumes the incoming image's extent and a tail of `max(0, outgoing_span - incoming_span)` allocated before the first media write. The incoming image enters the process window sector by sector through one 4 KiB buffer after each outgoing sector is programmed. When the blocks after the incoming extent are held, the outgoing image takes one fresh extent of its whole span and the ordinary `swapout()` then `swapin()` pair runs. | Supported. Refusal of the tail and of the fresh extent occurs before media mutation; the resident remains loaded and `sched()` sleeps on `runin`. |
| `sched()` with a pool-backed incoming image | `swapram_in()` restores the incoming image and releases its pool allocation. The outgoing image may consume the released pool allocation through the existing SwapRAM admission path; if compression cannot admit it, the scheduler must reserve ordinary flash backing before changing either image. The pool-backed incoming image supplies no flash extent to exchange. | Supported, provided reservation precedes `swapram_in()` and refusal preserves both images. |
| Fork child creation in `newproc()` | `swap_image_reserve()` reserves the child's complete flash extent or SwapRAM slot before PID allocation, process-list publication, or `swapout()`. `swapout(child, X_DONTFREE, ...)` copies the parent's resident bytes into backing owned by the child. | Supported. The child's own reservation is the expressly permitted destination. |
| Exec argument and environment spool | `exec_spool_alloc()` consumes a dedicated SwapRAM spool allocation or an independently allocated, sector-aligned temporary flash extent. `exec_spool_discard()` releases the same backing after exec completion or failure. The spool never carries the resident process image. | Outside resident eviction; supported as an independent temporary allocation. |
| `TFALLOC` temporary-swap ioctl | `swcioctl(TFALLOC)` replaces or releases the addressed temporary-swap unit's own erase-aligned extent. The ioctl does not copy the resident image and does not publish process swap addresses. | Outside resident eviction; supported as an independent temporary allocation. |
| SwapRAM evacuation | Every live pool image is already swapped out. Evacuation must reserve a complete flash extent for each image before writing that image, expand the pool image into that owned extent, publish `p_daddr`, `p_saddr`, and `p_addr` only after the final write, and then release its pool entry. It can report `SWAPRAM_EVAC_NOFLASH` while leaving unallocated images in the pool. | Supported after removal of admission-time departure routes. Evacuation consumes newly reserved backing owned by the evacuated image, rather than a resident route. |
| Heap growth in `brk()` | The hook changes the geometry of the resident image in the fixed process window. It consumes no swap extent and copies no image. A later scheduler eviction computes the outgoing span from the committed geometry and obtains any required tail before media writes. | Supported. Removing `swap_exchange_resize()` must not add an allocation to `brk()`. |
| Syscall stack growth | The syscall entry hook increases the resident stack geometry after checking the data/stack boundary. It consumes no swap extent and copies no image. A later eviction uses the incoming extent plus a preallocated tail. | Supported under the same later-eviction rule. |
| Signal-frame stack growth | `sendsig()` increases the resident stack geometry before constructing the frame. It consumes no swap extent and copies no image. A later eviction uses the incoming extent plus a preallocated tail. | Supported under the same later-eviction rule. |
| Exec geometry replacement | `exec_estab()` replaces the resident data and stack geometry after validating the new executable. It consumes no process-image swap extent; its argument spool remains independently owned. A later eviction uses the incoming extent plus a preallocated tail. | Supported. Removing the resident-route resize restores geometry changes to window-only operations. |
| Process-1 bootstrap geometry | `main()` installs process 1's resident data and stack geometry in the process window. It consumes no swap extent. Its first fork reserves the child's own backing; a later scheduler eviction follows the exchange rule. | Supported. |
| Resident exit | `exit()` discards the resident image. A routeless resident has no process-image flash or pool backing to release, so exit performs no image transfer and only wakes capacity waiters after ordinary resources are detached. | Supported. `swap_exchange_release()` becomes invalid because its ownership object is removed. |
| Swapped-process exit or teardown | A process that owns a flash extent or SwapRAM entry releases that owned backing through the corresponding image-release path; no resident bytes move. | Supported. Ownership ends at teardown. |

The falsifier would be a resident-copy path lacking the incoming extent, an
eviction-time tail, a fresh extent, SwapRAM admission, or the fork child's
reservation. The
source inventory finds no such path. The pool-backed scheduler case requires
an explicit pre-write reservation decision because its incoming image owns no
flash extent; that condition is part of the implementation and gate surface,
not an unproved assumption.

## 2. Contiguous window layout

The per-segment layout rounds data, stack, and u area independently to the
four-block erase-sector boundary. Each segment therefore adds zero through
three blocks of padding, for zero through nine padding blocks in total. The
existing contiguous layout rounds only the combined data, stack, and u-area
block count, for zero through three padding blocks. Per-segment alignment can
therefore consume as many as six blocks beyond the existing contiguous image,
and it changes every allocator, address-publication, SwapRAM, evacuation, and
inspection contract that currently describes one contiguous span.

The contiguous layout can order window writes without losing resident bytes.
Save the outgoing u area before loading any incoming u-area byte. For data and
stack, valid images satisfy `data_end <= stack_start`. If incoming data reaches
the outgoing stack, save the overlapping outgoing stack sectors before loading
those incoming data sectors. If incoming stack reaches the outgoing data, save
the overlapping outgoing data sectors before loading those incoming stack
sectors. Both crossings cannot occur together: they would imply
`incoming_stack_start < outgoing_data_end <= outgoing_stack_start <
incoming_data_end`, contradicting `incoming_data_end <=
incoming_stack_start`. Within a segment, matching offsets preserve bytes by
saving an outgoing sector before loading the corresponding incoming sector.
This establishes an acyclic save-before-overwrite order for the process
window.

The implementation selects sectors greedily, so the order needs a second
argument at sector granularity. Sector `a` must precede sector `b` when
scattering incoming sector `b` overwrites window bytes that outgoing sector
`a` has yet to save. Both images map image offsets to window addresses by a
strictly increasing function: data, then stack, then the u area in `u0`,
which lies above the process window. Take a dependency cycle and its
highest-numbered sector `h`, with predecessor `a < h` and successor
`b < h`. The edge from `a` to `h` places some outgoing bytes of `a` inside
the window span of incoming sector `h`, and the edge from `h` to `b` places
some outgoing bytes of `h` inside the span of incoming sector `b`. Outgoing
monotonicity puts the bytes of `a` below the bytes of `h`; incoming
monotonicity puts the span of `b` below the span of `h`. The bytes of `h`
would then lie inside a span that ends below the span holding bytes of `a`,
which lie below the bytes of `h`: a contradiction. Every unfinished set
therefore has a sector with no unsaved predecessor, the greedy scan finds
it, and the `swap exchange: ordering` panic is unreachable for valid
geometry. `swap_exchange_candidate_safe()` carries the invariant in its
comment.

The implementation keeps the contiguous layout. The owner selected direct
in-place sector exchange through one static 4 KiB SRAM buffer after the
allocator analysis showed that a sliding hole cannot obtain an adjacent
sector for an equal-span fork without consuming four extra blocks. For each
sector selected by the ordering above, the exchange reads the complete
incoming sector into the buffer, erases the sector, programs the corresponding
outgoing sector from the still-resident window and then scatters the buffered
incoming bytes into their window locations. The 4 KiB allocation fits inside
the measured 26.9 KiB gap between the kernel and SwapRAM end at `0x20037ee0`
and `U0AREA` at `0x2003e800`. Equal images allocate zero tail blocks, and the
exchange retains one erase of each sector rather than reserving or repeatedly
wearing the raw-swap scratch sector.

## 3. Resident route removal

The implementation removes the global `swap_exchange` owner/base/span record,
`swap_exchange_resize()`, `swap_exchange_release()` and every geometry caller
from `exec_subr.c`, `init_main.c`, `kern_mman.c`, `syscall.c` and
`sig_machdep.c`. Resident exit now wakes `runin` without releasing nonexistent
backing. `swap_image_release()` returns a flash-backed incoming image's extent
directly to `swapmap`; a pool-backed image releases only its pool allocation.

SwapRAM records again contain compressed bytes without admission-time flash
routes. Pool cancellation therefore frees only pool space. Evacuation first
reserves flash extents for every live pool image and assigns `p_daddr`,
`p_saddr` and `p_addr` in that pass, frees the extents and clears the
addresses again if any allocation fails, and writes only after the complete
reservation pass. `swapram_present()` keeps each image on the pool tier until
its last block is written and its pool entry is freed. Fork creation still
reserves the child's complete extent before process-state mutation, and the
scheduler retains the `runin` refusal path.

## 4. Direct in-place scheduler exchange

`swap_exchange_switch()` first handles a pool-backed incoming image through
the ordinary pre-write image reservation path. For a flash-backed incoming
image it attempts SwapRAM admission for the outgoing resident, then otherwise
reserves only the adjacent suffix
`max(0, outgoing_span - incoming_span)` under `splhigh()`. Equal fork geometry
therefore consumes zero tail blocks. Tail refusal precedes buffer acquisition,
media I/O, address publication and window mutation.

When the blocks after the incoming extent are held, `swap_exchange_switch()`
calls `swap_image_reserve_flash()` for one fresh extent of the whole outgoing
span, the same next-fit reservation `swap_image_reserve()` makes. The
reservation then carries the flash tier and a zero exchange span, so `sched()`
runs `swapout()` into the fresh extent and `swapin()` from the incoming one,
which returns to the map. The tail refusal alone would strand a large resident
beside a small incoming image whenever another image sits directly after it,
with a whole free run elsewhere in the map; origin/main admits that eviction,
and the fallback keeps it. ENOMEM means the pool, the tail and the fresh
extent all refused, with the map and the next-fit cursor unchanged.

The flash-to-flash path builds the contiguous data/stack/u-area mapping for
both images. It selects a sector only when scattering that incoming sector
cannot overwrite any other unsaved outgoing sector. The outgoing tail has no
incoming scatter and is saved first when a larger resident overlaps incoming
window destinations. Each selected incoming sector is read into one static
4 KiB buffer, its physical sector is erased and programmed from the outgoing
window, and only then is the buffered sector scattered into the incoming
window positions. The dependency check saves the outgoing u area before an
incoming u-area sector enters `u0`. Clean executable text is restored after
all outgoing sectors are durable, so a larger incoming clean-text prefix also
cannot overwrite unsaved outgoing data.

`fl_raw()` erases a sector only when a swap-image write starts on that
sector's first byte. `swap_exchange_program_sector()` therefore requires the
first piece of each sector to start at the sector boundary and panics `swap
exchange: geometry` otherwise; block-aligned segment starts make the panic
unreachable.

`swap_exchange_inplace()` publishes the outgoing flash addresses and incoming
window addresses only after every sector succeeds. A larger incoming extent
returns its unused suffix after the last read. A pool-backed incoming image
uses the existing SwapRAM restore path and performs no exchange.

## 5. Media-fault policy and host gate

`swap_with_buf_result()` exposes the existing buffer-I/O completion result to
the exchange while `swap_with_buf()` preserves the historical `hard err:
swap` policy for all existing callers. Every failed exchange read, erase or
program panics exactly `swap exchange: media fault`; metadata remains
unpublished before that panic.

`check-swap-exchange-inplace` links the production `vm_swap.c` and resource-map
allocator to a modeled 384 KiB NOR device. The baseline executes 57,206 checks
over all 576 ordered pairs from six data sizes (0, 3,000, 4,096, 32,768,
37,000 and 65,536 bytes) and four stack sizes (1,024, 5,000, 8,192 and 24,576
bytes), including an empty data segment, sizes that end inside a block and a
sector, and incoming data crossing an outgoing stack location. Each pair
exchanges both directions and checks the data, stack and u area byte for byte
in the window and flash. Six further pairs carry clean executable text on the
outgoing image, the incoming image or both: the text stays out of the flash
image, data loads above it and `exec_text_restore()` runs once for an incoming
image with text. The gate also checks zero-tail equal pairs, refusal with
unchanged media, map and cursor when the tail blocks are held and no run holds
the whole outgoing image, the fresh-extent fallback when one run does, one
erase and one sector-program pass per outgoing sector, and injected erase and
program failure at every sector of an eleven-sector image. The model erases to `0xff`, permits only bit clearing and
reports programming over unerased bytes as failure.

Seven calibrated mutants fail: `skip-sector-erase` at the media-fault oracle,
`write-u-after-reading-incoming` at `expected_in_u`,
`allocate-tail-after-first-write` at `tail_reserved_before_media`,
`ignore-program-fault` at the expected `run_exchange` panic,
`copy-buffer-before-program` at `expected_out_data`, `refuse-owned-tail`
(the tail refusal returns ENOMEM without trying a fresh extent) at
`panic_reservation.addr[0] == run`, and `load-data-over-clean-text` (incoming
data scatters to the window base) at `expected_in_data`.

## 6. Board-map replay

The host fixture replays the retained boot occupants through production
`malloc3_contiguous_next()`. The measured geometries are init at 12 blocks,
update at 8 blocks, the login shell at 20 blocks and the root shell launched
by `su` at 20 blocks. The test images require 68, 108 and 180 blocks for the
60, 100 and 172 KiB BSS builds. The resident test image owns no flash extent;
the allocation after the four boot occupants represents its fork child.

For initial cursor 4 the allocator places the boot images at bases 4, 16, 24
and 44, leaving `320@64`. The predicted child placements are:

| Image | Child extent | Residual free run | Prediction |
| --- | --- | --- | --- |
| fb60 | `68@64` | `252@132` | Fork admitted; equal parent and child need zero exchange-tail blocks. |
| fb100 | `108@64` | `212@172` | Fork admitted; equal parent and child need zero exchange-tail blocks. |
| forkbig | `180@64` | `140@244` | Fork admitted for cursor 4; equal parent and child need zero exchange-tail blocks. |

The RP2040 boot cursor can start at any of 95 aligned positions. The production
allocator sweep admits fb60 and fb100 at all 95 positions. It admits forkbig at
86 positions and refuses it for initial cursors 148 through 180 inclusive. For
a refusing cursor `c`, the free map is `(c - 4)@4` and
`(324 - c)@(c + 60)`; cursor 164, for example, leaves `160@4 160@224`.
The two runs total 320 blocks but neither holds the 180-block child. This
bounded replay therefore predicts restored fb100 capacity without adding a
placement policy. The retained board capture does not identify its initial
cursor or every transient exec-spool allocation, so a future board run remains
the deciding evidence for the exact live boot placement.

## 7. Existing gates and registration handoff

The existing gates now describe the routeless invariant:

- `check-swap-exchange-headroom` executes the compact production fork fixture,
  compiles `vm_swap.c` with SwapRAM disabled and checks nine required source
  connections plus three forbidden resident-route surfaces. Nine removal
  mutants and three route-reintroduction mutants calibrate the source contract.
  The gate retires the five owned-route behavioral mutants. The rewritten
  scheduler fixture installs an ordinary flash-backed incoming image and a
  fragmented free map after that image's eight-block extent. It checks that
  tail refusal preserves the resident, requeues a runnable resident, leaves
  the incoming image swapped, preserves the map, cursor, pool, process and
  user state, and sleeps on `runin`. The pool-backed scheduler fixture checks
  ordinary pre-write flash reservation and successful replacement.
- `check-fork-swap-refusal` removes resident-route setup, resize, transfer and
  release assertions. RAM-tier reservations now consume only pool capacity;
  ordinary flash image release wakes both `runout` and `runin`. The five build
  shapes execute 51,885 assertions each in the compact, wide and diagnostic
  configurations, 51,891 with a full descriptor map and 435 in the unaligned
  fixture. The 29 aligned reservation, publication, release, result and
  scheduler mutants and the two unaligned release mutants are rejected; the
  five owned-route mutant definitions left the mutation tool with their
  anchors. The seven scheduler mutants
  reject loss of the resident `SLOAD` restoration, loss of run-queue
  restoration, swap-in after refusal, reservation-free swapout and, for the
  candidate order of section 9, a sleep before the next candidate is tried,
  reselection of a refused candidate and refusal marks kept across the
  sleep.
- `check-swapram-evac` removes departure-route transfer from spool coexistence,
  requires evacuation to allocate every flash extent before writes, and checks
  complete rollback with pool images preserved on five short maps. The exact
  map evacuates all images.
- `check-kernel` includes the updated fork and exchange gates and exits
  successfully. Its calibrated shutdown-sync mutants print expected failures;
  the production shutdown fixture finishes with 124 checks passed.

The four RP2040 kernel configurations compile the changed sources with
`-Wall -Wextra -Werror -Wshadow -Wundef -Wformat=2` under GNU17 for
soft-float Thumb-1; `logged-build` is the evidence for that compile.

The root `Makefile` exposes `check-swap-exchange-inplace`, and the
`tests/kernel` `check` aggregate runs it, so `check-kernel` and every CI job
that runs the host kernel aggregate include it. `sys/arch/rp2040/doc/TESTING.md`
carries the routeless `check-swap-exchange-headroom` contract, the new
`check-swap-exchange-inplace` row, the fork gate's current assertion counts
and scheduler controls, and the evacuation rollback cases of
`check-swapram-evac`.

## 8. Board measurement

Revision `d4e3b8f3` on PICO-TO-DOS, PICO kernel `unix.uf2` SHA-256
`1f3d487dc94b174156da2b099a69c9e5d0b2c33df12fa05e1109bf5d9e49156d` (kernel
stamp `#9 2648: Tue Nov 10 13:49:11 PST 2026`) with the root `flash.uf2`
SHA-256 `1613cc2ad075f3a02456c5354eb089ce6b2c8771fff5270fbb94139a4b549597`
that the `ce242bcd` run in `review-06.md` used, bare Pico, boot ROM V3, USB
console, driven through `board_user_window.py` with no temporary extents
held. At the first prompt `machdep.swapram_images` read 3,
`machdep.swapram_evacuate` 0, `machdep.mpu.enable` 1, `machdep.mpu.ctrl` 0x5
and `machdep.mpu.programmed` 4; `ps axl` showed `init`, `update` and the login
shell swapped out and the root shell resident. The three `forktest` builds
(`BSSBYTES` of 60, 100 and 172 KiB) ran in that order, twice.

| Image | Round 1 | Round 2 | `ce242bcd` |
| --- | --- | --- | --- |
| 60 KiB bss | `FORKTEST CHILD OK`, status 0, 17 s | `FORKTEST CHILD OK`, status 0, 17 s | `FORKTEST CHILD OK`, 17 s |
| 100 KiB bss | `FORKTEST CHILD OK`, status 0, 27 s | `FORKTEST CHILD OK`, status 0, 27 s | `FORKTEST ENOMEM`, 25 s |
| 172 KiB bss | `FORKTEST CHILD OK`, status 0, 176 s | `FORKTEST CHILD OK`, status 0, 44 s | exec refused, `too big`, 2 s |

The times are console wall time from the command to its status line. No
panic occurred, and the kernel stamp read the same after the runs. A `talloc`
binary search on `/dev/temp0` afterwards measured a largest free run of 220
blocks.

The section 6 prediction holds for admission: all three forks are admitted
and each child completes. The 100 KiB fork that `ce242bcd` refused and the
172 KiB image that `ce242bcd` refused at exec both run. The run observes
admission and completion; it does not observe the child extent addresses or
the initial swap cursor, so the `@64` placements of section 6 remain a replay
result.

Two findings sit outside the prediction. The 172 KiB test took 176 s in
round 1 and 44 s in round 2; the 132 s difference is one stall of the kind
section 9 measures. The root image has 28 free inodes, and `stage()` in
`board_user_window.py` left every uuencode part and its join in `/tmp`, so
the fourth staged file exhausted the inode table and the shell could open no
pipe; `stage()` now removes the parts once the decoded file is measured.

## 9. Swap-in starvation behind a refused candidate

This section is the record the project requires before a kernel change that
rests on board behavior.

**Direct observation.** Same board, kernel and root image as section 8. A
console script (`repro1.py` in the session scratchpad, port held open, every
received chunk timestamped) ran a shell line of 20 iterations of
`echo i=$i; /tmp/talloc /dev/temp2 0`, where the `talloc` call releases an
empty unit and so costs one fork and one exec. With the swap map as found
and with no temporary extents the line took 28.7 to 29.8 s on each of three
runs, 1.4 to 1.5 s per iteration. With `/dev/temp0` holding 220 blocks and
`/dev/temp1` holding 132, which leaves 28 of the 380 usable blocks free,
four lines ran: three took 29.8 to 29.9 s, and one took 156.6 s because the
gap between `i=6` and `i=7` was 128.6 s. The console was silent for that
gap, printed no `cannot fork` message, and the remaining 13 iterations ran
at 1.4 to 1.5 s. One iteration in 80 stalled, for 127.1 s above the normal
iteration time. In a control on the same boot with only the 220-block hold,
which leaves 160 blocks free and so a fresh extent for the 20-block shell,
twelve lines (240 iterations) each took 29.2 to 29.6 s and none stalled.

**Governing invariant.** `review-07.md` sections 1 and 4: the resident owns
no flash route, and `swap_exchange_switch()` (`sys/kern/vm_swap.c`) refuses
an exchange when the outgoing image finds no pool room, no tail after the
incoming extent and no fresh extent; the refusal changes nothing and
`sched()` waits. Liveness needs more than the refusal: a swapped-out
runnable process whose exchange with the resident can succeed must not wait
behind one whose exchange cannot.

**Implementation hypothesis.** `sched()` (`sys/kern/vm_sched.c`) selects the
runnable swapped-out process with the strictly largest `p_time - p_nice * 8`,
the first in `allproc` winning a tie. After a refusal it sleeps on `runin`
and the next pass selects the same process. With 28 blocks free the shell's
fork reserves the child's 20-block extent and leaves 8. If `update`, whose
image is in the SwapRAM pool and which wakes every 30 s, becomes runnable
before the child is swapped in, `update` has the larger `p_time` and is
selected; the 20-block resident shell cannot leave, because the pool is full
and no 20-block run is free, and a pool-backed incoming image offers no
extent to exchange into. The child, whose extent equals the shell's span and
needs a zero-block tail, is never tried. `schedcpu()` (`sys/kern/kern_synch.c`)
increments `p_time` once a second and stops at 127, and `newproc()` puts the
child at the head of `allproc`, so after 127 s the child ties `update` at 127
and wins the tie; the exchange then succeeds and the loop continues. The
model predicts a stall of 127 s, at most one fork per `update` wake, only
when a fork falls between the wake and the child's swap-in.

**Falsifier.** The hypothesis fails if a kernel whose `sched()` moves to the
next candidate after a refusal still shows an iteration gap above 100 s
under the same holds, or if the unchanged kernel stalls with a swap layout
in which the resident can leave (the control below).

**Deciding capture.** The 127.1 s excess matches the 127 s saturation value
within the 1.5 s iteration granularity, with no refusal message and no
re-enumeration of the USB device across the gap. The identity of the refused
candidate is inferred from the source; the PICO kernel exposes no refusal
counter (`STORAGE_SWAPRAM_ATTEMPTS` needs `STORAGE_STATS`). The measurement
after the change is the second half of the capture.

**Predicted gate movement.** `check-fork-swap-refusal` gains a scenario with
two swapped-out candidates, the older pool-backed and refused, the younger
flash-backed with an extent the resident fits: production `sched()` must
reach the younger candidate's exchange before its first sleep. The existing
single-candidate refusal scenario keeps every state assertion; its pass and
SwapRAM service counts rise because the refusal now returns to selection
before the sleep on `runin`. A mutant that sleeps on `runin` straight after
a refusal must fail the new scenario, and a mutant that keeps a refused
candidate excluded across the sleep must fail the retry in the existing one.

**Gate outcome.** The gate moved as predicted. `check-fork-swap-refusal`
holds the two-candidate scenario in both resident states, the single-candidate
refusal scenario counts six scheduler passes and six SwapRAM services, and
the mutants `sched-sleep-after-refusal`, `sched-reselect-refused` and
`sched-keep-refused-marks` fail at their named assertions.

**Measurement after the change.** Bare Pico, boot ROM V3, USB console, root
`flash.uf2` SHA-256 of section 8, each kernel flashed
with the root image and driven from a fresh login. The driver holds
`/dev/temp0` and `/dev/temp1` by a descending search and accepts the layout
when a 20-block request on `/dev/temp2` is then refused with
`machdep.swapram_images` at 3: the pool is full and one fork-sized run is
free. It then runs 20 lines of 20 iterations of
`echo i=$i; /tmp/talloc /dev/temp2 0`, releases the holds, and runs the 60,
100 and 172 KiB `forktest` builds twice.

| Kernel | `sched()` | Stamp | `unix.uf2` SHA-256 | Holds | Lines | Iterations above 5 s |
| --- | --- | --- | --- | --- | --- | --- |
| control | reselects the refused candidate (`vm_sched.c` of the parent revision) | `#1 2699: Wed Nov 11 01:02:50 PST 2026` | `b87f65ce582b340903ef0d708013dee1d02d3b25922e4bf64de2cc9e0bb85815` | 188 + 136 | 20 | 5 of 400: 127.8, 128.2, 128.5, 128.5, 128.2 s |
| changed | passes over the refused candidate | `#1 2699: Wed Nov 11 00:01:37 PST 2026` | `6893c5101f97e87d6bae802056fdb4cdd8d0cfa16b5fed221027553395b9961b` | 200 + 132 | 20 | 0 of 400 |

Both kernels come from one tree and differ in `sys/kern/vm_sched.c` alone.
On the control kernel fifteen lines took 26.5 to 29.7 s and five took 153.4
to 155.7 s, each with one gap of 127.8 to 128.5 s and no `cannot fork`
message. On the changed kernel every line took 27.1 to 30.4 s. The falsifier
did not fire: no iteration on the changed kernel exceeded 5 s, against the
100 s bound. An earlier run of the `d4e3b8f3` kernel under holds of 260 and
100 blocks stalled 127.8 to 128.5 s in three of its first four lines; its
remaining lines were lost with the host session.

**Fork tests after the loop.** On both kernels the 60 KiB test passed in 17
to 18 s and the 100 KiB test in 27 s, twice. The 172 KiB test was admitted
once on the changed kernel (62 s) and refused with `FORKTEST ENOMEM` in its
second round; the control kernel refused it in both rounds. `ps axl` after
the runs showed the login shell's 49-block image at swap block 0xf7 and the
root shell's at 0x94 on the changed kernel, and at 0xbf and 0x153 on the
control. Two small images in the middle of the 380-block map leave no free
run for the 172 KiB child's extent, so the refusal follows the layout the
hold loop leaves behind and occurs with and without the scheduler change.
Section 8 measured the same test admitted twice on a map no hold had
touched. Next-fit placement of small images is the open cause; a placement
policy needs an owner decision (brief-07 item 6).
