# PicoCalc PSRAM bus capture

`sys/arch/rp2040/doc/PICOCALC.md` ("PSRAM swap") leaves three board
questions to a capture of SCK against a read data line:
- the pad delays that set the read sampling margin;
- whether the RP2040's output and input register stages advance on clk_sys
  or on the state machine's divided cycle;
- whether the clock can rise faster.

`tests/psram` answers each question only for assumed pad delays. Its
stage-gating table gives sampling margins against tACLK and tKOH of
20 / 7.5 ns with both stages on clk_sys, 12 / 15.5 ns with either on the
divided cycle, and 4 / 23.5 ns with both on it. This note sets the instruments that
measure them on the board, cheapest first.

## What the capture must resolve

SCK runs at 31.25 MHz, a 32 ns period (`PSRAM_CLKDIV` 2 at clk_sys
125 MHz, `psram_pio.h`). Placing a data edge within a quarter period needs
at least 125 MS/s. A Read ID frame is 98 clocks: 31 with the command and
address out, three with the pins released while the input stages fill, and
64 in. The simulator holds CS low 3.25 us for it. A 64-byte quad read is
2 command, 6 address and 6 wait clocks, 128 data clocks and two more while
the input stages fill, 144 clocks, with CS low 4.72 us in the simulator.
The nets are:
- SIO0-SIO3 on GP2-GP5;
- chip select on GP20;
- SCK on GP21.

## The RP2040 measuring itself on PIO1

The RP2040 can capture its own PSRAM bus with no added hardware.
- **Mechanism:** a GPIO's input "is always connected, so the PIOs can
  always see the state of all pins" (RP2040 datasheet 1.4.3, PIOx). A state
  machine on PIO1, which the kernel leaves unused (`locore0.S` holds only
  its vector stubs), can therefore sample GP2-GP21 while PIO0 drives them.
- **Program:** `wait 0 gpio 20` once, then `in pins, 32` alone inside the
  wrap: `.wrap_target` and `.wrap` both on the `in`, so EXECCTRL
  WRAP_BOTTOM and WRAP_TOP hold its address (datasheet 3.5.2). Autopush
  at 32 bits, `IN_BASE` 0 and clock divider 1 then take one 32-bit GPIO
  snapshot per clk_sys cycle, 125 MS/s. A state machine executes at most
  one instruction per cycle, so a loop that wraps over both instructions
  samples every second cycle, 62.5 MS/s.
- **Transfer:** a DMA channel paced by the PIO1 RX DREQ copies the samples
  to RAM. The PIO's DMA interface sustains one word per clock (datasheet
  3.1).
- **Size:** a Read ID frame is about 406 samples, 1.6 KB, and a 64-byte
  read about 590 samples, 2.4 KB. A 4 KB static buffer holds either.
- **Synchronizers:** INPUT_SYNC_BYPASS (datasheet 3.5.6.3, offset 0x038)
  belongs to each PIO block. Setting it on PIO1 removes the analyzer's two
  synchronizer cycles and leaves PIO0's read path as shipped.

What it settles: SCK and the four data lines pass through the same pad
input buffers. The capture therefore shows, in whole clk_sys cycles, when
each returned nibble changes relative to the SCK edge the PSRAM program
samples against. Comparing the sample where a data line settles with
the cycle of PIO0's `in pins, 4` tells which stage-gating placement holds.
It also shows whether any read sampled a nibble mid-transition.

The capture sees chip select, which the Core GPIO header lacks, and it adds
no probe capacitance to the bus. The same program covers every bus on the
board: the panel's SPI1, the SD card's SPI0 on GP16-GP19, and the keyboard
controller's I2C1.

Limits:
- **Resolution:** one clk_sys cycle, 8 ns. Both state machines run on
  clk_sys, so their sampling phases cannot be offset by less than a cycle.
- **One input path:** the analyzer reads through the same pads and input
  thresholds as the PSRAM program. A pad delay common to SCK and data
  cancels out of the measurement.
- **Bus contention:** PIO0, PIO1, USB, the DMA control registers and XIP
  aux share one downstream port of the bus fabric (datasheet 2.1), so the
  analyzer's DMA reads from PIO1's RX FIFO and the frame loop of
  `psram_frame()` reaching PIO0 take turns on it. A PIO1 RX FIFO that
  fills sets FDEBUG RXSTALL for the analyzer's state machine and loses
  samples (datasheet 3.4.6); a starved PIO0 loop shows as the driver's
  error -2. The capture routine fails closed: after the frame it reads
  PIO1's RXSTALL bit and the DMA channel's remaining transfer count, and
  discards a capture that stalled or came up short. Whether one sample per
  clock fits beside the driver's traffic is a measurement the first
  captures make.

Implementation sketch: a kernel option that links a capture routine into
PICOCALC_PSRAM, arms PIO1 and the DMA channel before `psram_read_id()` or
before a test frame, and copies the buffer out through a sysctl. A host
decoder reads the words with the frame parser in `tests/psram/pio_sim.py`.

## A second Pico on the Core GPIO header

The vendor assembly guide ("The Interfaces", Core GPIOs) puts GP2, GP3, GP4,
GP5, GP21 and GP28 on the Core GPIO header. That is the four SIO lines and
SCK, without chip select. A second Pico running an open-source PIO logic
analyzer firmware samples them from its own crystal.

The second Pico runs asynchronously to the PicoCalc's clock. Hypothesis:
the two crystals differ enough that the sampling phase drifts through the
SCK period over many frames, so a histogram of data-edge offsets from SCK
edges estimates the delay below one sample period, more finely with more
frames. Two references can also sit at an exact or small-ratio frequency
relation, which visits only a fixed set of phases. Before reading any
delay, histogram the phase of the SCK edges against the sample period; an
empty bin falsifies the sweep, and detuning the analyzer's clk_sys (for
example to 124 or 126 MHz) restores it. The instrument is also independent of the device under
test: its input path and thresholds belong to another chip, so its
result does not share the self-capture's input path.

Constraints:
- **Rate:** the analyzer firmware must reach at least 125 MS/s.
  Hypothesis: gusmanb's LogicAnalyzer and sigrok-pico reach 100 MS/s and
  above on an RP2040 (unverified; read each project's documentation before
  relying on a rate).
- **Trigger:** with chip select absent, trigger on the first SCK edge after
  an idle gap longer than tCPH, 50 ns.
- **Wiring:** short leads, under about 10 cm, with a ground return beside
  the signal leads; ground tied between the boards.
- **Power:** the analyzer Pico takes power from the host's USB, never from
  the header's supply.
- **No driving:** the analyzer drives none of the pins while the PSRAM
  driver runs.

## Commercial analyzers

An analyzer that samples at 125 MS/s or above on 3.3 V logic qualifies, for
example the Saleae Logic Pro or a DSLogic (specifications unverified). The
24 MHz analyzers built on the Cypress FX2 sample at 24 MS/s, under one
sample per SCK period, and do not qualify.

## Order

1. **PIO1 self-capture:** no purchase, chip select visible, and it answers
   the stage-gating question in whole cycles.
2. **Second Pico:** sub-cycle edge statistics from an independent input
   path, for the pad delays.
3. **Commercial analyzer:** only if both disagree, or if a later
   `PSRAM_CLKDIV` 1 (62.5 MHz) needs margins under 8 ns.

Prediction under the simulator's clk_sys placement for both stages
(`tests/psram`, pad delays of 3 ns, `stage_gating` in
`.build-logs/psram/psram-frames.json`): each returned nibble is valid
20 ns before the cycle of PIO0's `in pins, 4`, two full 8 ns samples.
It stays valid 7.5 ns after that cycle, under one sample. The other three
placements give 12 / 15.5 ns (either stage on the divided cycle) and
4 / 23.5 ns (both on it).

Readings:
- **Fewer than two settled samples before the read:** a capture showing a
  nibble settle with fewer than two whole samples before PIO0's read cycle
  falsifies the clk_sys placement. It points at the 12 or 4 ns rows.
- **Settling in the read sample or later:** that breaks the shipped read
  path.
