# PicoCalc bring-up: state of the port and the next work

Drafted by an AI assistant (Claude Opus) as the hand-off for the PicoCalc
work in PRs #344 through #369.

This note names what the PicoCalc work put in the tree, how the board builds
share one source tree, which test stages exist between a host compile and the
physical boards, what is still open, and the next work in order. The
authorities stay where they were: `sys/arch/rp2040/doc/PICOCALC.md` for the
board (pin map, driver timing, evidence), `sys/arch/rp2040/doc/PROFILES.md`
for image composition, and `sys/arch/rp2040/doc/TESTING.md` for what each gate
proves. Every figure below comes from the document it cites or from the merge
it names.

## What is in the tree

Console (PR #344): the ST7365P panel on SPI1 with hardware vertical scroll
(`st7365p.c`), a 53x32 xterm-basic terminal over a cell store (`lcdterm.c`,
`lcdfont.c`), the I2C1 keyboard controller (`picokbd.c`, `picokbd_map.c`) and
the tty glue (`lcdcons.c`).

PicoCalc drivers:

- Keyboard-controller sysctls for battery, backlight and power-off, and a BEL
  tone on PWM (`picokbd_req.c`, `picoaudio.c`; PR #350).
- SD card on SPI0 (`sdspi.c`, `sd.c`; PR #350): 512-byte sectors, MBR
  partitions `sd0a`/`sd0b` and the raw `rsd0*` nodes, card-change debounce by a
  committed and a candidate level, a per-sector replacement check, and
  `b_resid` reporting every unmoved byte.
- PSRAM swap through PIO as a separate opt-in kernel, `PICOCALC_PSRAM`
  (`psram.c`, `psram_xfer.c`; PR #348). The baseline `PICOCALC` kernel carries
  no PSRAM code; `check-config-generated-sync` holds the PSRAM Config to the
  PICOCALC Config plus its board line and `device psram`.

Programs and libraries:

- OpenBSD `bc` and `dc` on the tree's clean-room `lib/libbn` (PR #351), with
  the POSIX remainder scale and a leading array argument (PR #361), shipped as
  the `calc-bignum` closure group of the `picocalc` profile.
- `usr.bin/calc` and `lib/libcalc` (PR #345).
- Games in `gamebox` (fifteen, keen, bubble, fourfall, coil) and puzzles in
  `puzzlebox` (flood, samegame, unruly, lightup) (PRs #345, #354).

Kernel speed and correctness on both boards:

- CRC-32 a nibble at a time (PR #359).
- SysTick ticks that masked flash operations drop are counted back
  (PR #360).
- User integer division on the SIO hardware divider (PR #362).
- A port-owned heatshrink decoder for packed executables and swap, 67.7
  against 326.8 emulated cycles per expanded byte, with the vendored decoder
  out of the kernel (PR #367).

Build and test infrastructure:

- Per-build closure selection: `IMAGE_WITH` and `IMAGE_WITHOUT` on top of a
  profile, one closure per program and per multicall member, and
  `bmake MACHINE=rp2040 list-closures` (PR #363).
- The C17 source denominator no longer quotes its counts in prose; the
  checker's printed counts are the authority (PR #366).
- Every qemu test runs on the ARMv6-M CPU model, the qemu a.out loader runs
  real DiscoBSD binaries, and two Renode machines boot the PICO_UART and
  PICOCALC kernels (PR #346).
- The second-core plan and its reproducible cycle model
  (`docs/research/rp2040-second-core.md`, PR #357).

## The board builds

Kernel flash text from clean builds at main `df501d06`, against the
130,816-byte `RP2040.ld` budget:

| Board | Command | Kernel | Text | Root profile | Free blocks / inodes |
| --- | --- | --- | ---: | --- | --- |
| Raspberry Pi Pico | `bmake MACHINE=rp2040 distribution` | `PICO` (USB CDC) or `PICO_UART` | 107,432 / 95,970 | `full` | 239 / 32 |
| ClockworkPi PicoCalc | `bmake MACHINE=rp2040 BOARD=PICOCALC distribution` | `PICOCALC` | 123,332 | `picocalc` | 147 / 21 |
| PicoCalc with PSRAM swap | `bmake MACHINE=rp2040 BOARD=PICOCALC_PSRAM distribution` | `PICOCALC_PSRAM` | 125,448 | `picocalc` | 147 / 21 |

Free blocks are image figures from `PROFILES.md` as re-measured in the #350
merge; they sit one block below the figures #363 recorded for `base` through
`full`, and the cause is not traced. A booted, logged-in board reads three
fewer (see "Board runs"). PR #369, open, adds 304 bytes to PICOCALC.

## Test pipeline

| Stage | Entry | Proves | State |
| --- | --- | --- | --- |
| Host | `check-host` | Sources compile as strict C17 under sanitizers and match their oracles | CI on Linux and macOS |
| Cross | `check-cross`, `check-cross-kernel`, `check-board-build` | Kernel, libc and programs build for Cortex-M0+ with warnings as errors and fit the layout | CI |
| qemu-user Thumb-1 | `check-qemu` | Cortex-M0+ code executes on the ARMv6-M model | CI |
| qemu a.out loader | `bmake -C tests/qemu_aout check` | A DiscoBSD a.out runs its own libc and system calls | outside `check-qemu`; no fork, exec, signals, stat or tty ioctl |
| Renode | `check-renode`, `check-renode-picocalc` | PICO_UART and PICOCALC boot to a shell against third-party peripheral models | outside `check`; no CI owner |
| Bare Pico board | `discobsd-flash`, then `tests/rp2040` drivers | Behavior on the measured silicon | flash and boot of main observed 2026-11-06; the test programs not yet run |
| PicoCalc hardware | `PICOCALC.md`, "Board bring-up of the console" | Panel, keyboard, SD, PSRAM, power | not run: no PicoCalc has booted these kernels |

The macOS host tier has a 1,080-second budget (PR #358); two play mutants
that only a prompt host can see (`escape-deadline-5-ms`, `flash-too-short`)
report as unobserved there and must be rejected on Linux.

## Merge procedure

Each PR merges when CI is green on its head, a Codex review and a Copilot
review exist with every thread resolved, and a trial merge into current main
in a scratch worktree passes `tools/c17/source_denominator.py`,
`tools/check_build_observability.py` and `check-config-generated-sync`. Two
PRs that each add ledger rows, or one that adds a kernel config while another
edits `files.rp2040`, merge without a textual conflict and still break those
gates. Conflicts are resolved by reading base, ours and theirs and writing one
result that carries both sides' intent. PRs #358 through #367 merged without
any automated review; Codex reviewed them afterward, and their findings are
the first item below.

## Open work, in order

1. PR #365 carries the fixes for the after-the-fact review findings, then its
   own. Done on its branch (head `0438ea53`): STM32 division helpers selected
   per machine (`aeabi_div_idiv.S`, from #362), harness children launched
   through `run_owned` under execution ownership (from #361, #365, #367), and
   divider emulation in the qemu a.out runner (from #362). Remaining:
   - `sys/arch/rp2040/rp2040/clock.c` reads ICSR and TIMERAWL as unordered
     call arguments, so catch-up can count a tick twice at a boundary
     (from #360);
   - `fs` or `flash` after an `IMAGE_WITHOUT` distribution keeps a staged
     multicall binary that lacks a member the new selection installs
     (from #363);
   - `check-multicall-composition` requires a nonempty member map for
     profiles without games (from #363);
   - the macOS `%g` exact-tie cases, where Apple's libc keeps a trailing zero
     that C17 `%g` strips, must leave the host oracle on darwin as they already
     do for newlib under qemu, with a first-disagreement print;
   - Codex reviews of #358, #359, #366 and #367 are still to land.
   The aggregate gates (`check-qemu`, `logged-build`, `check-fs-profiles`,
   `check-config-generated-sync`, `check-divider`, `test_execution_test.py`)
   have not run on `0438ea53`; one SIGTERM ownership test missed its 20 s
   bound once and did not reproduce in six reruns. The review threads on #360,
   #361, #362 and #363 stay open until #365 merges.
2. PR #369 (issue #368): unmount of removed media, extent-aware write
   admission at securelevel 1, close at the last close, and flash
   `DIOCREINIT` refused under a mounted partition. Head `b56c6ede`; the
   Copilot thread is resolved; waiting on CI and a Codex review.
3. PR #343 (`fix(tar)`, opened 2026-10-24 by another session) fails three CI
   jobs and has no review; the owner decides whether to continue or close it.
4. The bare Pico campaign, steps 2-7 below.
5. Board runs on the PicoCalc: console bring-up in the `PICOCALC.md` order,
   `board_sd_detach.py` (from #369), PSRAM swap, USB-C power alone (the
   owner's board stays off on USB-C with no cells), and `bc | dc` and `calc`
   on the device.
6. Pipeline: a CI owner for the Renode gates, an STM32 build in CI (no job
   builds STM32, which is how #362's regression reached main), the qemu a.out
   loader grown into a gate, and the timing-bound flake in
   `tools/run_logged_gate_test.py`
   (`test_silent_leader_exit_wakes_before_inherited_output_pipe_closes`, 1 s
   bound, 1.0-1.6 s under load).
7. Software: re-record `docs/research/c17-shipped-resource-baseline.json`,
   which records commit `e7040aa1`, before the `calc-bignum`, `calc`,
   `puzzles` and `sdcard` closures; separate per-board output paths for
   `sdcard.img` and `flash.uf2`; `pow(-0, y)` for a negative odd integer `y`
   returns +infinity where Annex F gives -infinity; the `sys/exec.h:51` empty
   struct that keeps two console files off strict C17; the pixel-graphics
   console and the remaining survey games
   (`docs/research/picocalc-games-survey.md`).

## Board runs

The bare Pico (hostname `pico`) is attached to the development host through
usbipd as USB 2e8a:000a, console
`/dev/serial/by-id/usb-DiscoBSD_DiscoBSD_RP2040_console_rp2040-if00`. The
owner approved test flashes of it on 2026-11-06. `discobsd-flash` takes it
through BOOTSEL (2e8a:0003) and back without any usbipd action.

Observed on 2026-11-06, flashing main `df501d06`:

- Kernel `unix.uf2` SHA-256 `a34c9ff0c36b00ae1ef6b9b87fbb67f845064416e4c7d3603ef25cb19fce6de7`
  and root `flash.uf2` (full profile) SHA-256
  `0e9e660cec1fc382b410c005ccfdb22c643986b6d7f24c37c1172a89ad97e5f9`. The
  board boots to `login:` over USB CDC with `kern.version`
  `DiscoBSD 2.7 (PICO) #1 2259: Fri Nov  6 20:03:30 PST 2026`; `operator`
  logs in with an empty password; `machdep.mpu.enable=1`,
  `machdep.mpu.programmed=4`, `kern.clockrate` tick 1000 at hz 1000.
- `df` reads 236 free blocks on `/` where the image holds 239: the first
  boot creates `/var/log/messages`, `/var/log/wtmp` and `/var/run/utmp`, one
  block each.
- The clock starts from the image's last-update time, so after a reboot the
  board's date trails the host by the time since the image was built. The
  clock catch-up check (step 5) must measure an offset change, not an
  absolute offset.
- The board is left on that plain main image at `login:`.

Not run yet; a test root with the staged programs is built
(`test-flash.uf2`, full profile plus `calc-bignum`, `divtest`, `fptest`,
`romprobe`, `textcrc` and a raw copy of `textbox` as `/usr/rawbin/seq`, 106
free blocks) but its tests have not run:

2. `divtest`: the edge corpus against restoring division, the SIGALRM
   interleave loop, and the smallest error-free wait against the shipped four
   padding branches (PR #362).
3. dc and bc: `board_dcbc_smoke.py` with `DISCOBSD_BOARD_DCBC=run`, plus by
   hand `2 k 1.235 1 % p` (expect `.005`) and bc `f(a[])` (PR #361), which its
   check list lacks.
4. Packed against raw exec: 20 execs each of `/usr/bin/seq` (packed
   `textbox`, 38,900 bytes of text) and `/usr/rawbin/seq` under `time`;
   emulation predicts about 25 ms more per 40 KB packed after PR #367.
5. Clock catch-up (PR #360): the board-host offset before and after a
   `dd`-and-`sync` loop; prediction near zero change, against about 44 ms per
   sector erase before the change. No board script exists for it.
6. `board_stack_align.py` and `board_aout_admission.py`: no opt-in variable,
   and both reboot the board with `picotool reboot -f`.
7. `fptest` and `romprobe`: the boot-ROM float routines against libcalc's
   contract.

## Open decisions and residuals

- The puzzles license (L-4) and the first-round items CR-2, CR-3, CR-4, CR-6,
  T-7, T-10, TF-1 and TF-3 in `calculator-and-games-review.md`.
- libcalc's condition estimate is infinite when the inverse's 1-norm exceeds
  `DBL_MAX`.
- `kilo`'s highlight test fails when `TMPDIR` is a long path.
