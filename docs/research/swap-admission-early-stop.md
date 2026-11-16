# Admission and early stop for the SwapRAM pool

The SwapRAM pool decides whether to take a swap image by compressing it:
`swapram_reserve()` runs a counting pass of the heatshrink encoder over the
data and stack segments, to their end, before it asks the pool for room
(`sys/arch/rp2040/rp2040/swapram.c:457-505`). An image that will not fit, or
that does not compress, costs the whole pass anyway: 402 ms for 4 KB of
random bytes under the cycle model. The owner asked for the strategies other
systems use to avoid paying for that, decomposed, and measured on this tree
(decision D117, answering D96 and D97). This note surveys eight systems,
reduces their mechanisms to seven rules, measures each rule on the kernel's
own encoder, and recommends one set.

Recommendation: a running-ratio exit inside the counting pass (rule R,
from 512 consumed bytes), the byte budget the pool can actually grant
(rule E1) and the free-run pre-check (rule E0). Measured with GCC 14.2.1:
+56 B of text for both exits, 0 B of RAM; a random 4 KB image is refused in
50.5 ms instead of 402.2 ms; an admitted shell image pays 828 cycles, about
7 us; none of the 281 program segments of the build is refused falsely.

E0 and E1 change no outcome: they refuse earlier what the pool would refuse
anyway. Rule R is a new admission policy. Today `swapram_reserve()` admits
any image whose compressed segments plus the u area's worst case fit the
largest free run, so 4 KB of random bytes, which encodes to 4,608 B, enters
an empty pool. Under R it goes to the swap device instead; the section
"What rule R changes" costs that trade.

## Method

The measurement extends the Cortex-M0+ cycle model of
`docs/research/picocalc-device-first-memory.md` (pull request #644, branch
`docs/picocalc-device-first` at 3e406650) with the rules as C functions in
`swap-admission-early-stop/rules.c`. `build.sh` compiles them and the
vendored encoder with the options `bmake -n` prints for the PICO kernel's
`heatshrink_encoder.o`, takes `bcopy` and `bzero` from
`sys/arch/rp2040/rp2040/machdep.c` at build time, and links them at
0x20000000. `run.py` runs each rule under Unicorn with the cost table of
`docs/research/rp2040-second-core-emulation/cycles.py` (one cycle per fetch,
no bus contention), so every figure is a lower bound on silicon time at
125 MHz. The tree, the compiler (arm-none-eabi-gcc 14.2.1, Debian
`15:14.2.rel1-1`) and the input programs all come from `origin/main` 268483f7
built on one Linux host; `run-output.tsv` heads with the input hashes.

The reference verdict for a ratio rule is the full counting pass: refuse
when the whole input compresses above 15/16 of its length, the threshold of
rule E2 in `d2ffbd83:docs/research/memory-devices.md`. A rule refuses
falsely when it refuses an input the reference admits, and misses when it
admits one the reference refuses. Budget rules (E0, E1) are exact against a
different oracle, whether the compressed image fits the largest free run,
and are reported by what they save.

Inputs: the shell and `awk` data segments and the four synthetic inputs of
the device-first model (stack, u area, zeros, random), four synthetic inputs
built to separate the rules (an incompressible head before a compressible
tail and the reverse, a 16-symbol alphabet, a 64-byte random pattern
repeated), and the data and bss of every OMAGIC a.out the build installs,
281 segments, 1,269,704 B compressing to 72,809 B. The reference refuses
none of the 281 segments: the build's initial data segments all compress
well. The corpus therefore measures false refusals only; misses come from
the synthetic inputs alone, a denominator of two: only the random 4 KB input
and the 16-symbol 4 KB input are refused by the reference; both head and tail
mixtures and the repeated pattern are admitted by it.

Calibration: `selftest.sh` passes the unmodified build against the committed
`sizes.tsv`, `run-output.tsv` and `corpus.tsv`, and requires a build with
`-DRULES_MUTANT_NO_STOP`, which removes both exits from `enc_stop()`, to
fail; `run.py` rejects it because a 1 KB budget no longer stops the random
input early.

Prediction recorded before the run, from `d2ffbd83:docs/research/memory-devices.md`
(GCC 16.2.0): E2 refuses 4 KB of random bytes after 512 bytes in 48 ms
against 386 ms for the full pass, and the shell never trips it. Measured
(GCC 14.2.1): rule R from 512 refuses it in 50.46 ms against 402.23 ms, a
ratio of 0.125 against the predicted 0.124, and the shell passes every R
rule. The prediction held. The full counting passes run 3 to 4 % slower than
#644's golden figures (shell 13,744,928 against 13,333,267 cycles; random
50,278,234 against 48,191,895), which is the compiler difference; the
shell's data segment is 9,608 B in both builds.

## How other systems decide

| System, source | Rule | Class |
| --- | --- | --- |
| Linux zswap, `mm/zswap.c:893-931` at aebb7274 | compress into a page-sized buffer; output of a page or more is "compress poor" and stored raw, or rejected when writeback is off | output budget |
| Linux swap layer, `mm/page_io.c:128-236` at aebb7274 | a zero-filled folio sets a bit in the swap zeromap and is never compressed or written. `Documentation/admin-guide/mm/zswap.rst:96-101` at the same commit still describes same-value pages inside zswap; `zswap.c` no longer holds that code, so the zero case moved out of zswap and other repeated values are compressed like any page | same-filled (zero only) |
| Linux zram, `drivers/block/zram/zram_drv.c:334-350`, `:2239-2310` at aebb7274 | a page of one repeated machine word is stored as that word alone, testing the first word against the last before the loop; a page whose compressed size reaches the allocator's huge class is stored raw and flagged for writeback or recompression (`zram.rst:310-314`, `:377-402`) | same-filled; output budget |
| btrfs, `fs/btrfs/compression.c:614-643`, `:1246-1295`, `:1398-1460`, `:1543-1610` at aebb7274 | before compressing, sample 16 B of every 256 B; compressible if the sample's halves repeat, if fewer than 64 byte values occur, or if 64 or fewer values cover 90 % of it; incompressible if 200 or more do; otherwise Shannon entropy at or below 65 % of 8 bits passes and at or above 80 % fails | sampled statistics |
| macOS XNU, `osfmk/vm/vm_compressor.c:4840-4960` at f6217f89 | the codec gets a budget of the slot's free space, at most a page less 4 B; -1 means incompressible and the page is copied raw and counted; 0 means one 32-bit value, kept in a hash table with no segment space | output budget; same-filled |
| LZ4, `lib/lz4.c:751`, `:1113-1146` at 6b589e80 | the match search widens its step by one every 64 failed probes (`LZ4_skipTrigger` 6), so incompressible input runs faster rather than stopping; `dstCapacity` stops the encoder when output would overflow | search acceleration; output budget |
| OpenZFS in FreeBSD, `sys/contrib/openzfs/module/zfs/zio.c:1783-1799`, `zio_compress.c:118-150`, `module/zstd/zfs_zstd.c:460-512` at freebsd-src 88e7371d9 | a block must save 12.5 %, so the codec's budget is seven eighths of the input and a miss stores it raw; for zstd level 3 and up on blocks of 128 KB and more, an LZ4 pass runs first as a probe and a zstd-1 pass second, and the block is stored raw when both fail | output budget; probe pass |
| DragonFly HAMMER2, `sys/vfs/hammer2/hammer2_strategy.c:935-1030` at 94448f62 | LZ4 into half the block; after eight consecutive failures on an inode, try only every eighth block until one succeeds | output budget; refusal memo |
| OpenBSD hibernate, `sys/kern/subr_hibernate.c:525-560`, `:697-850` at 8849c5d46 | runs of free pages are encoded as a count and skip the compressor; the rest goes through zlib; a search of the file for "incompress", "abort" and `Z_BUF_ERROR` finds no incompressibility exit | same-filled (free pages) |
| heatshrink, `README.md:122-125` at 7d419e1 | no early exit; an optional index of 2^(window+1) bytes speeds the match search | search acceleration |

The local reference trees hold no compressed swap or compressed memory in
FreeBSD `sys/vm`, NetBSD `sys/uvm` or DragonFly `sys/vm` (`rg -il compress`
over each; the two matches, `vm_radix.c` and `pmap_tlb.c`, are unrelated);
FreeBSD, OpenBSD and DragonFly are shallow clones, so the dates of these
mechanisms rest on the files, not on history.

## The rules on this encoder

| Rule | Mechanism here | Added text (GCC 14.2.1, -Os) | RAM | Refused random 4 KB | Admitted shell, extra | False refusals: corpus; synthetic | Misses (of 2) |
| --- | --- | ---: | ---: | --- | --- | --- | --- |
| Full count, today | `enc()`, the counting pass of `sr_encode()` | 116 B (baseline) | 0 | 402.23 ms | 0 | not applicable | not applicable |
| E0 free-run pre-check | refuse before encoding when the largest run is below the u area's worst case plus a minimal segment | +8 to +16 B (I): a call to the existing `swapram_pool_largest()` (28 B in `rules.c`) and a compare | 0 | the whole pass, when the pool is that full | 217 cycles for 17 runs | exact | exact |
| E1 budget | exit once output passes the largest run less the u area's worst case | +12 B | 0 | 100.96 ms at a 1 KB budget; nothing at 4 KB: the random input does not fit (4,608 B), but the budget fires only after the encoder has drained its whole output, so the refusal (-2) arrives with the full 402.23 ms spent | 378 cycles at a 4 KB budget | exact against fit; 15 and 1 corpus segments exceed 1 KB and 4 KB budgets | exact |
| R running ratio | exit once the consumed input reaches N and the output exceeds 15/16 of it | +44 B (+56 B with E1) | 0 | N=256: 25.23 ms; N=512: 50.46 ms; N=1024: 100.96 ms | 828 cycles at every N | N=256: 3 of 281, 1 synthetic; N=512: 0, 1 synthetic; N=1024: 0, 0 | 0 |
| P prefix probe | a separate pass over the first N bytes, judged on its own | +38 B and a second call | 0 | N=256: 24.97 ms; N=512: 50.15 ms | N=256: 6.58 ms; N=512: 17.52 ms | N=64: 19; N=128: 7; N=256: 3; N=512 and up: 0; synthetic 1 to 3 | 0 |
| H byte entropy | order-0 entropy of the first N bytes, refuse at 6.4 or 7.0 bits | +156 B (104, a 36 B log helper, a 16 B table) | 512 B of counts: the codec workspace before the encoder starts, or the stack | 0.08 ms (N=256), 0.21 ms (N=1024) | 0.07 to 0.15 ms | 0 of 281; synthetic 1 or 2 | 1: the 16-symbol input |
| S same-filled | one repeated 32-bit word, stored as that word | +44 B, plus a one-word image record (+40 to +80 B, I) | 4 B a slot (I) | not applicable (0.11 ms on 4 KB of zeros against 1.64 ms) | 30 cycles | none: no corpus segment is same-filled | not applicable |
| E4 refusal memo | skip the pool for a slot's next swapouts after a ratio refusal | +20 B (I) | 1 B a slot | all of it, from the second refusal | 0 | no corpus case: the memo needs a process refused twice | not applicable |

Sizes come from `arm-none-eabi-nm -S` on `rules.o` and on builds with one
exit alone (`sizes.tsv`): `enc_stop` is 116 B with no exit, 128 B with the
budget, 160 B with the ratio and 172 B with both. Rows marked (I) are
estimates from comparable code.

What the inputs separate:
- **Incompressible head, compressible tail** (1 KB random, 3 KB zeros; the
  whole compresses to 1,181 B): R from 256 or 512, every prefix probe and
  entropy refuse it falsely. R from 1024 admits it, because the zeros after
  the head pull the running ratio under 15/16 before the check fires. A
  program's data segment of initialized tables followed by bss has this
  shape: `cribbage`, `m4` and `tclsh` are the three corpus segments R from 256
  refuses.
- **Compressible head, incompressible tail**: every rule admits it, and so
  does the reference (1,179 B for 4 KB).
- **Sixteen symbols**: byte entropy is 4.0 bits, so H admits it, but
  heatshrink spends 9 bits on every literal and finds few matches long enough
  to pay, so the output (4,193 B) exceeds the input. Entropy measures what an
  entropy coder could save; heatshrink is LZSS without one. Every
  encoder-based rule refuses it.
- **A repeated 64-byte pattern**: compresses to 108 B; the 64-byte prefix
  probe refuses it because it never sees a repeat.

R dominates P: the two refuse at the same cost, because both have encoded the
same bytes, but R's work on an admitted image is part of the pass it
replaces, while P's is spent twice. H is cheap and blind to what this encoder
cannot do. S and E4 address cases the corpus does not contain.

## What rule R changes

For a process whose data segment is the 4 KB random input, with the 4 KB
synthetic stack and the 3 KB u area, on a board without the PSRAM (states B
to D of `d2ffbd83:docs/research/memory-devices.md`):

| | Today: admitted to an empty pool | Rule R from 512: refused, written to `fl1` |
| --- | --- | --- |
| Encoder time at swapout | counting pass 402.2 + 7.9 ms, writing pass about the same (I: #644's golden puts the two passes within 0.02 % of each other), u area 27.8 ms: about 0.85 s | 50.5 ms, then nothing |
| Device time at swapout | none | 12 blocks: three 4 KB erases and 48 page programs, about 154 ms typical and 1.3 s worst case (DS arithmetic, W25Q16JV 45 ms and 400 ms per erase, 0.4 ms and 3 ms per page) |
| Pool held | about 5 KB of 16 KB: 4,608 B of data (512 B more than raw), 108 B of stack, the u area | none |
| Flash wear | none | three sector erases per swapout of this process |
| Swapin | decode, about 2.4 ms (#644 golden: data 1.15, stack 0.68, u area 0.56 ms) | an XIP read of 12 KB, under 1 ms (I) |

R turns about 0.85 s of encoding into about 0.2 s of encoding and flash
programming, and leaves 5 KB of pool for images that compress, at the price
of three erases each time such a process swaps out. With the card swap of
#615 the device write is a few milliseconds and the trade is better. A
PicoCalc kernel with the PSRAM does not bypass the pool today:
`PICOCALC_PSRAM` enables SWAPRAM, `sr_admit` starts at 1
(`rp2040/swapram.c:129`), `swap_image_reserve()` calls `swapram_reserve()`
before it allocates device backing (`sys/kern/vm_swap.c:266-277`), and
`psramprobe()` only sets `swapdev` (`dev/psram.c:124`). The D116 policy that
the pool takes no images on those kernels is a separate change the tree
lacks, and rule R's prerequisite there: until it lands, the rule runs on
PSRAM kernels and sends the images it refuses to the PSRAM.

## Recommendation

E0, E1 and R from 512 in `swapram_reserve()` and `sr_encode()`: about +56 B
in `sr_encode()` plus the E0 compare, 0 B of RAM, against the +40 to +90 B
that slice M1 of `d2ffbd83:docs/research/memory-devices.md` estimated. R
from 512 matches the reference on all 281 segments and refuses only the
synthetic head-then-tail input falsely, which then goes to the device: slower
but correct. R from 1024 removes that false refusal at twice the refusal
cost (101 ms). E5, the encoder index, still waits on 2 KB of RAM no
configuration has, and E3, a single pass into the run, still waits on a
board measurement of the pool time.

## Residual owner decisions

1. **The early-stop set for the pool.**
   - E0, E1 and R from 512 (recommended): 50 ms refusals, no corpus false
     refusals, +56 B and a compare.
   - E0, E1 and R from 1024: no false refusal on any input measured, 101 ms
     refusals.
   - E0 and E1 alone: exact, +12 B and a compare, but an incompressible image
     that fits still costs the full 402 ms.
2. **What "worth pooling" means.**
   - 15/16 of the input (recommended): the threshold the measurements use,
     close to heatshrink's own break-even.
   - 7/8, OpenZFS's 12.5 % saving: refuses more marginal images and keeps
     the 16 KB pool for the ones that compress.
   - 1/2, HAMMER2's half block: keeps only images that halve, which every
     corpus segment does (the worst, a 16 B segment, compresses to 44 %).
3. **Same-filled images.**
   - Leave them to the encoder (recommended): no corpus segment is
     same-filled, and a 4 KB zero segment already costs only 1.64 ms.
   - Add rule S with its image record: 0.11 ms instead of 1.64 ms for such a
     segment, for about +90 B.

## Unverified and not run

- Board: no rule ran on silicon; the cycle model counts one cycle per fetch
  and no XIP cache miss, so silicon times are higher.
- Runtime images: the corpus is each program's initial data and bss; images
  after heap growth, and stacks other than the synthetic one, are not
  measured.
- The kernel integration of each rule (where the u-area worst case and the
  largest run enter `sr_encode()`) is sized from standalone functions.
- Misses rest on two synthetic incompressible inputs, the random and the 16-symbol 4 KB.

## Sources

Primary sources fetched on 2026-11-16, pinned by commit:
- Linux, https://github.com/torvalds/linux at
  aebb7274476403040d582c61478df8f4a7bf9cd3: `mm/zswap.c`, `mm/page_io.c`,
  `drivers/block/zram/zram_drv.c`, `fs/btrfs/compression.c`,
  `Documentation/admin-guide/mm/zswap.rst`,
  `Documentation/admin-guide/blockdev/zram.rst`.
- XNU, https://github.com/apple-oss-distributions/xnu at
  f6217f891ac0bb64f3d375211650a4c1ff8ca1ea: `osfmk/vm/vm_compressor.c`.
- LZ4, https://github.com/lz4/lz4 at
  6b589e8053bd9c524138d78adec78fcaefa35cab: `lib/lz4.c`.
- heatshrink, https://github.com/atomicobject/heatshrink at
  7d419e1fa4830d0b919b9b6a91fe2fb786cf3280: `README.md`.

Local reference trees (read-only): freebsd-src 88e7371d9 (OpenZFS),
DragonFlyBSD 94448f62, openbsd-src 8849c5d46, netbsd-src 1e8e727e461.

Commands: `PYTHON=python3 sh docs/research/swap-admission-early-stop/selftest.sh SCRATCH`
after `bmake MACHINE=rp2040 build`; it needs
arm-none-eabi-gcc 14.2.1 to match the committed files, bmake, Unicorn 2.1.4
and Capstone 5.0.7.
