# Review 03: RP2040 resources and an in-place LARGE exchange

## Decision

The LARGE parent and child can exchange one equal-layout image through a
dedicated 4,096-byte kernel-SRAM bounce. The exchange retains the incoming
process's existing erase-aligned extent, overwrites that extent with the
outgoing process one sector at a time, and moves each saved incoming sector
into the resident window only after the outgoing sector passes readback. The
recorded workload then holds 60 small-process blocks plus one 156-block LARGE
image, or 216 of the 380 allocatable blocks. It does not need the 372 blocks
required by the present write-before-read scheduler path.

The supplied linked kernel has 76,304 bytes with no allocated ELF section
between `_ebss` and the process-0 user area. The process stack and its guard do
not occupy that interval. A 4,096-byte bounce and at most 64 bytes of
transaction state leave 72,144 bytes of the measured interval. The production
change must make the bounce an explicit linked object and must reject a linked
image whose sections, bounce, or transaction state reach `u0`.

Four findings constrain the implementation:

1. SwapRAM being closed in the LARGE epoch does not make its 16,384 physical
   bytes available. A LARGE process's resident window owns those same bytes.
2. The literal claim that image sector `k` corresponds to one contiguous RAM
   chunk is false at sectors 37 and 38 of the recorded image. Sector 37 joins
   data, data padding, stack, and stack padding. Sector 38 joins the user area
   and allocation-tail padding. The exchange needs gather and scatter maps.
3. `flash_erase()` and `flash_program()` return zero unconditionally after
   calling Boot ROM functions whose interfaces return `void`
   (`sys/arch/rp2040/dev/flash.c:294-349`). The exchange needs explicit erased
   and programmed readback before it can treat a sector as complete.
4. `fl_raw()` holds `splbio()` around the complete raw write
   (`sys/arch/rp2040/dev/flash.c:643-685`), and every blocking `spl` level is a
   global `PRIMASK` disable (`sys/arch/rp2040/include/intr.h:181-195`). Copying
   that outer critical section would suppress USB service for the whole
   exchange. The exchange must mask interrupts only inside each existing ROM
   erase or program call.

The design remains conditional on a production host gate, a linked-image gate,
and an explicitly authorized board gate. This review ran no board operation.

## Scope and evidence

The current research checkout is branch `docs/swap-exhaustion-research` at
`3c8908ad8427ecaae51c07897ffce759a748a61c`. The deciding prior board capture
is the `review-02.md:759-797` addendum. The capture records the child at blocks
88 through 243, the parent resident, four smaller processes holding 60 blocks,
and free runs whose largest member is 108 blocks. The capture is retained board
evidence; this review did not repeat it.

The supplied LARGE-bonus PICO artifact is
`../../mpu/large-bonus-region/sys/arch/rp2040/compile/PICO/unix.elf`. Read-only
identity checks produced:

```text
$ sha256sum ../../mpu/large-bonus-region/sys/arch/rp2040/compile/PICO/unix.elf
e1013ca65560d4cf6f40a5be5f98a9e1fb83b038960d4a0e5d9a54c9676fcb59
$ git -C ../../mpu/large-bonus-region rev-parse HEAD
77676485aaf7db07ffeafb40d5fe233e6ce3d835
```

Paired `sha256sum` checks found identical current and artifact-checkout bytes
for both RP2040 linker scripts and the 14 relevant source files: `flash.c`,
`flash.h`, `usb.c`, `usb.h`, `intr.h`, `machparam.h`, `swapram.h`,
`uarea_stats.h`, `locore.S`, `machdep.c`, `swapram.c`, `init_main.c`,
`vm_sched.c`, and `vm_swap.c`. The check establishes identity only for those
16 inputs.

`arm-none-eabi-readelf -SW` and `arm-none-eabi-nm -n -S` measured:

| Linked object | Interval | Bytes |
|---|---:|---:|
| `.swapram_pool` | `0x20024000..0x20028000` | 16,384 |
| `.data` | `0x20028000..0x20028100` | 256 |
| `.ramfunc` | `0x20028100..0x20028230` | 304 |
| `.bss` | `0x20028240..0x2002bdf0` | 15,280 |
| Section-unoccupied kernel RAM | `0x2002bdf0..0x2003e800` | 76,304 |
| process-0 user area `u0` | `0x2003e800..0x2003f400` | 3,072 |
| current user area `u` | `0x2003f400..0x20040000` | 3,072 |
| `.scratch` / `usb_tx_ring` | `0x20040000..0x20042000` | 8,192 |

`arm-none-eabi-nm -S --size-sort` also measured `boot2_copy` as 256 bytes,
`sr_tab` as 600 bytes, `cfree` as 1,024 bytes, `bufdata` as 4,096 bytes,
`usb_tx_ring` as 8,192 bytes, and `swapram_pool_mem` as 16,384 bytes.
`pahole -C user` reported:

```text
/* size: 972, cachelines: 16, members: 44 */
/* sum members: 970, holes: 1, sum holes: 2 */
```

`RP2040.ld:51-62` assigns the section-unoccupied interval to kernel `RAM` and
places both user areas above it. `kern.ldscript:18-21` places `_estack` at
`0x20040000`, the top of `u`, and `kern.ldscript:175-195` asserts that the RAM,
user-area, and scratch regions meet without gaps. The measured interval is
therefore ordinary kernel-RAM link headroom. It is neither the active stack,
nor a guard, nor a user-visible hole. The measurement does not protect against
a future section growing into the interval; the required link gate supplies
that protection.

The calibrated headroom command was:

```sh
PYTHON="$(command -v python3)"
export PYTHON PYTHONDONTWRITEBYTECODE=1
"$PYTHON" tools/check_kernel_headroom.py \
  --linker-script sys/arch/rp2040/conf/RP2040.ld \
  ../../mpu/large-bonus-region/sys/arch/rp2040/compile/PICO/unix.elf
```

It reported:

```text
PASS kernel-headroom .../unix.elf: flash image 96124 of 130816 bytes, headroom 34692 bytes, margin 1024
```

The executable-model command was:

```sh
PYTHON="$(command -v python3)"
export PYTHON PYTHONDONTWRITEBYTECODE=1
"$PYTHON" docs/research/swap-exhaustion/scratch-03/model_exchange.py
```

It reported:

```text
geometry: data_after_text=151704 at 0 stack=2244 at 152576 uarea=3072 at 155648
image: blocks=156 bytes=159744 sectors=39 program_pages=614
boundary sectors: 37_pages=10 38_pages=12
success: erases=39 programs=614 xip_read_bytes=479232
failure matrix: erase=78 program=1228 modes=before,after metadata_commits=0 adjacent_damage=0
mutants rejected: scatter-before-program,program-without-erase,commit-each-sector,release-before-commit,unrounded-segment-boundaries,program-all-padding-pages
```

The model is a finite design check, not a test of production code, Boot ROM,
the flash chip, USB timing, or the board.

## Resource inventory

The RP2040 datasheet index places the bus fabric in section 2.1.1, page 15;
SIO in 2.3.1, page 27; DMA in 2.5, page 91; memory, SRAM, and flash in 2.6,
2.6.2, and 2.6.3, pages 120 through 122; core-1 launch and Boot ROM contents in
2.8.2 and 2.8.3, pages 131 and 132; PIO in 3.2, page 312; and USB architecture
in 4.1.2, page 384 (`sys/arch/rp2040/doc/DATASHEET-INDEX.md:16-17,25-27,39-55,144-150,193-198`).
The PICO configuration runs `clk_sys` and `clk_peri` at 125 MHz
(`sys/arch/rp2040/compile/PICO/Config:18-20`). Access-rate descriptions below
name an architectural clock or operation bound only where the repository
preserves one.

| Resource | Address, size, access | PICO owner and idle period | Borrowing cost and breakage | Verdict |
|---|---|---|---|---|
| Striped SRAM0-SRAM3 | `0x20000000..0x20040000`, 256 KiB. Byte, halfword, and word CPU access through four word-striped 64-KiB banks at `clk_sys`; simultaneous masters can contend. Datasheet 2.1.1 and 2.6.2. | `USERRAM` consumes 144 KiB, kernel `RAM` consumes 106 KiB, and `u0` plus `u` consume 6 KiB (`RP2040.ld:37-62`). Bank identity rotates by word, so no contiguous virtual range belongs to only SRAM0, SRAM1, SRAM2, or SRAM3. | Borrowing a linked object requires explicit placement and overlap gates. Borrowing live user-window, kernel-data, or user-area bytes corrupts their owner. | The measured `0x2002bdf0..0x2003e800` link headroom is the selected bounce home. Other occupied ranges are unavailable. |
| SwapRAM pool inside striped SRAM | `0x20024000..0x20028000`, 16 KiB, ordinary SRAM. | `swapram_pool_mem` occupies the complete interval (`swapram.c:58-71`). In SMALL it holds compressed images. In LARGE the pool is empty and closed, but `user_top()` extends a `P_LARGE` process across the same bytes (`swapram.h:87-104`; `swapram.c:721-745`). | A bounce there overwrites the resident LARGE process whose exchange needs the bounce. | Neither a bounce home nor free kernel RAM during LARGE. "Closed" is an ownership transition, not physical idleness. |
| Section-unoccupied kernel RAM | `0x2002bdf0..0x2003e800`, 76,304 bytes, ordinary striped SRAM. | The supplied ELF has no allocated section in the interval. `_estack` is elsewhere, and the linker assigns the interval to kernel `RAM`. The interval stays idle until a linked kernel object claims it. | A future `.data`, `.ramfunc`, or `.bss` growth can consume it. A link assertion and symbol-range verifier must fail before overlap. | Best bounce home. Allocate one aligned 4-KiB object plus at most 64 bytes of state; 72,144 measured bytes remain. |
| SRAM4 and SRAM5 | `0x20040000..0x20041000` and `0x20041000..0x20042000`, 4 KiB each, directly mapped SRAM banks at `clk_sys`. Datasheet 2.6.2. | `usb_tx_ring` occupies all 8 KiB in the PICO ELF. Source requires each byte to be written before reading (`usb.c:329-341`), and `kern.ldscript:192-195` asserts containment. Ring occupancy varies, but its address capacity retains boot and console output. | Reusing live ring bytes loses or corrupts console data. Reducing capacity changes boot-message retention and congestion behavior. The Boot ROM uses the top bank during reset, before the kernel takes ownership (`RP2040.ld:64-78`). | Existing ring prevents a bounce. A separately validated 4-KiB ring reduction is a possible fallback, unnecessary for this exchange. |
| XIP cache SRAM | `0x15000000..0x15004000`, 16 KiB. It is SRAM-addressable only while the XIP cache is disabled; datasheet 2.6.2.1 and 2.6.3.1. | The kernel executes from external flash, so the cache is active during ordinary execution. `rp2040-second-core-hardware.md:53-61` records the cache-mode and shared-XIP constraints. | Disabling the cache removes caching from all XIP instruction and data fetches. Code must switch modes, restore and invalidate the cache, and avoid unsafe flash fetches during transitions. | Technically storage, but a high-risk bounce or relocation target. Reject while ordinary SRAM headroom exists. |
| USB DPRAM | `0x50100000..0x50101000`, 4 KiB. CPU byte, halfword, and word accesses; USB controller access at the 48-MHz USB clock. Datasheet 4.1.2; `usb.h:20-28`; `kern.ldscript:141-172`. | Endpoint/control state, Dhara and raw-flash buffers, and the shared codec workspace occupy the ledger below. `usbinit()` clears all 4 KiB before configuring endpoints (`usb.c:1115-1147`). | Endpoint ranges are live whenever USB runs. `flpage`, `flscratch`, and the codec have explicit owners. A USB reinitialization would clear relocated state. | The largest idle run is 944 bytes. It cannot hold a sector bounce, but it can host small nonexecuted metadata after lifetime and reset gates. |
| PIO instruction and FIFO state | PIO0 and PIO1 each have 32 16-bit instruction slots and four state machines, each with 4x32-bit TX and 4x32-bit RX FIFO words: 128 bytes of instruction storage and 256 bytes of FIFO storage across both blocks. Access is 32-bit MMIO with state-machine side effects. Datasheet 3.2 and 3.5.3. | The supplied ELF links only weak default PIO IRQ handlers; the PICO kernel has no PIO driver. The state is idle while no state machine runs. | Instruction memory is executable only by PIO state machines. FIFO reads consume words and writes enqueue them. A bounce protocol would add a PIO driver, serialize word access, and still hold less than one sector. | Neither general RAM nor useful relocation capacity. Reject. |
| DMA | Twelve channels with separate read and write bus masters; channel counters and a sniff accumulator hold control state, not a payload buffer. Datasheet 2.5. | The supplied ELF links only weak default DMA IRQ handlers. No PICO DMA owner was found. | DMA moves bytes between existing stores. It cannot read XIP while SSI is unavailable, contributes zero staging bytes, and cannot shorten NOR erase or program time. | Movement engine only. Exclude from the first exchange implementation. |
| SIO divider, interpolators, FIFOs, and spinlocks | Core-local divider state at `0xd0000060..0xd000007f`; two interpolators per core; two inter-core FIFOs of eight 32-bit words; 32 one-bit spinlocks. MMIO access has operation side effects. Datasheet 2.3.1. | User code and Boot ROM float/integer helpers own the divider under the MPU grant (`MPU.md:39-95`). The kernel uses neither interpolator. Core 1 remains parked, so the inter-core FIFOs are idle after Boot ROM handoff. | The divider is only eight-cycle arithmetic state. Interpolators transform register values. FIFO reads consume entries. None provides random-access 4-KiB storage, and borrowing the divider violates its ownership invariant. | Neither bounce nor relocation home. |
| Boot ROM and copied boot2 | Boot ROM `0x00000000..0x00004000`, 16 KiB, immutable read/execute storage on a separate bus slave. `boot2_copy` is 256 live bytes in ordinary PICO `.bss`. Datasheet 2.6.1 and 2.8.3. | ROM supplies flash connect, XIP exit, erase, program, cache flush, copy, fill, and arithmetic routines. Every flash mutation calls the SRAM boot2 copy to restore XIP (`flash.c:138-150,260-292`). | ROM cannot store mutable data. Reusing `boot2_copy` prevents XIP restoration after the first flash call. | ROM routines remain code providers; neither range is a bounce home. |
| Core 1 | Processor state only; core 1 has no private SRAM, allocated kernel stack, or private bounce memory. The shared SRAM and flash buses are the same resources listed above. | The USB driver relies on core 1 never being launched (`usb.c:939-947`). Boot ROM parks it until the launch handshake in datasheet 2.8.2. | Launch needs a stack, vector table, code, synchronization, and flash lockout if core 1 can fetch XIP. It consumes shared SRAM and adds a second flash-safety participant without adding storage. | Keep parked. Core 1 cannot solve the bounce requirement. |
| Watchdog scratch state | Eight 32-bit MMIO scratch registers at `0x4005800c` onward, 32 bytes total. | Scratch 0 persists the swap cursor, scratch 1 and 2 record masked flash sites, and Boot ROM reserves scratch 4 through 7 (`watchdog.h:28-34`; `machdep.c:217-258,510-547`). Repository source defines no owner for scratch 3. | Capacity is four bytes even if scratch 3 is available; reuse also changes reset evidence and requires a Boot ROM contract check. | State latch only. Neither bounce nor meaningful relocation target. |
| Per-process user area | `u0` and `u` are each 3,072 bytes. The linked `struct user` is 972 bytes; bytes 972 through 1023 are 52 bytes of layout space, and bytes 1024 through 3071 are the 2,048-byte kernel stack (`machparam.h:133-145`; `uarea_stats.h:20-22`). | `resume` exchanges all 3,072 bytes (`locore.S:90-126`), and swap-in/out transfers all 3,072 (`vm_swap.c:92-109,224-235`). Each nonresident process's kernel stack is part of its swapped user area. | The 2,100 bytes after `struct user` are not 2,100 padding bytes. Shrinking the image loses live stack frames unless a new capture and restore architecture proves stack liveness across system calls, faults, signals, sleep, and scheduling. | Preserve all 3,072 bytes. Reject as a space recovery. |
| Kernel buffer cache and clists | `bufdata` is 4,096 bytes and `cfree` is 1,024 bytes in the supplied ELF. Ordinary striped SRAM at `clk_sys`. | Four 1-KiB buffer-cache payloads are assigned at boot (`machdep.c:114-127`; `init_main.c:240-259`). The clist initializer puts all 32 blocks on the active free list (`init_main.c:262-278`). | Borrowing a busy buffer corrupts filesystem I/O. Borrowing a clist block corrupts terminal queues. A smaller configured pool changes whole-kernel capacity and needs independent workload gates. | Active kernel capacity, not bounce storage. |
| Heatshrink workspace and metadata | `swapram_codec_work` is 1,040 bytes at `0x50100740`; `sr_tab` is 600 bytes in `.bss`; `sr_encode` also uses a 64-byte automatic scratch (`swapram.c:76-97,255-318`). | Encoder and decoder share the DPRAM workspace sequentially through an owner. `sr_tab` persists process image metadata even when no codec call runs. | Borrowing the workspace during codec use corrupts compressed images. Moving `sr_tab` into the 944-byte DPRAM gap needs USB-reset ordering and linked-range checks. | Workspace is unavailable as a bounce. `sr_tab` relocation is a possible 600-byte main-SRAM recovery, not an exchange prerequisite. |

### USB DPRAM ledger

`kern.ldscript:141-173`, `usb.h:62-71`, and `usb.c:80-85,1142-1147`
produce this PICO layout:

| Interval | Bytes | Owner |
|---|---:|---|
| `0x50100000..0x50100240` | 576 | USB setup, endpoint controls, buffer controls, EP0, notify, bulk OUT, and bulk IN buffers |
| `0x50100240..0x50100640` | 1,024 | Dhara `flpage` |
| `0x50100640..0x50100740` | 256 | Unassigned gap |
| `0x50100740..0x50100b50` | 1,040 | Shared heatshrink workspace |
| `0x50100b50..0x50100f00` | 944 | Unassigned gap |
| `0x50100f00..0x50101000` | 256 | Raw-swap and Dhara `flscratch` |

The two gaps total 1,200 bytes, and the largest contiguous gap is 944 bytes.
Neither can preserve a complete erase sector.

### External flash ledger

`RP2040.ld:21-35` and `flash.h:31-53,67-74,86-101` establish the fixed
regions and the 256-byte program and 4-KiB erase geometry.

| Resource | Address and measured capacity | Idle or slack | Borrowing result | Verdict |
|---|---|---|---|---|
| boot2 and kernel | `0x10000000..0x10020000`, 128 KiB | The supplied artifact uses 96,124 of 130,816 post-boot2 bytes and leaves 34,692 bytes; preserving the 1,024-byte margin admits at most 33,668 additional linked bytes. | Code growth consumes flash headroom but provides no mutable bounce. | The exchange code fits only if the existing headroom gate remains green. |
| Dhara root | `0x10020000..0x101a0000`, 1,536 KiB physical and 989 1-KiB logical sectors | The build creates a 988-KiB filesystem (`distrib/rp2040/Makefile.inc:1-9`). Retained board `df` reports 979 filesystem blocks, 665 used, and 314 free (`STORAGE.md:121-129`). Current root artifacts were absent, so these usage counts were not remeasured. | The free logical blocks belong to the mounted filesystem and Dhara journal. Repartitioning them would reduce root capacity and violate the fixed decision. | Keep the root capacity. Root slack cannot satisfy raw swap allocation. |
| Raw swap | `0x101a0000..0x10200000`, 384 KiB or 384 1-KiB blocks | Relative blocks `[0,4)` form the 4-KiB rewrite scratch sector. Blocks `[4,384)` provide 380 allocatable blocks (`flash.h:86-101`; `init_main.c:135-166`). | The scratch sector can stage one sector but adds an erase and 16 programs per exchanged sector. Allocatable blocks already have process owners. | Retain the partition and existing extent. Use direct XIP reads and a RAM bounce. |
| Erase granularities | 4 KiB sectors; the ROM call is configured with a 64-KiB block and opcode `0xd8`, and it uses the larger erase only for a complete aligned block (`flash.h:67-74`). A 32-KiB erase is a chip command but is not configured here. | Larger erases can reduce command count only when all covered sectors may be destroyed together. | A 32- or 64-KiB erase would destroy incoming sectors that a 4-KiB bounce has not saved. Larger erases add speed, not capacity. | Use exactly one 4-KiB sector per exchange step. |
| W25Q16JV security registers | Three 256-byte pages, 768 bytes total, through special program/read/erase opcodes `42h`, `48h`, and `44h`; lock bits are one-time programmable (`firmware-updates.md:154-180`). | Lock state on the measured board was not checked. The pages sit outside the ordinary XIP storage path. | Capacity is below one sector. Use needs a new special-command path and risks irreversible lock state. | Reject as bounce, relocation, or swap capacity. |

## Ranked reuse opportunities

The ranking combines useful bytes, implementation risk, and whether the option
solves a complete 4-KiB erase transaction.

| Rank | Opportunity | Recoverable or usable bytes | Risk and falsifier |
|---:|---|---:|---|
| 1 | Allocate the exchange bounce in measured kernel-RAM headroom. | 4,096-byte bounce from 76,304 measured bytes; 72,144 remain after a 64-byte state budget. | Low relative risk. Falsifier: the production linked image places any other live symbol in the bounce, misaligns it, or moves `_ebss` or the bounce end to or above `u0`. |
| 2 | Reduce the USB transmit ring from 8 KiB to 4 KiB and dedicate one direct bank. | 4,096 main/scratch SRAM bytes. | Medium risk: early output retention and congested console operation shrink. Falsifier: boot output, sustained output, panic output, or host-open-after-boot loses required bytes under a calibrated USB workload. |
| 3 | Move `sr_tab` from striped SRAM into the 944-byte USB DPRAM gap. | 600 main-SRAM bytes. | Medium risk: DPRAM reset and owner lifetimes differ from `.bss`. Falsifier: USB reset or reinitialization changes a live entry, linked ranges overlap, or SwapRAM process churn disagrees with the SRAM baseline. This does not solve the sector bounce. |
| 4 | Disable the XIP cache and use its SRAM alias. | 16,384 bytes. | High risk despite the larger byte count. Falsifier: any XIP execution or data load becomes unsafe during the mode, cache restoration leaves stale lines, or board latency and correctness differ from the cached baseline. Ordinary SRAM headroom makes the risk unnecessary. |
| 5 | Use a 256-byte RAM page plus the reserved raw-swap scratch sector. | Requires only 256 RAM bytes but consumes the existing 4-KiB flash scratch sector. | High wear and failure complexity. Each of 39 sectors needs one scratch erase and 16 scratch programs before the destination erase, for 78 erases and 1,238 programs total instead of 39 and 614. Falsifier: any injected failure lacks a uniquely defined panic state or adjacent swap data changes. A 512- or 1,024-byte RAM choice still needs the flash sector because neither preserves all incoming bytes across the destination erase. |
| 6 | Treat PIO, SIO, DMA, security registers, peripheral FIFOs, or core 1 as storage. | At most hundreds of side-effectful bytes; DMA and core 1 add zero bytes. | Reject. Falsifier for the rejection: a design presents 4,096 random-access bytes that remain readable across XIP-off erase and program calls without consuming another listed owner or adding irreversible flash state. |
| 7 | Shrink the swapped user area. | A naive `3072 - 972` calculation claims 2,100 bytes per image; only 52 are layout space and 2,048 are the kernel stack. | Reject under the present process architecture. Falsifier for the rejection: a complete live-stack capture and restore design passes context-switch, nested syscall, sleep, signal, fault, fork, exec, and exit gates while preserving `resume` and core-dump interfaces. |

## Exact exchange design

### Eligibility and transaction boundary

The scheduler considers the exchange only when all of these observations hold
before the first erase:

- one selected process is resident but removed from the run queue and has
  `SLOAD` cleared by `sched()`; the other is selected for swap-in and owns one
  committed, erase-aligned, contiguous raw-flash image;
- neither image is in SwapRAM and neither process or extent has another active
  transaction;
- `p_dsize`, `p_ssize`, `p_tsize`, `swaptext()`, and the text inode identity
  agree, so the clean executable text and every image offset have the same
  meaning for both processes;
- the incoming `p_daddr`, `p_saddr`, and `p_addr` describe the expected
  contiguous image, the rounded image span agrees with the extent reservation,
  and the outgoing resident addresses equal the LARGE data, stack, and `u0`
  locations;
- the bounce, callback table, transaction record, the existing 256-byte
  `flscratch` program-page buffer, and any panic/reporting resources are
  acquired without sleeping. The page buffer needs a distinct exchange owner.

An eligibility or initial XIP-read failure before the first erase restores the
scheduler selection and uses the ordinary reservation-controlled path. The
helper sets a destructive-started bit immediately before its first erase. Any
error after that transition panics; ordinary fallback is no longer sound.

### Recorded image geometry

The recorded processes have `p_tsize = 8,040`, `p_dsize = 159,744`,
`p_ssize = 2,244`, and `USIZE = 3,072`. `vm_swap.c:30-45,92-109,150-168`
excludes clean text, rounds each of the three stored portions to 1-KiB blocks,
and rounds the complete allocation to four blocks. The resulting image is 156
blocks, 159,744 bytes, or 39 erase sectors.

| Portion | Image-relative interval | Resident interval |
|---|---:|---:|
| Clean text, excluded from swap | none | `0x20000000..0x20001f68` |
| Data after clean text | `[0,151704)` | `0x20001f68..0x20027000` |
| Data padding | `[151704,152576)` | synthesized as `0xff` |
| Stack | `[152576,154820)` | `0x2002773c..0x20028000` |
| Stack padding | `[154820,155648)` | synthesized as `0xff` |
| User area | `[155648,158720)` | `0x2003e800..0x2003f400` (`u0`) |
| Allocation tail | `[158720,159744)` | synthesized as `0xff` |

Sectors 0 through 36 each map to 4,096 contiguous data bytes. Sector 37,
image offsets `[151552,155648)`, has this gather/scatter map:

| Sector-37 offset | Bytes | Meaning |
|---|---:|---|
| `[0,152)` | 152 | final data bytes |
| `[152,1024)` | 872 | data padding, forced to `0xff` |
| `[1024,3268)` | 2,244 | stack |
| `[3268,4096)` | 828 | stack padding, forced to `0xff` |

Sector 38, image offsets `[155648,159744)`, contains 3,072 user-area bytes
followed by 1,024 bytes of allocation-tail padding forced to `0xff`. A generic
implementation must derive these overlaps from the three segment lengths and
their 1-KiB rounded starts. It must not hardcode sector 37 or the recorded
sizes.

### Sector operation

For sector indices from zero through the image-sector count minus one:

1. Read the complete incoming sector from its raw-flash XIP address into the
   4-KiB bounce. `flash_read()` itself is an XIP `bcopy`
   (`flash.c:352-359`); the exchange should use the same bounded read callback.
2. Define the logical outgoing sector from the resident data, stack, and
   user-area overlaps, with every nonpayload byte equal to `0xff`. Keep the
   complete incoming sector in the bounce. Materialize only one outgoing page
   at a time in the existing 256-byte `flscratch` buffer; a second 4-KiB store
   is neither needed nor budgeted.
3. Erase exactly the destination 4-KiB sector.
4. Read back all 4,096 bytes through XIP and require every byte to equal
   `0xff`.
5. Program each 256-byte page that intersects outgoing payload. Program an
   entire intersecting page from `flscratch`, with `0xff` in its internal
   padding. Sectors 0 through 36 use 16 pages each, sector 37 uses 10 pages,
   and sector 38 uses 12 pages, for 614 program calls.
6. Regenerate one expected outgoing page at a time in `flscratch` and compare
   the corresponding 256 direct-XIP bytes against it. Cover the complete
   sector, including every erased padding-only page.
7. Scatter only the incoming payload overlaps from the bounce into resident
   data, stack, and `u0`. Ignore incoming padding. Increment the completed
   sector count only after scatter completes.

The success path performs 39 erases, 614 programs, and three 4-KiB XIP reads
per sector: the saved incoming read, erased readback, and programmed readback.
The model therefore counted 479,232 XIP-read bytes. It also proved that the
guard sectors immediately before and after the extent remain unchanged.

The destination is the incoming process's existing extent. The exchange does
not call `mfree()`, does not allocate another extent, and does not publish a
new next-fit cursor. The raw-swap rewrite scratch sector remains untouched.

### Flash, interrupt, and core ordering

The current RAM-resident wrappers connect the flash, exit XIP, invoke one ROM
operation, flush the cache, run `boot2_copy` to restore XIP, call
`clock_catchup()`, and restore interrupts (`flash.c:260-349`). The exchange
must call one wrapper at a time. The wrapper's interrupt mask covers only one
erase or one program. XIP is restored before the wrapper returns, so the USB
interrupt can run between every operation and between every readback.

The scheduler keeps the two processes invisible for the transaction without
holding `PRIMASK`: the outgoing process remains off the run queue with `SLOAD`
clear, and the incoming process remains nonresident. Core 1 remains parked.
If a later port launches core 1, the exchange requires the same SRAM-only
flash lockout discipline described by `rp2040-second-core-hardware.md:57-61`
before the first erase.

DMA provides no benefit in the first implementation. It cannot fetch from XIP
while SSI is unavailable, and RAM copies are outside the NOR command's
XIP-off interval.

### Time and console budget

The repository preserves a W25Q16JV 4-KiB sector-erase maximum of 400 ms
(`sys/arch/rp2040/include/watchdog.h:36-42`). It does not preserve a verified
maximum page-program time for the fitted part. Historical 45-ms erase and
0.4-ms program figures in research notes are estimates, not acceptance bounds.
The auditable bounds therefore remain symbolic:

```text
one full sector <= tSE,max + 16*tPP,max
                   + 12,288 XIP-read bytes
                   + RAM gather/scatter + ROM/cache overhead

whole exchange <= 39*tSE,max + 614*tPP,max
                  + 479,232 XIP-read bytes
                  + RAM copy + ROM/cache overhead
```

The known erase term alone is at most 15.6 seconds across 39 sectors. The
longest presently bounded continuous interrupt mask is one erase wrapper,
whose chip erase component is at most 400 ms. The program-wrapper mask lacks a
complete numeric maximum until `tPP,max` is verified. USB can service and NAK
between calls, but source inspection does not prove that a host preserves the
CDC session across every measured masked interval.

The missing `tPP,max`, wrapper overhead, and host USB tolerance prevent a
complete numeric console-latency budget in this review. Implementation approval
requires a stated acceptance budget followed by `TIMERAWL` captures for every
erase, every program, each sector, and the whole exchange on the authorized
board. The board gate must also demonstrate that the USB console remains
usable throughout and immediately after the exchange.

### Publication and ownership

All extent and process metadata remain unchanged while sectors are mixed. On
success, a short globally masked publication section performs one logical
transition:

1. copy the incoming process's flash `p_daddr`, `p_saddr`, and `p_addr` to the
   outgoing process;
2. assign the resident data address, computed stack address, and `&u0` to the
   incoming process;
3. transfer the reservation's sole owner from incoming to outgoing without
   freeing the extent or moving `swapnext`;
4. clear the outgoing process's swap lock and leave it nonresident;
5. set `SLOAD` on the incoming process, reset the two process times and swap
   counters in the same manner as `swapout()` and `swapin()`, and insert a
   runnable incoming process exactly once on the run queue; and
6. publish the transaction as complete, then restore interrupts.

A gate must inspect the process table, extent owner table, resource map, and
run queue at that boundary. Exactly one process owns the extent; exactly one
process owns each resident address; and a runnable process appears on the run
queue exactly once.

The exchange composes with the planned reservation layer by consuming the
incoming process's already committed reservation and changing only its owner.
Shared headroom admission counts one retained equal-layout extent, not a
second image. A different layout, text identity, segment size, backing tier,
or extent shape rejects exchange eligibility before destruction and uses the
ordinary reservation-based swapout then swapin path. Once an erase begins,
layout fallback is prohibited.

### Failure state

Before the first erase, a failed precondition or incoming read leaves both
images intact and can restore scheduler state. After the first erase begins,
every erase, program, XIP read, readback, gather, scatter, or invariant failure
panics.

At a destructive failure:

- every earlier completed sector contains outgoing bytes in flash and incoming
  bytes in resident RAM;
- the current sector's complete incoming bytes remain in the bounce;
- the current outgoing bytes remain in resident RAM because scatter follows
  successful programmed readback;
- the current flash sector can be erased, partially programmed, or contain an
  otherwise mismatching value;
- every later sector retains incoming bytes in flash and outgoing bytes in
  resident RAM; and
- the extent owner, process addresses, resource map, run queue publication,
  and next-fit cursor retain their pretransaction values.

That state contains two mixed images and has no ordinary scheduler recovery.
Panic preserves the first failure instead of executing either mixture. A power
loss needs no swap journal recovery: boot clears and initializes RAM, opens the
swap device, and reconstructs the free map from allocatable blocks `[4,384)`
without reconstructing processes from old raw-swap bytes
(`init_main.c:135-166`). The partially exchanged extent is therefore dead data
after reset. This conclusion covers process swap only; it does not authorize
changing root or temporary-extent recovery.

## Budgets

| Budget | Measured baseline | Exchange allocation or limit | Required gate |
|---|---:|---:|---|
| Kernel SRAM interval | 76,304 bytes from `_ebss` to `u0` | 4,096-byte bounce plus at most 64 bytes state; 72,144 remain | Linked symbols, section ranges, 4-byte alignment, nonoverlap, and `_ebss`/bounce end below `u0` |
| Kernel flash | 96,124 of 130,816 bytes; 34,692 headroom | At most 33,668 additional bytes while retaining the 1,024-byte margin | Existing calibrated kernel-headroom check on the production ELF |
| Raw swap allocation | 380 allocatable 1-KiB blocks | One existing 156-block extent; zero new blocks; next-fit cursor unchanged | Resource-map and reservation-owner snapshots before and after |
| NOR operations for recorded image | none in this review | 39 erases, 614 programs, 479,232 XIP-read bytes | Host operation counts plus authorized board counters and timing |
| Interrupt masking | Per ROM call in current wrappers | Never around the whole image or sector loop | Whole-image-mask mutant and board USB service observation |

The flash allowance is a ceiling derived from the existing margin, not an
estimate of expected code size. The production diff must report the actual
linked delta.

## Required gates

### Production host fixture

The host fixture must compile and invoke the production gather/scatter and
exchange helper through an emulated NOR callback table. A second handwritten
exchange in the test would preserve shared mistakes and is insufficient. The
fixture must cover:

- success for the recorded 39-sector geometry and for generated equal layouts
  that place each segment end before, on, and after a 1-KiB and 4-KiB boundary;
- exact flash bytes, resident bytes, padding, operation counts, adjacent guard
  sectors, extent ownership, cursor value, metadata publication, and run-queue
  membership;
- every one of the 39 erase positions and 614 program positions in both
  fail-before and fail-after modes;
- callbacks that return an error without changing media, change media and then
  return an error, silently leave an erase incomplete, partially program a
  page, corrupt a readback byte, and damage an adjacent byte;
- each failure after destructive start reaching the panic oracle with the
  completed/current/later sector state described above and with zero metadata
  commits; and
- initial read and eligibility failures taking the ordinary fallback only
  before destructive start.

The finite model in `scratch-03/model_exchange.py` checks 78 erase-failure
cases and 1,228 program-failure cases, but the production fixture must repeat
those cases against production code.

### Negative controls and mutants

The gate must reject at least these mutants:

- scatter incoming RAM before outgoing flash readback;
- program without erasing;
- commit metadata after each sector;
- free or transfer the extent before complete success;
- use unrounded segment starts;
- program all padding-only pages;
- include clean text in the swapped image or overwrite clean text in RAM;
- copy incoming padding into a resident segment instead of ignoring it;
- duplicate an extent owner or leave the incoming owner after publication;
- hold `splbio()` or `splhigh()` around the complete sector or image loop;
- omit erased or programmed readback;
- permit adjacent-sector damage;
- accept unequal data, stack, user-area, text, inode, or extent layouts; and
- update the next-fit cursor even though the exchange allocates nothing.

Each negative control needs a known-good fixture that passes and a known-bad
mutant that fails for the intended reason.

### Linked-image gates

The PICO and PICO_UART linked-image checks must establish:

- one 4,096-byte exchange bounce, at least 4-byte aligned, wholly inside kernel
  SRAM and disjoint from `.swapram_pool`, `u0`, `u`, `.scratch`, USB DPRAM,
  `flpage`, `flscratch`, `boot2_copy`, and all loadable sections;
- transaction state at or below 64 bytes and wholly outside user-owned memory;
- `_ebss`, bounce, and state ending below `u0` with a reported remaining byte
  count;
- RAM flash helpers and every transitive call made while XIP is down remaining
  executable without XIP;
- the 1,024-byte kernel-flash margin; and
- PICO and PICO_UART generated Makefiles remaining synchronized when their
  source lists or checks change.

### Explicitly authorized board gate

Board execution remains outside this review. A later, separately authorized
run must boot the production LARGE-bonus PICO kernel, run `bigtest`, observe a
successful fork, force at least two parent/child residency alternations, let
the child exit, reap it, and reach `BIGTEST OK`. The capture must include:

- process and extent-owner snapshots at each exchange;
- raw-swap operation counters and unchanged adjacent-sector sentinels;
- `TIMERAWL` durations for each erase, each program, each complete sector, and
  the complete exchange;
- the longest continuous interrupt-masked interval;
- continuous USB enumeration and usable console input/output during and after
  the alternations; and
- the child exit status, parent reap result, and final resource map.

No board gate, flash command, serial-device access, or hardware measurement ran
for this review.

## Addendum: the kernel RAM gap in every configuration

The `_ebss` symbol of each built kernel, against `u0` at `0x2003e800`
(`arm-none-eabi-nm unix.elf`), leaves this much section-unoccupied kernel
RAM; the linker's `RAM` region ends at `u0` in every configuration
(`sys/arch/rp2040/conf/RP2040.ld`, `kern.ldscript`), and no source names an
address range above `_ebss` below `u0`:

| Kernel | `_ebss` | Bytes to `u0` |
|---|---|---:|
| PICO | `0x2002bdf0` | 76,304 |
| PICO_UART | `0x20027bf0` | 93,200 |
| PICOCALC | `0x2002f228` | 62,936 |
| PICOCALC_PSRAM | `0x2002f440` | 62,400 |

The smallest gap, 62,400 bytes, exceeds the exchange bounce fifteenfold. It is
also larger than the 48 KiB that `RP2040.ld` records the user window gaining
when the SwapRAM pool shrank from 64 KiB to 16 KiB, so the region sizes no
longer follow from the kernel's measured data and BSS. Spending the gap on a
larger user window, a larger SwapRAM pool placed outside the LARGE bonus, or
neither is an architecture decision separate from the exchange; the bounce
takes 4,096 bytes of it either way.
