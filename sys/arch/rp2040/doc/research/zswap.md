# A compressed RAM tier in front of the flash swap

`options SWAPRAM` puts a heatshrink-compressed pool of kernel RAM ahead of
the raw flash swap unit `fl1`. `kern/vm_swap.c` offers each swapout to the
tier first; an image the pool can hold stays in RAM and never touches QSPI
NOR, and an image it cannot hold takes the existing flash path with nothing
about it changed. The option is off in `compile/PICO/Config`, where the two
lines that turn it on sit commented out.

## What it costs

| Build | text | bss |
|---|---|---|
| `compile/PICO/Config` as shipped | 90,901 | 39,608 |
| plus `options SWAPRAM` and `options "SWAPRAM_KB=64"` | 95,049 | 108,008 |

`arm-none-eabi-size` on `compile/PICO/unix` after `bmake clean` and a full
rebuild each way. The option costs 4,148 bytes of text against a 128 KB
kernel flash region already 90,901 bytes full, and 68,400 bytes of bss:
65,536 for the pool, 1,040 for the encoder, 590 for the decoder, and 1,234
for the segment table, the per-process table, and the pool descriptor. The
RAM region at 0x20018000 is 157,696 bytes, so 108,008 leaves 49,688 free.
With the option off both numbers are byte-identical to the tree without this
change, because `arch/rp2040/conf/files.rp2040` marks all four new sources
`optional swapram` and `config` leaves them out of `OBJS` entirely.

## Codec parameters

heatshrink is vendored at upstream commit
`7d419e1fa4830d0b919b9b6a91fe2fb786cf3280` (v0.4.1 plus one commit, the
master branch head), ISC licensed, under `sys/arch/rp2040/heatshrink`.
`heatshrink_encoder.c` and `heatshrink_decoder.c` are byte-identical to
upstream; `heatshrink_config.h` is upstream's tuning point and carries the
port's values, and `heatshrink/compat` supplies the two standard headers a
`-nostdinc` kernel lacks beyond the three `dhara/compat` already provides.

`HEATSHRINK_DYNAMIC_ALLOC` is 0: every buffer is a file-scope static in bss,
because the kernel has no allocator for this and the kernel stack is 2,100
bytes -- USIZE is 3,072 and `struct user` measures 972 of it.

Window 9 and lookahead 8 come out of the sweep below, run by
`test/swapram/Makefile`'s `sweep` target over the a.out binaries the
distribution build leaves in `distrib/obj/destdir.rp2040`. The figure is the
whole-image ratio: data past the clean text, plus a 4 KB stack, plus the
3,072-byte u area.

| window | lookahead | encoder bss | sh | awk | tclsh |
|---|---|---|---|---|---|
| 8 | 4 | 1,554 | 4.65x | 8.25x | 5.59x |
| 8 | 6 | 1,554 | 6.06x | 19.15x | 8.32x |
| 9 | 4 | 3,090 | 4.53x | 7.76x | 5.34x |
| 9 | 6 | 3,090 | 5.96x | 18.42x | 8.02x |
| **9** | **8** | **3,090** | **6.40x** | **29.93x** | **9.15x** |
| 10 | 4 | 6,162 | 4.35x | 7.30x | 5.08x |
| 10 | 6 | 6,162 | 5.87x | 17.65x | 7.80x |
| 10 | 8 | 6,162 | 6.33x | 29.09x | 8.96x |
| 11 | 8 | 12,306 | 6.22x | 28.16x | 8.77x |
| 11 | 10 | 12,306 | 6.19x | 33.57x | 8.95x |

Lookahead is the lever, not window. `get_lookahead_size` in
`heatshrink_encoder.c` caps `max_possible` at 2^lookahead, so lookahead 4
bounds a match at 16 bytes, and a swap image is mostly bss and untouched
stack -- 96.3 percent zeros in awk's data segment. Coding a 10 KB zero run
in 16-byte matches takes 640 back-references where 256-byte matches take 40.
Lookahead costs no RAM at all: only the window sizes the encoder's buffer.
Window 9 beats 8 and 10 on all three images and halves the encoder's buffer
against window 10.

The brief named lookahead 4. That value is measurably wrong for this
content, by a factor of four on awk, at zero RAM saving, so the port ships
lookahead 8 and this paragraph is the disclosure.

`HEATSHRINK_USE_INDEX` is 0. The index changes no output byte -- every row
above is identical with it on -- and it costs 2,048 bytes of bss plus a
512-byte frame in `do_indexing`, measured by `arm-none-eabi-gcc
-fstack-usage`, against a 2,100-byte kernel stack. It buys 27 percent of
encode time on the awk image. The stack is the scarcer resource.

## Measured ratio, per segment

`make -C sys/arch/rp2040/test/swapram run IMAGE=...` at window 9, lookahead
8, index off, with a 4 KB stack:

| image | segment | raw | compressed | ratio | zeros |
|---|---|---|---|---|---|
| sh | data | 5,248 | 1,475 | 3.56x | 71.5% |
| sh | stack | 4,096 | 108 | 37.93x | 87.5% |
| sh | u | 3,072 | 358 | 8.58x | 87.6% |
| sh | whole image | 12,416 | 1,941 | **6.40x** | |
| awk | data | 13,780 | 234 | 58.89x | 96.3% |
| awk | whole image | 20,948 | 700 | **29.93x** | |
| tclsh | data | 1,992 | 535 | 3.72x | 74.5% |
| tclsh | whole image | 9,160 | 1,001 | **9.15x** | |

The data row is real: it is `a_data` read out of the a.out followed by
`a_bss` zeros, which is exactly what `exec_estab` leaves in core and what
`swapout` writes past the clean text. The stack and u rows are synthetic --
zeros with a plausible live top -- because neither is readable off the host,
so the whole-image ratio is a measurement of the data segment and an
estimate of the other two. Only the board settles them.

At 6.4x, the worst of the three, a 64 KB pool holds about 33 sh-sized
images; NPROC is 25.

## Speed

Host encode, measured, on this x86-64 host at `-O2`, worst segment first:

| segment | KB/s |
|---|---|
| sh data, 71.5 percent zeros | 15,472 |
| u area, 87.6 percent zeros | 164,602 |
| awk data, 96.3 percent zeros | 1,082,719 |

The board figure is an estimate, not a measurement: no Cortex-M0+ number for
heatshrink exists in this research pass, and this bench has no SWD probe to
time a kernel path with. Taking the host at 100 to 200 times the M0+'s
per-byte rate for branchy byte-at-a-time code -- single-issue Thumb-1 at 125
MHz with no cache against a wide out-of-order core at several GHz -- the
worst measured segment lands at 77 to 155 KB/s and the typical one at 0.8 to
1.6 MB/s. sh's 12,416-byte image then compresses in roughly 40 to 90 ms.

`ram-compression.md` (rpi notes repository) gives the flash path as about 0.5 s for a 33 KB image,
page-program dominated, which is 190 ms for the same 12,416 bytes, plus a
4 KB sector erase per 1 KB unit written and the wear that carries. The tier
is therefore expected to be two to five times faster on the slowest image
and an order of magnitude faster on a bss-heavy one, and to cost no flash
wear at all. Decode is faster than encode in LZSS by construction: it copies
matches rather than searching for them.

## Design

Three segments go to swap per swapout -- the data area past the clean text,
the stack, and the USIZE u area -- and the tier compresses each as its own
stream into one pool allocation, because swapin writes each to a different
address and a single stream would have to be cut at the same boundaries.

`swapram_out` reserves the encoder's worst case for the whole image before a
byte is compressed. heatshrink emits a literal as nine bits, so that bound
is the input plus an eighth plus four bytes per stream. A reservation that
succeeds therefore cannot run out part way, and `swapout` never has to
unwind a half-written image; `swapram_commit` returns the unused tail once
every length is known. The consequence is that the tier refuses an image
whenever no single free run holds the raw size, even when the compressed
image would have fit -- at which point `swapout` uses flash, which is
correct rather than optimal. The alternative, reserving a guessed fraction
and unwinding on overflow, trades that for a failure path in the middle of
a swapout, and the flash path is right there.

`swapram_pool.c` is a first-fit byte allocator over an address-ordered free
list held outside the pool, so an allocation never writes a header into the
bytes it hands out and a corrupt image cannot corrupt the allocator. It
names allocations by offset, takes no kernel header, and reports every
inconsistency by return value, which is what lets the host test link the
shipped file and run it under valgrind. `swapram.c` turns a returned -1 into
a panic.

Nothing on the tier's path sleeps or allocates. That is the invariant that
makes the file-scope encoder, decoder, and segment table safe: `swapout`
runs from `sched` in process 0 and from `newproc` in a forking process, and
the flash path sleeps inside `swap()` on `B_DONE` while this path has no
`geteblk`, no `sleep`, and no buffer, so the two contexts cannot interleave
inside it.

Swap accounting stays exact. A process the tier took never reaches
`malloc3`, so it holds no swapmap blocks, and `swapout` leaves `p_daddr`,
`p_saddr`, and `p_addr` zero to say so; `swapin` asks `swapram_present`
first and frees nothing from `swapmap` for such a process. `pstat` reading
`swapmap` through sysctl therefore sees a map with no entry for it, which is
the truth.

The u area is captured after the `u_ru.ru_nswap` increment, in the order the
flash path already used, because for the current process that increment
lands inside the USIZE bytes about to be copied.

Deepest stack on the path, from `-fstack-usage`: `swapout` 56,
`swapram_put` 64, `heatshrink_encoder_poll` 88, and the leaf calls under it
at 24 or less, so about 250 bytes against 2,100 available. Swapin is
shallower: `swapin` 48, `swapram_in` 96, `heatshrink_decoder_poll` 48.

## Enabling it

In `sys/arch/rp2040/compile/PICO/Config`, uncomment:

    options         SWAPRAM                     # RAM tier ahead of fl1
    options         "SWAPRAM_KB=64"             # kbytes of bss for the pool

then

    cd sys/arch/rp2040/compile/PICO
    ../../../../../tools/bin/config Config
    bmake clean && bmake all

`SWAPRAM_KB` defaults to 64 in `machine/swapram.h` if only `options SWAPRAM`
is given. The pool is bss, so every kilobyte comes out of the 49,688 bytes
the RAM region has left at 64 KB.

`config` gates the sources on a valueless option, because
`mkmakefile.c:240` matches `optional X` only against an option with no
value. A single `options SWAPRAM=64` would silently drop all four files from
the build, which is why the size is a separate line.

## Host tests

    cd sys/arch/rp2040/test/swapram
    make run IMAGE=<a.out>          # round trip, ratio, speed, allocator
    make valgrind IMAGE=<a.out>
    make sweep IMAGE=<a.out>

The default `IMAGE` is `distrib/obj/destdir.rp2040/bin/sh`, which exists
after `bmake MACHINE=rp2040 build`. The build runs `-Wall -Wextra -Werror`;
the two vendored codec files take `-Wno-implicit-fallthrough` alone, for a
switch upstream falls through without the attribute GCC 7 wants, and the
kernel build never sees that warning because `CWARNFLAGS` is `-Wall` and
`-Wimplicit-fallthrough` rides with `-Wextra`.

`make run` compresses each segment, expands it, and compares byte for byte;
a mismatch prints `ROUND TRIP FAILED` and exits nonzero. `codec: 4
termination checks pass` drives the guards that keep a truncated, corrupt,
or oversized stream from spinning the sink-and-poll loops forever, which is
the failure a kernel with interrupts enabled and no watchdog cannot recover
from. `pool: 26 checks pass` covers the live sequence -- reserve, trim, free -- plus coalescing on
both sides of a hole, first-fit reuse, grain rounding, the free-list slot
limit, and five misuse cases that must be refused rather than silently
corrupt the list.

## Board test sequence

`swapramdebug` in `swapram.c` is 1, so every swapout and swapin prints one
line naming the tier that took the image.

1. Flash a kernel built with the option on, and log the console:
   `minicom -D /dev/ttyACM0 -C zswap.log`.
2. Log in and confirm the pool exists by forcing one swap. The scheduler
   swaps when a second process wants core, so from the shell:

       sleep 30 &
       ls -l /usr/bin

   Expect a `swapram: pid N ram R -> C bytes, pool F free` line for the
   shell's image and a matching `swapram: pid N in C -> R bytes` when it
   comes back. `R` should be `p_dsize - p_tsize` plus `p_ssize` plus 3072.
3. Force many swaps with several jobs and a memory-hungry one:

       for i in 1 2 3 4; do sleep 60 & done
       awk 'BEGIN{for(i=0;i<20000;i++)a[i]=i; print "done"}'
       cc -c /usr/src/bin/echo/echo.c

   Every image should print `ram` while the pool has room. The `pool F free`
   figure must fall as images accumulate and rise again as they come back.
4. Force the fallthrough deliberately by shrinking the pool. Rebuild with
   `options "SWAPRAM_KB=8"` and repeat step 3: the first image or two print
   `ram`, the rest print `swapram: pid N flash R bytes, pool F free`, and
   the board keeps working. That is the path that proves the tier is
   optional rather than load-bearing.
5. Compare wall time. `time` the step-3 awk run with the option on and with
   it off; the option-on run should be the faster of the two, and the flash
   activity LED (`led_control(LED_SWAP)` in `dev/flash.c`) should stay dark
   during the `ram` swaps.

## What a regression looks like

| Symptom | Meaning |
|---|---|
| `panic: swapram: short expand` | a compressed image did not decode to the length swapout recorded; the codec or the pool offsets are wrong, and the image is lost |
| `panic: swapram: reservation overflow` | the encoder produced more than input plus an eighth; either `SR_WORST` is wrong or a segment length changed between `swapram_out` and `swapram_put` |
| `panic: swapram: image already resident` | a process was swapped out twice without an intervening swapin -- a leak in the tier's table, not in the pool |
| `panic: swapram: free` or `: trim` | the free list disagrees with an offset the table holds; the host test's misuse cases cover exactly these returns |
| `pool F free` falls and never rises | swapin is not freeing; every forked child that never runs would leak its reservation |
| every image prints `flash` from the first swap | the pool never initialized or `SWAPRAM_KB` is too small for one worst-case reservation |
| a process returns from swap with corrupt data, no panic | the expand wrote the right length to the wrong address; check the three destinations in `swapin` against `daddr + tsize`, `saddr`, and `&u0` |
| HardFault inside `heatshrink_encoder_poll` | the kernel stack overflowed; `USIZE` leaves 2,100 bytes and section 10 of BOOT-MAP.md reads the fault frame's PC out of `unix.dis` |

A regression that is not this tier's fault looks the same with the option
off. That is the first check on any of the above.

## Not verified

- Nothing here ran on hardware. Every board figure is an estimate.
- The stack and u-area ratios are synthetic, as marked above. A real u area
  holds a `struct user` with live pointers and an idle kernel stack; the
  model is zeros with a plausible live top.
- The Cortex-M0+ throughput rests on a 100-to-200x host ratio stated as
  reasoning, not measurement. The board test's step 5 is what replaces it.
- The flash path's 0.5 s per 33 KB comes from `ram-compression.md` (rpi notes repository) and was
  not re-measured here.
- `swapramdebug` has no runtime control; changing it needs a rebuild.
