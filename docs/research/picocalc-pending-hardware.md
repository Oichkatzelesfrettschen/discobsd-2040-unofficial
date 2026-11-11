# PicoCalc work that waits on hardware

The branch `picocalc-wip-pending-hardware` holds every change whose claim a
ClockworkPi PicoCalc decides: a change that a bare Pico, a host gate and
Renode can establish lands through the working branch and `main`, and a
change that needs the carrier's panel, keyboard controller, SD socket, PSRAM
or power path waits here until a board run is recorded. This file is the
branch's ledger. Each row names the claim, the artifact that carries it, the
measurement that decides it and the state.

## Changes held on this branch

| Change | Artifact | Deciding measurement | State |
| --- | --- | --- | --- |
| Retire the PICO, PICO_UART and PICOCALC kernel configurations in favor of the union image | `docs/research/rp2040-single-kernel.md`, `sys/arch/rp2040/compile` | The union kernel (`PICOCALC_PSRAM` with a PicoCalc descriptor written by `distrib/rp2040/mkboarduf2.py --board=PICOCALC_PSRAM`) boots on a PicoCalc to a login on the panel and keyboard, with the `lcd0:`, `kbd0:`, `sd0:` and `psram0:` probe lines | open: the bare-Pico half is measured (`docs/research/swap-exhaustion/review-07.md` section 10), the PicoCalc half is not run |

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

## Rules for this branch

- A new PicoCalc-only change starts here when its claim needs the carrier.
  A change that touches PicoCalc files but is decided by a host gate or
  Renode goes through the working branch.
- The branch follows `main` by merge. A row leaves the tables when its board
  run is recorded in `PICOCALC.md` and its change has merged.
