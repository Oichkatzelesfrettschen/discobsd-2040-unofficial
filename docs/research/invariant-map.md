# Invariant map: four contracts, who enforces them, who owes them

A claim about the port is useful when it names the code that enforces it,
the callers that must uphold it, and the evidence that tests it. This note
records that chain for four contracts the port's design rests on, so a
reader or a model answering a question about one of them can separate a
checked condition from a documented precondition. Each entry carries the
same fields: the claim, what enforces it, what the caller owes, the
configuration it depends on, the source evidence by rank (AGENTS.md,
Evidence), the existing test, and what stays open. A field left open stays
open; a plausible explanation does not fill it.

The sources are the tree at the commit this note lands in. Symbols were
found with `graft grep` and `git grep`, since the C tier drops cross-file
callers of a name that a header prototype and a definition both carry.

## 1. Raw swap: erase alignment, ordering, and page preservation

### 1a. A swap image piece is programmed at page granularity and erases a sector only at its boundary

- Claim: `flash_swap_append` (sys/arch/rp2040/dev/flash_swap.c) refuses an
  offset that is not program-page aligned, erases a sector when and only
  when the piece reaches an erase-sector boundary, programs whole pages,
  and pads a short final page with 0xff.
- Enforced by: the function itself. Its only checks are the page-alignment
  test on entry and the sector-boundary test inside the loop; the erase
  callback is invoked at the boundary and nowhere else.
- Caller obligation: everything the file-scope comment states and the
  function does not check. The first nonempty piece of an image starts on
  an erase-sector boundary; later pieces start on page boundaries and
  arrive in ascending order; no two images overlap. The function carries no
  state across calls, so a piece that arrives out of order or starts inside
  a sector another piece has already programmed is not detected here.
- Relevant configuration: `SWAP_IMAGE_ALIGN` (sys/kern/vm_swap.c) selects
  the aligned allocation and the `B_SWAPIMAGE` flag; without it every swap
  write is a plain `B_WRITE` and takes the rewrite path.
- Source evidence (rank 4, port source): flash_swap.c L26-L27 (page test),
  L30-L32 (boundary erase), L41-L46 (pad); sys/sys/buf.h L194 names
  `B_SWAPIMAGE` "ascending write in an erase-aligned image".
- Existing test: tests/rp2040/flash_swap/flash_swap_test.c, run by
  `check-flash-swap`, drives the production algorithm against a NOR model
  that rejects 0-to-1 transitions, unaligned calls, duplicate erases and
  overlapping images (flash_swap.c L10-L12); its assertions count erases
  per sector and refuse a misaligned offset without a program call.
- Open: the model rejects a violation of the caller contract, so the test
  proves the algorithm under callers that keep it and says nothing about a
  caller that breaks it on the board.

### 1b. The callers keep the ordering contract, each in its own way

- Claim: the three producers of `B_SWAPIMAGE` writes supply erase-aligned,
  ascending, non-overlapping pieces.
- Enforced by, per producer:
  - Process swapout (sys/kern/vm_swap.c L166-L169): `malloc3_contiguous_next`
    allocates data, stack and u-area as one run aligned to
    `SWAP_IMAGE_ALIGN` and rounded to a whole number of erase sectors
    (`swapimagespan`, L39-L46), then writes them in that order (L197,
    L205, L235). Alignment and ascending order come from the allocator and
    the write order, not from the flash layer.
  - Exec argument spool (sys/kern/exec_subr.c L57-L69): `exec_spool_flush`
    writes from a position that only advances, so pieces ascend.
  - Temporary swap devices (sys/dev/swap.c L42-L53): `swtemp_write_flags`
    grants `B_SWAPIMAGE` only while the block written is exactly the next
    expected one; any other write disables append mode for that extent for
    good and falls back to `B_WRITE`.
- Caller obligation: the resource map must never hand out the first erase
  sector, which the rewrite path uses as its spare (flash.c L563-L568,
  `FLASH_SWAP_SCRATCH_OFFSET` equals `FLASH_SWAP_OFFSET` in flash.h L97);
  `fl_raw` refuses a write below `FLASH_SECTOR_BYTES` (flash.c L585-L586)
  as the last line of that defense. `swap_cursor_init` starts the next-fit
  cursor at `SWAP_IMAGE_ALIGN` (machdep.c), so the map begins past the
  spare.
- Relevant configuration: `SWAP_IMAGE_ALIGN`, `SWAPRAM` (a RAM-tier image
  writes no flash and publishes no cursor), `COMPACT_SWAPMAP` (descriptor
  width, machdep.c static asserts).
- Source evidence (rank 3 and 4): vm_swap.c as cited; swap.c as cited;
  flash.c `fl_raw` L571-L605 selects append or rewrite on the flag alone.
- Existing test: sys/arch/rp2040/test/swapram/swapram_evac_test.c panics
  on a write without `B_SWAPIMAGE` or past the map; `check-exec-spool`
  covers the spool over modeled SwapRAM (TESTING.md L279);
  flash_swap_test.c "temporary extent in strict block order uses append
  mode ... a later duplicate block uses the rewrite path".
- Open: no test writes a process image through `fl_raw` on the board and
  reads it back; the board-side evidence for 1a and 1b is the port running
  with swap in use, which is rank 1 but indirect.

### 1c. A rewrite preserves the untouched pages of every sector it touches

- Claim: `flash_swap_rewrite` copies each affected sector through the spare
  sector, erases the destination, and programs back every page, the new
  bytes where the range covers them and the copy elsewhere.
- Enforced by: the function (flash_swap.c L74-L115): spare erase, copy,
  destination erase, program-back, one sector per loop.
- Caller obligation: page-aligned offset and length, a sector-aligned spare
  that is never the destination (checked, L68-L77); the spare's own
  previous content is sacrificed, so the spare must hold nothing anyone
  reads. Exclusive use of the scratch buffer and the spare sector for the
  duration (`flscratch_take(FLS_SWAP)` under `splbio`, flash.c
  L588-L602).
- Source evidence (rank 4): as cited; flash_swap_test.c asserts the
  neighbors of a rewritten block are unchanged and that a rewrite crossing
  a sector boundary reports each operation failure.
- Open: power loss between the destination erase and the program-back
  leaves that sector's untouched pages only in the spare. No recovery
  protocol exists in the tree (`git grep -i power sys/arch/rp2040/dev`
  finds none). Hypothesis: none is required, because the raw swap region
  holds process images and temporary extents that do not survive a reboot
  and `swap_cursor_init` re-derives its position from watchdog scratch or
  ROSC sampling rather than from flash content. A claim that the rewrite
  is crash-atomic is false; a claim that it needs to be is unsupported.

## 2. Packed executables: preflight, then commit

- Claim: `exec_hsaout_check` (sys/arch/rp2040/rp2040/exec_hsaout.c) never
  destroys the running image on a damaged container. It expands both
  streams into a discard buffer and compares lengths and CRCs with the
  header while the old image is intact; only after that does it commit,
  and a mismatch on the second expansion into the user window kills the
  process before any of its bytes run.
- Enforced by: the function's order (file comment L1-L10; preflight
  L145-L160; commit comment L188-L193; `psignal(p, SIGKILL)` L206).
  Every failure after recognition is `EFTYPE`, which the exec dispatcher
  passes through so the container is never offered to another format
  (L2-L5, L119).
- Caller obligation: the exec dispatcher must treat `EFTYPE` as final
  rather than trying the next format; the raw a.out layout check must have
  run with the pack flag masked (L111-L116), because the container is
  shorter than its image and the raw file-size test would reject it.
  `p_tip` holds the executable so exit releases the exclusion after a
  failed second pass (L188-L193).
- Relevant configuration: the SwapRAM tier decides whether the argument
  spool and the image land in RAM or flash (`check-swapram` ties
  vm_swap.o, exec_hsaout.o and kern_sysctl.o to Config).
- Source evidence (rank 4): as cited. Rank 1: board_exec_hsaout.py runs
  textcrc raw and packed across a swap (TESTING.md L447).
- Existing test: `check-aout` (exec_aout.h midmag macros and the layout
  check before commit, TESTING.md L149), `check-swapram`,
  `check-exec-spool`, textcrc on the board.
- Open: the verdict for `AOUT_TOOBIG` is `ENOMEM` rather than `EFTYPE`
  (L134), the one recognized failure a caller could retry after freeing
  memory; whether any caller does is not traced here.

## 3. USB console: who owns an endpoint buffer

- Claim: a DPSRAM endpoint buffer belongs to the controller while
  `AVAILABLE` (buffer control bit 10) is set and to the processor once the
  controller clears it; the driver never writes a buffer the controller
  owns and never arms a buffer twice.
- Enforced by: `usb_buf_arm_word` (usb.c L354-L360), which writes the
  control word, waits, and only then sets `AVAILABLE`, the sequence
  datasheet 4.1.2.7.1 requires across the clock domains; `usb_tx_kick`
  (L744-L775), where `tx_busy` blocks a second IN transfer until the
  completion interrupt; `usb_rx_rearm_if_idle` (L829-L841), which re-arms
  the bulk OUT buffer only when both `AVAILABLE` and `FULL` read clear
  (L837), because an OUT token enters its data phase only while
  `AVAILABLE` is set (4.1.2.8.3) and a buffer left un-armed makes the
  controller NAK every keystroke, which the host sees as a write timeout
  (L811-L826).
- Caller obligation: the tty layer reaches the endpoint only through
  `usbstart` and `usbputc`, which go through the transmit ring (file
  comment L33-L37) rather than the buffer; the ring is the software-owned
  state and the buffer is the hardware-visible one. The interrupt handler
  is the only reader of completion state.
- Relevant configuration: `USB_TXRING` (ring depth, 8K holds a boot);
  the RP2040-E15 guard (L386-L392) measures a frame position with
  TIMERAWL to decide when an EP0 IN may be armed.
- Source evidence (rank 2): datasheet 4.1.2.7.1, 4.1.2.7.4, 4.1.2.8.3
  as the comments cite. Rank 4: usb.c as cited. Rank 1: the console
  itself on the board, which is indirect evidence for every ownership
  transition it happens to exercise. docs/research/usb-outwedge-sim.py
  is a host model of the mechanism `usb_rx_rearm_if_idle` answers, not
  an observation of the silicon. `check-renode` bears on none of this:
  tools/renode/machine.resc boots the PICO_UART kernel and states that
  Renode_RP2040 models no USB, so its console is UART0.
- Existing test: none on the host reaches the buffer words; the board
  console is the evidence, and the board observation below is what it
  looked like on 2026-09-19.
- Board observation (rank 1, kernel `DiscoBSD 2.7 (PICO) #1 1108: Wed
  Sep 16 09:05:20 PM PDT 2026`, which carries a56808da, the re-arm fix):
  eight host sessions in a row over /dev/ttyACM0 through pyserial, each
  an open, a paced byte stream (3 ms per byte, 50 ms per line, three
  uuencoded 7 KB programs sent with `cat >` and decoded on the board with
  matching cksum), commands, and a close, all answered. The ninth open
  received nothing: a carriage return produced no echo, a one-byte
  host write was accepted and never drained (close blocked until the
  host timeout), `lsusb -v` string reads timed out, and `picotool reboot
  -f` reported the request sent while the host recorded no disconnect
  and the device number stayed 2. The device stayed enumerated at full
  speed with its three interfaces. This is neither the bulk-OUT wedge of
  docs/research/usb-outwedge.md (there IN keeps delivering and the host
  write fails with EIO) nor the quirk of
  docs/research/console-silent-after-web-session.md (there the reset
  interface answered and rebooted the board): here no endpoint answered,
  the reset interface included. Hypothesis: the kernel stopped servicing
  the USB interrupt or stopped altogether, after a session that ran
  textcrc in the background across console-driven swapping and after
  `sysctl -n hw.machine` answered at 15:27. The decisive probe is the
  UART0 fallback on GP0/GP1 during the next occurrence, or a BOOTSEL
  replug followed by the same sequence on the current kernel; until then
  the observation is a hang of unknown site, not an ownership defect.
- Open: the ownership handoff on the IN side after a SET_INTERFACE or
  reset, where the comment at L822 says the guard "fires only" after the
  handlers leave `AVAILABLE` set, is a claim about every reset path; a
  reader of a new reset path owes it a check.

## 4. Signal frames: what holds at entry, in the handler, and on return

- Claim: `sendsig` (sys/arch/rp2040/rp2040/sig_machdep.c) carves the
  frame below an eight-byte-aligned stack pointer, reserves the 32 bytes
  the exception return pops (r0-r3, r12, lr, pc, xPSR; ARMv6-M ARM
  B1.5.6) ahead of the saved context, and clears STKALIGN (xPSR bit 9) in
  the frame the handler starts on, so the handler enters AAPCS-aligned and
  the saved context is never overwritten by the hardware frame.
  `sigreturn` finds the context at `tf_sp + 32` and restores the original
  xPSR, mask and stack state.
- Enforced by: sendsig L37-L48 (frame comment, `sizeof(struct sigframe)`
  104, a multiple of eight), L71-L74 (`& ~7U` on both the signal stack and
  the interrupted sp), L122-L128 (STKALIGN cleared for the handler,
  restored by sigreturn); a frame that does not fit the stack raises
  SIGILL (L79-L84). sigreturn L164-L176 rejects an unreadable context
  with EFAULT and masks `sigcantmask` out of the restored mask.
- Caller obligation: libc's trampoline (lib/libc/arm/sys/sigaction.S)
  issues the sigreturn SVC with sp at the handler entry value, `sfp + 32`
  (sigreturn comment L155-L160); a trampoline that moves sp first breaks
  the context lookup. The handler is ordinary C and owes nothing beyond
  AAPCS.
- Relevant configuration: `DIAGNOSTIC` and `SYSTRACE` print the frame;
  neither changes it.
- Source evidence (rank 2): ARMv6-M ARM B1.5.6 (exception entry frame)
  and B1.5.8 (STKALIGN adjustment), as the comments cite. Rank 4: as
  cited. Rank 1: tests/rp2040/sigtest/sigtest.c on the board, whose four
  checks are each the symptom of one wrong layout (mask equals the
  trampoline address, EINTR returns the handler address, and so on).
- Existing test: sigtest on the board, "SIGTEST OK"; `sysctl -w
  kern.systrace=2` shows each frame.
- Board observation (rank 1, 2026-09-19, the PICO #1 kernel named in
  section 3): sigtest built from this tree (7280 bytes, cksum
  1576466912), sent over the console and verified by cksum on the board,
  printed every check ok and `SIGTEST OK`: pause returns -1 with EINTR,
  the mask is preserved, the handler sp is eight-byte aligned
  (0x20023d88), sc_psr is Thumb (0x61000000), sc_sp sits 0x48 above the
  handler sp, which is the 104-byte frame less the 32-byte hardware
  frame, sc_pc is in text, and r7 survives. fptest from the same session
  printed `FPTEST OK`, which is the Boot ROM float contract rather than
  this entry. textcrc ran in the background across console-driven
  swapping in the session that preceded the hang recorded in section 3;
  its output was not captured, so the packed-text restoration claim of
  section 2 has no new board result.
- Open: sigreturn checks that the context is readable, not that it was
  written by sendsig; a handler that rewrites `sc_psr` chooses its own
  xPSR, and what the exception return does with an arbitrary value there
  is a question for the ARM ARM rather than the port.

## 5. Heap growth: the kernel refuses, libc keeps the refusal, malloc keeps its arena

- Claim: a refused heap extension advances nothing. The kernel refuses a
  break that would exceed the process's permitted size or reach its
  stack; libc's `sbrk` then returns `(void *)-1` without moving its
  recorded break; `malloc` then returns NULL with ENOMEM without linking
  the refused region into its arena.
- Enforced by: `brk` in sys/kern/kern_mman.c L16-L60, two independent
  refusals before any state changes: `u_tsize + newsize + u_ssize` above
  the ceiling (`swapram_ceiling(p)` under `SWAPRAM`, `MAXMEM` otherwise,
  L33-L38), and `p_daddr + newsize` above `p_saddr` (L46-L49), the
  stack's current boundary, which the size sum alone does not see. Only
  after both does it set `p_dsize`, clear the new bytes, and return the
  new break (L51-L59). lib/libc/arm/sys/sbrk.c: `sbrk` returns
  `(void *)-1` when `_brk` refuses and moves `_curbrk` only on success
  (its comment records the version that returned the old break and let
  malloc carve blocks from memory the process did not own).
  lib/libc/gen/malloc.c L163-L177: the arena grows only when `sbrk`
  answers with an address, and a `-1` answer is ENOMEM before any link.
- Caller obligation: none beyond the interface; the chain is three
  enforcers in series, and each keeps the refusal a refusal.
- Relevant configuration: `SWAPRAM`. `swapram_ceiling` (swapram.c
  L756-L760) is `MAXMEM` plus `SWAPRAM_BONUS` only under `P_LARGE`; the
  kernel comment at kern_mman.c L28-L32 states the asymmetry: a large
  process may grow to its ceiling, and a small one cannot acquire LARGE
  through `brk`, because its stack already sits under the window and the
  bonus lies above that stack.
- Source evidence (rank 3 and 4): as cited.
- Existing test: `check-libc-malloc` (TESTING.md L283) compiles malloc.c,
  calloc.c and the ARM sbrk.c from the tree over an `sbrk` and `_brk`
  the test owns, so exhaustion is a ceiling the test sets; it proves
  `sbrk` returns -1 with brk's errno on refusal and that an oversized
  request is ENOMEM before any arena call. It is a host binary: the
  kernel's two refusals and the syscall boundary are outside it.
  swapram_evac_test.c checks `swapram_ceiling` at both values.
- Open: no board run drives a process to its ceiling and reads the
  ENOMEM back through malloc; and the ceiling check reads `u_ssize`,
  the stack's size so far, so a process whose stack later grows toward a
  data segment admitted under the second check is a case the stack-fault
  path, not `brk`, decides.

## 6. SwapRAM: what a RAM-tier image excludes, and what it does not

- Claim: a swapout the RAM tier accepts allocates no flash swap-map
  extent, writes no raw-flash swap image, and publishes no flash cursor;
  the process holds its image as compressed bytes in the pool and its
  flash addresses stay zero.
- Enforced by: sys/kern/vm_swap.c L160-L165: when `swapram_out` accepts,
  the flash addresses `a[0..2]` are zero and the `malloc3_contiguous_next`
  branch, the `swap_cursor_publish` call, and the three `swap` writes are
  skipped (L166-L235, each under `if (! ram)`); swapram.c L11-L13 states
  that an image in the pool holds no swapmap blocks and that swapout
  leaves `p_daddr`, `p_saddr` and `p_addr` zero to say so. On swapin
  (vm_swap.c L86-L89) a present image comes back whole from the pool and
  nothing is freed.
- What the claim does not cover, each with its enforcer:
  - Executable text. `swapin` restores text from the executable through
    `exec_text_restore` before it consults the pool (L71-L80): the pool
    holds the mutable image alone, so a RAM-tier swapin still reads the
    executable, packed or raw, and a corrupt text kills the process.
  - Evacuation and the epoch. swapram.c L15-L24: the ordinary swapout and
    swapin paths neither sleep nor allocate, which is what lets the codec
    and segment table be file-scope statics; the evacuation
    (`swapram_evacuate`, L613-L692) and the epoch requests run in the
    swapper and in process context, sleep in `swap_with_buf`, use the
    decoder while no swapin can, and close the pool to new images for
    their duration. An evacuated image is written to flash through the
    `B_SWAPIMAGE` path of section 1b (the evacuation test panics on any
    other write shape).
  - The LARGE window. Under `P_LARGE` the pool is the top of a large
    process's stack (swapram.c L57-L60; `user_top`, L764-L768), so the
    same bytes cannot hold compressed images and serve as a process's
    bonus memory at once; the epoch machinery (L141-L148, L198-L206)
    keeps a live spool SMALL because the LARGE window overlaps it, and
    admission is closed whenever the epoch is not SMALL.
- Caller obligation: a user of the pool's address range (a LARGE process,
  the spool) obtains it through the epoch, never by reading
  `swapram_epoch` and assuming; a caller of `swapram_evacuate` waits for
  the answer the sysctl reads back (swapram.h L81).
- Relevant configuration: `SWAPRAM`, `SWAP_IMAGE_ALIGN`; `swapram_init`
  panics unless the linker placed the pool exactly at the window's end
  (L772-L778), which is the geometry every claim above rests on.
- Source evidence (rank 3 and 4): as cited. Rank 1: the board programs
  evactest, epochtest, bigtest and hugetest (tests/rp2040/swapram_*),
  each of which patterns memory, drives the transition, and verifies the
  pattern; `check-swapram` ties the linked tier to Config.
- Existing test: swapram_evac_test.c on the host (the evacuation and
  ceiling arithmetic); the four board programs above. evactest was sent
  to the board on 2026-09-19 and verified by cksum but not run: the
  console hang of section 3 intervened, so its result is owed.
- Open: whether an image admitted to the pool and then evacuated lands
  in flash with the same bytes is proven by evactest's pattern check and
  by nothing on the host; and the claim that nothing on the swapout
  path sleeps is a property of every function it calls, which a new
  call into a sleeping path breaks silently.

## How to use this map

A question about one of these contracts starts from the entry, reads the
cited spans, and treats the "open" field as the falsifier still owed. A
model's answer that turns a caller obligation into a guarantee of the
callee (section 1a is the discriminator: "validates ascending erase-aligned
writes" is false; "validates page alignment and relies on the caller for
ordering" is true) fails the entry; so does one that turns "the RAM tier
writes no flash" into "SwapRAM never touches flash" (section 6), or that
reads the size ceiling as the only refusal in `brk` (section 5). The
console transfer that put the board programs in place is replayable:
uuencode the a.out, send it to `cat > /tmp/NAME.uu` one byte per 3 ms
and one line per 50 ms, decode with uudecode, and compare cksum against
the host before running it; a faster stream drops bytes in the console's
input path. docs/research/graft-concept-review.md reads the graft concept
layer against these six entries.
