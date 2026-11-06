# PicoCalc bring-up: state of the port and the next work

Drafted by an AI assistant (Claude Opus) as the hand-off for the PicoCalc
work in PRs #344, #345 and the test-pipeline PR that follows them.

This note names what the PicoCalc work put in the tree, how the two board
builds share one source tree, which test stages exist between a host compile
and the physical boards, and the next work in order. The authorities stay
where they were: `sys/arch/rp2040/doc/PICOCALC.md` for the board (pin map,
driver timing, evidence), `sys/arch/rp2040/doc/PROFILES.md` for image
composition, and `sys/arch/rp2040/doc/TESTING.md` for what each gate proves.
Every figure below comes from the document it cites.

## What is in the tree

Console (PR #344, merge commit `6a3cbc88`):

- `sys/arch/rp2040/dev/st7365p.c`: the 320x320 ST7365P panel on SPI1 with
  hardware vertical scroll and a 16-entry palette.
- `lcdterm.c`, `lcdfont.c`: a 53x32 xterm-basic terminal over a cell store:
  SGR colors including the bright 90-97 and 100-107 forms, scroll regions,
  the line-drawing set, cursor and device reports.
- `picokbd.c`, `picokbd_map.c`: the I2C1 keyboard controller polled split
  phase from a callout, with Ctrl tracking and the F-key sequences.
- `lcdcons.c` and `compile/PICOCALC`: the tty glue and the kernel
  configuration, 119,316 of 130,816 bytes of flash text.

Calculator, mathematics and games (PR #345):

- `lib/libcalc`: clean-room calculator mathematics written from
  `docs/research/picomite-maths-clean-room-spec.md`.
- `lib/libbn`: a clean-room `BN_*` subset for the OpenBSD dc and bc port.
- `usr.bin/calc`: a scientific calculator for the 53x32 console.
- `games/fourfall`: a clean-room falling-block game, the fourth `gamebox`
  entry point.
- Plan 9 `bc` and `dc` shipped in the PicoCalc image (`NOTICE` section 16).
- The two review rounds and their dispositions:
  `docs/research/calculator-and-games-review.md` and
  `calculator-and-games-review-round2.md`.

Test pipeline (the PR that carries this note):

- `tools/qemu-arm.mk`: every qemu test runs on qemu's ARMv6-M CPU model
  (`-cpu cortex-m0`), which faults on Thumb-2 instructions and unaligned
  loads as a Cortex-M0+ does; `tests/qemu_cpu_model` proves the model choice.
- `tests/libc_contracts check-qemu`: the strtol, `random()` and printf
  contracts run against the tree's libc sources as Thumb-1 programs.
- `tests/qemu_aout`: a loader that runs a real DiscoBSD a.out (the tree's
  crt0, libc and `svc` system calls, and a boot-ROM function table) under
  qemu-arm; outside `check-qemu` until it serves stat, fork and signals.
- `check-renode-picocalc`: a Renode PicoCalc machine (panel and keyboard
  models) that boots the PICOCALC kernel to a shell and types a command.
- Four fixes to the pinned Renode RP2040 models (`tools/renode/patches`
  0005-0008).

## The two board builds

| Board | Command | Kernel | Root profile | Free root blocks |
| --- | --- | --- | --- | --- |
| Raspberry Pi Pico | `bmake MACHINE=rp2040 distribution` | `PICO` (USB CDC console) or `PICO_UART` (UART0) | `full` | 280 |
| ClockworkPi PicoCalc | `bmake MACHINE=rp2040 BOARD=PICOCALC distribution` | `PICOCALC` (panel and keyboard) | `picocalc`: `full` plus `calc-bignum` and `calc` | 210 |

`BOARD` picks the profile an unset `PROFILE` builds, and any other value is
refused (`distrib/rp2040/Makefile.inc`). Shared between the boards: every
kernel source except the console drivers, every user program, `lib/libc`,
the one manifest `distrib/rp2040/mi.rp2040`, the flash layout and every host,
cross and qemu gate. The PicoCalc-only programs are manifest closures the
`full` profile does not name, so the bare Pico image is unchanged by them.
`tools/host-test.mk` and `tools/qemu-arm.mk` hold the resources the host and
qemu gates share: the sanitizer probe, the fortified host build, the tree's
libm compiled for the host, and the newlib Thumb-1 link.

## Test pipeline

Each stage is a separate evidence class; a later stage never substitutes for
an earlier one.

| Stage | Entry | Proves | State | Gating |
| --- | --- | --- | --- | --- |
| Host | `check-host` (with `check-picocalc-host`, `check-libcalc`, `check-libbn`, `check-calc-host`, `check-fourfall-host`) | Sources compile as strict C17 under sanitizers and match their oracles | runs in `logged-check` and CI | none |
| Cross | `check-cross`, `check-cross-kernel`, `check-board-build` | Kernel, libc and programs build for Cortex-M0+ with warnings as errors and fit the layout | runs in `logged-check` and CI | arm-none-eabi toolchain |
| qemu-user Thumb-1 | `check-qemu` | Code compiled for Cortex-M0+ executes on the ARMv6-M model: libcalc, libbn, calc, the libc contracts, the u-area exchange loop, Smaller C | runs in `logged-check` and CI | qemu-arm; `REQUIRE_QEMU=1` turns a missing emulator into a failure |
| qemu a.out loader | `bmake -C tests/qemu_aout check` | A real DiscoBSD a.out runs its own libc and system calls | prototype, outside `check-qemu` | serves no fork, exec, signals, stat or tty ioctl |
| Renode, bare Pico | `check-renode` | The PICO_UART kernel boots from the boot ROM to a shell, reports its capacity record and passes `mputest` and its residue probe | passes, 76 s wall, outside `check` | Renode and fetched models with patches through 0008 |
| Renode, PicoCalc | `check-renode-picocalc` | The PICOCALC console path to a login and a typed command against panel and keyboard models, and the PL022 transmit-FIFO status bits and discarded writes of the SPI1 controller | passes, 90 s wall, outside `check` | Renode, fetched models with patches through 0008 |
| Bare Pico board | `discobsd-flash` or `picotool`, then the drivers under `tests/rp2040` | Behavior on the measured silicon: flash, USB console, MPU, swap | not run for this work | the owner's explicit flash opt-in |
| PicoCalc hardware | the bring-up order in `PICOCALC.md`, "Board bring-up of the console" | Panel, scroll, keyboard firmware, USB-C power, character rate | not run: no PicoCalc has booted this kernel | a PicoCalc on the bench and the owner's opt-in |

Both Renode gates drive the emulator through its console monitor on stdin
(`tools/renode/renode_monitor.py`) and hold no listening socket, so they
run on a host that refuses one, as the host that produced this note does.
Patches 0005-0008 touch only the PL022 and GPIO rows above GP8, which the
PICO_UART kernel does not use, and `check-renode` passes with them applied.

## Next work

Pipeline:

1. Give `check-renode` and `check-renode-picocalc` a CI owner: a job that
   caches the fetched and built models keyed on `RP2040_COMMIT` and the
   patch series, installs the pinned Renode build, and runs both gates
   after the kernel and flash image build.
2. Grow the qemu a.out loader into a gate: serve stat, dup, fcntl and a
   path-root prefix (estimated one day), then fork, exec and wait (two), so
   the shipped utility binaries run against host files; then join
   `check-qemu`. `docs/research/qemu-thumb1-test-tier.md` ranks the host
   gates that gain a qemu variant next (the `tests/kernel` ILP32 programs,
   then the rest of `tests/libc_contracts`).
3. Try the unmerged `raspi-pico` QEMU machine (GPL-2.0, out of tree) as an
   opt-in whole-kernel target beside Renode, bounded to one or two days;
   the falsifier is the PICO_UART kernel reaching a shell.
4. Extend the Renode keyboard model with hold-repeat and FIFO overflow, and
   add a Ctrl-C case; add an SD-card model on SPI0 when the SD driver lands.

Board runs, each waiting on the owner's opt-in:

5. PicoCalc console bring-up in the `PICOCALC.md` order, then compare a
   board photograph with a `DumpPpm` frame to settle orientation and color.
6. `bc | dc` and `calc` on the PicoCalc, and the boot-ROM float provider
   against libcalc's contract (`docs/research/libcalc-implementation.md`).
7. USB-C power alone: the owner's board stays off on USB-C with no cells
   (`PICOCALC.md`, "USB-C and power"); a USB-A-to-C cable and then the
   power key separate a missing CC pull-down from the PMU's power-on
   policy.

PicoCalc drivers (designs in `PICOCALC.md`, "Next work"):

8. SD card on SPI0, PSRAM as swap through PIO, the battery, backlight and
   power-off registers as `machdep.picocalc.*` sysctls, a BEL tone on PWM.

Software:

9. Port OpenBSD dc and bc onto `lib/libbn`: a lazy register table in place
   of the 771 KB extended one, an `hcreate` replacement, libedit removed,
   then the -m32 differential (`docs/research/openbsd-dc-bn-port.md`).
10. Done (PR #353 and the printf/libm conformance branch): `%g` in
    `lib/libc/stdio/doprnt_float.c` runs `general_cvt()`, which counts
    significant digits from the first nonzero digit as C17 7.21.6.1p8
    requires. The decimal conversions generate digits from the double's bits
    in exact integer arithmetic (`decimal_cvt()`), so every digit is
    correctly rounded with ties to even, the 0 flag pads with zeros under
    every floating conversion (`%010.2f` of 3.14159 is `0000003.14`), a
    negative zero keeps its sign, `l` leaves the default precision at six,
    and `%F` prints. `check-libc-printf-g` holds each of these with a mutant
    (`sys/arch/rp2040/doc/TESTING.md`). Open limits: a precision past about
    320 digits prints the exact digits and then zeros; the `%lg` width defect
    named by an earlier note is not reproduced (`%10lg` and `%-10lg` of 3.5
    pad to ten columns, and the `%lf` and `%lg` defect found instead was the
    default precision of 15), so the failing input of that note stays
    unidentified. calc formats from `%e` and stays as it is.
11. Done (PR #353 and the printf/libm conformance branch):
    `include/math.h` defines `HUGE_VAL`, `HUGE_VALF`, `HUGE_VALL` and
    `INFINITY` as infinities, and `check-libc-huge-val` holds the overflow
    and pole returns of lib/libm (`sys/arch/rp2040/doc/TESTING.md`). `exp()`
    overflows above ln(DBL_MAX) with `ERANGE` and reduces its argument with a
    split ln 2, which moves its error from 5e-14 to 5e-16 and lets
    `exp(709.782712893384)` return a finite value; `log(0)`, `y0(0)`,
    `y1(0)`, `yn(n, 0)` and `pow(0, y<0)` report the pole as `ERANGE`. Open
    limits: `log` and the Bessel functions return `-HUGE_VAL` for a negative
    argument where Annex F asks for a NaN; `pow` of a negative base and a
    non-integer power returns 0 with `EDOM`; `ldexp` flushes a result under
    2^-1024 to zero, so `exp` returns zero where the result is a subnormal
    under 2^-1024. calc and libcalc still range-check before calling libm.
12. The remaining games from `docs/research/picocalc-games-survey.md` and
    the tile-graphics device from `docs/research/picocalc-calculator-and-gui.md`.
13. `sys/exec.h:51` declares an empty struct, a GNU extension that keeps
    `picokbd.c` and `lcdcons.c` off strict C17.
14. Refresh the retained C17 shipped-source and shipped-resource baselines,
    which record an earlier tree and have no live gate.
15. Follow the second-core plan in `docs/research/rp2040-second-core.md`
    (PR #357): a board baseline of packed against raw `exec_hsaout`, then
    single-core decoder and divider wins, then SPI1 DMA for the PicoCalc
    panel, and only then a core-1 observer behind single-writer rings and a
    SIO FIFO doorbell. The plan rules out a big-lock SMP kernel, keeps a
    core-0 inline path under every core-1 service, and rests on source
    readings and emulation, with the board measurements still to be taken.

Open decisions and residuals:

- The puzzles license (L-4) and the first-round items CR-2, CR-3, CR-4,
  CR-6, T-7, T-10, TF-1 and TF-3 in `calculator-and-games-review.md`.
- libcalc's condition estimate is infinite when the inverse's 1-norm
  exceeds `DBL_MAX`.
- `kilo`'s highlight test fails when `TMPDIR` is a long path; it passes
  with `/tmp`.
