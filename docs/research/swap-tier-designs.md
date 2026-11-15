# Swap tier designs for a 264 KB machine with four hardware cases

Evidence class: sources read, host arithmetic over retained board counters,
and no new board, Renode or build run. Every size and time figure labeled
"estimate" is unmeasured; every figure labeled "measured" comes from a
retained capture named beside it. Tree read at `6fb763b1`. `origin/main`
moved by two commits while this note was written (`git diff --stat 6fb763b1
origin/main` touches `sys/arch/rp2040/dev/usb.c`, `tests/kernel/` and
`docs/research/driver-datasheet-audit.md`, none cited here). Source hashes are in the last section.

## Question and fixed decisions

Which swap arrangement gives the least NOR flash wear and the least kernel
RAM and text on this system, for four hardware cases:

| Case | Hardware | Kernel that carries it |
| --- | --- | --- |
| P | bare Pico, flash only | PICO |
| M | PicoCalc with the 8 MB PSRAM | PICOCALC_PSRAM |
| S | PicoCalc with an SD card (SPI, swap partition type 0xb8) | PICOCALC (links the SD driver, `PICOCALC.md:71`) or PICOCALC_PSRAM |
| MS | PicoCalc with both | PICOCALC_PSRAM |

The owner has decided: one swap device chosen at boot, in the order PSRAM,
then the SD swap partition, then the flash area `fl1`; the compressed RAM
pool (SwapRAM) stays in front; a card absent at boot falls back to flash; a
card pulled while holding images kills the processes swapped to it and the
system stays up; the flash swap shrinks once measured. This note takes those
as constraints and ranks the designs inside them.

PICOCALC_PSRAM covers M, S and MS in one binary: PSRAM presence is already
a runtime decision, and `psramprobe()` leaves `swapdev` on `fl1` when the
part is absent (`sys/arch/rp2040/dev/psram.c:99-128`;
`docs/research/rp2040-single-kernel.md:37-45`). PICOCALC carries the SD
driver without the PSRAM driver (`PICOCALC.md:39,71,87`), so it also covers S
and costs no PSRAM text.

## What the tree does today

### Baseline traffic (bare Pico, measured)

`docs/research/trim-survey/board/swap/admission/fixed-sh.txt` and
`main-sh.txt` hold 24 compiles of the two-line program on the measured Pico
with a `STORAGE_STATS` kernel (`sys/arch/rp2040/doc/research/flash-traffic-per-workload.md:186-231`).
`delta.py` in that directory recomputes the table; the fixed-shell arm gives
the figures used below.

| Quantity over 24 compiles | Value | Per compile |
| --- | ---: | ---: |
| swap sector erases | 1,412 | 58.8 |
| swap bytes written | 4,986,464 | 207,769 |
| swap bytes read | 5,281,532 | 220,064 |
| SwapRAM attempts, admissions | 305, 63 | |
| refused: fragment, nospace | 196, 46 | |
| refusals where compressed data+stack alone fits the largest free run (`refused_data_fits`) | 183 of 242 | |
| refused swap-outs by span | 192 of 6 sectors, 49 of 5, 1 of 12 or more | |
| admitted u area, mean | 1,016 B (64,012 / 63) | reserved 3,460 B |
| admitted data+stack, mean | 3,310 B (208,566 / 63) | |
| zero bytes in refused images | 69.1 percent (2,930,560 / 4,243,040) | |
| 4 KiB data chunks entirely zero, refused images | 4 of 782 (0.5 percent) | |

The refused set is 242 swap-outs for 1,412 erases, 5.8 sectors each, and
the dominant class (192 swap-outs of 6 sectors) accounts for 1,152 of them,
82 percent. Fragmentation, not capacity, causes 81 percent of the refusals
(196 of 242), and 76 percent of the refusals (183 of 242) would have fit
their compressed data and stack. The pool is not too small for the hot
image; it is badly shaped and its u-area reservation is 2.4 KiB too large.

Swap is the binding wear term on the bare Pico: the hottest observed sector
reaches the rated 100,000 cycles after about 104,000 compiles against about
977,000 for the root (`flash-traffic-per-workload.md:134-171`).

### What the three other cases do to wear

PSRAM swap has no erase: `psramstrategy()` completes inside the call and
the 384 KB `fl1` region goes unused (`sys/arch/rp2040/doc/PICOCALC.md:974-976`).
In case M and case MS the NOR swap wear is zero by construction once the
part probes. The PSRAM row on the board is "not run" (`PICOCALC.md:90`); the
transfer path is exercised by a timing model only (`PICOCALC.md:86`).

SD swap moves the same 207,769 B per compile to a card whose flash
translation layer is outside the kernel. Its wear is unmeasured (see
"SD behavior").

### Where a swap I/O error goes today

Tracing a read error on the swapper's path (2.11BSD lineage, this tree):

1. `sched()` selects a swapped-out runnable process and calls `swapin(p)`
   (`sys/kern/vm_sched.c:118`).
2. `swapin()` reads the data, stack and u segments through `swap()`
   (`sys/kern/vm_swap.c:380-387`). The text is restored from the executable
   and a failure there already kills the owner: `exec_text_restore()` fails,
   `psignal(p, SIGKILL)`, and the swapper goes on (`vm_swap.c:357-367`).
3. `swap()` takes a cache header and calls `swap_with_buf()`
   (`sys/kern/vm_swp.c:74-82`). `swap_with_buf_result()` returns -1 when
   `B_ERROR` is set or `b_resid` is nonzero (`vm_swp.c:53-54`) and
   `swap_with_buf()` turns -1 into `panic("hard err: swap")`
   (`vm_swp.c:62-68`). The 2.11BSD original is the same two lines
   (`211bsd/sys/sys/vm_swp.c:61-62`).
4. A strategy routine sets `B_ERROR` for: `psramstrategy()` on ENXIO,
   EINVAL and a failed frame (`psram.c:68-96`); `sdstrategy()` on a replaced
   card, a short or failed sector (`sys/arch/rp2040/dev/sd.c:341-395`);
   `flstrategy()` on a failed program or a refused write outside the
   writable range (`sys/arch/rp2040/dev/flash.c:1104,1145`).

Every other consumer of swap blocks panics the same way: the write side of
`swap_image_copy_out()` (`vm_swap.c:682,690,720`), the in-place exchange
(`panic("swap exchange: media fault")`, `vm_swap.c:525-528,603-607`), the
exec argument spool (`sys/kern/exec_subr.c:73,128`), the SwapRAM evacuation
(`sys/arch/rp2040/rp2040/swapram.c:336`) and the temporary devices that
`fsck` uses (`sys/dev/swap.c:175,227,231,366,369`). The one consumer that
returns an error is the `ps` read of a swapped u area
(`sys/kern/kern_sysctl.c:895-908`, EIO).

The SD driver adds a second lifetime problem. `sd_replaced()` makes every
transfer fail with EIO until the last close (`sd.c:347-350`; the flag clears
only in `sdclose()` when no partition is open, `sd.c:307-316`). A swap
partition is opened once by `init_main()` (`sys/kern/init_main.c:145`) and
never closed, so a card pulled and reinserted stays failed until reboot.

### The minimal "kill the owner" change

The reusable precedent is `swapin()`'s text failure: the process returns
loaded with bad content and SIGKILL, and the ordinary exit path (which needs
its u area) closes its files and frees its slot. The minimal change is:

1. `sys/kern/vm_swp.c`: add `swap_result()` (cache header, call
   `swap_with_buf_result()`, release, return an errno) beside `swap()`.
2. `sys/kern/vm_swap.c` `swapin()`: read the u area first with
   `swap_result()`, then data and stack. A data or stack failure zero-fills
   that segment's window, prints one line, calls `psignal(p, SIGKILL)` as at
   `vm_swap.c:365`, and still calls `swap_image_release()` (`vm_swap.c:318`)
   so the extents return to `swapmap`.
3. `sys/kern/vm_swap.c` `swap_image_copy_out()` and
   `swap_exchange_program_sector()`: write failures return an error. The
   process is still resident, so the unwind is `swap_image_cancel()`
   (`vm_swap.c:286-314`), keeping `SLOAD`, and a device-failed flag that
   makes `swap_image_reserve_flash()` return ENOMEM. This holds for the
   copy-out path only. A failure inside `swap_exchange_inplace()` has no
   unwind: earlier sectors of the incoming image are already scattered into
   the window while later outgoing sectors are unwritten, so RAM and the
   device each hold part of both images (`docs/research/swap-exhaustion/plan.md:25-27`).
   Both processes are then lost, and today its write (`vm_swap.c:525-528`)
   and read (`vm_swap.c:603-607`) both panic. The exchange tier served 91 of the 242 device swap-outs
   in the baseline run, so on a removable device the exchange tier should be
   skipped (reserve a fresh extent instead, `swap_image_reserve_flash()`),
   which costs the in-place saving only where the card is a swap target. The refusal path of
   `sched()` (`vm_sched.c:86-107`) and fork's ENOMEM already handle a refused
   reservation (`docs/research/swap-exhaustion/plan.md:43-48`).
4. `sys/dev/swap.c` `swcread`, `swcwrite`, `swstrategy` unit paths: use
   `swap_result()` and return EIO; without this a user-reachable temporary
   device I/O still panics.
5. `sys/arch/rp2040/dev/sd.c`: nothing for the kill itself; a swap-aware
   `sdclose`/re-accept is a separate decision (see "Open questions").

Hole in the minimal change: a failed read of the u area. The process cannot
run `exit()` without its u area, because the open files and current
directory are recorded there (the kernel stack of a sleeping process also
lives in it, `zswap.md:47`). When the whole device is gone, the u area is
the one segment that cannot be skipped, and it is on the dead device with
the rest. The options are a leak of those file and inode references, a
reaper that frees the slot without closing, or keeping the u area off the
removable device. The last is design 11 below and is the only one that
retires the failure class.

## Designs

Cost figures in the text column are estimates from reading the code; none is
built. The text headroom is 15,512 bytes on PICOCALC_PSRAM after the tile atlas
(`PICOCALC.md:460`); `lcdgfx.c` adds 2,145 text bytes to that kernel
(`PICOCALC.md:52`), leaving about 13.4 KB if the two stack (derived, not
re-measured), and the kernel data and bss end 62,408 bytes below the end of
RAM (`PICOCALC.md:50`). The estimates below sum to 2.9 to 5.6 KB for every
design except 3, and 1.8 to 3.1 KB for the set this note recommends (1, 2, 6,
7, 8, 11); both fit. PICO has 35,428 bytes of headroom (`PICOCALC.md:460`).

### 1. Reservation at the measured size

Mechanism: `swapram_reserve()` counts the data and stack exactly and
reserves the u area at `SR_WORST(3072)` = 3,460 bytes because the clock
interrupt rewrites accounting fields in the current process's u area between
the count and the capture (`swapram.c:56,457-505`;
`zswap.md:146-165`). Reserving at the measured size means encoding the u
area once under `splclock()` and trimming to the length found, or reserving
the counted length plus a bound on the changed bytes.

Shipped: the size-then-allocate shape is zswap's (store only what the pool
accepts, refuse otherwise); no system read for this note reserves a
worst-case slack that it later trims. Novelty bounded by what was read.

Cost: text estimate 0.2 to 0.4 KB; bss 0.

Effect on erases (hypothesis): 183 of 242 refusals have data and stack that
fit; the mean u slack is 2,444 B (3,460 - 1,016) and 180 refusals fall in
the two lowest shortfall bins (at most 2 KiB short). If every one of the 180
became an admission and each took 6 sectors from flash, erases fall by
1,080 of 1,412 (76 percent, upper bound). Each admission leaves less room
for the next, so the realized figure is lower; `flash-traffic-per-workload.md:211-221`
records the same caution.

Failure: a count mismatch panics today (`swapram.c` `count mismatch`); the
change keeps that class and must keep the encode under one tick-safe
section, or the bound on changed bytes must be proved.

### 2. Compaction on refusal

Mechanism: when the pool has `avail >= need` but no run of `need`, slide
live images toward the pool base and update `e_off` in at most `NPROC` = 16
table entries (`swapram.c:84-96`), then retry the allocation. The condition
is exactly the `fragment` refusal reason, 196 of 242 here.

Constraint found: the exec argument spool lives in the same pool and keeps
its offset across sleeps (`swapram.c:183-195`), so
compaction cannot move a spool. It slides images between pinned spools. The
codec workspace must be free (`sr_codec_owner`, `swapram.c:131-165`).

Shipped: compaction in a compressed-memory store appears in the
zsmalloc/zram family; unverified here (not fetched). Zephyr's `sys_heap`
coalesces but does not compact; TLSF is constant time and does not move
blocks (see sources).

Cost: text estimate 0.15 to 0.3 KB; bss 0. Time estimate 0.2 to 0.5 ms
for a 16 KB move (4 to 10 cycles per word at 125 MHz); unmeasured.

Effect: upper bound 196 of 242 refusals, roughly 83 percent of erases,
overlapping design 1; the two do not add. The floor under both is the 46
`nospace` refusals, about 270 erases (19 percent of 1,412).

Failure: a move interrupted by a reset loses nothing (images are never read
after a reboot, `swap-exhaustion/plan.md:23-24`).

### 3. Size-class (chunked) pool

Mechanism: fixed chunks (for example 256 B) and a per-image chunk list in
place of the first-fit byte allocator, so the pool cannot fragment
externally. Shipped as size classes in Linux zsmalloc and slab allocators;
unverified here.

Cost: estimate text 0.4 to 0.8 KB, bss about 64 B for a 16 KB pool of 256 B
chunks plus longer table entries. The decoder reads one contiguous input
(`hsx_dec_input(decoder, sr_pool + off, clen)`, `swapram.c:320`), so
chunking needs a multi-segment input or a staging copy; unverified whether
the decoder API tolerates it.

Effect: same refusal set as design 2 (fragment) with internal waste of up to
one chunk per segment, about 12 percent of a 2 KB image at 256 B. No
pin problem. Defer behind design 2; revisit if measured pinned spool bytes
leave compaction too little room.

### 4. Same-filled and zero-run elision

Mechanism: skip the encoder for a block that is one repeated value and
record it in a bitmap. Shipped in Linux zswap ("a page is checked if it is
a same-value filled page before compressing it", length recorded as zero)
and zram (`same_pages`, "no memory is allocated for such pages").

Measured here: the zero bytes are plentiful (69.1 percent of refused bytes,
72.7 percent of admitted) but scattered; at 4 KiB granularity only 4 of 782
refused data chunks were entirely zero. heatshrink already codes a long
zero run in a few back-references (`zswap.md:68-75`), so elision saves
encoder time and not pool bytes.

Cost: bitmap 1 bit per block, about 25 B per 196-block image, up to 400 B
for 16 images; text estimate 0.1 to 0.2 KB.

Effect on erases: at most 0.5 percent on the flash path (the extent is
contiguous and rounded to a 4 KiB sector, so a zero 1 KiB block saves no
erase). On PSRAM or SD the saving is bytes written; the zero fraction at
512 B and 1 KiB granularity is unmeasured because `SR_PROBE_CHUNK` is 4096
(`swapram.c:356`). Changing that constant in a `STORAGE_STATS`
kernel measures it.

### 5. Compressed writeback

Mechanism: write the heatshrink stream of a refused image to the backing
device instead of the expanded segments. Records need a portable layout
(`docs/research/rp2040-memory-wear-engineering-program.md:416-432`, the H05
record) and an eviction order of reserve, write, publish, release.

Shipped: Linux zram. Written-back pages are stored "in decompressed (raw)
form" by default, and the `compressed_writeback` attribute (set before the
device is initialized) "causes zram to write compressed pages to the backing
device" (kernel zram documentation). compcache forwarded writes to a backing
swap when allocation failed (LWN 2009 summary). This port's
`zswap.md:324-326` already lists compressed
flash records as unimplemented.

Cost: estimate text 0.5 to 1.5 KB (record format and restore validation),
no extra bss if the pool stages the stream.

Effect on erases (hypothesis): the mean admitted image compresses to
4,326 B (3,310 + 1,016), so 2 sectors in place of the 5.8 sector mean of a
refused flash swap-out: 66 percent fewer erases on those swap-outs, 83
percent at one sector. The stack and u ratios in `zswap.md:87-108` are
synthetic; the 4,326 B figure is measured over images the pool took, which
are the smaller ones. Refused images are larger.

Failure: a record failing validation kills its owner (design 8); the
contract of `swap-exhaustion` restoration says decode failure must not
return through a partly restored image.

Weak on PSRAM: an encode costs an estimated 40 to 90 ms for a 12.4 KB image
(`zswap.md:129`) against about 2.7 ms to move 33 KB through 64 B PSRAM frames
(5.2 us a frame, `docs/research/picocalc-psram.md:177-186`; estimate, the
board is not run).

### 6. LRU writeback from the pool to the backing device

Mechanism: when the pool is short, evict the least recently used images to
the backing device and admit the new one, in the swapper, with admission
closed while it runs, exactly as `swapram_evacuate()` does for all images
(`swapram.c:693-784`, `swapram_service()` at 790). A per-image age stamp
costs 4 B per table entry.

Shipped: Linux zswap evicts "on an LRU basis to the backing swap device"
when the pool reaches its size limit, with an optional shrinker that writes
cold pages proactively; zram writes idle pages to a backing device
(`idle`, `huge`, `huge_idle`, `incompressible` modes) and carries a
`writeback_limit` budget because of flash wear. Both are documented kernel
behavior (see sources).

Why it fits the measurement: the refused swap-outs are the hot class (8 a
compile, the `cc` driver's shell asleep while a pass runs) because cold
images, the login shell, `init` and `update`
(20, 12 and 8 blocks in the placement model,
`docs/research/swap-exhaustion/placement-study.md:23-25`), occupy the pool. The
current policy refuses the newest image. LRU eviction writes each cold
image once and keeps the hot one in RAM.

Cost: estimate text 0.6 to 1.0 KB (single-image evacuation, age stamp,
low-water test in `swapram_service()`), bss 64 B. Eviction must stay in the
swapper: the tier's file-scope codec and "nothing on this path sleeps"
invariants (`zswap.md:174-179`, `swapram.c:15-25`) rule out eviction from a
forking process's `swapout()`.

Effect (hypothesis): steady-state erases fall to the cold set written once
per boot, about 10 sectors (40 blocks) for three sleepers, if the hot set
fits beside the pinned spools. The hot set is about 2 to 3 images of
roughly 4.3 KB, close to the 16 KB pool, so the realized figure lies between
this floor and the design 1 plus 2 bound (about 270 erases per 24
compiles). The decisive number is the compressed hot working set: the
`admitted_data_bytes` and `admitted_u_bytes` counters over a run with the
cold images evicted.

### 7. Boot-time device priority

Mechanism: probes run before `init_main()` opens `swapdev`
(`init_main.c:145`). `psramprobe()` already sets `swapdev` on success
(`psram.c:124`). Add an SD probe after `sdprobe()` that, only while
`swapdev` still names `fl1`, initializes the card, reads the MBR and takes
the first entry of type 0xb8 inside the card, setting `swapdev` to that
minor. `fsutil` already writes type 0xb8 for `swap`
(`tools/fsutil/fsutil.c:737-738`) and nothing in the kernel reads it yet.
The crash-dump partition is 0xb9 and the filesystem 0xb7 (`dump.h:51`).

Shipped: swap priority exists in NetBSD (`swap_priority` list,
`netbsd-src/sys/uvm/uvm_swap.c:83-90`, allocation down the list with round
robin inside a priority, `:1605-1655`) and OpenBSD (the same design,
`openbsd-src/sys/uvm/uvm_swap.c:82-89`), in Linux (`SWAP_FLAG_PREFER`,
higher priority exhausted first, `swapon(2)`), and not in FreeBSD
(round-robin `swdevhd`, `freebsd-src/sys/vm/swap_pager.c:927-971`) or
4.4BSD (interleaved `swdevt` with a `dmmax` stripe, no priority,
`4.4BSD-Lite2/sys/vm/vm_swap.c:55-134`). This
port's boot-time single-device choice is the smallest member of that family.

Cost: estimate text 0.3 to 0.5 KB. Card initialization is bounded: an
absent card ends `CMD0` after 5 tries, a slow card at most `INIT_US` =
1 s (`sys/arch/rp2040/dev/sdspi.c:47-57,239-280`), added to boot; unmeasured.

Hazards found by reading:
- `COMPACT_SWAPMAP` stores 16-bit addresses and sizes (`sys/sys/map.h:41-48`),
  so a partition above 65,535 blocks (64 MiB) wraps. `machdep.c:158-167`
  asserts the bound for flash and PSRAM only. The SD path must clamp `nswap`.
- `init_main.c:146-153` panics "swap size" when no swap exists. With the flash
  swap shrunk to zero and no PSRAM or card, the kernel does not boot
  (`PICOCALC.md:896-901` names the same panic). The last link of the chain
  needs a "no swap" mode.
- The flash swap shrink is already a loader-stamp mechanism
  (`PICOCALC.md:880-886`, 368 KB to none).

### 8. Swap error kills the owner

Described under "The minimal kill the owner change". Cost estimate: text
0.3 to 0.5 KB; no bss. No wear effect. On PSRAM and bare flash it converts
a panic into a kill for a failed frame or program; on SD it also handles a
pulled card.

Failure behavior after the change: the device-failed flag stops new swap-outs
to the dead device, so the system runs with only the pool and the resident
process until reboot, or until the owner chooses to re-select `fl1` once no
image remains on the dead device (open question).

Shipped: FreeBSD and NetBSD do not panic on a swap read error. FreeBSD
prints "swap_pager: I/O error - pagein failed", the fault returns
`KERN_OUT_OF_BOUNDS` and the process takes SIGBUS (`swap_pager.c:1707-1722`,
`vm_fault.c:1468-1480,1777-1779,775-778`); on a failed pageout it drops the swap
assignment, never reallocates that block and redirties the page
(`swap_pager.c:1739-1747`). NetBSD marks the slot bad
(`uvm_swap_markbad()`, `uvm_fault.c:430-436`, `uvm_swap.c:1718-1744`) and
the faulting process is signalled through the trap path
(`trap.c:575-610`: EINVAL SIGBUS, ENOMEM SIGKILL, default SIGSEGV). Linux
`do_swap_page()` returns `VM_FAULT_SIGBUS` for a folio that is not
up to date after the read. None of them has the single-address-space u-area
problem above, because their kernel stacks are not swapped with the process.

Removable-media precedent, searched in the local corpus with `rg -n
"orphan|SW_CLOSING|ENXIO|swapoff|g_wither|provider"` over
`freebsd-src/sys/vm/swap_pager.c` and `rg -n "detach|vdevgone|swap_off|gone"`
over the NetBSD and OpenBSD `uvm_swap.c`. FreeBSD has the case: when a
GEOM provider disappears, `swapgeom_orphan()` sets `SW_CLOSING` on the
device (`swap_pager.c:3235-3257`), and `swp_pager_getswapspace()` skips a
closing device (`swap_pager.c:946`), so new swap-outs stop while images
already on it fail their pagein with SIGBUS. That is the device-failed flag
of the change above, in another kernel. NetBSD and OpenBSD have no detach
hook for a swap device: `swap_off()` runs only from `swapctl(2)` (negative,
terms above; a hook elsewhere in the tree was not searched). 2.11BSD has an
in-lineage kill for a different failure: `swkill()` posts SIGKILL and sets
`SULOCK` when swap space runs out (`211bsd/sys/sys/vm_swp.c:71-90`), which
this tree's `swapin()` text failure already echoes.

### 9. SD multi-block writes and erase-block alignment

Mechanism: `sd.c` loops one 512-byte sector per command (`sd.c:376-382`) and
`sdspi_write_span()` follows each CMD24 with a busy wait up to 500 ms and a
CMD13 status read (`sdspi.c:355-388`). CMD25 with ACMD23 pre-erase sends n
blocks in one command; both are valid in SPI mode (SD Physical Layer
Simplified Specification v3.01, Table 7-3 and the application command table;
`docs/research/log-structured-store-scope.md:196-199` records the same for
v6.00). The specification says a preceding ACMD23 makes the following
multiple write "faster" and that "some of the cards will be faster"
(v3.01 section 4.3.4): not guaranteed.

Alignment: the allocation unit is 16 KB to 64 MB in the SD Status register
(`log-structured-store-scope.md:195-196`, second-hand); a partition that
starts on an AU multiple costs nothing and lets image extents inherit
alignment from the partition. `fsutil` can place the swap partition on a
4 MiB multiple (8,192 sectors); whether it does is not checked here.

Effect on NOR erases: none (the card is the target). On time: swap writes
are 207,769 B per compile, 406 sectors. The 2012 smartphone study found most
random writes on eight cards at or below 0.02 MB/s
(`log-structured-store-scope.md:209-210`, second-hand), which would put
random-write SD swap at about 10 s of write time per compile against a
10 to 25 s compile. SPI mode must treat the card as class 0, so the speed
class gives no sequential guarantee (`log-structured-store-scope.md:191-194`,
from v6.00). Image writes are sequential, so the figure is a pessimistic
bound, not a prediction; only a card measurement decides.

Cost: estimate text 0.5 to 0.8 KB (CMD25 framing, stop token, ACMD23);
bss 0.

### 10. TRIM and erase on an SPI-mode SD card (verified)

The question "no TRIM on SPI-mode SD" has two answers by command:
- CMD32, CMD33 and CMD38 (erase) are valid in SPI mode (v3.01 command table,
  "SPI Mode: Yes" for CMD32/33/38, ACMD22/23 yes; read from the fetched PDF,
  hash in Sources).
- Discard (CMD38 argument 1) and FULE (argument 2) are not supported through
  the SPI interface in the v6.00 text, per the earlier note
  `docs/research/log-structured-store-scope.md:179-188` (second-hand; v6.00
  not re-read here). Discard first appears in the SD 5.1 specification; the
  Linux patch series adding it requires the `DISCARD_SUPPORT` bit of the SD
  Status and falls back to ERASE (LKML 2019 search summary; the patch body
  was not retrievable).

So only plain erase exists, which wants AU-aligned ranges and adds up to
250 ms per partially erased AU to the timeout (`log-structured-store-scope.md:274`,
second-hand, v6.00 sections 4.14.1 and 4.14.4). Freed swap extents are never
read again, so no data hazard exists, but no benefit is demonstrated and the
cost per free is large. Verdict: do not build.

### 11. Keep the u area in RAM (added; not in the brief list)

Mechanism: send only data and stack to PSRAM or SD; keep each swapped
image's u area in the SwapRAM pool, where it compresses to 358 B (sh) to
1,016 B (measured mean). A dead removable device then loses data and stack
only; `swapin()` zero-fills and SIGKILLs and `exit()` runs with a valid u
area. It retires the u-area hole in the minimal change.

Cost: pool bytes, about 1 KB per swapped process (5 KB for five); estimate
text 0.2 to 0.4 KB. The u area also costs no SD write (3 KiB of every
image). Unverified: the in-place exchange assumes one contiguous
three-segment extent (`vm_swap.c:419-448`) and would need a two-segment form.

## Hardware case by design

Entries say build, defer, no or n.a., with the number that decides.

| Design | P bare Pico | M PSRAM | S SD | MS both |
| --- | --- | --- | --- | --- |
| 1 Reservation at measured size | Build first: 183 of 242 refusals fit; at most 76 percent of erases; confirm with one 24-compile `STORAGE_STATS` run | n.a. (no NOR swap wear); pool RAM and encode time only | Build with 6 (less SD traffic); same 183 of 242 | as M |
| 2 Compaction | Build after 1 if `fragment` stays above half of refusals (196 of 242 now) and pinned spool bytes are small | Defer | Defer | Defer |
| 3 Size-class pool | No; revisit if pinned spools block compaction | No | No | No |
| 4 Zero elision | No: 4 of 782 chunks zero (0.5 percent) | Defer | Measure at 512 B first (probe constant); build if zero sectors exceed the per-sector SD cost | Defer |
| 5 Compressed writeback | Build only if 6 leaves erases above floor: 2 sectors against 5.8 | No: encode 40-90 ms (estimate) against about 1 ms PSRAM for the same image | No until SD write latency is measured | No |
| 6 LRU writeback | Build, highest value: converts the cold set to one write per boot; decide by compressed hot set against the 16 KB pool | No | Build: cold images to the card, hot in pool | No |
| 7 Boot priority | n.a. | Shipped (`psram.c:124`) | Build; clamp `nswap` to 65,535 blocks; add no-swap mode | Build; PSRAM wins, SD swap partition ignored |
| 8 Error kills owner | Build (small; panic today on a failed program) | Build | Build, required (pull while mounted) | Build |
| 9 SD CMD25 and alignment | n.a. | n.a. | Alignment: build (free). CMD25: defer to a card measurement; 406 sectors a compile | Alignment only |
| 10 SD erase or discard | n.a. | n.a. | No: Discard absent in SPI; erase costs 250 ms per partial AU | No |
| 11 u area in RAM | n.a. | Not needed (module not hot-pulled) | Build with 8: the only design that retires the u-area hole | n.a. |

## Findings that change the plan

1. The pool refusal set is a shape problem, and one number bounds the
   gain. 183 of 242 refusals have data and stack that fit; 196 of 242 are
   fragmentation; the u reservation is 2.4 KiB larger than the u area ever
   takes. Designs 1, 2 and 6 attack erases at 76 to over 90 percent of
   upper bound without touching flash format. Designs 3 and 5 are not needed
   first.
2. A pulled card cannot be survived by "kill the owner" alone: the u area
   is on the dead device and `exit()` needs it. Keeping u in the pool
   (design 11) is the minimal complete fix; otherwise a kill leaks file and
   inode references.
3. On PSRAM boards SwapRAM likely costs more than it saves: a 16 KB pool, an
   estimated 4.1 KB of text, and an encode of 40 to 90 ms for a 12.4 KB image
   (`zswap.md:129`) against about 3 ms to move a 33 KB image through PSRAM
   (516 frames of about 5 us each way, estimate), which is about 1 ms for
   12.4 KB. `swapram_admit(0)` (`swapram.c:657`)
   is a runtime off-switch, so the check is one line plus a timed compile.
   This conflicts with the decision to keep the pool in front and is the
   owner's call.

## Unverified items

- Every text and time estimate above; no design is built or timed.
- All PSRAM transfer timing: simulator only; board "not run" (`PICOCALC.md:86,90`).
- SD write latency, AU size, open-AU count and wear of the target card;
  the bench board has no SD socket (`PICOCALC.md:84`).
- SD specification v6.00 claims (Discard and FULE absent in SPI, 250 ms per
  AU, Class 0 in SPI): second-hand from `log-structured-store-scope.md`;
  v6.00 and v9.10 copies were not retrievable. Only v3.01 was read.
- zsmalloc size classes and zram compaction: not fetched. FreeRTOS heap_4 and
  heap_5 coalescing and NuttX allocators: page fetch truncated or absent;
  no claim here rests on them. compcache's writeback policy: LWN summaries
  only; the policy to move unused pages to physical swap was a proposal.
- Whether a data or stack zero-fill followed by SIGKILL always reaches
  `exit()` before the process executes a user instruction: derived from
  the text-failure precedent (`vm_swap.c:365`), not run.
- Fork and exec write paths under a failed device: `kern_fork.c:186` calls
  `swapout()` in the forking process; the unwind of a failed child image was
  not traced.
- ESP-PSRAM64H limits are taken from `docs/research/picocalc-psram.md:56-65`
  (Table 10-5: tCEM 8 us, tCPH 50 ns); the Espressif PDF was not re-opened.

## Open questions for the owner

1. Pool in front on PSRAM boards (finding 3): measure, then keep or drop.
2. After a card pull, re-select `fl1` once no image remains on the dead
   device, or run poolless until reboot?
3. A "no swap" mode for the end of the chain (PSRAM absent, card absent,
   flash swap shrunk to zero): permit, or keep a floor on `fl1`?
4. Same card reinserted: treat as lost (decided) or compare the CID
   at insertion and keep images? The driver reads no CID (`sdspi.c` command
   list) so this needs new code.

## Sources

Local files cited by line are in the tables below with their sha256. Local BSD source trees cited by tree-relative
path (the local corpus of 2.11BSD, 4.4BSD-Lite2, NetBSD, OpenBSD and FreeBSD).

External, fetched 2026-11-15:
- Linux zswap: `docs.kernel.org/admin-guide/mm/zswap.html` (same-value pages,
  LRU eviction to the backing device, shrinker, `max_pool_percent`,
  `accept_threshold_percent`).
- Linux zram: `docs.kernel.org/admin-guide/blockdev/zram.html` (`mem_limit`,
  `backing_dev` writeback modes, `writeback_limit`, `same_pages`, multi-
  algorithm recompression, flash wear warning).
- `swapon(2)`, man-pages: `man7.org/linux/man-pages/man2/swapon.2.html`
  (`SWAP_FLAG_PREFER`, equal-priority round robin, `SWAP_FLAG_DISCARD`; no
  error behavior stated).
- Linux `mm/memory.c` `do_swap_page()`: `VM_FAULT_SIGBUS` when the folio is not
  up to date after the read (raw.githubusercontent.com, master, read through
  the second 100,000 characters).
- Zephyr heap: `docs.zephyrproject.org/latest/kernel/memory_management/heap.html`
  (8-byte chunks, power-of-two buckets, coalescing, no compaction, constant
  time, `sys_multi_heap`).
- RTOS allocators in the local project corpus: `rg -il tlsf` over the
  projects directory to depth 4 in `*.c`, `*.h` and `*.md`, and `find` for
  directories named zephyr, nuttx, freertos or linux-* to depth 4, found
  none. The RTOS statements here rest on the fetched Zephyr page and the TLSF
  abstracts alone; FreeRTOS heap_4 and heap_5 and NuttX are unverified.
- TLSF: Masmano, Ripoll, Crespo and Real, ECRTS 2004, pp. 79-86, and
  Software: Practice and Experience 2008 (abstract-level search results:
  Theta(1) cost, immediate coalescing, bitmap search; full papers not read).
- SD Physical Layer Simplified Specification v3.01 (Part 1), fetched from
  `cs.utexas.edu/~simon/378/resources/Part_1_Physical_Layer_Simplified_Specification_Ver_3.01_Final_100518.pdf`,
  sha256 `91c67d3f7807548a786a12bec610e906ae3b2f6ca3cc953b1a29008ae2415cc6`
  (a university mirror; the copy was read from session scratch and is not in
  the tree):
  SPI command table (CMD25, CMD32, CMD33, CMD38, ACMD22, ACMD23 valid in SPI),
  section 4.3.4 pre-erase, section 4.6.2.2 write busy 250 ms for SDHC.
- Linux `mmc: core: Add discard support to sd` (2019, Altman): CMD38
  argument 1, `DISCARD_SUPPORT` bit; read through search summaries only.
- ESP-PSRAM64H datasheet: the Espressif PDF redirects to a documentation
  platform page that returned no text, so the limits are second-hand from
  `docs/research/picocalc-psram.md:56-65` (Table 10-5: tCEM 8 us, tCPH 50 ns,
  clock period 7 ns minimum). For swap, tCEM caps one CE#-low frame at
  8 us, so the driver cuts every transfer into 64-byte frames of about
  5 us (`picocalc-psram.md:177-186`, `psram_xfer.c:251-258`); a 1 KiB swap
  block is therefore 16 frames, each with interrupts masked, and a 33 KB
  image is about 516 frames each way.

Local BSD sources (tree-relative paths and sha256 prefixes):

| Path | sha256 |
| --- | --- |
| `211bsd/sys/sys/vm_swp.c` | `016e1f6e0f605cb66d593607b865bf7bf2aca25b35f42cd541b6871675d1fdda` |
| `4.4BSD-Lite2/sys/vm/vm_swap.c` | `dfff5ff70a28f28f7f1f804090d6e18cd45d81500fca573f52dd7ac00df7c36d` |
| `netbsd-src/sys/uvm/uvm_swap.c` | `2dabc1f84e91e27d0d014335063e817069ea722e7d44d008660bdc54225efc99` |
| `netbsd-src/sys/uvm/uvm_fault.c` | `712ff2464d1a749b163a5563b772b88cf9d7b3f993807437d7a81512a0381c22` |
| `netbsd-src/sys/uvm/uvm_pager.c` | `2ff9e1c142c45962b60c7d9997bfb72b5287839bed03a06d0cb9013bf94fc36b` |
| `netbsd-src/sys/arch/amd64/amd64/trap.c` | `22d7a7ff40345084ffb71b6f352233370b32ca542c503a0491c3534000c79c83` |
| `openbsd-src/sys/uvm/uvm_swap.c` | `7636873a0fdf5f4a635944c37184cc4354dc43a6f2d70ee562fc3f78c1ee1ec7` |
| `openbsd-src/sys/uvm/uvm_fault.c` | `4aad2a4a82d64deff3fbfbf5b113fa6640520f6bdd57e4a4c7a3a05abe8b538f` |
| `freebsd-src/sys/vm/swap_pager.c` | `178141f5f50b721ed5393b4c77c2afd977c6980f1d6748f7ac2aa0194560f94b` |
| `freebsd-src/sys/vm/vm_fault.c` | `eacbf3e32a28ae1ac612cf2623b8578013707b65979667230fc9263c06cefe2b` |

Local tree files (this repository):

| Path | sha256 |
| --- | --- |
| `sys/arch/rp2040/doc/research/zswap.md` | `4382aa041037e3b8efa926938e6981e55b1b571143dde8d3a0e3b8d889a5cc15` |
| `sys/arch/rp2040/doc/research/flash-traffic-per-workload.md` | `24d4cc2882674db07a614fdcca960510c27d38413a540f86c6846d12ecb3bda3` |
| `sys/arch/rp2040/doc/STORAGE.md` | `a255b9d7366ef6c8c6eef6ace3b716c4648b4f80077c4095aec667b1279d9a71` |
| `sys/arch/rp2040/doc/PICOCALC.md` | `9f3f0f7ed4cda7f71c0faeee071e9baf70dac35d9f65c142fd67c5ec3c639c00` |
| `sys/arch/rp2040/dev/psram.c` | `09a72c1ef9c33abe37fc20d8a0d930f9317256319b61f1fd9aabacb8d2a03bc2` |
| `sys/arch/rp2040/dev/sd.c` | `9f733b8f781bad50b9d313b486281f998772294d431f2dc87be343478b3183c3` |
| `sys/arch/rp2040/dev/sdspi.c` | `444941de53e05eab97ea5736a8dd5a93ed4d336bc45c4af534499cf25a1bc5b5` |
| `sys/arch/rp2040/dev/flash.c` | `837b1dedd99793a916ce16cbc275c8410bda211bdab5a91ba9a6c573467e8c95` |
| `sys/arch/rp2040/dev/psram_xfer.c` | `427b385914f9040d865e8309f08dbd04bef27f20132ba2753a34adc1632ba67a` |
| `sys/dev/swap.c` | `0bd26536d71c53dc78c76deb0b55aaa907bb8bb6ac49e05be1ca7542dce1b6b3` |
| `sys/kern/vm_swap.c` | `6ca0b4aa84364c908ef6f8047149c452e5169ee38cf3cada5a0dc88bd7c877c4` |
| `sys/kern/vm_swp.c` | `76da84dccf9336bfbc7754b1b23eb3679a805d1d54a7e547cb17718407353e87` |
| `sys/kern/vm_sched.c` | `1e7d799c7e7373dd1845be2067ef81a86a68bb841e2c0ce9d8127935e9c53f53` |
| `sys/kern/kern_fork.c` | `d95e61f9de3d0726bb43b2224e735089c9a43a95c83d76dc524993d5459a968e` |
| `sys/kern/kern_sysctl.c` | `27a12149a8a458049f9284e4256ee2a693b74e009ee8c711e66cc02031754fee` |
| `sys/kern/exec_subr.c` | `761b64a0f154f1e9a7a531df6836b91680d20086390bd92f081a5250694e28cc` |
| `sys/kern/init_main.c` | `85ecd43ea6d8b97821f9582ac38b37a0cf1de299d074eaeaaf2ebf615a8e19dd` |
| `sys/arch/rp2040/rp2040/machdep.c` | `765bdc0bc259d6799d3d93b64b3bff0ddf89192f2455dc2a89a9c8169d0987e4` |
| `sys/arch/rp2040/rp2040/swapram.c` | `fedafc5eb416bb13954d68327a14103a7e88690262a526d58f6b74c6e71701b2` |
| `sys/arch/rp2040/include/machparam.h` | `ea1dd5dff87c14bc194472a3c43532b8b362b08dfeee390a351a1807b889ddad` |
| `sys/sys/map.h` | `d4de09dff62fbd99374d16c1ef3b687b2372c968de875b2854464e6f28c5b1a1` |
| `sys/arch/rp2040/include/dump.h` | `cb9ee725f536444ad6761adea870ac7800c2e6b2fe698985bf4923ba562a2e97` |
| `sys/arch/rp2040/compile/PICOCALC_PSRAM/Config` | `c8c9b7ef874ab72726ee0091772bff84b0db2c5fac2e620483846640d8e349ee` |
| `docs/research/swap-exhaustion/plan.md` | `027e189ff2fcb6495d66406ecc63bd3bef079bd9514af4fc7f55156aa542a04f` |
| `docs/research/swap-exhaustion/placement-study.md` | `928146ed4e730e9520656535fdab3ff0a4443853722dd7ae823736e15a150e98` |
| `docs/research/trim-survey/board/swap/admission/main-sh.txt` | `0527eed65e8a81bc863e116796cd5875a220ffb106870e4468e045a710dc2791` |
| `docs/research/trim-survey/board/swap/admission/fixed-sh.txt` | `533c80080bd65af3188c53cf92c22383ef7ddf35e0f83214f260aaef56ebf959` |
| `docs/research/renode-sd-card-model-probe.md` | `23bc542f71784023df14df2bbf0a24c240ca92081245ebfe794323497f344412` |
| `docs/research/picocalc-psram.md` | `4146ce33e41fddac44d664767abd4445565d100b34558907bb014c181d659c2f` |
| `docs/research/log-structured-store-scope.md` | `f464c23e42f39ddbe2a8f3e59d6e8c5a614e55bac4629542bf63d7ac3634164b` |
| `docs/research/rp2040-memory-wear-engineering-program.md` | `03dbb2af7eb0e07cce3b3d3dbcead230b5b3213d4ae8ea1fc743c1b7c481be0f` |
| `docs/research/rp2040-single-kernel.md` | `90f26a153a5707815ad292fcdcb023350852979885d0a444f0626e0d4ccaca4b` |
| `tools/fsutil/fsutil.c` | `52cf32d84783ceacf1d44b1309caa294d19d8f5a43c94a86240143ef0c89df6c` |
