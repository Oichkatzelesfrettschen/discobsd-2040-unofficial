# RAM and flash pressure on DiscoBSD/RP2040: compression options

Scope: DiscoBSD (2.11BSD-derived) on RP2040 (Cortex-M0+, 125 MHz, 264 KB SRAM,
no MMU). User window: 96 KB at 0x20000000, one resident process at a time
(a.out loaded whole). Kernel data/buffers: about 40 KB. Process switch swaps
data+stack (20-70 KB) to a raw 256 KB region of 2 MB QSPI NOR flash: sector
erase (4 KB) about 45 ms, page program (256 B) about 0.4 ms, so a 33 KB swap
write costs roughly 0.5 s and wears the flash; XIP reads run at bus speed.
Root filesystem sits behind the Dhara translation layer. Binaries are
cross-compiled a.out, `-Os`, Thumb-1; gzip removes about 30 percent of a
binary's size (a fact given, not re-derived here; used below as the baseline
ratio for whole-binary LZ-class compression).

All numbers below are cited to a source or explicitly marked as a reasoned
estimate with its basis stated. No official cycles-per-byte figure for any
of these codecs on Cortex-M0+ specifically was found in this research pass;
that gap is called out at each point it matters, and the report reasons from
the decoder's operation count rather than inventing a benchmark.

## 1. RAM-resident compressed swap (zram-style)

### Candidate codecs

| Codec | Basis | Decoder RAM | Decoder code | License |
|---|---|---|---|---|
| heatshrink (LZSS) | window_sz2/lookahead_sz2 params | as low as 50 bytes; "under 300 bytes" for general use; optional index adds 2^(window+1) bytes (up to 512 B extra during index construction) | not published in bytes by the project | ISC (permissive) [atomicobject/heatshrink README](https://github.com/atomicobject/heatshrink/blob/master/README.md) |
| LZ4 | LZ77, byte-oriented tokens | small (streaming state is a few pointers); a hand-written Cortex-M0 Thumb decoder exists | small; ARM's own example targets M0 directly | BSD 2-Clause [LZ4 decompression routine for Cortex-M0 and later, ARM Community](https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/lz4-decompression-routine-for-cortex-m0-and-later) |
| miniLZO (LZO) | LZO1X subset | decompressor needs about 16 KB *static buffer*; decompressor code about 868 B (628 B "unsafe") | 868 B / 628 B | GPLv2+ (copyleft -- LZO's own license; not compatible with a BSD-style DiscoBSD tree without isolating it) [miniz/minilzo footprint discussion](https://demin.ws/blog/english/2012/09/18/miniz-minilzo/), [minilzo-rs](https://github.com/badboy/minilzo-rs) |
| LZSS (generic, heatshrink's ancestor) | sliding window + literal/match flag | window buffer only (no separate LZ77 hash table needed for decode) | trivial (a few hundred bytes of decode loop) | public-domain scheme; heatshrink's ISC implementation is the practical instance here |
| Zstd-lite | reduced window log | Decompressors cost about 2x the window size in RAM; an 8 KiB window needs roughly 16 KiB decoder RAM; minimum Window_Size is 1 KB | not published for a stripped build; the reference decoder itself is far larger than heatshrink/LZ4 | BSD or GPLv2 dual license [Tailscale's small-window zstd note](https://github.com/qwenode/tailscale/blob/v1.33.0/smallzstd/zstd.go), [RFC 9659](https://www.rfc-editor.org/info/rfc9659/) |

Zstd-lite is disqualified on RAM alone: even its smallest practical window
(8 KiB) costs about 16 KiB of the 100 KB budget for decoder state before a
single byte of swap data is held, against heatshrink's and LZ4's low-hundreds
of bytes. It is included here only because the question asked for it; it is
not a candidate for this board.

miniLZO's 16 KB static buffer requirement rules it out on the same basis, and
its GPLv2 license is a second, independent disqualifier for a BSD-licensed
kernel tree unless it is kept fully out-of-tree and invoked as an external
tool -- not an option for an in-kernel swap path.

heatshrink and LZ4 both fit the RAM budget comfortably (low hundreds of
bytes for heatshrink; LZ4's Cortex-M0 assembly decoder needs no arena beyond
its output buffer). heatshrink is purpose-built for this class of hardware:
Zephyr carries an open RFC to add it specifically for memory-constrained
targets ([zephyrproject-rtos/zephyr#61822](https://github.com/zephyrproject-rtos/zephyr/issues/61822)), and CNX Software's writeup frames it explicitly as "an
ultra-lightweight compression library for embedded systems" ([CNX Software](https://www.cnx-software.com/2021/09/29/heatshrink-an-ultra-lightweight-compression-library-for-embedded-systems/)).

### Ratio on code + zero-filled bss

No heatshrink-specific ratio number for ARM Thumb object code was found. Two
data points bound the estimate:

- The given fact that gzip removes about 30 percent from these binaries
  (ratio approximately 1.43:1) is a whole-binary figure -- text, data, and
  bss together, with bss run-length-friendly zeros diluted by the same
  proportion as the rest of the file when measured against on-disk a.out
  size (bss occupies no space in the file at all, so this ratio describes
  text+data only).
- A memory-constrained-systems study running heatshrink, LZ4, FastLZ, Miniz,
  and LZW head-to-head on an STM32U575ZI (Cortex-M33, 160 MHz) against a
  sensor-data corpus found Miniz (a DEFLATE implementation) reaching 33.81
  percent of the binary-format input size (about 3:1) and LZ4 the fastest at
  9.4 ms for a comparable payload, with total RAM (including buffers) as low
  as 58 KB for the LZ4 case ([Frontiers in Computer Science, 2026](https://www.frontiersin.org/journals/computer-science/articles/10.3389/fcomp.2026.1925314/abstract)). That corpus is sensor telemetry, not code, so its
  ratio does not transfer directly, but it confirms LZ4-class codecs clear
  2:1-3:1 on structured, non-random embedded data when memory allows the
  larger buffer sizes used in that study -- which this board's 100 KB budget
  does not.

For the swap payload specifically (data+stack, not the whole a.out), bss and
fresh stack are largely zero-filled, which run-length/LZ77-class codecs
compress far better than the 1.43:1 whole-binary figure -- reasoned estimate,
not measured: a page of zero bytes collapses to a handful of tokens under
LZSS or LZ4, so the effective ratio on a real data+stack image sits between
the 1.43:1 gzip whole-binary floor and something close to Miniz's observed
3:1 depending on how much of the payload is genuinely zero versus
initialized data. Using the conservative bound (1.43:1, i.e., no credit for
the zero-fill advantage) is the safe planning number; the optimistic bound
(2:1-3:1) is achievable if bss dominates the payload, common for a process
with large static buffers and a small initialized-data section.

### Does 100 KB hold two or three 33 KB images?

At the conservative 1.43:1 ratio: 33 KB / 1.43 = 23.1 KB compressed. Three
such images total 69.2 KB, leaving about 31 KB of the 100 KB budget for
codec working RAM (heatshrink's few hundred bytes to at most a few KB with
an index) and bookkeeping -- three images fit with margin. At the low end of
the given payload range (20 KB), four or more fit; at the high end (70 KB),
even at the conservative ratio one 70 KB image compresses to 49 KB, so two
fit (97.9 KB) but a third does not. **Answer: yes for the stated 33 KB case
-- three fit comfortably at the conservative ratio, and the true ratio is
likely better because bss and stack compress far above the whole-binary
average.**

### Latency and wear versus flash swap

- Flash swap-out of a 33 KB image: given as about 0.5 s (page-program
  dominated), plus the wear from repeated 4 KB sector erases (about 45 ms
  each) on the same 256 KB swap region every switch.
- RAM-compressed swap: no official Cortex-M0+ cycle count exists for
  heatshrink or LZ4 decode. Reasoning from operation count: both are
  byte-oriented LZ77/LZSS variants with a tight literal-or-copy inner loop
  and no floating point or wide multiplies -- the class of code that runs at
  roughly 5-30 cycles per byte on a single-issue, no-cache Thumb-1 core
  (ARM's own Cortex-M0 LZ4 decoder blog post describes hand-tuning the inner
  copy loop to shave individual cycles, consistent with a decode cost
  measured in single-digit-to-low-tens of cycles per byte rather than the
  "~1 byte/cycle" figure LZ4 quotes for cached 32-bit superscalar cores
  ([LZ4 decompression routine for Cortex-M0 and later](https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/lz4-decompression-routine-for-cortex-m0-and-later); general LZ4 throughput claim: [lz4.org](https://lz4.org/)). At 125 MHz, even a
  pessimistic 30 cycles/byte gives about 4.2 MB/s, so a 33 KB image
  decodes in roughly 8 ms; at an optimistic 8 cycles/byte, roughly 2 ms.
  Compression (encode) is typically several times slower than decode for
  LZ77-family codecs, but even a 10x encode/decode asymmetry keeps a 33 KB
  compress under 100 ms -- still an order of magnitude faster than the 0.5 s
  flash write, with zero flash wear.

RAM-compressed swap wins on both axes the flash path loses on: it is roughly
50-250x faster per swap event (single-digit-to-tens of ms versus 500 ms),
and it consumes zero flash erase/program cycles, which matters because NOR
endurance is the harder constraint to recover from once exhausted.

### Tiered design: RAM first, flash overflow

Given the ratio and capacity analysis, a RAM-tier holding 2-3 compressed
images before falling back to flash is directly buildable within the stated
100 KB free-SRAM budget:

1. On swap-out, compress data+stack with heatshrink (favor it over LZ4 here
   for its sub-kilobyte, license-clean, RTOS-precedented footprint) into a
   fixed-size RAM pool.
2. If the pool has room, keep the image there; only fall back to the flash
   256 KB region when the RAM tier is full or a single image's compressed
   size exceeds what remains.
3. Process switches that hit the RAM tier pay single-digit milliseconds and
   no flash wear; only switches that spill to flash pay the 0.5 s/wear cost
   already budgeted.

This is architecturally the same trade Linux's zram and zswap make: zram
holds the entire compressed page set in a RAM-backed block device --
allocating nothing until a page is actually swapped, and growing/shrinking
as swap activity demands, with roughly 3:1 expected compression driving the
recommendation that a zram device not exceed about twice physical memory
([Zram, Wikipedia](https://en.wikipedia.org/wiki/Zram); [zram: Compressed RAM-based block devices, Linux kernel docs](https://docs.kernel.org/admin-guide/blockdev/zram.html)). zswap is the closer
architectural analog to the tiered design proposed here: it is explicitly a
*compressed cache in front of* a real backing swap device, evicting to that
backing store on an LRU basis only when its own pool fills or a page won't
compress ([zswap: compressed swap caching, LWN](https://lwn.net/Articles/528817/); [zswap admin guide, Linux kernel docs](https://github.com/torvalds/linux/blob/master/Documentation/admin-guide/mm/zswap.rst)). The RP2040 design mirrors zswap with the
256 KB flash region playing the role of the backing device instead of a disk.

RetroBSD (the PIC32-based project DiscoBSD forks from) documents the same
underlying tension between swap and flash wear on hardware of similar
character: "RAM is a lot faster than trashing swap space. If your swap is
on flash and you are trashing it heavily, it will not last long," and notes
its own 96 KB-per-process, kernel-in-32-KB-RAM split ([RetroBSD, GitHub](https://github.com/RetroBSD/retrobsd)) -- the numbers this project's fixed facts already mirror. No RetroBSD
commit or wiki page proposing a compressed-RAM swap tier was found in this
pass; that appears to be an open idea rather than a solved one in the
2.11BSD-derived embedded-Unix lineage. FUZIX, the closer no-MMU precedent,
keeps exactly one process resident and swaps whole images to an SD card at
context switch on its most constrained targets, with each process capped at
64 KB of code+data and swap explicitly *not yet supported* on its banked
(flat, no-MMU) memory model variant ([FUZIX Memory Management wiki](https://github.com/EtchedPixels/FUZIX/wiki/Memory-Management)) -- FUZIX has not built compressed swap either. No
Contiki-NG, RIOT, or Zephyr memory-compression subsystem (as opposed to
codec libraries usable for one) turned up in this research pass; RIOT and
Zephyr's published RAM/flash footprints (Contiki-NG about 29.8 KB RAM/50.6
KB flash, RIOT about 33.0 KB RAM/59.9 KB flash, Zephyr about 52.9 KB
RAM/130.5 KB flash) are baseline OS costs, not compression subsystems
([IoT OS benchmark, IEEE](https://sandro2pinto.github.io/files/ieeeiotj2019-iotosbench.pdf)). **A RAM-compressed swap tier on this class of MCU
Unix is a novel contribution here, not a port of prior art** -- the prior
art (zram/zswap) is the design pattern to borrow, not code to reuse.

## 2. Compressed executables, decompressed at exec time

The a.out loader already reads the whole image into RAM; the only added
cost is decoding instead of a straight copy.

**Cost per exec, 30 KB image:** using the same 5-30 cycles/byte bound as
above (no Cortex-M0+-specific figure exists for heatshrink or LZ4; this is
the same reasoned estimate, carried over), decode of 30 KB at 125 MHz costs
roughly 7-22 ms. Against a raw flash-to-RAM copy at XIP bus speed (already
fast, sub-millisecond for 30 KB at bus clock) this is a real added latency
per exec, but it is one to two orders of magnitude below the 0.5 s a single
flash *swap-out* already costs elsewhere in the system -- exec-time
decompression is cheap relative to the operations this system already
budgets for, not cheap in absolute terms against a bare memcpy.

**Kernel code size:** heatshrink's decoder is small enough that Zephyr's own
RFC frames adding it as a low-cost addition to a constrained image ([zephyrproject-rtos/zephyr#61822](https://github.com/zephyrproject-rtos/zephyr/issues/61822)); no exact byte figure for the decoder
alone (isolated from encoder and tooling) was published by the project, so
budget it at "comparable to or smaller than the existing a.out loader's
copy loop" rather than a hard number -- flagged as an estimate, not a cited
figure.

**Flash savings:** if the root filesystem stores compressed a.out images at
the gzip-observed 1.43:1 ratio, the same 30 percent space reduction the
fixed facts already state applies to every binary on the 2 MB flash,
directly reducing pressure on the Dhara-managed filesystem region and,
because fewer flash pages are read per exec, reducing XIP/FTL read traffic
proportionally too.

**Precedents:**

- Linux's compressed kernel image is the direct model: a small
  self-extracting stub decompresses the kernel proper into memory at boot,
  trading a fixed one-time decompression cost for a smaller on-disk/on-flash
  image and faster load-from-media time, since transferring the compressed
  bytes from slow media costs more than the extra CPU time to decompress
  them ("faster to load because the time it takes for decompression to run
  is shorter than the time it takes to transfer an uncompressed image")
  ([how the ARM32 Linux kernel decompresses](https://people.kernel.org/linusw/how-the-arm32-linux-kernel-decompresses), [Baeldung on Linux kernel images](https://www.baeldung.com/linux/kernel-images)). This is exactly the trade at exec
  time here: the QSPI NOR read is already fast (XIP bus speed), so the case
  for exec-time decompression rests on flash-space savings and reduced FTL
  churn, not on load-time speedup -- weaker than the boot-image case, where
  the source medium (historically disk) was the bottleneck.
- uClinux distinguishes ROMfs (uncompressed, XIP-capable) from cramfs
  (compressed, no XIP -- "you cannot run applications in place... it
  requires more RAM since all application code needs to be copied into RAM
  for execution") ([comparison of embedded filesystems](https://topic.alibabacloud.com/a/comparison-of-file-systems-in-embedded-systems-jffs2-yaffs-cramfs-romfs-ramdisk-ramfstmpfs_8_8_31786050.html)). This is the same fork DiscoBSD faces: compressed exec
  images and XIP userland (question 3) are mutually exclusive by
  construction, because a compressed image must land in RAM before a single
  instruction can run from it.
- UPX packs executables with an embedded decompression stub run at load, and
  is explicitly designed so "executables suffer no memory overhead... because
  of in-place decompression" ([UPX homepage](https://upx.github.io/)) -- the same shape of design as an a.out
  loader that decodes heatshrink-compressed images straight into the 96 KB
  user window, though UPX targets desktop-class ELF/PE loaders, not a
  from-scratch embedded a.out loader, so no code can be borrowed directly.
- No RetroBSD or FUZIX discussion of compressed-executable loading was found
  in this pass; both projects' constraints (RetroBSD's PIC32 MIPS target,
  FUZIX's 8/16-bit targets) differ enough in instruction set and toolchain
  that no direct precedent transfers -- this, like the RAM-swap tier above,
  would be new ground for the 2.11BSD-derived lineage.

## 3. Execute-in-place userland (text never touches RAM or swap)

### Requirements

- **Position-independent code (PIC):** text must run correctly regardless of
  its load address, since XIP means the flash address *is* the run address
  and multiple processes' images cannot all be linked to the same fixed
  origin the way a single-resident-process a.out currently is implicitly
  free to assume.
- **A relocating/no-MMU-aware loader:** on true MMU-less ARM, the standard
  mechanism is FDPIC (Function Descriptor PIC), which uses a dedicated
  call-clobbered register (r9 on ARM) to hold the GOT address, function
  descriptors in the GOT for indirect calls, and either a Data Section Base
  Register/Table scheme or full per-process GOT to let independently
  relocated text and data segments find each other without a real MMU
  ([MMU-less systems and FDPIC, MaskRay](https://maskray.me/blog/2024-02-20-mmu-less-systems-and-fdpic)). FDPIC's predecessor mechanisms --
  `-mid-shared-library` and XFLAT -- both existed for exactly this purpose
  and both were superseded or removed (`-mid-shared-library` left Linux in
  2022) because they were narrower and harder to maintain than FDPIC; this
  is a signal that a from-scratch no-MMU PIC scheme is a multi-year-maintained
  problem in mainline toolchains, not a weekend patch.
- **FUZIX's approach:** FUZIX does not appear to implement flash-XIP
  userland execution on ESP8266 or RP2040 in any material this research
  turned up -- its own wiki describes swapping whole process images to an SD
  card on constrained no-MMU targets, and states plainly that its banked
  no-MMU memory model does not yet support swap at all ([FUZIX Memory Management wiki](https://github.com/EtchedPixels/FUZIX/wiki/Memory-Management)). **This
  claim is not confirmed positively; it is an absence-of-evidence finding**
  -- a targeted read of the FUZIX RP2040/ESP8266 platform source (not done in
  this pass) would be needed to state with confidence that FUZIX has no XIP
  userland, only that no such design surfaced in search.
- **uClinux XIP with romfs/cramfs:** confirmed precedent. ROMfs stores files
  uncompressed and sequentially, letting application code execute directly
  from flash with only the writable `.data` section copied to RAM; cramfs
  is the mutually exclusive alternative (compressed, no XIP) already
  covered in question 2 ([embedded filesystem comparison](https://topic.alibabacloud.com/a/comparison-of-file-systems-in-embedded-systems-jffs2-yaffs-cramfs-romfs-ramdisk-ramfstmpfs_8_8_31786050.html)).
- **NuttX's approach:** confirmed and closest existing precedent to what
  DiscoBSD would need to build. NXFLAT is a binary format built for XIP from
  a filesystem, working over ROMFS (host-built, read-only) or XIPFS
  (writable, runtime-downloadable); "ROMFS allows you to execute programs
  in place (XIP) in flash without copying anything other than the `.data`
  section to RAM" ([NXFLAT, NuttX docs](https://nuttx.apache.org/docs/latest/components/nxflat.html), [NuttX FLAT Binary Format, NuttX docs](https://nuttx.apache.org/docs/latest/components/filesystem/nxflat.html)).

### Does Thumb-1 support PIC well enough?

Partially, at a real cost, not cleanly. FDPIC's own author-documented
trade-offs apply directly to ARMv6-M/Thumb-1:

- A dedicated register (r9) is permanently reserved to hold the GOT address
  and must be saved/restored across external calls -- on Thumb-1, which has
  only 8 freely usable low registers plus restricted access to the high
  registers, reserving one permanently is a proportionally larger loss than
  on a 32-bit-ISA, more-register-friendly core.
- Consecutive external calls require spilling r9 to a call-saved register
  before each PLT-style call, adding instructions per external call site.
- The offsetting benefit MaskRay reports -- "FDPIC often generates smaller
  code than `-mno-fdpic` on architectures where PC-relative addressing is
  expensive, such as ARM" -- is real but was measured on ARM generally, not
  isolated to Thumb-1/ARMv6-M specifically ([MMU-less systems and FDPIC, MaskRay](https://maskray.me/blog/2024-02-20-mmu-less-systems-and-fdpic)).

No source found in this pass gives a hard percentage code-size or
cycle-count delta for `-fpic`/FDPIC specifically on `-march=armv6-m`
(Cortex-M0+); GCC's own ARM Options documentation confirms `armv6-m` is a
recognized architecture and that Thumb-1 vs. Thumb-2 selection is automatic
from `-mcpu`/`-march`, but says nothing PIC-cost-specific ([GCC ARM Options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)). **Absence of a
measured number is itself the finding here**: before committing to XIP
userland, build a `-fpic`/FDPIC-enabled Thumb-1 toolchain and measure actual
code-size and cycle deltas on real DiscoBSD binaries -- do not plan this
measure on the strength of the ARM-general FDPIC claim alone.

Given the reserved register, the call-site tax, and the unverified Thumb-1-
specific magnitude, XIP userland is the correct long-term direction but the
highest-uncertainty, highest-effort item of the four questions: it requires
a working FDPIC (or equivalent DSBR/DSBT, NuttX-style) toolchain port that
does not exist for this target today, a relocating/XIP-aware loader, and
measurement before the code-size and per-call-overhead cost is known with
any confidence.

## 4. Other RAM/flash pressure reducers, and traps to avoid

- **bss elimination from the swap path.** bss is zero-filled by definition;
  it needs no storage in the swap image at all, only a record of its size so
  it can be re-zeroed on restore. If the current swap path writes bss bytes
  to flash, this is a straightforward, high-value cut with essentially zero
  added kernel complexity: skip bss in the swap-out write, zero it on
  swap-in. This is strictly a subset of the "compress data+stack" measure in
  question 1 taken to its logical limit (bss compresses to near nothing
  under heatshrink/LZ4 already; skipping it outright is cheaper than
  compressing it).
- **Stack sizing.** An oversized default per-process stack inflates the
  20-70 KB swap payload directly; static stack-depth analysis (or a
  measured high-water mark under a debug build) to right-size the default
  stack, rather than a conservative round number, shrinks every swap event
  proportionally with no runtime cost and no kernel-code growth.
- **Text reload-from-flash instead of swap-to-flash.** The fixed facts
  already describe swap payload as data+stack, not text -- consistent with
  classic Unix practice of never writing clean (unmodified) text pages to
  swap, since they can always be re-fetched from the executable image
  instead. Make this explicit and complete: any code path that could still
  copy text into the swap write (e.g., a debugger or a demand-paged text
  variant) should discard-and-refetch from the root filesystem's XIP window
  instead, never round-trip text through the flash swap region.
- **Read text through the raw XIP window, not through the Dhara-translated
  filesystem path.** If executables are stored under the FTL-managed root
  filesystem, reading them at exec time or after a discard goes through
  Dhara's logical-to-physical indirection layer, not straight XIP-window
  bus-speed reads. Dhara's own design goal is NAND wear-leveling and
  power-fail atomicity for logical-sector writes, not read-path
  transparency ([dlbeer/dhara](https://github.com/dlbeer/dhara)) -- reading executable text through it on every exec
  adds indirection cost the raw XIP window does not have. Where the
  toolchain allows it, keep a separate raw-flash region for XIP-executable
  binaries outside the Dhara-managed filesystem, mirroring how the swap
  region itself is already a raw 256 KB region rather than a filesystem
  file.
- **Shared libc text does not help this architecture the way it helps a
  true multi-process no-MMU system, and reaching for it anyway is a
  "robbing Peter to pay Paul" trap.** Classic no-MMU shared-library
  designs (uClinux's XFLAT/DSBR-DSBT, the "Quasistatic Shared Libraries and
  XIP" line of research) save RAM by letting several *concurrently
  resident* processes share one copy of libc's text ([Shared Libraries without an MMU](https://xflat.sourceforge.net/NoMMUSharedLibs.html); [Quasistatic Shared Libraries and XIP](https://www.researchgate.net/publication/220094160_Quasistatic_Shared_Libraries_and_XIP_for_Memory_Footprint_Reduction_in_MMU-less_Embedded_Systems)). This system
  keeps exactly one process resident by construction (the 96 KB window
  holds one image), so there is no second concurrent process in RAM to
  share libc text *with* at any instant -- the RAM-sharing benefit that
  justifies the complexity on uClinux does not exist here. What would carry
  over is libc text staying resident in the raw XIP flash window rather than
  being duplicated inside every a.out swapped to the flash swap region
  (shrinking the swap payload, not sharing concurrent RAM) -- but that
  benefit is the same one already captured by "text is never swapped, only
  reloaded" above, achieved without the FDPIC/DSBT machinery. Building a
  full no-MMU shared-library scheme to chase a RAM benefit this
  single-resident-process design cannot realize spends kernel complexity
  and Thumb-1's already-scarce registers (a fixed base-register tax on
  every function call, per FDPIC's own documented cost) for a win that
  belongs to a different architecture (concurrent multi-process residency),
  not this one.
- **Growing the compressed-swap RAM tier's window/index size is itself a
  robbing-Peter case.** heatshrink's optional index trades ratio and speed
  for RAM: a window_sz2 of 10 costs 1 KB, and the index add-on costs
  2^(window+1) bytes plus a temporary 512-byte stack allocation during
  construction ([heatshrink README](https://github.com/atomicobject/heatshrink/blob/master/README.md)). Every byte spent there is a byte not available
  to hold a third or fourth compressed image in the 100 KB tier from
  question 1 -- tune window size against the measured ratio/RAM trade on
  real DiscoBSD swap payloads, not against heatshrink's own general-purpose
  defaults.

## Ranked measures (at most four)

1. **Stop writing bss and text to the flash swap path; swap data+stack
   only, and re-zero bss / re-fetch text on restore.**
   Expected gain: eliminates the zero-fill and clean-text fraction of every
   swap write outright -- likely the single largest fixed cost cut available,
   since bss is common in buffer-heavy embedded processes and currently
   appears to be included in the "data+stack, 20-70 KB" swap payload.
   Cost: no new kernel subsystem; a size-tracking field per segment and a
   zero-on-restore path. Near-zero RAM and flash-code cost.
   Port effort, imperative: audit the current swap-out/swap-in path in the
   DiscoBSD RP2040 tree; confirm which segments it actually writes; strip
   bss from the write and add a zero-fill on restore; verify text pages are
   never dirtied and are always re-fetched from the root flash image rather
   than written back.

2. **Add a RAM-resident compressed swap tier ahead of the flash swap
   region, using heatshrink.**
   Expected gain: 2-3 compressed 33 KB images (more at the low end of the
   20-70 KB range) held in the 100 KB free-SRAM budget at a conservative
   1.43:1 ratio, cutting flash-swap writes -- and their 0.5 s cost and wear
   -- by roughly that same fraction of switches, at a decode/encode cost
   estimated in single-digit-to-tens of milliseconds (not directly
   measured on this core; flagged above).
   Cost: sub-kilobyte decoder/encoder RAM per heatshrink's published
   figures, a small (low-hundreds-of-bytes to ~1 KB) kernel code addition,
   and a fixed-size RAM pool carved from the 100 KB free budget.
   Port effort, imperative: vendor heatshrink (ISC license, ARMv6-M builds
   cleanly with GCC); wire it into the swap-out path as a first-attempt
   target ahead of the existing flash-swap code path, falling back to flash
   only when the RAM pool is full or an image's compressed size will not
   fit; tune window_sz2/lookahead_sz2 against measured ratios on real
   DiscoBSD process images before fixing the pool layout.

3. **Store root-filesystem a.out binaries heatshrink-compressed and
   decompress at exec time into the resident window.**
   Expected gain: roughly the same ~30-43 percent flash-space reduction the
   given gzip figure already establishes, applied to every binary on the 2
   MB flash, plus a proportional cut in FTL read traffic per exec.
   Cost: the same small decoder already vendored for measure 2 (shared
   code); 7-22 ms estimated added latency per exec of a 30 KB image
   (estimate, not measured); a build-time compression step in the image
   toolchain.
   Port effort, imperative: extend the a.out loader to detect a compressed-
   image flag and decode through heatshrink into the destination window
   instead of a straight copy; add the compression step to the
   cross-toolchain's image-build pipeline; this measure and XIP userland
   (below) are mutually exclusive per-binary by construction, so decide
   file-by-file which binaries take this path versus XIP.

4. **Execute-in-place userland via FDPIC/PIC Thumb-1, modeled on NuttX's
   NXFLAT/ROMFS/XIPFS design.**
   Expected gain: potentially eliminates text from the resident RAM window
   and the swap path entirely for XIP-eligible binaries -- the largest
   theoretical win of the four, but unquantified: no Thumb-1-specific
   code-size or cycle-cost number for FDPIC exists in the sources found
   here.
   Cost: a permanently reserved Thumb-1 register (r9-equivalent) for the
   GOT, per-call-site overhead on external calls, and a new relocating/XIP-
   aware loader plus a DSBR/DSBT-or-FDPIC-equivalent runtime -- none of
   which exists in the current DiscoBSD RP2040 tree.
   Port effort, imperative: build and measure a `-fpic`/FDPIC-capable
   `arm-none-eabi` Thumb-1 toolchain against real DiscoBSD binaries first,
   before committing to this design; if the measured code-size and per-call
   overhead is acceptable, implement a NuttX-NXFLAT-style loader and a raw
   XIP flash region (outside the Dhara-managed filesystem, per the point
   above) to hold XIP-eligible binaries; treat this as the last measure to
   attempt, after 1-3 have been implemented and measured, since its cost is
   the only one of the four not yet bounded by a real number.

## Measured follow-up: the ratio and the swap time the tier actually gets

Recommendation 1 above is implemented as `options SWAPRAM`; the full record
is in `zswap.md`, and the numbers that answer this report's open questions
are repeated here.

The ratio this report could not find for Thumb-1 swap payloads was measured
against real a.out data segments plus their zero-filled bss: 6.40x for sh,
29.93x for awk, 9.15x for tclsh over the whole image of data, stack, and u
area, at heatshrink window 9 and lookahead 8. That is well above the 1.43x
this report reasoned from whole-binary gzip, because a swap image is not a
binary: awk's data segment is 96.3 percent zeros once bss is counted.

Lookahead is the parameter that decides it. `get_lookahead_size` in
heatshrink's encoder caps a match at 2^lookahead bytes, so the lookahead 4
this report's parameter sketch assumed bounds matches at 16 bytes and drops
awk from 29.93x to 7.76x. It costs no RAM; only the window sizes the
encoder's buffer.

Against this report's figure of about 0.5 s for a 33 KB flash swap-out, the
tier's expected cost for sh's 12,416-byte image is 40 to 90 ms, estimated
from a measured host encode rate of 15.5 MB/s on the least compressible
realistic segment and a stated 100-to-200x host-to-M0+ per-byte ratio; the
same image on flash costs about 190 ms and a 4 KB sector erase per 1 KB
unit. The estimate is not a measurement and the board test in `zswap.md`
is what replaces it.

The codec's working RAM came out at the low end of this report's range:
1,040 bytes for the encoder and 590 for the decoder, with the search index
off, which also removes the 512-byte `do_indexing` frame this report flagged
as the robbing-Peter case. The index changes no output byte on these
payloads, so its cost buys only encode speed.

## Licenses referenced

- heatshrink: ISC (permissive) -- [atomicobject/heatshrink](https://github.com/atomicobject/heatshrink)
- LZ4: BSD 2-Clause -- [lz4.org](https://lz4.org/)
- LZO / miniLZO: GPLv2+ (copyleft; incompatible with in-kernel use in a
  BSD-licensed tree without isolation) -- [minilzo-rs](https://github.com/badboy/minilzo-rs)
- Zstd: dual BSD / GPLv2 -- [RFC 9659](https://www.rfc-editor.org/info/rfc9659/)
- UPX: GPLv2 with a runtime/stub exception for packed output -- [upx.github.io](https://upx.github.io/)
