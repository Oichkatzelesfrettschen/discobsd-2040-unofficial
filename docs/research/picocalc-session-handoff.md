# PicoCalc bring-up: state of the port and the session plan

Hand-off for the PicoCalc work in PRs #344 through #370.

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
  `puzzlebox` (flood, samegame, unruly, lightup) (PRs #345, #354). `ridge`,
  the tile platformer on `/dev/lcdgfx`, joins `gamebox` in the `picocalc`
  profile only (`sys/arch/rp2040/doc/research/games.md`).

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

Kernel flash figures from clean builds of main with the `-Os` change and the
`/dev/lcdgfx` pixel device (PR #379), then with the tile atlas and the raw key
events, under the Linux CI compiler (Ubuntu 24.04 `gcc-arm-none-eabi`
15:13.2.rel1-2, GCC 13.2.1), against the 130,816-byte `RP2040.ld` FLASH
region. Flash image is the span `check-kernel-headroom` measures: `.text`
plus the `.data` and `.ramfunc` initializers, without `.boot2`.

| Board | Command | Kernel | Text | Flash image | Headroom | Root profile | Free blocks / inodes |
| --- | --- | --- | ---: | ---: | ---: | --- | --- |
| Raspberry Pi Pico | `bmake MACHINE=rp2040 distribution` | `PICO` (USB CDC) or `PICO_UART` | 95,403 / 85,618 | 95,396 / 85,556 | 35,420 / 45,260 | `full` | 239 / 32 |
| ClockworkPi PicoCalc | `bmake MACHINE=rp2040 BOARD=PICOCALC distribution` | `PICOCALC` | 112,975 | 113,088 | 17,728 | `picocalc` | 147 / 20 |
| PicoCalc with PSRAM swap | `bmake MACHINE=rp2040 BOARD=PICOCALC_PSRAM distribution` | `PICOCALC_PSRAM` | 115,133 | 115,304 | 15,512 | `picocalc` | 147 / 20 |

The PicoCalc rows include the tile atlas and the raw key events
(`sys/arch/rp2040/doc/PICOCALC.md`, Tile atlas). Under GCC 13.2.1 at `-Os`,
before and after, in text / data / bss from `arm-none-eabi-size` on `unix`,
built in a scratch copy of main:

| Kernel | Before | After | Headroom before | Headroom after |
| --- | --- | --- | ---: | ---: |
| `PICOCALC` | 111,543 / 368 / 48,824 | 112,975 / 368 / 53,088 | 19,160 | 17,728 |
| `PICOCALC_PSRAM` | 113,701 / 424 / 48,976 | 115,133 / 424 / 53,240 | 16,944 | 15,512 |
| `PICO` | 95,395 / 248 / 39,848 | 95,395 / 248 / 39,848 | 35,428 | 35,428 |
| `PICO_UART` | 85,610 / 192 / 14,832 | 85,610 / 192 / 14,832 | 45,268 | 45,268 |

The change adds 1,432 bytes of text and 4,264 of bss to each PicoCalc kernel and
nothing to the Pico kernels. The before column is 8 bytes of text below the
figures of the pixel-device paragraph that follows, the offset `version[]`
carries between build paths; the before and after columns share one path. The
PICOCALC_PSRAM kernel keeps 15,512 bytes of the region, above the 4,096-byte
floor the tile work set, and the kernel's data and bss end 62,408 bytes below
the end of the RAM region.

Before the pixel device, which adds 2,145 text bytes to each PicoCalc kernel
and the `/dev/lcdgfx` node (one inode) to the `picocalc` profile, `-Os` under
GCC 13.2.1 measured 109,406 (PICOCALC) and 111,564 (PICOCALC_PSRAM). The
sources without the pixel device at `-O`, which the kernels used before
`-Os`, measured
109,488 (PICO), 97,746 (PICO_UART), 126,100 (PICOCALC) and 128,296
(PICOCALC_PSRAM) text bytes under GCC 13.2.1, and 107,768, 96,298, 124,356 and
126,472 under the local GCC 16.2.0. With `/dev/lcdgfx` (PR #379) the `-O`
PICOCALC_PSRAM kernel under GCC 13.2.1 loads 130,984 bytes into the
130,816-byte region, 168 over, and `ld` accepts it because the `.ramfunc`
initializers carry no region. Under `-Os` that kernel loads 113,868 bytes under
GCC 13.2.1 and 113,276 under GCC 16.2.0.

Local GCC 16.2.0 `-Os` text bytes, in the same board order: 94,878, 85,121,
108,942 and 111,004. The Linux x86-64 build of Arm GNU Toolchain 15.3.Rel1
(tarball SHA-256
`563bebb2b97d53382b956d6ee1fe61e2cae26699901417234a37df505ef9b5fa`) gives
95,422, 85,625, 109,446 and 111,556 at `-Os`. The macOS job installs the Darwin
package of that release, which this table does not measure.

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
| Bare Pico board | `discobsd-flash`, then `tests/rp2040` drivers | Behavior on the measured silicon | flash and boot of main observed 2026-11-06; campaign run 2026-11-07, see "Board runs" |
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

### Bare Pico campaign, 2026-11-07 (main `4bcdaad0`)

Images, built from a clean tree with `IMAGE_WITH=calc-bignum` and
`FS_INODES=144`: kernel `unix.uf2` SHA-256
`85378d94d3d99f2eef6a5fa037bf7fa1a3ca8a6783747a5bd70a491ca391239b`; test root
`flash.uf2` SHA-256
`0bf515990bbd70d4c05811c0c070062b5797e8ad9d697c9ee13b68f7c63a03b9` (the full
profile, `calc-bignum`, `divtest`, `fptest`, `romprobe`, `textcrc`,
`/usr/bin/time`, and a raw copy of `textbox` as `/usr/rawbin/seq`; `fsutil`
reports 99 free blocks and 38 free inodes, a booted `df` reads 96). Every
step below ran on this pair; the booted `kern.version` is
`DiscoBSD 2.7 (PICO) #1 2291: Sat Nov  7 09:47:21 PST 2026`. Console commands
in the steps ran as `operator`.

Setup findings:

- `/etc/rc` failed on the first test root (128 inodes) and left the board in a
  root shell with no hostname: `fsck -p` exits 8 with "DIRECTORY CORRUPTED" on
  `/usr/bin` and two unreferenced files. The directory's second block holds
  `fptest` at inode 128. `sbin/fsck/setup.c` sets `imax` to
  `(fs_isize - (SUPERB+1)) * INOPB`, which is 128 for the 128-inode image, and
  `dircheck()` in `sbin/fsck/dir.c` and the pass-1 loop accept only inodes
  below `imax`; `tools/fsutil/inode.c` and the kernel accept inode 128 (`ls -i`
  prints it and `fptest` runs). The image's highest inode is therefore
  invisible to `fsck`, which reports 104 files where `fsutil --check` counts
  105. Confidence high for the off-by-one (inode, count and bound all read
  directly). The test root works around it with `FS_INODES=144`; the fix is
  outside this note.
- `/usr/bin/time` is absent from every shipped profile, and the shell has no
  `time` keyword, so the brief's `time` commands need the program staged.
- The tty echoes the shell prompt after every command, so a harness that
  compares output lines has to drop the `operator@pico:~$ ` line; the three
  scripts below do. `bc -c` ends its output without a newline, so a sentinel
  echo needs a blank echo before it.
- After review, the dc and bc script ends a command only at a complete,
  newline-terminated sentinel line, and the clock script runs its workload
  under `set -e`, keeps `dd`'s diagnostics and requires the records `dd` reports
  out to match the bytes written. The recorded board runs used the earlier
  forms of both; no run since exercised the new ones on the board.
- The board's `sh -n` runs the script instead of parsing it, `tail -n 1` is
  not accepted (`tail -1` is), and `bc FILE` goes on to read standard input.
- The USB link wedged once after `picotool reboot -f` on the running kernel
  (the board enumerated at address assignment and failed every descriptor
  read), and once after a kernel-only `discobsd-flash` near the end of the
  campaign. Each leaves the stale USB/IP import holding the device; the first
  needed the owner and the second is open. The scripts now reboot only through
  `discobsd-flash --bootsel` and `--eject`, which restarted the board
  cleanly in the dc and bc, stack alignment and a.out admission runs.
- Another session flashed the same board between 11:25 and 11:42 PST (a
  kernel `#2295` and its own root for `board_large_mpu.py`); console lines
  typed in that window landed in its shell. The steps below ran before and
  after that window on the images above, restored by `discobsd-flash` with the
  same two files.

Steps:

1. `divtest`, three runs, identical: the corpus is 141 values with 0 wrong;
   the SIGALRM loop completes 3,000,000 divisions with 3 alarms delivered, 0
   wrong in the loop and 0 wrong in the handler; the padding probe gives 2000,
   2000, 2000, 1998, 0, 0 and 0 wrong of 2000 for 0 through 6 padding
   branches, so the smallest error-free padding is 4, equal to the shipped 4;
   `DIVTEST OK`, 3.4 s real. The shipped wait of eight cycles has no slack on
   this board: six cycles fail 1998 of 2000 probes. The alarm count deviates
   from the interleave's design of 100 deliveries at 10 ms: `setitimer()` in
   `sys/kern/kern_time.c` stores `ITIMER_REAL` in whole seconds and rounds a
   sub-second interval up to one second, so the loop runs for 3 s and takes 3
   deliveries. The interleave covers 3 handler entries per run; a child that
   signals the parent in a loop would raise the count.
2. `board_dcbc_smoke.py`: all 15 checks pass, including the two added ones
   (dc `2 k 1.235 1 % p` prints `.005`; `scale=2; 1.235%1` in bc prints
   `.005`) and bc `f(a[])` called with an array, which prints `7`.
   `2 1000 ^ p` prints in 2.6 s. The one-line form `define f(a[]) { return
   a[0]; }` fails with "return unexpected", as does `define f(n) { return n;
   }`: bc takes a newline after `{`, and the script's check uses that form.
   The prediction (`.005` and an accepted array argument) holds; the one-line
   spelling in the brief is not accepted.
3. Packed against raw `seq`, 10 interleaved pairs of 20 execs under `time`:
   packed 20.81 s (standard deviation 0.35), raw 20.05 s (0.25), so packed
   costs 38 ms more per exec (standard error 7 ms). Prediction: about 25 ms.
   The measurement is 1.9 standard errors above it and 2 below the 52 ms that
   two decode passes at the 67.7 cycles per byte of PR #367 would give
   (`docs/research/rp2040-second-core-hardware.md` counts two passes per
   exec); neither value is excluded. Each exec costs about 1 s either way,
   because a fork under the single-process window swaps the parent through the
   raw swap partition.
4. `board_clock_catchup.py` (shipped kernel): 24 passes of a 32 KB `dd` and
   `sync` in 248.8 s move the host-minus-board offset from the interval
   35080.864 .. 35080.989 s to 35081.263 .. 35081.325 s, and an equal idle wait
   to 35081.305 .. 35081.377 s; 12 passes in 122.9 s move it from 35081.338 ..
   35081.419 s to 35081.527 .. 35081.617 s and an idle wait to 35081.580 ..
   35081.678 s. The excess over the idle control, 2 * second - first - third
   with each bound propagated, is the interval +0.160 .. +0.481 s for 24
   passes and -0.043 .. +0.316 s for 12. Prediction: near zero. Against the
   8.45 and 4.22 s that 44 ms per sector erase would lose, the intervals sit
   inside the tolerance of a tenth of that floor (0.845 and 0.422 s), so both
   runs pass the final rule. The excess roughly doubles with the work, which
   fits the host model's bound of one tick lost per masked window
   (`check-clock-catchup`). The run summary printed at the time used the
   midpoints (+0.321 and +0.137 s) against a 0.25 s absolute tolerance, which
   failed the 24-pass run; the rule changed to the tenth-of-floor tolerance
   after those two runs and then to the interval rule above after review, and
   the verdicts above restate the saved offsets under the final rule. A kernel
   built with the `if (owed != 0)` guard in `clock_catchup()` made false
   (unix.uf2 SHA-256
   `bbb82164070a0c7e6980f0d74900e431fba0cc2e288b8ede3655788c6494b38b`) lost
   108 s to the 12-pass workload and 12.5 s to the idle control, 23 times the
   44 ms floor. Under the final rule its baseline measurement has no common
   point (35364.057 .. 35348.016 s), so the script ends inconclusive with exit
   status 2 rather than reporting a failure; the clock moved 16 s during one
   40-line date loop. The 44 ms per erase figure therefore understates the
   loss without the catch-up. The final script was not rerun on the board
   because the USB link wedged first.
5. `board_stack_align.py`: 12 of 12 pass (awk `print 1+1` prints `2` at eight
   environment lengths and `printf` prints `3 2.5 z` at four).
   `board_aout_admission.py`: the truncated and the even-entry image return
   status 127 with the shell's "not found" (`exec_aout_check()` returns
   `ENOEXEC`, so the shell reads the header as a script), the oversized image
   returns 126 "too big" (`ENOMEM`), and the shell then runs `echo`, a child
   `sh` and `awk`. Two outcomes, not the three distinct errors the script's
   description gave; the description now states the observed ones.
6. `fptest` prints `FPTEST OK` in 0.1 s. `romprobe` prints the ROM header
   (magic bytes `0x4d 0x75 0x01`, version 3), the function table at `0x7a`, the
   data table at `0xc4`, the lookup at `0x1d`; the SF table at `0x1cc` with
   `byte[-2]=0x20`, size 128, entries `0x2b45 0x2b41 0x2c0d 0x2cd9 0x2803
   0x2803`; and the SD table at `0x24c` with `byte[-2]=0`, size 0, entries
   `0x2e3d 0x2e31 0x2f99 0x30f5 0x3447 0x3447`.

State at the end: the board holds the kernel and test root named above and
does not enumerate (the second USB wedge). It has not been returned to the
plain main image. The board was later recovered and is covered by the 2026-11-08 entry.

### Bare Pico re-measure, 2026-11-08 (main `7d8d1bce`)

**Scope.** This run measures the tree after the kernels moved to `-Os` (2fc33ee2) and the interval timer moved to tick resolution (PR #390). The owner approved it on 2026-11-08.

**Images.** All were built from a clean worktree of `7d8d1bce`.

| Image | Contents | `unix.uf2` SHA-256 | `flash.uf2` SHA-256 |
|---|---|---|---|
| Plain | kernel and full root | `75f84e68f328a2874c44c3d196dcdccfb80e04854b9a0697dbb39f04babf1dd8` | `804f51b257be39539b6ed420ea17b5e9a9f87ec636d2616ac9529c91e5027b45` |
| Test root | `IMAGE_WITH=calc-bignum`, `FS_INODES=144`, plus `divtest`, `fptest`, `romprobe`, `textcrc` and a raw copy of `textbox` as `/usr/rawbin/seq`, staged under their own `default` section with `filemode 0755` | plain kernel | `c22e06ef4e651805b82cadd1b3c1eb443885356db7aec8698e78a03a245cc0a2` |
| Calibration kernel | `if (0 && owed != 0)` in `clock_catchup()` | `040747daeebd22991efdc98a0df38e1cb78523feed004523228ae566520e46eb` | test root |

- **Booted version:** `kern.version` is `DiscoBSD 2.7 (PICO) #1 2410: Sun Nov  8 08:43:29 PST 2026`.
- **Test root space:** 108 free blocks and 39 free inodes, of which a booted `df` reads 105.
- **Plain root after restore:** a booted `df` reads 238 free blocks, the image's 241 less the three first-boot log files.

**Setup finding: a staging error made a boot loop.**
- **Symptom:** a staging edit deleted both `filemode 0775` lines of `distrib/rp2040/mi.rp2040`, so every `/bin` and `/sbin` program shipped with mode `0644`. The board then re-enumerated about every 7.5 s and printed nothing on the console.
- **Diagnosis:** a host Dhara decoder recovered both roots from their `flash.uf2`. It replays `tools/flashimg`'s vendored map in reverse and matches the source `sdcard.img` byte for byte. `fsutil --extract` diff of the two showed the mode change.
- **Fix:** staging under its own `default` section with `filemode 0755` booted.
- **Not a kernel defect:** a root whose `/sbin/init` cannot execute restarts by design.
- **Reboot check:** the good root survived three consecutive `--bootsel`/`--eject` reboots with no loop.

**Steps.** Console commands ran as `operator`.

1. **Boot and capacity.**
   - **Boot:** the board boots to `login:` over USB CDC; `kern.clockrate` gives hz 1000.
   - **Kernel stack:** `sysctl kern.capacity` reads a stack peak of 1,736 of 2,048 bytes with 312 untouched and no saturation, on every boot of both kernels. The retained stdio workload record is 1,828.
   - **Clists:** the first boot of the test root read a clist peak of 32 of 32 with 7 allocation failures. The four later boots read peaks of 19, 19, 8 and 8 with no failures.
   - **Dropped console input:** a 120-character console line typed while the board was idle lost its last two characters (`?` and the carriage return). Clist exhaustion is the likely cause, not traced.
2. **Interval timer (PR #390).**
   - **Result:** three runs of `divtest` give `collision: 957441 / 958209 / 958049 divisions, 100 alarms, 0 wrong, 0 wrong in handler` and `DIVTEST OK` in 1.70, 1.71 and 1.70 s real.
   - **Prediction:** 100 alarms in about 1.0e6 divisions.
   - **Falsifiers:** 3,000,000 divisions with fewer than 100 alarms, or fewer than about 0.8e6 divisions at 100 alarms. Neither occurred.
   - **Before the change:** on 2026-11-07 the same loop took 3 alarms in 3,000,000 divisions.
   - **Divider padding:** the probe is unchanged. Padding 0 through 3 fails 2000 of 2000, and 4 through 6 fail 0, so the smallest error-free padding is 4, equal to the shipped value.
   - **`/usr/bin/time`:** the runs exercise it as a `utilbox` member (PR #389) on the board.
3. **Packed against raw `seq`.**
   - **Method:** six interleaved pairs of 20 execs each under `time`. The seventh pair's console line lost characters, as in step 1.
   - **Packed:** 26.14, 26.51, 26.42, 26.45, 26.59 and 26.95 s; mean 26.51 s, sd 0.26.
   - **Raw:** 25.74, 27.41, 26.99, 26.74, 26.54 and 26.15 s; mean 26.60 s, sd 0.60.
   - **System time:** packed stays at 6.8 s while raw ranges from 5.8 to 12.1 s.
   - **Packed minus raw:** the paired difference is -4 ms per exec with a standard error of 13 ms.
   - **Sampling interval:** the six pair differences have a mean of -4.25 ms
     per exec and a two-sided 95% paired Student t interval of approximately
     -37.3 to +28.8 ms per exec. The interval assumes independent paired
     differences and excludes systematic timing and console errors.
   - **Prediction failed:** the prediction was the 2026-11-07 38 ms within two standard errors. The result is about 2.8 combined standard errors from it, so the packed premium is not resolved at this sample size.
   - **Across-run comparison:** both arms are about 30 percent slower than on
     2026-11-07, at 1.33 s per exec against 1.02 s. The kernel configuration
     and other conditions differ across the runs, so the comparison does not
     isolate the cost of `-Os` on fork, swap and exec. The new timer adds one
     comparison per tick.
4. **Clock catch-up, with the final script (`board_clock_catchup.py`).**
   - **Shipped kernel:** 786,432 bytes were written in 287.5 s (at least 192 sectors erased, 8.45 s lost without catch-up). The excess over the 356.0 s idle control is +0.138 .. +0.547 s, inside the 0.845 s tolerance: PASS.
   - **Calibration kernel:** INCONCLUSIVE (exit 2). Its baseline's 40 date lines have no common point (28963.970 .. 28945.557 s), because the clock lost about 18 s within the loop, as on 2026-11-07.
   - **What the calibration shows:** the script rejects the known-bad kernel as not passing, but does not report it as failing. The script now classifies a measurement whose clock fell behind across the loop as FAIL (same branch as this note).

**State at the end.** The board holds the plain images above and sits at `login:`.

### Packed executable timing on an `-Os` kernel, 2026-11-09

On one bare RP2040 board, 24 alternating pairs of ten `seq` executions
measured a packed-minus-raw elapsed-time difference of **39.75 ms per
execution**. The paired standard deviation was 40.93 ms and the standard
error was 8.36 ms. A two-sided 95% Student t interval with 23 degrees of
freedom was 22.47 to 57.03 ms per execution. That interval assumes the
paired differences support the independent-pair t model; alternating order
does not establish independence. The positive interval resolves the sign
for this workload under that model and includes the earlier 38 ms estimate.
The result does not isolate decompression time, establish the effect of
switching optimization settings, or apply automatically to other programs,
images, boards or workloads.

The PICO kernel and full-profile root were built from clean commit
`f3b2d0482af51b98b9243e7a3d4b3d7e5ef168b6` with
`PYTHON` set to the host interpreter, using `bmake MACHINE=rp2040
logged-distribution` and `bmake MACHINE=rp2040 logged-flash`. The kernel
uses `-Os`, `SWAPRAM_KB=16`, and a 125 MHz CPU. The booted kernel reported
build number 2479. The kernel UF2 SHA-256 was
`487fa565aa9032ec0cca1682851cd7d8b708ca1621b7ec6517c21abf39341b30`.
The test root UF2 SHA-256 was
`1326a4a39977f915ee675c54996eebf87bf931a9845def10d34aa38eccf95cce`.
The test root added a directory `/usr/rawbin` and a raw `textbox` copy as
`/usr/rawbin/seq`, with 144 inodes. A host filesystem check passed with
197 free blocks. Host extraction and `hsaout -d` verified the installed
packed `textbox` expands byte-for-byte to the raw copy; both expanded files
had SHA-256 `3b5bf95030390a1b510cbd50e90e30843b1057f76a350c8080b9d8a5ccbe14f6`.
Both applet paths printed `1` in a board smoke check.

The runner sent the following command over USB serial, substituting
`/usr/rawbin/seq` for the raw arm and checking the marker for exit status 0:

    /usr/bin/time sh -c 'for i in x x x x x x x x x x; do /usr/bin/seq 1 >/dev/null || exit 1; done'; echo QZX$?

The runner paced console input, required a complete status marker and a
`time` output line, and alternated packed-first on odd pairs with raw-first
on even pairs. The first attempted 20-execution command exceeded the console
line editor's input capacity and lost its marker; the runner excluded that
unverified attempt before the fixed 24-pair sequence. Each accepted arm
returned status 0. `time` prints elapsed time in hundredths of a second,
corresponding to 1 ms per execution for a ten-execution loop. The measured
packed and raw elapsed times in seconds, in pair order, were:

    packed 13.23 13.45 13.25 13.57 13.24 13.40 13.24 13.28 13.31 13.22 13.29 13.28
    raw    12.71 13.96 12.73 12.96 12.89 13.56 12.79 12.99 12.94 12.94 13.37 12.75
    packed 14.22 13.24 13.47 13.44 13.66 13.86 13.25 13.49 13.39 13.22 13.29 13.29
    raw    12.97 12.75 12.72 12.77 12.88 12.77 12.75 13.85 12.80 12.96 12.93 13.30

The packed-first pairs averaged +53.0 ms per execution and the raw-first
pairs averaged +26.5 ms; these descriptive subsets leave order and time
effects uncertain. System-time differences averaged +27.33 ms per execution
with a paired 95% t interval from -27.71 to +82.38 ms. Both arms had
system-time spikes and the calculation retained every successful pair.
The unmodified `-Os` packed loader expands both streams during preflight and
again after commit; the loop also exercises shell, fork, wait and swap paths.
Direct timing around the loader and a matched `-O` kernel remain separate
experiments.

After the measurement, `discobsd-flash` restored the same kernel and the
plain full-profile root, whose UF2 SHA-256 was
`caf6b3ad761300d457f9d668235b041bd1ef0e32d7e0c8f4b89cddddb3d3bee8`.
The restored board reached the USB console `login:` prompt.

### Packed executable timing on an `-O` kernel, 2026-11-09

The bare PICO board ran the same test root and executable pair used for the
preceding `-Os` measurement. A separate clean build of commit
`f3b2d0482af51b98b9243e7a3d4b3d7e5ef168b6` used ARM GCC 16.2.0 with
`COPTS=-O`, `-g`, `-Wall -Wextra -Werror` and the PICO configuration. The
kernel's `kern_time.c` retained the build's explicit `-Os` rule. The kernel
UF2 SHA-256 was
`c7c26d7a1a6ebb1298db78b46c1a6e2e8bcc7ee8c07b747e6a154490fb445545`;
the test-root UF2 SHA-256 was
`1326a4a39977f915ee675c54996eebf87bf931a9845def10d34aa38eccf95cce`.

Twelve alternating pairs of ten executions used the same command and status
check as the preceding measurement. Packed execution averaged 1.1205 seconds
per execution and raw execution 1.0572 seconds. The mean paired premium was
63.33 ms, standard error 8.68 ms, with a 95% paired t interval of 44.24 to
82.43 ms under the independent-pair model. The paired elapsed times in
seconds, packed then raw, were:

    packed 11.31 11.27 10.98 11.14 10.90 11.61 11.05 10.97 11.65 11.64 10.95 10.99
    raw    10.62 10.48 10.71 10.54 10.54 10.58 10.66 10.50 10.57 10.55 10.46 10.65

The earlier `-Os` run averaged 1.3399 and 1.3002 seconds per execution for
packed and raw, respectively. The `-O` means are 16.4% and 18.7% lower
relative to those `-Os` means. The optimization campaigns ran sequentially,
not interleaved across kernels, so time-dependent board and filesystem
effects remain possible. The result does not isolate the compiler's effect
or establish the cause of either premium. GCC's reported optimizer settings
for `-O` and `-O1` were identical on this toolchain.

`discobsd-flash` then restored the kernel and plain root named in the
preceding section; the board reached the USB console `login:` prompt.

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
