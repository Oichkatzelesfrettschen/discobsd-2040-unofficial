# Swap tier model

A host model of the two swap tiers of the bare Pico kernel -- the
compressed RAM pool (SwapRAM) and the raw flash swap -- driven by a swap
trace of the board's compile workload and calibrated against the board's
admission counters. `results.md` ranks the design variants it measures.

Gate: `bmake MACHINE=rp2040 check-swap-tier-model` (`check.sh`), about 35
seconds. A host without `cffi`, `hypothesis` or `pytest` prints a
SKIP naming the module.

## What runs

`native.py` compiles these tree sources unmodified into one host shared
object and loads it with cffi in ABI mode, checking the structure sizes the
cdef assumes against the compiled ones:

| Source | Role in the model |
| --- | --- |
| `sys/arch/rp2040/rp2040/swapram_pool.c` | every pool allocation, free and trim |
| `sys/arch/rp2040/rp2040/swapram.c` | the admission cross-check (`test_swapram_crosscheck.py`) |
| `sys/arch/rp2040/dev/flash_swap.c` | the flash writer under a NOR model |
| `sys/kern/subr_rmap.c` | the swap-map ring and its next-fit placement |
| `sys/arch/rp2040/heatshrink/heatshrink_encoder.c`, `rp2040/hsx_decoder.c` | the codec swapram.c calls |

`swapram.c` and `subr_rmap.c` see the stand-in headers of
`sys/arch/rp2040/test/swapram/shim`, as that directory's evacuation test
compiles them. `glue.c` supplies the remaining kernel symbols and mirrors
the static `swap_exchange_tail_reserve()` of `sys/kern/vm_swap.c`.

`model.py` replays a trace: the admission rule of `swapram_reserve()`
(compressed data and stack plus `SR_WORST(USIZE)`, first fit, trimmed to
the compressed total at commit) over the real pool, then the flash path of
`swap_exchange_switch()` and `swap_image_reserve()` (exchange into the
incoming image's extent when the tail fits, else a fresh extent from
`malloc3_contiguous_next()`) over the real map. When the map cannot hold
an image the replay stops at that record with both images in place, as
`newproc()` refuses the fork and `sched()` defers the swap; such a replay
raises `model.Refused` instead of reporting counters. An image erases every
sector its bytes reach; `test_calibration.py` checks that count sector by
sector against `flash_swap_append()` writing the same images into the NOR
model.

Python libraries used: `cffi` (loading the C), `hypothesis` (stateful
property tests and the swapram.c cross-check) and `pytest` (the runner).

## Trace schema

One JSON object per line (`trace.py` states every field): `op` is `meta`,
`out`, `in`, `exit` or `mark`; an `out` record carries `pid`, `name`,
`fork`, `incoming`, the raw `data`, `stack` and `u` bytes, the compressed
`cdata`, `cstack` and `cu` bytes and `zero_sectors`, the image-relative
indices of its all-zero 4 KiB sectors. A `mark` with `what`
`snapshot` separates the boot prefix from the measured compiles. A board
capture with these fields replaces a generated trace unchanged:
`trace.load()` validates it and `model.run()` replays it.

## Calibration

Targets are the counter deltas of
`docs/research/trim-survey/board/swap/admission/fixed-sh.txt`, read at run
time through that directory's `delta.py`. `calibrate.py` fixes the
tolerance of each fitted counter; the bounds were written before the first
fit and were not widened.

The generator follows the processes the board runs: init runs `/etc/rc`
under `sh`, starts `update` (`etc/daemons`) and the console getty, whose
login shell runs the capture's commands. Each `cc -o d d.c; sync` forks the
`cc` script's shell (`distrib/rp2040/cc`), which forks smlrc, as, ld and rm.
A fork reserves an image for the child (`newproc()` in
`sys/kern/kern_fork.c`) and the parent's wait swaps the parent out with the
child as the incoming image (`sched()` in `sys/kern/vm_sched.c`), so every
spawn costs two swap-out attempts. Raw sizes put each program in its board
span bin; compressed sizes and the update schedule are fitted (`fit.py`).

Three trace models were fitted in turn:

1. The process structure above with update running at empty-window moments.
   Every fitted counter but `swapram_short_6k` within tolerance.
2. Model 1 plus update displacing a running pass (`sched()` swaps out a
   process in core for a second when the incoming one has been out for
   two), which accounts for the span-5 attempts beyond the login shell's 96.
3. Model 2 plus update swapped in ahead of a child forked a moment earlier
   (`sched()` takes the process out longest). It moves the flash and
   exchange split toward the board's 151 and 91.

Model 3 is the baseline. Residuals of the fitted vector (`FITS[1]`):

| Counter | Board | Model | Bound |
| --- | ---: | ---: | --- |
| attempts | 305 | 306 | 5 % |
| admissions | 63 | 63 | 10 % |
| flash swap-outs, exchange swap-outs | 151, 91 | 149, 94 | 10 % each |
| refused fragment, nospace | 196, 46 | 197, 46 | 10 %, 8 |
| RAM spans 2 and 5 sectors | 11, 52 | 12, 51 | 5, 8 |
| flash spans 5, 6, 7, 12 or more | 49, 192, 0, 1 | 50, 192, 0, 1 | 8, 5, 5, 1 |
| shortfall 1, 2, 3, 4 KiB | 172, 8, 2, 47 | 178, 10, 6, 48 | 15, 10, 10, 10 |
| shortfall 6, 7 KiB | 12, 1 | **0**, 1 | 10, 2 |
| swap sector erases | 1,412 | 1,417 | 3 % |

Held out, not used to choose sizes: refused want bytes -9.0 %, refused free
bytes -4.1 %, refused largest-run bytes -8.7 %, admitted compressed data
-4.5 %, admitted u bytes -8.6 %, admitted scan bytes +4.8 %, refused scan
bytes +0.2 %, swap write bytes +0.2 %, refusals with data fitting 221
against 183 (+20.8 %).

The erase count is not independent agreement: every refused image erases
its span, so 1,412 is 49 x 5 + 192 x 6 + 15 and 58.8 erases a compile
restate the span bins (`test_board_identities`).

**The boot prefix.** The records before the first snapshot are not
fitted, and they set the pool layout every compile starts from. Against
the "before" block of `fixed-sh.txt` the model's prefix makes 28 attempts
(board 33), admits 18 (25) and refuses 10 by fragment (8); refusals see
11,608 free bytes and a 5,880-byte largest run on average (8,572 and
4,674); RAM spans of 1, 2, 3 and 5 sectors are 0, 1, 5, 12 (1, 3, 3, 18)
and flash spans 0 of 3 sectors and 10 of 5 (2 and 6). The board sent two
3-sector images to flash at boot where the model keeps init's class in the
pool, and init's place there makes the hole below it. Variants 1, 2 and 3
act on that layout and inherit this disagreement.

**The miss.** No model produces the twelve refusals short by 5 to 6 KiB.
`refused_data_fits` agrees in count: on the board 59 refusals have
compressed data and stack larger than the largest run, as many as the
4 KiB and 6 KiB bins hold together (47 + 12); in the model 22. A refusal 5 to 6 KiB short of the
cc shell's request needs an extra login-shell-sized image in the pool while
the cc shell swaps; the board's 46 nospace refusals against 48 login-shell
waits allow two such admissions, but in the fitted layout the pool's two
free runs at that moment (3.7 KiB between update and init, 3.2 KiB above
the fork copy) cannot hold the login shell's 7.1 to 7.2 KiB request. The mechanism that admits those two
is not in the models; a board trace (`swapramdebug` pool lines) would name
it. `test_calibration.py` pins the miss set, so a change in either
direction fails the gate.

**Out of sample.** `main-sh.txt`, the capture with the shell before its
break-growth fix, is replayed with only the two `bin/sh` classes changed
from that capture's own byte sums (the cc shell one sector longer, the
login shell's compressed data 284 bytes shorter). Every fitted counter
lands within tolerance, including the split that flips from 196/46 to
240/2 (model 242/1): the login shell's request drops below the free bytes
at its wait, so its refusals become fragment refusals. That holds for
`FITS[1]`; the three other fits in `calibrate.FITS` calibrate `fixed-sh.txt`
alike but miss the main replay's 3 and 4 KiB bins, and one of them keeps
17 nospace refusals.

## Oracles

`test_docs.py` reads each expected value from the document that states it.

| Value | Source | Compared with |
| --- | --- | --- |
| 100,000 program-erase cycles per sector | `sys/arch/rp2040/doc/research/flash-traffic-per-workload.md` line 52, from the W25Q16JV datasheet revision G | `variants.ENDURANCE` |
| 4 KB sector erase 45 ms typical | the same file, line 55 | `include/swapram.h` ("a 45 ms sector erase") |
| 16 KiB pool | `sys/arch/rp2040/doc/research/zswap.md` line 7; flash-traffic line 223 | `compile/PICO/Config` `SWAPRAM_KB=16`, `Config.pool_kb` |
| window 9, lookahead 8 | `zswap.md` line 49 | `heatshrink/heatshrink_config.h` |
| u-area reservation 3,460 bytes | flash-traffic line 224 | `model.sr_worst(USIZE)` |
| kernel 128 KB, root 1536 KB, raw swap 384 KB at 0x101a0000 | `sys/arch/rp2040/doc/STORAGE.md` lines 94-96 | `dev/flash.h`, `Config.flash_kb`, `model.FLASH_SWAP_OFFSET` |
| pool placement | `zswap.md` (kernel build section) | `conf/kern.ldscript`, `STORAGE.md` line 8 |

`sys/arch/rp2040/doc/DATASHEET-INDEX.md` indexes the RP2040 datasheet and
lacks flash endurance and erase time; the W25Q16JV figures come from
the flash-traffic note, which cites the part's datasheet.

`zswap.md`, `STORAGE.md` and `kern.ldscript` agree on the pool placement
(`.swapram_pool` after ordinary kernel data and bss); `test_pool_placement`
and `test_storage_states_the_linked_placement` hold the two documents to
the script.

## Files

| File | Purpose |
| --- | --- |
| `check.sh`, `Makefile` | the gate |
| `native.py`, `glue.c` | build and load the shipped C |
| `engine.py` | the pool, map, NOR model and flash writer handles |
| `trace.py` | the schema and the generator |
| `model.py` | the replay and the variant knobs |
| `calibrate.py`, `fit.py` | targets, tolerances, fitted vectors; the search |
| `variants.py` | the variant table `results.md` reports |
| `mutants.py` | six mutants of the pool and flash writer the properties must reject |
| `test_*.py` | the gate's tests |
