# PicoCalc PSRAM as swap: protocol, PIO program, driver and evidence

The PIO program is written from
the two datasheets named below; no existing PSRAM driver source was read.
Nothing here ran on a board. `sys/arch/rp2040/doc/PICOCALC.md` carries the
evidence table.

## Finding

The PicoCalc's ESP-PSRAM64H is a quad-I/O device on GP2-GP5 (SIO0-3), GP20
(CS) and GP21 (SCK), which is no hardware SPI pin set, so state machine 0
of PIO0 clocks it. `psram_pio.h` holds a 19-word program, `psram_xfer.c`
moves frames of at most 64 bytes through it, and `psram.c` makes the device
a block device, major 3, that replaces `fl1` as the swap device when the
part identifies itself at boot. The driver links only into the
PICOCALC_PSRAM kernel configuration, selected by `BOARD=PICOCALC_PSRAM`; the
PICOCALC kernel carries none and links to the size it had before the driver
existed. A host simulator executes the shipped program words against a timing
model of the part and passes 766 checks; eight mutants of the program and
driver fail it. PICOCALC_PSRAM adds 2,096 bytes of text, 56 of data and 144
of bss to PICOCALC; PICO, PICO_UART and PICOCALC link to the same text and
data as before.

## Sources

| Source | Identity |
| --- | --- |
| ESP-PSRAM64 and ESP-PSRAM64H Datasheet, Espressif, Version 1.1, October 2020, 24 PDF pages (21 numbered) | `https://www.javanelec.com/stfiles/getappdocument/1/true/84288ad5-f467-49de-9d1e-2bf12d2d4fcc.pdf`, SHA-256 `ab68624cdecfc6a768260747bec907aa20940f14129f269b58c3b2a52acd0605` |
| the same, Version 1.0, June 2018, 26 PDF pages | `https://cdn-shop.adafruit.com/product-files/4677/4677_esp-psram64_esp-psram64h_datasheet_en.pdf`, SHA-256 `88a9b9535c699a97dc8c6fd123cb58829c39812dbdd641067d4ed9988602a011` |
| RP2040 Datasheet, build in `discobsd-2040-notes/docs/rp2040/rp2040-datasheet.pdf` | section pages through `sys/arch/rp2040/doc/DATASHEET-INDEX.md` |

Espressif's own download URL
(`https://www.espressif.com/sites/default/files/documentation/esp-psram64_esp-psram64h_datasheet_en.pdf`)
returned an HTML page to a command-line client, so the two mirrors above are
the retained copies. Version 1.1 changed only the marking appendix
(release notes), and the values compared across the two (tCEM, tCPH, tCHD,
tSP, the maximum clocks and the command table) agree. The Espressif sheet
is the one the board's part number names. Its timing, current and opcode
rows equal those of Lyontek's LY68L6400 Rev. 1.2, whose command table adds
one note the Espressif sheet lacks ("Linear burst is prohibited", on the
SPI-mode 02h write at 133 MHz, a command the driver does not issue), and
page 5 of the V1.1 PDF carries a hidden text layer from Lyontek's
LY68S3200, so the Espressif sheet was built on a Lyontek template. AP Memory's APS6404L-3SQR shares the opcodes and the Read
ID bytes (MF 0x0D, KGD 0x5D) but prints different limits (tCHD 3.0 ns,
tCPH 18 ns, tACLK 5.5 ns), so it is a family member, not this part's
specification; `docs/research/hardware-reference-coverage.md` holds the
comparison.

Page references below read `doc N/21 (pdf P)`: the page number printed on the
sheet, then the page in the PDF file.

## Protocol facts the driver uses

| Fact | Source |
| --- | --- |
| 64 Mbit, 8M x 8, 3.3 V for the "H" part, 133 MHz maximum clock; a burst that crosses a page boundary has an 84 MHz maximum | Section 1, doc 1/21 (pdf 4); Table 1-1 |
| The device powers up in serial mode; after power the part needs 150 us and a user reset before use | Sections 5.3 and 3, doc 6/21 and 3/21 (pdf 9, 6) |
| Page size 1 KB, address bits 9:0; the default is linear burst across the page boundary, wrap-32 only after command 0xC0 | Sections 4 and 5.2, doc 4/21 and 6/21 (pdf 7, 9) |
| Command table: Fast Read Quad 0xEB has a serial command, quad address and 6 wait cycles in serial mode, and quad command, address and 6 wait cycles in quad mode; Quad Write 0x38 or 0x02 has no wait cycles; Enter Quad 0x35 exists in serial mode only; Exit Quad 0xF5 in quad mode only; Reset Enable 0x66, Reset 0x99 and Read ID 0x9F (serial mode only) | Section 5.4 truth table, doc 6/21 and 7/21 (pdf 9, 10) |
| Reset is the pair 0x66 then 0x99 in separate frames, and any other command between them abandons the reset; the pair exists in serial and quad format | Section 8, Figures 8-1 and 8-2, doc 14/21 (pdf 17) |
| Read ID: command 0x9F, 24 address bits, then manufacturer 0x0D, known-good-die byte, and a 48-bit EID; a known-good die reads 0b0101_1101 (0x5D), a failing die 0b0101_0101 (0x55); the output has no wait cycles | Section 6.4, Table 6-1 and Figure 6-7, doc 11/21 (pdf 14) |
| Read data is valid tACLK after the falling edge of CLK; quad data carries the high nibble first, SIO0 holding the lowest bit of each nibble | Sections 6.1 and 7.1, Figures 6-3 and 7-1, doc 8/21 and 12/21 (pdf 11, 15) |
| A command ends only when CE# rises; a pending refresh is blocked while CE# is low. A read needs a CE# hold longer than tACLK + tCLK after the last rising edge to latch its last data | Section 5.5, doc 7/21 (pdf 10) |
| tCEM, CE# low pulse width, 8 us maximum; tCPH, CE# high between bursts, 50 ns minimum; tCSP 2.5 ns; tCHD 20 ns; tSP and tHD 2 ns; clock period 7 ns minimum; tCH and tCL 0.45 to 0.55 of the period; tACLK 2 to 6 ns; tKOH 1.5 ns; tHZ 6 ns | Table 10-5, doc 17/21 and 18/21 (pdf 20, 21) |
| The operating range is -40 to 85 C and Table 10-5 gives one tCEM for it; this revision has no separate hot-temperature value | Table 10-2, doc 16/21 (pdf 19) |

RP2040 facts: the instruction encodings, side-set, autopull and autopush
rules, FIFO depths and joining, and the input synchronizer latency come from
datasheet sections 3.4 (page 321), 3.5.1 (page 331), 3.5.3 and 3.5.4
(pages 334 and 335) and 3.5.6.2 and 3.5.6.3 (pages 341 and 342); the
register offsets from section 3.7 (page 368); function select 6 routes a
GPIO to PIO0 (section 1.4.3, page 12); PIO0 is bit 10 of RESETS.

## The PIO program

Side-set is two non-optional bits at GP20 (bit 0, CS) and GP21 (bit 1,
SCK), leaving three delay bits. IDLE is CS high, SCK low (value 1), LOW is
CS low, SCK low (0), HIGH is CS low, SCK high (2). OUT, IN and SET map to
GP2-GP5. The words, from `sys/arch/rp2040/dev/psram_pio.h`:

    00: pull block              side 1     0x88a0   fence: next frame starts at a word
    01: out x, 10               side 1     0x682a   x = nibbles to send, less one
    02: out y, 5                side 1     0x6845   y = turnaround passes, less one
    03: out isr, 10             side 1     0x68ca   ISR = nibbles to read
    04: out null, 7             side 1     0x6867   control word spent; autopull
    05: set pindirs, 15         side 1     0xe88f   drive SIO0-3
    06: out pins, 4             side 0     0x6004   send: CS falls, nibble out
    07: jmp x--, 6              side 2     0x1046   send: SCK rises
    08: set pindirs, 0          side 0     0xe080   release SIO0-3, SCK falls
    09: mov x, isr              side 0     0xa026
    10: mov isr, null           side 0     0xa0c3
    11: jmp !x, 17              side 0     0x0031   no read: finish
    12: jmp x--, 13             side 0     0x004d   x = nibbles to read, less one
    13: nop                     side 0     0xa042   turnaround: SCK falls
    14: jmp y--, 13             side 2     0x108d   turnaround: SCK rises
    15: in pins, 4              side 0     0x4004   read: SCK falls, sample
    16: jmp x--, 15             side 2     0x104f   read: SCK rises
    17: nop                     side 0 [1] 0xa142   hold CS low past tCHD
    18: jmp 0                   side 1     0x0800   CS high

One transaction is one chip-select frame. The control word the first FIFO
entry carries is, from bit 31: nibbles to send less one (10 bits), turnaround
passes less one (5), nibbles to read (10), 7 unused bits. Data nibbles
follow as 32-bit words, first nibble in bits 31:28. A serial-mode byte goes
out as eight nibbles of one bit each on SIO0, and the ID is read from
bit 1 of each nibble, so one program serves the serial commands before quad
mode and every quad frame after it.

Each loop pass is two instructions. A pass drives data and the falling
SCK edge in one instruction and the rising edge in the next, so data
changes half a clock before the device samples it (tSP) and holds half a
clock after (tHD). The state machine runs at clk_sys / 2 (62.5 MHz), so
SCK runs at 31.25 MHz, a 32 ns period, which is 23 percent of the 133 MHz
maximum and below the 84 MHz limit for a burst that crosses a page.

### Where the read samples

The device launches nibble k after the falling edge that follows rising
edge n+k-1, where n is the clock count before the first data clock, and
holds it until the next falling edge plus tKOH. Valid time therefore runs
from fall + tACLK(max) = fall + 6 ns to the next fall + 1.5 ns. The output
register stage, the two input synchronizer flip-flops and the input register
stage run on clk_sys, not on the state machine's divided cycle: the clock
divider gates execution only (datasheet 3.5.5) and the synchronizers are
timed against CLK_SYS (3.5.6.3, the warning on bypassing them). An edge
leaves the state machine one clk_sys cycle (8 ns) before it reaches the pad
and an IN sees the pad as it stood three clk_sys cycles (24 ns) earlier, 32 ns
in all, which is one SCK period at `PSRAM_CLKDIV` 2. An IN executed in the
falling-edge instruction of a pass therefore lands c - s after the rising edge
that nibble is sampled on, with c the state machine cycle (16 ns) and s the
sum of the output and input pad delays. The window then leaves 2c - s - 6 ns
of margin against the device launching late and s + 1.5 ns against it
holding too short:

| s (ns) | margin from tACLK (ns) | margin to the hold edge (ns) |
| --- | --- | --- |
| 0 | 26 | 1.5 |
| 6 | 20 | 7.5 |
| 10 | 16 | 11.5 |
| 26 | 0 | 27.5 |

`out_ns` and `in_ns` in `tests/psram/pio_sim.py` are the two delays; neither
is in a datasheet, and 6 ns is an assumption that the board run replaces.
The sample point is 2c - s - 6 from one limit and s + 1.5 from the other, so
a longer pad delay trades the first margin for the second; no s from 0 to 26
ns leaves the window.

The datasheet counts the stages as "a register stage between each state
machine and the pin mapping logic, and a register stage between the input
mapping logic and each state machine" without saying which clock advances
the two register stages. `Pio(out_gated=..., in_gated=...)` in the
simulator moves either one onto the state machine cycle, which lengthens the
sample delay by 8 ns each at `PSRAM_CLKDIV` 2 and moves the sample point
toward the tACLK edge. At s = 6 ns the shipped driver reads correctly under
all four placements:

| output stage | input stage | margin from tACLK (ns) | margin to the hold edge (ns) |
| --- | --- | --- | --- |
| clk_sys | clk_sys | 20 | 7.5 |
| clk_sys | state machine cycle | 12 | 15.5 |
| state machine cycle | clk_sys | 12 | 15.5 |
| state machine cycle | state machine cycle | 4 | 23.5 |

The shipped placement is clk_sys for both. If the board shows the stages on
the state machine cycle, the margin from tACLK falls to 4 ns and a pad delay
sum above 10 ns breaks the read; the divider or the pass count then decides
the retune.

The first IN executes after the rising edge one clock past the one the first
nibble is sampled on, so the turnaround loop runs the wait cycles plus two
passes before it and the frame ends two clocks after the last data clock; the
control word stores that as `wait + 1` after the less-one. A read
count is a multiple of
eight nibbles because autopush pushes whole words; the driver rounds a read
up to four bytes and discards the tail.

### Frame length against tCEM

A frame of N bytes is 2 + 6 + 2N clocks (command, address, data) for a write
and 2 + 6 + (6 + 2) + 2N' for a read, with N' the byte count rounded up to
four. At 32 ns per clock and 64 bytes that is 4.4 us for a write and 4.72 us
for a read in the simulator, 59 percent of the 8 us limit. 128 bytes would
hold CS low for about 8.9 us, past the limit, and is one of the mutants.
`psram_chunk_len()` cuts a transfer at 64-byte block boundaries, which are
also 1 KB page boundaries, so no frame crosses a page.

### Why the frame runs from RAM with interrupts masked

The state machine stalls, rather than aborts, when a FIFO runs dry, which
stretches CS low. A TX underrun is only slow, but an RX overrun stalls a
read on a rising edge and shifts the sample window, so the driver treats
`FDEBUG.RXSTALL` as a failed frame and reads again, twice at most. Interrupts
are masked for a frame (5.2 us in the simulator, at an access cost of six
system clocks) and the loop executes from `.ramfunc`, so an XIP cache miss
cannot stretch it. The four TX words that start a frame go in with the state
machine disabled, so it never waits for the words that carry CS low.

## The driver

`psram_xfer.c` is the only file that touches the PIO block. It routes the
six pins to function 6 after the state machine has set CS high through a
forced `set pins` and `set pindirs`, so CS never falls while the pins
connect. `psram_device_init()` sends the reset pair in quad format and in
serial format, because whichever mode a warm reboot finds the part in
completes one pair and the other pair is a command cut short, which the
model treats as no command; the datasheet does not say. It then reads the ID, requires
manufacturer 0x0D and known-good-die 0x5D, enters quad mode, and writes and
reads back 32 bytes across a 64-byte frame boundary and a page boundary.
`psram.c` registers `psramdriver`; `config()` calls its probe before
`init_main()` opens `swapdev`, so a part that fails any step prints one line
(`psram0: id %x/%x is not an ESP-PSRAM64H, swap stays on fl1` or
`psram0: not usable (error %d), swap stays on fl1`) and `swapdev` stays at
the `fl1` the configuration names.

### Swap integration

`swapdev` is a variable that `init_main()` reads after `config()` returns,
and a block device in `bdevsw` slot 3 carries `d_open`, `d_strategy`,
`d_psize` and `d_close`. The tree needs no new mechanism for that:
`vm_swap.c` calls `swap()`, which calls `bdevsw[major(swapdev)].d_strategy`,
and `psramstrategy()` completes inside the call as `flstrategy()` does. Three
interactions needed attention:

- `SWAP_IMAGE_ALIGN` (4 blocks) rounds every image to the erase sector the
  flash needs. PSRAM has no sectors, so the rounding wastes at most 3 KB of
  8 MB per segment; the code is unchanged.
- `swap_cursor_publish()` stores a sector index in the low byte of
  watchdog scratch0 to spread flash wear. With 2,047 sectors an index would
  overwrite the tag, so under `PSRAM_ENABLED` the index is masked to a byte;
  a stale value either sits below the sector count, which only moves the
  starting point, or is rejected and reseeded.
- `COMPACT_SWAPMAP` entries are 16 bits, and 8,192 blocks fit; a static
  assertion keeps that true.

The `SWAPRAM` tier still sits first: a swapout the tier takes never reaches
the block device. The 384 KB flash swap region is unused while the PSRAM is
the swap device.

This is the whole architecture the tree needs for boot-time selection with
a fallback, so it is implemented. The alternatives, each of which needs an
owner decision because the tree has no mechanism for it, with the cost of
each against the 2,008 bytes of this one:

| Alternative | What changes | Cost |
| --- | --- | --- |
| PSRAM as a second tier behind SWAPRAM and ahead of `fl1` (both devices live) | `vm_swap.c` chooses a device per image; `swapin` and the `swapram` evacuation path must find which device holds an image; a second swap map or a combined address space | estimated, not built: 1 to 2 KB more text, a second map in bss, a second meaning for `p_daddr`; keeps flash swap as overflow that 8 MB makes unneeded |
| A compile-time device (`swap on psram0` in Config) with no fallback | `swapdev` fixed by `swapunix.c`; no probe branch to `fl1` | estimated, not built: a few hundred bytes less text, but a missing part panics at boot instead of falling back |
| 4.4BSD-style `swdevt[]` with interleaved devices | a table, a per-device map and `swfree` | the largest change; the interleave buys nothing when one device holds 8 MB |

The replace-`fl1` choice made here is the one that ships without any of
them, and the owner confirmed it for the PICOCALC_PSRAM configuration: an
unused 384 KB flash region is acceptable once the PSRAM probes, and the
second-tier design stays unbuilt.

## Evidence

All of it is host and build evidence. Observed on this tree, with the
simulator's pad delays at 3 ns each:

| Check | Result |
| --- | --- |
| Every word of `PSRAM_PIO_WORDS` decodes, with an independent disassembler, to the assembly in its comment | 19 of 19 |
| Waveforms on SIO0-3 against SCK rising edges for serial Read ID, quad write 0x38 and quad read 0xEB | command, address and data nibbles equal the datasheet's; pins released before the device drives them |
| Longest frames (64 bytes) | CS low 4.45 us write, 4.72 us read, limit 8 us; tCSP 16 ns (limit 2.5), tCHD 48 ns (20), tCPH 352 ns (50), SCK period 32 ns (7) |
| 199 frames from initialization and 40 random write and read pairs of 1 to 301 bytes at unaligned addresses, across 64-byte and 1 KB boundaries and the end of the device | memory equals a shadow copy, no timing violation, no indeterminate sample |
| Read sampling window margin | 20 ns against tACLK, 7.5 ns against tKOH with both register stages on clk_sys; 12 / 15.5 ns with one on the state machine cycle and 4 / 23.5 ns with both; pad delay sums from 0 to 10 ns pass |
| Interrupts masked per frame | 5.2 us at most |
| Faults | a failing die, a wrong manufacturer, an absent part and a stuck SIO2 are each reported with the right error; a 3.2 us register access cost and a 30 ns pad delay are each noticed |
| Block range | `psram_block_offset()` refuses negative blocks, blocks past the device (4194304 blocks of 1024 bytes wrap a 32-bit offset to 0) and lengths past the end |
| Pad drive | the six pads read back 0x67 (SIO0-3) and 0x63 (CS, SCK) from a PADS_BANK0 model reset to 0x56: DRIVE 8 mA, replacing the reset 4 mA field |
| Clock duty | 241 of 244 cycles in initialization are 16 ns high and 16 ns low (50%); the 3 others are the stretched cycle between the output loop and the turnaround of a read, 16 ns high and 96 ns low |
| Mutants | thirteen, each killed: SCK never rising, data changing in the rising cycle, CS left low, one wait clock short, one address nibble dropped, a 128-byte frame, a frame ignoring the 64-byte block, a word that no longer matches its comment, a turnaround one pass long, a block number multiplied before it is bounded, a length not bounded against the device end, a pad drive strength ORed into the reset field, a clock whose low phase is twice its high phase |

Size, from `arm-none-eabi-size`: PICOCALC text 118,268, data 304, bss
45,888, equal to the kernel built from the commit before the driver; the
PICOCALC_PSRAM kernel is text 120,384, data 360, bss 46,032. The driver
accounts for 2,096 text bytes, and the longer name in `version` and
`osversion` for 20 more. The text budget was 3.5 KB. PICO (107,432 text,
248 data, 39,848 bss) and PICO_UART (95,970, 192, 14,832) are unchanged, and their disassembly and read-only data differ
from the baseline only in the build timestamp string. The symbol sizes of
PICOCALC_PSRAM equal those of the driver-bearing PICOCALC link except
`version` and `osversion`.

### Build selection

The owner chose the driver as a PicoCalc option: the default PicoCalc build
keeps `fl1` swap with no driver. The driver is a separate kernel
configuration, `sys/arch/rp2040/compile/PICOCALC_PSRAM`, for three reasons.
config(8) turns one Config into one Makefile whose `PARAM` and object lists
decide what links, so a make-level switch would need a second generated
Makefile or a regeneration per build, and the tracked generated Makefile
that `check-config-generated-sync` compares would stop describing the build.
`PICO_UART` already sets the precedent of a variant configuration named for
what it changes. And `MACHINE_KERNEL_CONFIGS` then carries the variant into
`check-uf2-images`, the divider and SwapRAM verifiers and the CI UF2
artifacts without a second enumeration. The cost is a repeated Config:
config(8) has no include directive, so the generated-sync gate requires the
PICOCALC_PSRAM Config to equal the PICOCALC Config plus its board line and
`device psram`. `distrib/rp2040/Makefile.inc` selects it by name,
`BOARD=PICOCALC_PSRAM`, with the `picocalc` root profile.

### What the simulator cannot establish

- Which clock advances the output and input register stages (the table
  under "Where the read samples" bounds the consequence), the pad delays,
  the real tACLK between its 2 and 6 ns, the drive strength
  and the trace on the PicoCalc board. The margin table above is a bound
  over the datasheet, not a measurement; a board capture of SCK against a
  read data line decides it, and the pad slew and drive settings in
  `psram_xfer.c` are the knobs.
- That a serial command cut short is ignored. The datasheet does not state
  it; the reset sequence depends on it.
- The recovery time after reset. The datasheet gives 150 us for power-up
  and none for reset; the driver waits 150 us.
- Contention in the cycle where a read releases SIO0. The model enforces
  tACLK(min) 2 ns against the release.
- The effect of J703, the expansion header that shares GP2-GP5, GP21 and
  GP28 with the PSRAM, when a module is fitted.

### Renode

The model repository the tree pins, `matgla/Renode_RP2040` at commit
`205a5e4b25440582008a4292074bb07f80a72328`, does model PIO:
`emulation/peripherals/pio/rp2040_pio.cs` (SHA-256
`87c9461bb3f1589eeabbb8d79803a440f6fd0e477156c846c730f7262d613f53`) drives a
native `piosim` library, version 0.0.1, and reads and writes GPIOs through
`RP2040GPIO`. Its C# glue reads pins with `GetGpioStateBitmap` and has no
input synchronizer latency, which this read path depends on, so a read test
against it would need that latency added to `piosim` or the sample point
changed. Writing the test also needs a pin-level PSRAM peripheral for GP2-GP5,
GP20 and GP21 and a PICOCALC machine description, which the tree does not
have. `renode-test` cannot run on this host and the pinned tree is not
fetched here. No Renode PSRAM test ships; the claim stays host and build
evidence.

## Reproduce

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON
    bmake MACHINE=rp2040 check-psram-pio

The gate compiles `psram_xfer.c` for the host with hooks that forward each
register access to `tests/psram/pio_sim.py`, runs `tests/psram/test_psram.py`,
and then runs each mutant. It writes `.build-logs/psram/psram-frames.json`.
The kernels build with `bmake MACHINE=rp2040 kernel`; the PSRAM kernel is
`sys/arch/rp2040/compile/PICOCALC_PSRAM/unix.uf2`.
