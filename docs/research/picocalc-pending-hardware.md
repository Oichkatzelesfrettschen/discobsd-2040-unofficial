# PicoCalc work that waits on hardware

A ClockworkPi PicoCalc decides two kinds of open claim: a change that is
held back until the carrier's panel, keyboard controller, SD socket, PSRAM
or power path has run it, and a claim whose code is on `main` under build,
host and Renode evidence with its board row still "not run". This file
lists both, each with the measurement that decides it.
`picocalc-first-power-up.md` orders the first session's observations and
states each one's prediction.

## Changes held until a board run

| Change | Artifact | Deciding measurement | State |
| --- | --- | --- | --- |
| Retire the PICO, PICO_UART and PICOCALC kernel configurations in favor of the union image | `docs/research/rp2040-single-kernel.md`, `sys/arch/rp2040/compile` | The union kernel (`PICOCALC_PSRAM` with a PicoCalc descriptor written by `distrib/rp2040/mkboarduf2.py --board=PICOCALC_PSRAM`) boots on a PicoCalc to a login on the panel and keyboard, with the `lcd0:`, `kbd0:`, `sd0:` and `psram0:` probe lines | open: the bare-Pico half is measured (`docs/research/swap-exhaustion/review-07.md` section 10), the PicoCalc half is not run |
| Give the union configuration the `core1` option before it replaces PICO | `sys/arch/rp2040/compile/PICOCALC_PSRAM/Config`, `sys/arch/rp2040/doc/CORE1.md` | `check-renode-union` passes the core 1 stages `tools/renode/check-boot.py` runs for `PICO_UART`, the kernel RAM headroom of `rp2040-single-kernel.md` holds with `core1.c` linked, and the PicoCalc renderer's use of core 1 is settled | open: the PICO and PICO_UART kernels link the observer, the union image has no `machdep.core1` node |

## Claims on `main` whose board rows read "not run"

`sys/arch/rp2040/doc/PICOCALC.md` owns these rows; the code is on `main`
under build, host and Renode evidence. A PicoCalc session works through them
in this order, because each later row needs the earlier device.

| Claim | Fixture or procedure | State |
| --- | --- | --- |
| The panel initializes, shows text and scrolls | boot the PICOCALC kernel | not run |
| The keyboard controller answers at 0x1F and its events type | console login | not run |
| `/dev/lcdgfx` shapes, timings and text restore | `tests/rp2040/lcdgfxtest` | not run |
| The tone is audible, 1 kHz for 100 ms at 25 percent duty | console bell | not run |
| Battery, backlight and power-off registers; split-phase key polling | `sysctl machdep.picocalc` | not run |
| SD card-detect polarity, the 12.5 MHz clock and busy times of real cards | `sd0` probe and transfers | not run |
| A UFS partition on the card mounts beside the flash root; `fdisk` and `mkfs` through `rsd0` | console procedure in `PICOCALC.md` | not run |
| The pulled-card sequence | `tests/rp2040/sd_detach/board_sd_detach.py` behind its opt-in | not run |
| PSRAM Read ID 0x0D, quad mode and readback at 31.25 MHz | `psram0` probe | not run |
| The kernel swaps to PSRAM and falls back to `fl1` when the part is absent | boot with and without the part | not run |

## When a row leaves

A held change lands through a pull request once its deciding measurement is
recorded, and its row leaves the first table with that merge. A claim row
leaves the second table when `PICOCALC.md` records its board run. A new
PicoCalc change enters the first table only when the carrier decides its
claim; one that a host gate or Renode decides lands directly.
