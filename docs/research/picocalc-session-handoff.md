# PicoCalc bring-up: state of the port and the session plan

Drafted by an AI assistant (Claude Opus) as the hand-off for the PicoCalc
work in PRs #344 through #370.

This note names what the PicoCalc work put in the tree, how the board builds
share one source tree, which test stages exist between a host compile and the
physical boards, and the open work divided into self-contained session briefs.
Each brief names its prerequisites, its branch and worktree, its tasks in
order with their acceptance checks, and the record it updates when it ends, so
a fresh session can take one brief without the others. The authorities stay
where they were: `sys/arch/rp2040/doc/PICOCALC.md` for the board (pin map,
driver timing, evidence), `sys/arch/rp2040/doc/PROFILES.md` for image
composition, and `sys/arch/rp2040/doc/TESTING.md` for what each gate proves.
Every figure below comes from the document it cites or from the merge it
names.

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
- Removable-media handling in the generic disk layer (PR #369, issue #368):
  a mount on removed media is unmounted, write admission at securelevel 1
  compares partition extents, a mount closes the device only at its last
  close, and flash `DIOCREINIT` is refused under a mounted partition.
- PSRAM swap through PIO as a separate opt-in kernel, `PICOCALC_PSRAM`
  (`psram.c`, `psram_xfer.c`; PR #348). The baseline `PICOCALC` kernel carries
  no PSRAM code; `check-config-generated-sync` holds the PSRAM Config to the
  PICOCALC Config plus its board line and `device psram`.

Programs and libraries:

- OpenBSD `bc` and `dc` on the tree's clean-room `lib/libbn` (PR #351), with
  the POSIX remainder scale and a leading array argument (PR #361), shipped as
  the `calc-bignum` closure group of the `picocalc` profile.
- `usr.bin/calc` and `lib/libcalc` (PR #345).
- Conforming floating output and libm returns (PRs #353, #365). `%g` in
  `lib/libc/stdio/doprnt_float.c` counts significant digits from the first
  nonzero digit (C17 7.21.6.1p8), and `decimal_cvt()` generates the decimal
  digits from the double's bits in exact integer arithmetic, so every digit
  is correctly rounded with ties to even; the 0 flag pads with zeros under
  every floating conversion, a negative zero keeps its sign, `l` leaves the
  default precision at six, and `%F` prints. `include/math.h` defines
  `HUGE_VAL`, `HUGE_VALF`, `HUGE_VALL` and `INFINITY` as infinities; `exp()`
  overflows above ln(DBL_MAX) with `ERANGE` and reduces through a split
  ln 2 (error 5e-16); `log(0)`, `y0(0)`, `y1(0)`, `yn(n, 0)` and
  `pow(+-0, y<0)` report the pole as `ERANGE`, and `pow(+-0, y)` keeps the
  zero's sign for odd integer `y` (F.10.4.4). `check-libc-printf-g` and
  `check-libc-huge-val` hold each with a mutant (`TESTING.md`).
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

Kernel flash text from clean builds of #369's head `b56c6ede`, whose tree is
main `37cef7bb`, against the 130,816-byte `RP2040.ld` budget:

| Board | Command | Kernel | Text | Root profile | Free blocks / inodes |
| --- | --- | --- | ---: | --- | --- |
| Raspberry Pi Pico | `bmake MACHINE=rp2040 distribution` | `PICO` (USB CDC) or `PICO_UART` | 107,680 / 96,210 | `full` | 239 / 32 |
| ClockworkPi PicoCalc | `bmake MACHINE=rp2040 BOARD=PICOCALC distribution` | `PICOCALC` | 123,620 | `picocalc` | 147 / 21 |
| PicoCalc with PSRAM swap | `bmake MACHINE=rp2040 BOARD=PICOCALC_PSRAM distribution` | `PICOCALC_PSRAM` | 125,728 | `picocalc` | 147 / 21 |

Free blocks are image figures from `PROFILES.md` as re-measured in the #350
merge; they sit one block below the figures #363 recorded for `base` through
`full`, and the cause is not traced. A booted, logged-in board reads three
fewer (see "Board runs"). PR #369 added 248, 240, 288 and 280 bytes of text
to the four kernels; it changes no root image.

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

Each PR merges when CI is green on its head (a cancelled check is not green),
a Codex review and a Copilot review exist with every thread resolved, and a
trial merge into current main in a scratch worktree passes
`tools/c17/source_denominator.py`, `tools/check_build_observability.py` and
`check-config-generated-sync`. Two PRs that each add ledger rows, or one that
adds a kernel config while another edits `files.rp2040`, merge without a
textual conflict and still break those gates; a stale generated kernel
Makefile is regenerated in two steps, as `tools/config/check_generated_sync.py`
does: `bmake -j1 MACHINE=rp2040 config` run inside `tools/config` builds the
generator, then `../../../../../tools/config/config Config` run inside
`sys/arch/rp2040/compile/BOARD` rewrites that board's `Makefile`. Conflicts
are resolved by reading base, ours and theirs and writing one result that
carries both sides' intent. PRs #358 through #367, except #365, merged
without any automated review; Codex reviewed them afterward, and #365 carries
the fixes for their findings: STM32 division helpers selected per machine,
harness children launched under execution ownership, divider emulation in
the qemu a.out runner, the timer sampled before the SysTick pending bit in
`clock.c`, the multicall games restaged for every image, and the full
composition built by `check-multicall-composition`.

Pass `-R Oichkatzelesfrettschen/discobsd-2040-unofficial` to every `gh`
command: the checkout's default `gh` repository is `upstream`.

## Session plan

The briefs below are independent of each other, apart from the PicoCalc
brief, which needs the PicoCalc attached; the PicoCalc brief needs the PicoCalc attached. PR #343
waits on an owner decision and belongs to no brief.

Open every session with the same checks: `git fetch origin`, `git worktree
list --porcelain` from the canonical checkout, the CI and review state of each
open PR, and free space on `/tmp`, which other sessions' test runs fill; route long
output to files outside it.

### Bare Pico campaign: the test programs on silicon

Prerequisites: none. #365 put the divider and harness fixes on main for
the campaign to measure. The owner approved test flashes of the bare Pico on
2026-11-06; flash nothing else without a new approval. Rebuild the test root
from current main: stage the entries below in `distrib/rp2040/mi.rp2040`,
build, flash, and revert the staging before any commit.

The bare Pico (hostname `pico`) is attached to the development host through
usbipd as USB 2e8a:000a, console
`/dev/serial/by-id/usb-DiscoBSD_DiscoBSD_RP2040_console_rp2040-if00`.
`discobsd-flash` takes it through BOOTSEL (2e8a:0003) and back without any
usbipd action. `operator` logs in with an empty password.

The test root holds the full profile plus `calc-bignum`, `divtest`, `fptest`,
`romprobe`, `textcrc` and a raw copy of `textbox` as `/usr/rawbin/seq`; at
main `df501d06` it left 106 free blocks. Record each run's kernel and root
SHA-256 and `kern.version` under "Board runs".

1. `divtest`: the edge corpus against restoring division, the SIGALRM
   interleave loop, and the smallest error-free wait against the shipped four
   padding branches (PR #362).
2. dc and bc: `board_dcbc_smoke.py` with `DISCOBSD_BOARD_DCBC=run`, plus by
   hand `2 k 1.235 1 % p` (expect `.005`) and bc `f(a[])` (PR #361), which
   its check list lacks; add both to the script's list.
3. Packed against raw exec: 20 execs each of `/usr/bin/seq` (packed
   `textbox`, 38,900 bytes of text) and `/usr/rawbin/seq` under `time`;
   emulation predicts about 25 ms more per 40 KB packed after PR #367.
4. Clock catch-up (PR #360): the board-host offset before and after a
   `dd`-and-`sync` loop; prediction near zero change, against about 44 ms per
   sector erase without catch-up. The clock starts from the image's
   last-update time, so measure the offset change, not the absolute offset.
   No board script exists; write one under `tests/rp2040/clock_catchup`
   with an exact opt-in variable.
5. `board_stack_align.py` and `board_aout_admission.py`: neither has an
   opt-in variable, and both reboot the board with `picotool reboot -f`.
   Add the opt-in gate before running them.
6. `fptest` and `romprobe`. `fptest` checks the boot-ROM float helpers bit
   for bit on finite normal operands and signed zero, with subnormal, NaN,
   infinity and rounding-boundary cases deferred to
   `sys/arch/rp2040/doc/research/float-libs.md`; `romprobe` prints the ROM
   header and the resolver's table lookups so a resolver exit 70 can be traced.
   Neither exercises libcalc's numeric contract.

Done when each step has a recorded observation or a named blocker, the board
is back on plain main at `login:`, and the scripts the steps added are in a
merged PR.

### PicoCalc board bring-up

Prerequisites: the PicoCalc attached to the host and the owner's approval to
flash it. No PicoCalc has booted these kernels.

1. Console bring-up in the `PICOCALC.md` order: panel, keyboard, then the
   tty.
2. SD: mount, read and write through `sd0a`, then `board_sd_detach.py` (from
   #369) for removal under a mount.
3. PSRAM swap on the `PICOCALC_PSRAM` kernel.
4. Power: the keyboard-controller battery, backlight and power-off sysctls,
   and USB-C power alone (the owner's board stays off on USB-C with no
   cells).
5. `bc | dc` and `calc` on the device.

Done when each step's observation is recorded in `PICOCALC.md` with the
kernel's SHA-256 and `kern.version`.

### Test pipeline owners

Prerequisites: none beyond current main.

1. A CI owner for `check-renode` and `check-renode-picocalc`.
2. An STM32 build in CI. No job builds STM32, which is how #362's division
   regression reached main.
3. The qemu a.out loader grown into a gate in `check-qemu`, with its
   execution class added to `TESTING.md`.
4. The timing-bound flake in `tools/run_logged_gate_test.py`
   (`test_silent_leader_exit_wakes_before_inherited_output_pipe_closes`,
   1 s bound, 1.0-1.6 s under load): replace the wall-clock bound with an
   event the test observes.

Done when each item is merged with its gate calibrated against a known-good
and a known-bad input.

### Software follow-ups

Prerequisites: none.

1. Re-record `docs/research/c17-shipped-resource-baseline.json`, which
   records commit `e7040aa1`, before the `calc-bignum`, `calc`, `puzzles` and
   `sdcard` closures.

The per-board image paths (`sdcard-BOARD.img`, `flash-BOARD.uf2` beside the
PICO names), strict C17 for `picokbd.c` and `lcdcons.c`
(`check-console-c17-cross`) and the `publishat_beneath` ignore entries are
done.

Done when each item is merged or carries a tracking issue.

### Pixel console and games

Prerequisites: none beyond current main. The pixel-graphics console and the
remaining survey games in `docs/research/picocalc-games-survey.md`, each game
as its own closure in the `picocalc` profile with a free-block figure in
`PROFILES.md`.

### Owner decision: PR #343

PR #343 (`fix(tar)`, opened 2026-10-24 by another session) fails three CI
jobs and has no review. Its Codex worktree holds one unpushed commit. The
owner decides whether to continue or close it; no session touches it before
that.

## Board runs

Observed on 2026-11-06, flashing main `df501d06` to the bare Pico:

- Kernel `unix.uf2` SHA-256 `a34c9ff0c36b00ae1ef6b9b87fbb67f845064416e4c7d3603ef25cb19fce6de7`
  and root `flash.uf2` (full profile) SHA-256
  `0e9e660cec1fc382b410c005ccfdb22c643986b6d7f24c37c1172a89ad97e5f9`. The
  board boots to `login:` over USB CDC with `kern.version`
  `DiscoBSD 2.7 (PICO) #1 2259: Fri Nov  6 20:03:30 PST 2026`;
  `machdep.mpu.enable=1`, `machdep.mpu.programmed=4`, `kern.clockrate` tick
  1000 at hz 1000.
- `df` reads 236 free blocks on `/` where the image holds 239: the first
  boot creates `/var/log/messages`, `/var/log/wtmp` and `/var/run/utmp`, one
  block each.
- The board is left on that plain main image at `login:`.

## Open decisions and residuals

- The puzzles license (L-4) and the first-round items CR-2, CR-3, CR-4, CR-6,
  T-7, T-10, TF-1 and TF-3 in `calculator-and-games-review.md`.
- libcalc's condition estimate is infinite when the inverse's 1-norm exceeds
  `DBL_MAX`.
- `kilo`'s highlight test fails when `TMPDIR` is a long path.
- Floating output past about 320 digits of precision prints the exact digits
  and then zeros. The `%lg` width defect named by an earlier note does not
  reproduce (`%10lg` and `%-10lg` of 3.5 pad to ten columns), so its failing
  input stays unidentified.
- `log` and the Bessel functions return `-HUGE_VAL` for a negative argument
  where Annex F asks for a NaN; `pow` of a negative base and a non-integer
  power returns 0 with `EDOM`; `pow(+-0, NaN)` returns 0 where Annex F gives
  a NaN; `ldexp` flushes a result under 2^-1024 to zero, so `exp` returns
  zero where the result is a subnormal under 2^-1024.
