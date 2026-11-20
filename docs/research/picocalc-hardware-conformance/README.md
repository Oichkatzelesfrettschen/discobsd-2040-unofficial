# PicoCalc V2.0 hardware conformance: derivations, call graph, and calibrated gates

This document records the hardware-conformance audit, mathematical coefficient
decompositions, preprocessed `PICOCALC_SD` call graph, and calibrated host
verification across the ClockworkPi PicoCalc Mainboard V2.0 drivers in
`sys/arch/rp2040/dev/`. Every technical claim carries its evidence class:
`Measured` (observed in compiled binaries, unit suites, emulator runs, or named
board logs), `Documented` (derived directly from primary silicon datasheets or
the V2.0 schematic), `Inferred` (analytical model across components without
bench waveform capture), or `Not run` (hardware probes remaining outside this
offline build and host session).

## Scope and primary references

Four subsystem surfaces govern the PicoCalc carrier hardware on RP2040:

1. **Display controller** (`st7365p.c`, `paneltest_sets.h`): Sitronix ST7365P /
   ST7796S 320x320 RGB565/RGB666 panel on SPI1 (GP10--GP15).
2. **Keyboard and backlight controller** (`picokbd.c`, `picokbd_req.c`,
   `picokbd_req.h`, `picocalc_reg.h`): CKS32F103 (`U201`) I2C1 target `0x1F` on
   GP6/GP7, clocked at 100.00 kHz under UM10204 Fast-mode rules.
3. **SD card socket** (`sd.c`, `sdspi.c`): HRO TF-03A (`J303`) push-in socket on
   SPI0 (GP16--GP19) with active-low card detect (`SD_DET`) on GP22.
4. **Audio PWM chain** (`picopcm.c`, `picopcm.h`, `picoaudio.c`): RP2040 PWM
   slices 5 and 6 driving dual `NC7WZ16` buffers (`U501`), passive RC/AC
   filtering (`R501`--`R506`, `C502`--`C505`), volume pot `SW501`, and
   `AW8010` mono amplifiers (`U502`, `U503`) on GP26/GP27.

All schematic designators match `docs/research/picocalc-pin-ledger/board-nets.tsv`.
The reference PDFs live in the sibling `discobsd-2040-notes` repository under
`docs/picocalc/` with the following SHA-256 digests (`Documented`):

| Document | Part or scope | SHA-256 digest |
| --- | --- | --- |
| `ST7365P_SPEC_V1.0.pdf` | Sitronix ST7365P LCD controller | `8971993df5a55c72180f700f9cd04dcb2a4305b86a87c11ba3de401d3eadfb79` |
| `clockwork_Mainboard_V2.0_Schematic.pdf` | PicoCalc mainboard V2.0 schematic | `b6fd79c6cbe9b0825525211d0f2e3e0fd269a2f9a6fc22580452de3012156ede` |
| `hro_tf_03a_lcsc_c693842.pdf` | HRO TF-03A microSD socket (`J303`) | `7385912bf9734522893094b933539e0a5b9b88248288d00e24d5af27c55b0a9f` |
| `willsemi_was7227q_datasheet_rev1.4.pdf` | WillSemi WAS7227Q USB mux (`U701`, `U703`) | `a7cdc26d4fcbfedf9cc16b54d1c5c160c038669ea66ce07b7dfd299a05733458` |
| `willsemi_ws4603_datasheet_rev1.2.pdf` | WillSemi WS4603 load switch (`U102`) | `a2879e120ed7450f780fe66db9797b5d89035f931f05bf2a83217661ccb28c4f` |

Additional board power-path facts (`Documented`):

- `U102` (`R104 = 3.4 kohm` on `ISET`) sets typical over-current trip
  `I_OC = 6.8 kohm / R_SET = 2.00 A` and integrates reverse-current blocking
  with no internal body diode (`willsemi_ws4603_datasheet_rev1.2.pdf`, pages 1
  and 8).
- `U701` and `U703` (`OE` grounded low) route `D+/D-` to `D1+/D1-` when `SEL`
  is low and to `D2+/D2-` when `SEL` is high (`willsemi_was7227q_datasheet_rev1.4.pdf`,
  page 2 truth table).
- `J303` shorts pin `CD` (`SD_DET`, pulled to `3V3_OUT` via `R301 = 10 kohm`
  and internal `PC_PADS_PUE`) to `P3` (`VSS1 = GND`) when a card is seated,
  yielding `SD_DET = 0` present and `SD_DET = 1` empty (`hro_tf_03a_lcsc_c693842.pdf`,
  page 1 switch diagram).

## Coefficient decompositions

### ST7365P initialization and frame-rate law

The ST7365P datasheet section 9.3 specifies Command Table 2 gating via `CSCON`
(`F0h C3h` then `F0h 96h` to unlock, page 245; `F0h 3Ch` then `F0h 69h` to lock).
Earlier initialization tables copied vendor ILI9488P parameters (`15`-byte
`E0h`/`E1h`, `C1h 41h` outside the `00h..27h` defined `VRH` range, `3`-byte
`C5h`, undefined `F7h`, and unlocked leaving Command Table 2 open).

`st7365p_init()` now sends:
`init_unlock` (`F0h C3h/96h`) -> `init_gamma` (`E0h` and `E1h`, 14 parameters
each, ST7365P_SPEC_V1.0 pages 240--241) -> `init_seq` (`C0h 80h 25h`,
`C1h 13h`, `C2h A5h`, `C5h 1Ch`, `MADCTL 48h`, `COLMOD 55h`, `B0h 00h`, `INVON`,
`B4h 02h`, `B6h 80h 02h 3Bh`, `B7h C6h`) -> `init_frame` -> `init_lock`
(`F0h 3Ch/69h`) before `SLPOUT` (`Documented`, verified statically by
`check-picocalc-conformance`; live Renode `check-renode-picocalc` stays
`Not run` locally because external `tools/renode/vendor/Renode_RP2040` models
are absent, while `tools/renode/check-picocalc.py --self-test` passed).
`paneltest_sets.h` keeps sets `1..31` unchanged and appends `set 32`
(`legacy-ili9488-78hz`) under `LCD_PANELTEST` (`Measured` SHA-256 identity of
sets `1..31` preserved in `tools/unicorn/panel-programs.tsv`, checked by
`check-panel-programs`).

From FRMCTR1 (`B1h`, page 219) and BPC (`B5h`, page 222, `VFP = 2, VBP = 2`),
the datasheet normal-mode frame rate equation is (`Documented`):

    f_FR(FRS, DIVA, RTNA) = F_osc / (2^DIVA * (168 + RTNA + 32 * (15 - FRS)) * (480 + VFP + VBP))

With nominal internal oscillator `F_osc = 10^7 Hz`, `FRS = 12`, `DIVA = 0`,
`RTNA = 1`, and `480 + VFP + VBP = 484`:

    f_FR_nom = 10,000,000 / (265 * 484) = 10,000,000 / 128,260 = 77.9666 Hz

Prior historical GSCAN measurements (`PANELTEST.md`, 2026-11-18) observed
effective panel oscillator scaling `alpha = f_meas / f_nom = 0.962..0.974`
(`F_osc_eff approx 9.62..9.74 MHz`), giving `74.94 Hz` measured under the
`78 Hz` table (`Measured` on pre-W1 build `ab9ac8e7a`; post-W1 silicon readback
is `Not run`).

### RP2040 DW_apb_i2c timing and bus recovery (`picokbd.c`, `picokbd_req.c`)

At `CPU_KHZ = 125000`, one `ic_clk` period is `t_sys = 8 ns`. RP2040 section
4.3.14.1 defines the DW_apb_i2c Fast/Standard SCL phase durations as (`Documented`):

    t_LOW  = (IC_FS_SCL_LCNT + 1) * t_sys
    t_HIGH = (IC_FS_SCL_HCNT + IC_FS_SPKLEN + 7) * t_sys

Solving directly for `f_SCL = 100,000 Hz` (`T_SCL = 1,250` cycles = `10,000 ns`)
under UM10204 Table 10 Fast-mode constraints (`t_SP <= 50 ns`, `t_LOW >= 4,700 ns`,
`t_HIGH >= 4,000 ns`, `t_SDA_HOLD >= 300 ns`) yields the exact compile-time
constants verified in `picokbd.c` (`Documented` equations, `_Static_assert`
enforced at build time):

| Register field | Expression at `CPU_KHZ = 125000` | Value | Cycles | Time (`ns`) | Bound |
| --- | --- | --- | --- | --- | --- |
| `IC_FS_SPKLEN` | `(125000 * 50) / 1000000` | `6` | `6` | `48` | `<= 50 ns` |
| `IC_FS_SCL_LCNT` | `(1250 * 3) / 5 - 2` | `748` | `749` | `5,992` | `>= 4,700 ns` |
| `IC_FS_SCL_HCNT` | `1250 - 749 - (6 + 7)` | `488` | `501` | `4,008` | `>= 4,000 ns` |
| `IC_FS_SCL` sum | `t_LOW + t_HIGH` | -- | `1,250` | `10,000` | Exact `100.00 kHz` |
| `IC_SDA_HOLD` | `(125000 * 3) / 10000 + 1` | `38` | `38` | `304` | `>= 300 ns` |

Prior code passed raw `LCNT = 750` and `HCNT = 500` with `SPKLEN = 46`, yielding
`1,297` cycles (`10,376 ns`, `96.38 kHz`) and `368 ns` spike suppression above
UM10204's `50 ns` ceiling. `i2c_bus_clear()` implements UM10204 section 3.1.16
recovery in open-drain SIO mode (`PC_SIO_OE_CLR`/`PC_SIO_OE_SET`/`PC_SIO_OUT_CLR`
and `PC_SIO_GPIO_IN` at constant literal addresses, 0 findings under
`check-divider`) with up to 9 SCL pulses at 10 us period followed by STOP on
initialization and error restarts. `kbdreq_level_for()` conditions `REG_ID_BKL`
(`16..240`, step `16`) and `REG_ID_BK2` (`0` or `32..224`, step `32`) prior to
I2C transmission (`Measured` in `kbdreq_test` and 12 `kbdreq-mutants`).

### SD SPI framing and card detect (`sdspi.c`, `sd.c`)

1. **`N_CR` bound**: SD Physical Layer Simplified Specification Part 1 section
   7.5.1 defines `N_CR` as `0..8` stuffing (`0xff`) bytes preceding R1. Iterating
   `i = 0; i <= NCR_MAX && (r & 0x80); i++` (`NCR_MAX = 8`) samples bytes
   `0..8`, accepting valid `ncr = 8` responses while rejecting `ncr = 9`
   (`Measured` in `sdspi_test` and `sdspi-mutant-ncr-off-by-one`).
2. **PL022 speed transition**: RP2040 section 4.4 / PL022 TRM section 3.3.2
   require polling `SSPSR_BSY` (`bit 4`) clear before clearing `SSPCR1_SSE` and
   reprogramming `SSPCPSR`/`SSPCR0` in `sd_speed()`.
3. **CSD v2.0 2 TiB clamp**: In `sdspi_csd_sectors()`, a maximum 22-bit
   `C_SIZE = 0x3fffffU` yields `(0x3fffffU + 1) * 1024 = 2^32` 512-byte sectors,
   which wraps 32-bit `unsigned nsectors` to `0` (`SDE_IO` card refusal).
   Clamping `c_size == 0x3fffffU` to `~0U` (`4,294,967,295` sectors) avoids the
   wrap while leaving `0x3ffffeU` (`0xfffffc00U` sectors) exact (`Measured` in
   `sdspi_test`).
4. **Active-low `SD_DET` fast-fail**: `sd_setup()` checks
   `sd_card_present()` (`sd_det_pin() == SD_DET_PRESENT`, where
   `SD_DET_PRESENT = 0` per `J303 TF-03A`) and returns `ENODEV` before calling
   `sdspi_init()`, while preserving 50 ms edge debouncing in `sd_detect_poll()`
   (`Measured` in `sdstrat_test` and `sdstrat-mutant-det-empty`).

### Audio PWM carrier filter and linear DC-bias ramp (`picopcm.c`, `picopcm.h`)

From `board-nets.tsv` lines 137--142 (`Documented` component values, `Inferred`
linearized RC model):

- U501 (`NC7WZ16` on `ALDO4 = 3.3 V`) drives `R501/R502 = 220 ohm` into shunt
  `R503/R504 = 100 ohm` in parallel with `C502/C503 = 100 nF`, followed by
  series coupling capacitor `C504/C505 = 47 uF` and load `R505/R506 = 1.8 kohm`.
- Parallel shunt resistance at the filter node (`DC + AC`) is:

      R_load = 100 || 1800 = 94.737 ohm
      R_th   = 220 || 100 || 1800 = 66.221 ohm

- Low-pass corner frequency and `500 kHz` PWM carrier attenuation
  (`PCM_LEVELS = 250`, `125 MHz / 250 = 500 kHz`):

      f_c       = 1 / (2 * pi * R_th * C502) = 24.034 kHz   (23.16 kHz unloaded by 1.8 kohm)
      |H(0)|    = R_load / (220 + R_load)    = -10.43 dB
      |H(500k)| = |Z_par(500k) / (220 + Z_par(500k))| = -36.80 dB  (-26.37 dB relative to DC)

- AC-coupling high-pass time constant and linear ramp transient response:
  Steady-state half-duty (`PCM_SILENCE = 0x80`, compare `125/250 = 0.5`) places
  `V_step = 3.3 * 0.5 * (100 / 320) = 0.5156 V` behind `R_th_dc = 220 || 100 = 68.75 ohm`,
  so `tau_hp = (68.75 + 1800) * 47e-6 = 87.83 ms` (`f_hp = 1.81 Hz`).
  An un-ramped step injects an instantaneous `496.7 mV` discontinuity across
  `R505` with `1 / (j omega)` step spectrum. Sweeping duty linearly over
  `N = 128` samples at `8,000 Hz` (`T = 16.0 ms`) limits slew to `32.2 V/s`
  and multiplies the spectral envelope by `sinc(f T)` (slope `-40 dB/dec`),
  attenuating the `1 kHz` click band by `-34.03 dB` (`-20 * log10(pi * 1000 * 0.016)`).
  `pcmclose()` drains the ring, holds underrun compare duty at `0` via
  `pcm_closing` while ramping `PCM_SILENCE - 1..0` whenever the sample clock is
  running, then masks the IRQ and lowers both outputs via `picoaudio_mute()`
  (`Measured` trajectory in `picopcm_test` and 14 `picopcm-mutants`).

## Preprocessed `PICOCALC_SD` call graph (`cflow` and `cscope`)

`tools/capture_rp2040_callgraph.py` now supports `--config` and `--preprocess`,
forwarding configured `DEFS`, `PARAM`, `INCLUDES`, and `CFLAGS` tokens (including
`-DKERNEL` and `-nostdinc` compatibility paths) to `arm-none-eabi-gcc -E`.
Adding `-DKERNEL` resolved `#include <time.h>` macro gating across kernel
headers cleanly. Capturing the live `PICOCALC_SD` configuration (`Measured` via
`cflow --preprocess` + `cscope` in 4 convergence passes across `99` C files and
`345` indexed headers/sources):

- **Graph size**: `1,168` nodes, `2,815` directed edges (`direct.dot` SHA-256
  `87827659264bcc8969e7b712a35bb35de5d398d8853258486a32276a7691325e`,
  `cscope.out` SHA-256 `6687624dce341dcb8a9dbfcfa0396551eb6be808511a1e2cc86a66aece95dc63`).
- **Resolved PicoCalc paths**:
  - `lcdcons_init -> st7365p_init -> {clear_margin, panel_begin, panel_wake, pc_gpio_output, pc_pin, pc_unreset, run_seq}`
  - `lcdcons_init -> picokbd_init -> {i2c_bus_clear, i2c_enable, i2c_restart, i2c_setup, pc_unreset}`
  - `{i2c_restart, picokbd_init, recover_reset} -> i2c_bus_clear -> {i2c_half_bit, pc_pin}`
  - `cpu_sysctl -> picocalc_sysctl` (`cscope` caller xref; indirect `sysctl` table)
  - `{sdopen, sdrootattach, sdswapattach} -> sd_setup -> {printf, sd_accept, sd_card_present, sd_partitions, sdspi_init}`
  - `{sd_setup, sddump} -> sdspi_init -> {sd_acmd, sd_cmd, sd_done, sd_rx, sd_tail, sdspi_csd_sectors}`
  - `sdprobe -> sd_speed`
  - `pcmopen -> {board_id, pc_pin, pc_unreset, pcm_ramp}`
  - `pcmclose -> {pcm_fill, pcm_nap, pcm_ramp, picoaudio_mute}`
  - `{pcmopen, pcmclose} -> pcm_ramp -> {pcm_fill, pcm_nap, pcm_put}`
  - `PWM_IRQ_WRAP_Handler -> {pcm_fill, pcm_get, pcm_level}`

## Kernel footprint and calibrated gates

Baseline sizes were measured at commit `95e5dfab4` (`origin/main`) with
`arm-none-eabi-gcc 16.2.0` (`Measured`):

| Configuration | Baseline text/data/bss | Final text/data/bss | Text delta (B) |
| --- | --- | --- | --- |
| `PICO` | `99277 / 128 / 42656` | `99277 / 128 / 42656` | `0` |
| `PICO_DIAG` | `101820 / 128 / 42768` | `101820 / 128 / 42768` | `0` |
| `PICO_UART` | `91238 / 120 / 18032` | `91238 / 120 / 18032` | `0` |
| `PICOCALC` | `128739 / 144 / 53080` | `129270 / 144 / 53080` | `+531` |
| `PICOCALC_LEAN` | `120741 / 144 / 48152` | `121057 / 144 / 48152` | `+316` |
| `PICOCALC_LEAN_DIAG` | `123284 / 144 / 48256` | `123600 / 144 / 48256` | `+316` |
| `PICOCALC_SD` | `131291 / 144 / 58944` | `131822 / 144 / 58944` | `+531` |
| `PICOCALC_SD_DIAG` | `156419 / 472 / 59168` | `156999 / 472 / 59168` | `+580` |

Bare Pico configurations retain zero size impact (`+0 B`). `PICOCALC` flash
headroom stays above the `1,024 B` minimum margin under `check-kernel-headroom`
(`Measured`), and `check-divider` reports `divider_findings=0` across all ELFs.
`check-picocalc-conformance` (`tools/picocalc/verify_conformance.py` calibrated by
`tests/picocalc/conformance_test.py` against the live tree and 13 synthetic
single-file mutants) enforces all quantitative driver invariants in `HOST_GATES`.

## Unexecuted emulator and hardware boundary (`Not run`)

The following host-external and physical-board checks did not run in this session:

- **Live Renode suites (`check-renode*`)**: `Not run` because the pinned third-party RP2040 peripheral build (`tools/renode/vendor/Renode_RP2040`, commit `205a5e4b`, fetched separately by `tools/renode/fetch-renode-rp2040.sh`) is not present on this host (`tools/renode/check-picocalc.py --self-test` passed locally).
- **`check-matezinho-sd` default runtime**: vanilla `dotnet run --project sdtest` exits `150` because `.NET 8.0.0` runtime is absent (host carries `3.1.32`, `9.0.22`, `10.0.14`); running the exact suite with `DOTNET_ROLL_FORWARD=Major` compiles `libsd_driver.so` and passes all three `FileSD` card scenarios.
- `ST7365P` post-W1 readback and optical contrast under native defaults (`0xf7`/`E9h` omitted).
- Continuity probe at `U703` pin 10 (`WAS7227Q`) vs `SW701` pole 1 and micro-USB reverse-leakage test across `U102` (`WS4603`).
- Multimeter polarity check on `J303` pin 10 (`TF-03A`) and hot-pull `board_sd_detach.py`.
- Audio jack/speaker oscilloscope check for 500 kHz residual carrier and open/close ramp click suppression.
