# PicoCalc devices and memory tiers: a device-first decomposition

Evidence classes: source and datasheet reading, host arithmetic, and one
emulation cycle model (Unicorn with the Cortex-M0+ cost table of
`docs/research/rp2040-second-core-emulation/cycles.py`). No board, Renode or
kernel build ran for this note. Tree read at `origin/main` 32927ed0. Open pull
requests are cited by branch head: #637 kernel matrix (local `daa9228e`),
#615 SD swap (`450d8a65`), #626 lost swap device (`6f3a5c5f`), #624 PSRAM
`/tmp` (`8ed99d4a`), #622 scratch rebalance. Files of the notes repository
(`discobsd-2040-notes`, read at 5167978) carry their SHA-256 in
"Sources". Inferences are marked (I).

## Question and short answer

Owner decision D42a asks for the function and interconnection of every
PicoCalc device, the keyboard controller and any RAM it has included, and a
memory design decomposed from those devices. The immediate case is the 16 KB
compressed RAM tier (`options SWAPRAM`, `SWAPRAM_KB=16`) of PICOCALC_LEAN
(`daa9228e:sys/arch/rp2040/compile/PICOCALC_LEAN/Config:108-109`), with the
ESP-PSRAM64H answering at boot and without it.

- PSRAM answers: the tier should take no swap image. On the cycle model a
  shell-sized image costs 256 ms to enter the pool and about 1.4 ms to move
  through the PSRAM (section "Transfer cost per image"), and the PSRAM has no
  wear for the pool to save. The pool keeps exec argument spools and the
  codec workspace keeps packed-text decoding. Cost: an estimated 12 to 24
  bytes of text and no RAM (I).
- PSRAM absent: the LEAN UF2 on a bare Pico (`sys/arch/rp2040/rp2040/board.c:176`,
  one build for every board) or a PicoCalc whose U301 fails its probe. Swap
  then lands on the card's 0xb8 partition (#615) or `fl1`, and the pool stays
  in front for the wear and card-pull reasons below.
- The 16,384 bytes stay allocated in either state: the pool is a link-time
  NOLOAD section (`sys/arch/rp2040/conf/kern.ldscript:135-137`). Removing the
  tier from LEAN returns 15,920 bytes of RAM and 4,778 bytes of text as the
  orchestration record states them (BOARD.md entry 23, GCC 14.2.1; the record
  names no build method, and this note did not rebuild), at the price of
  unpooled flash swap on every PSRAM-absent board.

## Devices and their connections

The PicoCalc mainboard V2.0 carries a Raspberry Pi Pico module on two
20-pin headers. The Pico's RP2040 reaches six board devices directly; the
keyboard controller reaches four more, which the RP2040 sees only through
the controller's register protocol.

| Device | Part | Link to the RP2040 | Function and owner | Source |
| --- | --- | --- | --- | --- |
| MCU | RP2040 B2 on the Pico (U1), 2 MB W25Q16JV QSPI flash (U3) | itself | kernel, root `fl0`, swap `fl1` | `hardware-reference-coverage.md:86-88` |
| Keyboard controller | U201, printed CKS32F103Rx; vendor firmware names STM32F103R8T6 | I2C1 target 0x1F, GP6/GP7, about 96 kHz, polled (no interrupt line) | key matrix, both backlights, AXP2101, Pico enable, amplifier enable, headphone detect | `PICOCALC.md:500-531`, `:640-643`; notes `picocalc_keyboard/README.md:2` |
| PMU | AXP2101 (U101) | none: on the controller's second I2C bus (PB11 SDA, PB10 SCL) | battery charge, rails, power key | notes `conf_app.h:11-17`; `hardware-reference-coverage.md:146` |
| PSRAM | ESP-PSRAM64H, 8 MB, mainboard part U301 (`hardware-reference-coverage.md:149`) | PIO0 SM0, GP2-GP5, GP20 CS, GP21 SCK, quad at 31.25 MHz; pins shared with header J703 | swap device when it probes | `PICOCALC.md:122`, `:984-1013`; `psram_pio.h:135` |
| Panel | ST7365P, 320x480x18-bit frame memory, 320x320 glass | SPI1 GP10-GP15, 12.5 MHz | console, `/dev/lcdgfx` | `PICOCALC.md:125`, `:174-199` |
| SD socket | TF-03A (J303) | SPI0 GP16-GP19, card detect GP22, 12.5 MHz | `sd0`, dumps, swap with #615 | `PICOCALC.md:126`, `:645-660` |
| Audio | PWM slice 5, GP26/GP27, buffer NC7WZ16 (U501), class-D AW8010 x2 (U502, U503) | PWM out only | bell, `/dev/audio`; the controller gates the amplifiers | `PICOCALC.md:127`; `hardware-reference-coverage.md:151-152` |
| Headphone detect | PJ-327C (J501) contact HP_DET | none: controller pin PC12 | controller drives PA_EN (PA14) low while headphones are in | notes `picocalc_keyboard.ino:497,503-504,603-612` |
| USB-serial | CH340C (U702) behind WAS7227 switches U701/U703 and DIP SW701 | UART0 GP0/GP1 in the default switch position; the controller's UART1 and USB otherwise | `/dev/tty0`, the controller's firmware path | `PICOCALC.md:132-140`; notes `INDEX.md:83-89` |
| Pico micro-USB | RP2040 USB device | native | CDC-ACM console `/dev/ttyUSB0`, `picotool reboot -u` | `PICOCALC.md:139-140` |
| UART1 | GP8/GP9 | not routed to J702 or J703 | none | notes `INDEX.md:21-22` |

### The keyboard controller in detail

Part identity is open. The schematic prints CKS32F103Rx; the vendor README
names STM32F103R8T6; the LQFP64 pin map agrees with both, and the flash-size
letter (8, B, C or E) awaits the top mark or the flash-size word at
0x1FFFF7E0 (`hardware-reference-coverage.md:148`, `:235-236`, `:268-271`).
SRAM is 20 KB on every candidate: ST DS5319 lists 20 Kbytes for the x8/xB
devices (notes `st_ds5319...txt:21`, `:405`) and the CKS x8/xB sheet 20KB
(notes `cks_cks32f103x8_xb...txt:14`, `:258`). Flash is 64 KB for the R8
the README names (DS5319 line 20 gives 64 or 128 Kbytes for the x8/xB
family).

The firmware (`picocalc_keyboard.ino`, BIOSVERSION 0x16 in `conf_app.h:4`)
runs an Arduino Wire target. `receiveEvent()` (lines 109-212) keeps at most
two received bytes and serves a fixed register file (`reg.h:7-24`,
`reg.ino:3`, 15 bytes). A write (bit 7 set) reaches stored state in three
places only: BKL (0x05) and BK2 (0x0A) through `reg_set_value()` followed by
a backlight update, and OFF (0x0E) through the power-off countdown
`pmu_online`. A read or write of RST (0x08) resets the controller after a
delay. The largest reply is the C64 matrix register, ten bytes. No register
reads or writes the controller's SRAM, so the RP2040 can use none of the
20 KB; the two writable bytes each change a backlight.

A firmware written to export SRAM is possible in principle and is not
recommended. The bus runs near 96 kHz (`PICOCALC.md:640-643`), about 10 KB/s
before protocol overhead, and 400 kHz fast mode would raise that to about
40 KB/s (I); a register request already takes about 50 ms through the
shared poller (`PICOCALC.md:545-547`). The controller also controls the
Pico's power (PA13) and resets on a read of 0x08, so its memory would vanish
with a misdirected read. The AXP2101's four data-buffer registers (0x04-0x07,
notes `AXP2101_Datasheet_V1.4.txt:1312`) sit on the controller's bus and are
equally out of reach.

The tree drives six registers (`sys/arch/rp2040/dev/picocalc.h:45` sets
address 0x1F; `picokbd.c:53-55` sets the clock near 96 kHz):

| Register | Kernel use | Source |
| --- | --- | --- |
| 0x01 VER | read once at probe for the `kbd0:` version line | `picokbd.c:114` |
| 0x09 FIF | polled, 16 ms apart idle and 2 ms apart while events arrive | `picokbd.c:115-120` |
| 0x05 BKL | `machdep.picocalc.lcd_backlight`, read and write | `picokbd_req.h:37` |
| 0x0A BK2 | `machdep.picocalc.kbd_backlight`, read and write | `picokbd_req.h:38` |
| 0x0B BAT | `battery` and `charging`, read only | `picokbd_req.h:39,43` |
| 0x0E OFF | `poweroff`, write after `sync()` | `picokbd_req.h:40,45` |

Requests share the poller through the mailbox of `picokbd_req.c`, three
exchanges at most (`picokbd_req.h:59`) and a one-second deadline
(`picokbd.c:567`); RST (0x08) is never read (`PICOCALC.md:530-531`).

Two consequences for the kernel follow from the register file: headphone
insertion is invisible to the RP2040 (the controller mutes the speakers by
itself), and battery state reads 0 both for an empty cell and for no cell
(`PICOCALC.md:561-564`).

## Every RAM reachable from the RP2040

| Store | Size | Path | Current owner (kernel) | Idle | Bandwidth, latency | Wear, volatility |
| --- | ---: | --- | --- | ---: | --- | --- |
| Striped SRAM0-3 | 256 KB | 0x20000000, four banks | 192 KB user window, 58 KB kernel data, 6 KB u areas (`RP2040.ld:47-60`) | LEAN: 14,648 B (`daa9228e:CAPACITY.md:105`, GCC 14.2.1) | one word per bank per clock | none; lost at reset |
| SRAM4/SRAM5 | 8 KB | 0x20040000 | USB transmit ring, 8 KB (`RP2040.ld:62-76`); #622 splits it 4 KB ring, 4 KB tile atlas | 0 | as SRAM | none |
| USB DPSRAM | 4 KB | 0x50100000, 32-bit peripheral, execute-never | endpoints to 0x5010023F, Dhara page 0x50100240-0x5010063F, codec workspace 0x50100740-0x50100B4F (1,040 B), flash scratch 0x50100F00-0x50100FFF (`kern.ldscript:159-190`) | 256 B at 0x50100640 and 944 B at 0x50100B50 (I: no endpoint buffer above 0x5010023F, per the comment at :161) | as SRAM, no set/clear aliases (datasheet 4.1.2.7, line 28833) | none |
| XIP cache | 16 KB | 0x15000000 when the cache is disabled (datasheet line 8969) | the flash cache the kernel executes through | 0 | n/a: disabling it uncaches every kernel instruction fetch | none |
| Watchdog scratch | 32 B | WATCHDOG_SCRATCH0-7 | swap cursor in scratch0 (`machdep.c:271-307`), reset reason | most of 28 B (I) | register | survives a watchdog reset |
| PSRAM | 8 MB | PIO0 frames of 64 B, no memory mapping | swap (main); #624 splits it 2 MB swap and 6 MB `/tmp` (`8ed99d4a:psram_xfer.h:32`) | 0 when up | 64-B frame: CS low 4.45 us write, 4.72 us read; at most 5.2 us masked (`picocalc-psram.md:278-281`, simulator); about 12 MB/s (I) | none; ICC 40 mA active, ISB 200 uA (`hardware-reference-coverage.md:229`; notes PSRAM sheet line 876) |
| Panel frame memory | 345,600 B (320x480x18 bit) | SPI1, RAMWR/RAMRD (2Eh) | lines 0-319 the console scroll area; lines 320-479 the bottom fixed area (`st7365p.c:384`) | 160 lines, 115,200 B of 18-bit pixels (I: unlit on the 320-line glass, `hardware-reference-coverage.md:242`) | write 66 ns per bit clock, read 150 ns (notes ST7365P lines 2564, 2568); about 1.2 MB/s write and 0.6 MB/s read of payload (I) | none; lost with panel power |
| Controller SRAM | 20 KB | none (section above) | firmware | 0 usable | n/a | n/a |

The XIP cache, the controller SRAM and the AXP2101 buffer drop out on
mechanism. The PSRAM is the only large store, and it is a block device
rather than memory: the RP2040 maps only the QSPI flash into its XIP window,
so kernel data structures cannot live there. The panel's hidden 115 KB is
the one other volatile store above a few kilobytes; whether the glass leaves
lines 320-479 dark and whether RAMRD returns them over four-wire SPI are
both unmeasured.

## Transfer cost per image

The swapper moves three segments per process: the data area past the clean
text, the stack, and the 3,072-byte u area (`swapram.c:4-6`).
`swapram_reserve()` runs a counting encode over data and stack before it
knows whether the pool can take the image (`swapram.c:478-494`), then
`swapram_put()` encodes all three again into the reservation (`:530-550`).
Swapout and swapin run to completion with no context switch
(`swapram.c:15-19`), so their time is time no process runs.

The cycle model ran the tree's encoder (`heatshrink_encoder.c`, window 9,
lookahead 8, index off) and decoder (`hsx_decoder.c`) built for Cortex-M0+
at `-Os` by GCC 16.2.0, the flags `COMPILE_C` gives both objects
(`compile/PICO/Makefile:555-559`). `build.sh` takes those flags from the
`bmake -n` expansion of the two rules in `compile/PICO`, compiles `bench.c`
with them (warnings as errors) and records them on the `# cflags` line of
`run-output.tsv`. Inputs: the data segment (a_data plus
a_bss zeros) of the local `bin/sh/sh` (SHA-256 prefix 12d2c1314ce7cc5d,
text 33,852, data 2,432, bss 7,176) and `usr.bin/awk/awk` (fb9e44f2955ffbb0,
48,772 / 2,272 / 10,796), built 2026-11-13; the synthetic 4 KB stack and
3 KB u area of `sys/arch/rp2040/test/swapram/swapram_test.c:391-398`; 4 KB of
zeros; 4 KB of random bytes. The model charges one cycle per instruction
fetch, so a board running the encoder from the XIP cache can only be
slower (I). The harness links the kernel's own `bzero()` and `bcopy()`
(`machdep.c`, copied verbatim into `bench.c`; `build.sh` fails when either
body or `UNALIGNED` differs from `machdep.c`), because the encoder's
`memset()` and `memcpy()` reach them through `dhara/compat/string.h`. A
per-function run of the same model attributes 95.5 percent of the sh counting
pass to `heatshrink_encoder_poll()` and 1.4 percent to those two routines;
`run.py` keeps totals only. `picocalc-device-first/run-output.tsv` holds the
rows, and `selftest.sh` shows the gate failing on an aliased output
directory, a drifted `bzero()` and a failed round trip.

Correction: an earlier harness supplied byte-loop `bzero()` and `bcopy()`
where the kernel runs four words per iteration. `heatshrink_encoder_reset()`
clears its 1,024-byte buffer once per encode pass, so the byte loop cost
about 7,000 extra cycles (0.06 ms) per pass, and the kernel's `bcopy()` took
the same cycle count to within 0.2 percent. Counting plus writing passes, in
cycles (before, after):

| Segment | Before | After |
| --- | ---: | ---: |
| sh data+bss | 26,678,632 | 26,664,840 |
| awk data+bss | 6,248,264 | 6,234,568 |
| 4 KB random | 96,390,533 | 96,376,453 |

Decode cycles did not move, since the decoder calls neither routine. The
first harness compiled with a subset of the production options (no
`-fno-strict-aliasing`, `-fshort-enums`, `-fno-delete-null-pointer-checks`,
`-mabi=aapcs`, `-mfloat-abi=soft`, section options or configuration
defines). The rebuild under the full derived set changed no cycle count in any
column of any row, so every figure above stands for the production flags. The
derived times move by 0.3 ms or less: sh pool swapout 256.2 to 255.9 ms,
refusal 114.5 to 114.4 ms, awk swapout 92.7 to 92.4 ms and refusal 32.8 to
32.6 ms; the 4 KB incompressible rows (771 and 386 ms) and the 3.1 ms sh
swapin do not change at the printed precision.

| Image (raw bytes) | Pool swapout | Pool refusal | Pool swapin | PSRAM each way | `fl1` swapout |
| --- | ---: | ---: | ---: | ---: | ---: |
| sh (16,776 B, 2,001 compressed) | 255.9 ms | 114.4 ms | 3.13 ms | 1.4 ms (I) | 257 ms typical, 2.2 s maximum |
| awk (20,236 B, 680 compressed) | 92.4 ms | 32.6 ms | 3.40 ms | 1.6 ms (I) | 257 ms typical, 2.2 s maximum |
| 4 KB incompressible | 771 ms | 386 ms | 1.15 ms | 0.3 ms (I) | 51 ms typical |

Pool swapout sums the data and stack counting and writing passes and one
u-area pass; a refusal is the two counting passes the flash path then
follows. PSRAM figures multiply 64-byte frames by the simulator's 5.2 us.
`fl1` figures are datasheet arithmetic (I). A swap image owns an
erase-aligned run of `fl1`, and `fl_raw()` hands a `B_SWAPIMAGE` write to
`flash_swap_append()`, which erases each 4 KB sector as the write reaches it
and programs 256-byte pages (`sys/arch/rp2040/dev/flash.c:986-1018`,
`sys/arch/rp2040/dev/flash_swap.c:20-53`). Both images span five sectors
(the measured mean over refused images is 5.8, `swap-tier-designs.md:61`);
each costs one erase of 45 ms typical and 400 ms maximum and sixteen
programs of 0.4 and 3 ms (W25Q16JV, notes lines 3003-3005). SD has a bus floor of 10.7 ms for the sh image at
12.5 MHz plus card busy time, which no run has measured.

Prediction against measurement: `zswap.md:123-136` estimated 40 to 90 ms
for an sh image of 12,416 bytes and a tier two to five times faster than
flash, from host encode rates and a stated 100-to-200x host-to-M0+ ratio.
Per pass, the model puts the sh data segment at 90 KB/s, inside that
note's 77 to 155 KB/s band for its worst segment, so the per-byte estimate
for data holds. Three things the estimate left out make the difference:
the kernel encodes data and stack twice (count, then write); this build's
sh data segment is 9,608 bytes against that note's 5,248, from a different
build of sh; and the u area runs at 113 KB/s and the stack at 532 KB/s per
pass, well under the 0.8 to 1.6 MB/s that note took as typical. Scaled to
the 5,248-byte segment, the model gives about 160 ms (I). The encoder
spends its time in the unindexed match search, which keeps the codec at
1,040 bytes (`ram-compression.md:476-480`). The pool's value on flash is
wear, not speed; its decode at 20 to 35 cycles per byte is the fast half.

Calibration: `rp2040-second-core-emulation/build.sh` reproduced its input
hashes and exits 1 on `expected-output.txt`: that file records the
vendored decoder at 386.8 cycles per byte, and the tree's word-buffered
`hsx_decoder.c` (12e6d190) now gives 67.7 including CRC-32. The recorded
output is stale; this note leaves it as it stands.

## Device properties that decide placement

| Store | Latency for a 17 KB image | Wear | Capacity | Removable | Present on |
| --- | --- | --- | --- | --- | --- |
| Pool (SRAM) | 256 ms in, 3 ms out (model) | none | 16 KB: 3 images at `compile/PICO/Config:64`'s count, 8 of this sh at 2,001 B | no | every kernel with SWAPRAM |
| PSRAM | about 1.4 ms each way (I) | none | 8 MB, or 2 MB with #624 | no (mainboard part) | PicoCalc, when the probe passes |
| SD 0xb8 | unmeasured, floor 10.7 ms | card FTL, unmeasured | up to 65,532 blocks (#615) | yes | PicoCalc with a prepared card |
| `fl1` | 257 ms typical | 100,000 cycles per sector; the hottest swap sector binds the board (`swap-tier-designs.md:68-70`) | 384 KB | no | every board |
| Panel lines 320-479 | about 15 ms in, 30 ms out (I) | none | 115 KB (I) | no | PicoCalc, unverified |

## Tier design per board state

| State | Swap images | 16 KB pool | Exec spools | Buffer cache | `/tmp` |
| --- | --- | --- | --- | --- | --- |
| A. PicoCalc, PSRAM up (normal: U301 is a mainboard part) | PSRAM directly | no images; spools only | pool, then PSRAM | 4 buffers (`machparam.h:56`) | PSRAM minor 1 (#624) where a mount slot is free; LEAN keeps `NMOUNT=2` (D44), so `/tmp` and an SD partition compete for one slot |
| B. PicoCalc, PSRAM failed, card with 0xb8 | card, pool in front | images, `init` first (I) | pool, then card | 4 | root |
| C. PicoCalc, PSRAM failed, no usable card | `fl1`, pool in front | images | pool, then `fl1` | 4 | root |
| D. Bare Pico on the LEAN UF2 | `fl1`, pool in front | images | pool, then `fl1` | 4 | root |
| E. State B after a card pull (D9) | pool, then a compressed `fl1` tier of about 32 KB | images | pool | 4 | root |

State A rests on the costs above: the pool adds about 255 ms per swapout and
saves no wear, and a refused image still pays its 114 ms counting pass
before the PSRAM write. State B keeps the pool for removal, not speed:
#626 spares pool-backed images when the card leaves and panics when process
1's image was on it (`6f3a5c5f`, "init lost with the swap device"), so an
admission order that keeps `init` in the pool turns that panic into a
recoverable loss (I; `swap-tier-designs.md:571-579` reaches the same
requirement). States C and D are the wear case the tier was built for; the
reservation and LRU designs of `swap-tier-designs.md:195-389` still apply
there. State E is owner decision D9, unbuilt.

## The 16 KB tier on PICOCALC_LEAN

Bypass when PSRAM answers. `swapram_admit(0)` is not that bypass: `sr_admit`
gates exec spools as well as images (`swapram.c:192`, `:472`), and
`swapram_service()` reopens admission after any evacuation request
(`:793-796`), so one `machdep.swapram_evacuate` write would undo it. The
bypass is a separate condition in `swapram_reserve()` alone, true while
`swapdev` names the PSRAM (`psram.c:124` sets it before `binit()` and the
swap open, `init_main.c:70,113,145`). Spools keep the pool, the codec
workspace keeps packed-text decoding (`exec_hsaout.c:244-253`), and
`swapram_evacuate()` finds no image to move.

| Option | Kernel text | Kernel RAM | State A swapout (sh) | States B-D |
| --- | ---: | ---: | --- | --- |
| As built: pool in front everywhere | 0 | 0 | 256 ms (model) | pool in front |
| Bypass on PSRAM (recommended) | +12 to +24 B (I) | 0 | about 1.4 ms (I) | pool in front |
| Bypass, and the pool extends the buffer cache when PSRAM answers | +150 to +400 B (I) | 0 | about 1.4 ms (I) | pool in front |
| No SWAPRAM in LEAN | -4,778 B | +15,920 B | about 1.4 ms (I) | unpooled `fl1` or card |

The compile-out row is the recorded figure quoted above; the other rows are
unbuilt estimates. Under the bypass the 16,384 bytes hold only
spools while PSRAM answers. The buffer-cache option would carve up to 15
more 1 KB buffers from the pool at `binit()` time (`init_main.c:244-270`;
`buf[NBUF]` and `bufdata` are static, `machdep.c:130-134`), turning the
five loops and one range check bounded by `&buf[NBUF]` in `ufs_bio.c`
(lines 375, 419, 462, 594, 666, 716) into a variable bound. Its
benefit is a hypothesis: fewer root and SD reads per compile, which the
`STORAGE_STATS` read counters measure before any build.

One change helps every pooled state: the counting pass runs to its end
even when its running length already exceeds the pool's largest free run
(`swapram.c:478-487`). Passing that run as a cap ends a doomed count early;
the sh refusal would fall from 114.4 ms toward the fraction of the image
encoded before the cap (I), for an estimated +20 to +40 bytes of text.

## Prior decisions against this design

- D9 (after a card pull, pool plus a small compressed flash tier of about
  32 KB, research and review first): state E. Its writer pays the encode
  cost above, so it suits rarely swapped images; `swap-tier-designs.md:309-342`
  holds the compressed-writeback design.
- D11 (per-board flash swap split): state A leaves `fl1` idle, and only
  states C and D need all of it. One LEAN binary serves every state, so the
  split is a per-board layout decision through the loader stamp
  (`PICOCALC.md:900-908`), not a kernel option.
- D33 (UART buffers against every RAM kind): the scratch banks are full in
  every configuration; the USB DPSRAM holds about 1.2 KB unclaimed on
  SWAPRAM USB kernels (I), the one reachable spare that needs no new device;
  the XIP cache, PSRAM and controller SRAM cannot hold kernel data.
- D42-D45: D42's "swap is fast anyway" holds for PSRAM, and the model puts
  the pool slower than the PSRAM by two orders of magnitude, which answers
  the D42 follow-up with a bypass. D43 (panel descriptor default) leaves
  the PSRAM probe unchanged, since `psramprobe()` tests the board identity
  (`psram.c:104`). D44 keeps LEAN at two mounts while #624 gives the
  PSRAM `/tmp` a third slot on the full kernel (`8ed99d4a`,
  `PICOCALC_PSRAM/Config:57`), so on LEAN with a card the rc mount of
  `/dev/psram1` and an SD mount take the same slot. D45's lean root image
  needs no change from this design.

## Falsifiers and the measurements that decide

1. Pool against PSRAM on the board: `pc_micros()` around
   `swapram_reserve()` plus `swapram_put()`, and around the
   `psramstrategy()` calls of one swapout, for the same sh image in one
   PICOCALC_LEAN run. The bypass stands while the pool time exceeds the
   PSRAM time; a ratio below 1 refutes it. The model predicts about 180.
2. Model fidelity: the pool timer alone against the model's 256 ms for
   this sh build; a board figure under half the model's flags the cost
   table.
3. Bypass outcome: a 24-compile `STORAGE_STATS` run in state A with and
   without the bypass; the prediction is SwapRAM admissions 0, swap writes
   all on PSRAM, compile wall time lower.
4. Buffer-cache option: root and SD read counts for the same run at 4 and
   19 buffers; no change in reads rejects it.
5. Panel store: write a pattern to lines 320-479 and read it back with
   RAMRD while watching the glass. A lit row or a mismatched read removes
   it from the table.
6. Controller identity: top mark of U201 or the flash-size word, as
   `picocalc-first-power-up.md` orders.

## Owner decisions

1. PICOCALC_LEAN with PSRAM: bypass the pool for swap images (recommended),
   keep it in front as built, or remove SWAPRAM from LEAN.
2. The 16 KB while PSRAM answers: spools only (recommended until
   measurement 4), or extend the buffer cache.
3. Cap the counting pass at the largest free run, on every SWAPRAM kernel.
4. State B admission order: keep process 1 in the pool when swap is on a
   removable card.
5. Board time for measurements 1, 2 and 5 under the hardware opt-in.
6. PICOCALC_LEAN with PSRAM and a card: the second mount slot goes to the
   PSRAM `/tmp` or to the SD partition, or LEAN takes `NMOUNT=3` at the
   1,224 bytes of bss the orchestration record measured for it (BOARD.md
   entry 14).

## Anomalies recorded

- `swapram.c:59-61` says reset-time clearing leaves the pool untouched;
  `kern.ldscript:131-133` and `locore0.S:112-122` clear from `_sdata` to
  `u_end`, which includes the pool. The ldscript comment matches the code.
- `rp2040-second-core-emulation/expected-output.txt` predates the
  word-buffered decoder (Calibration, above).

## Sources

Notes repository files (SHA-256):

| File | SHA-256 |
| --- | --- |
| `docs/picocalc/code/picocalc_keyboard/picocalc_keyboard.ino` | 7b2826a9957511bf4377b2b0086be99f533d8003447d68fb37973c163027f394 |
| `docs/picocalc/code/picocalc_keyboard/reg.h` | 29adb634aeb0632a2def6149e14146a1281c916d6da9a4d320291cd6e2d8fc6e |
| `docs/picocalc/code/picocalc_keyboard/conf_app.h` | 69d58f3c004859863b39ec6055a9201d9c74a18c277c007c6b0f3f9fd783d466 |
| `docs/picocalc/code/picocalc_keyboard/README.md` | a432dd8bf47a4b9fc7e94de84410539054710ba6a9f6730cb20f6a3219494299 |
| `docs/picocalc/INDEX.md` | b3e9f89aff46dd2ec6f9137f848fe9067bc5899f0db2447eb756efaedceee49d |
| `docs/picocalc/clockwork_Mainboard_V2.0_Schematic.txt` | 03086d8bda88a3bf51ed8a0dcce17159ccb033de4a5c7dc21d1309941a8cc5f6 |
| `docs/picocalc/parts/st_ds5319_stm32f103x8_xb_datasheet.txt` | cdd8831bec325178a8eb74a54daca84f923fcd3b05a00267f70e0bc99a2b29eb |
| `docs/picocalc/parts/cks_cks32f103x8_xb_datasheet_zh_v1.15.txt` | 6af48314a130dae9ac77df591f62a412f99ea1fced4ce788a076190f77f0e191 |
| `docs/picocalc/ST7365P_SPEC_V1.0.txt` | ec6e56b6da66825dcee8a220233d8a063bbe01e793ed1337c567fa7ca99d6fca |
| `docs/picocalc/AXP2101_Datasheet_V1.4.txt` | 827b58042f1c7716870cf6f2fbc064479ecc2affad8f4b5ab5fb5f4cd27730e8 |
| `docs/picocalc/esp_psram64_esp_psram64h_datasheet_en_v1.1_wayback.txt` | 336647dc678c72421930e3f92329e7ea108e489b2f7b6cebd8c331cebad155f5 |
| `docs/rp2040/rp2040-datasheet.txt` | 8de3ce7e60ea13e8b4c6e4fc45aafb11744039f2f1fcc2062de23e8e7cdb181f |
| `docs/rp2040/W25Q16JV_Datasheet.txt` | 02533ae522f2ae25bd9ef3afcef6dec1389e9847a05262887bec94459be298b4 |

Orchestration records outside the tree: `BOARD.md` entry 23 (SHA-256
8845dafc0be7f178916f97ed2832e0c47cecb598426175acfe3db3226d5a7199) and
`DECISIONS.md` D9, D11, D33, D42-D45 and D42a
(4cdccb89204296010478c794089dc8bede39bfbeb306d94d07e821669c5da309).

Published sources: ClockworkPi PicoCalc repository and wiki,
https://github.com/clockworkpi/PicoCalc (the keyboard firmware, schematic
and wiki pages mirrored in the notes repository above).

Reproduction: `PYTHON=python3 sh docs/research/picocalc-device-first/build.sh OUTDIR`
builds the workload and runs the model; it needs arm-none-eabi-gcc, Unicorn
and Capstone, takes the a.outs from a built tree (`SH_AOUT` and `AWK_AOUT`
name others) and exits 1 unless its output equals `run-output.tsv`, whose
first lines record the compiler and the input hashes.
