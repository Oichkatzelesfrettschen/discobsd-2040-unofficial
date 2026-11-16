# Brief: a board descriptor patched into the kernel UF2

Re-read this file at every work-item boundary and after every compaction. It
is the goal file; the orchestrator steers by editing it.

## Problem and decided design

`docs/research/rp2040-single-kernel.md` shows the four RP2040 kernel
configurations (PICO, PICO_UART, PICOCALC, PICOCALC_PSRAM) reduce to one
kernel once the kernel knows its board and console before any PicoCalc pin is
driven. The owner chose the home of that knowledge: one compiled kernel
carries a small checksummed `.board` section, and the image tooling under
`distrib/rp2040` patches that section inside `unix.uf2` per board. Per-board
`unix.uf2` files then differ only in that section.

Facts to confirm from source:

- The console major is the compile-time `CONS_MAJOR`, set per configuration
  in `sys/arch/rp2040/compile/*/Makefile` and read in `sys/dev/cons.c`,
  `sys/arch/rp2040/rp2040/sysctl.c`, `dev/usb.c`, `dev/uart.c` and
  `dev/lcdcons.c`. Kernel `printf` output reaches the console through its own
  path; find it (`machdep.c`, `cngetc`).
- `lcdprobe()` (`dev/lcdcons.c`, SPI1 GP10-GP15), `sdprobe()` (`dev/sd.c`,
  SPI0 GP16-GP19 and GP22), `psramprobe()` (`dev/psram.c`, GP2-GP5, GP20,
  GP21) and the keyboard read `picokbd_version()` (I2C1 0x1F, GP6/GP7) drive
  pins that a bare Pico may have wired to other hardware.
- The boot ROM writes UF2 payloads of 256 bytes at 256-byte aligned
  addresses (`tools/check_rp2040_uf2.py` checks this), so a section of at
  most 256 bytes aligned to 256 lands whole in one UF2 block.

## Work, in order

1. **Format.** Define `struct board_desc` in a new header under
   `sys/arch/rp2040/include/`: magic, format version, length, board identity
   (bare Pico, PicoCalc), console major and minor, and a CRC-32 over the
   preceding bytes. Place one `const` instance in a `.board` output section
   in `conf/kern.ldscript` and `conf/RP2040.ld`, 256-byte aligned and at most
   256 bytes. The compiled default is valid and names a bare Pico with the USB
   console, so an unpatched kernel boots as PICO does.
2. **Reader.** One C file (`sys/arch/rp2040/rp2040/board.c`) validates the
   section and exposes `board_id()` and `board_console()`; an invalid
   descriptor (bad magic, unknown version, short length, CRC mismatch,
   unknown board) selects the bare Pico with the USB console and prints one
   diagnostic line at boot. The reader is written so the host test compiles
   the same file.
3. **Patcher.** `distrib/rp2040/mkboarduf2.py`: read `unix.uf2`, find the one
   block whose payload holds the descriptor magic at offset 0 (fail on zero or
   several), rewrite the fields and CRC, rewrite nothing else, write the
   per-board UF2. A `--self-test` covers the patcher. Wire it into
   `distrib/rp2040/Makefile.inc` so each `BOARD` gets its kernel UF2 from the
   one kernel binary; keep the existing four kernels building until the union
   kernel lands.
4. **Kernel use.** Replace every `CONS_MAJOR`/`CONS_MINOR` read in the files
   above with `board_console()`; keep `CONS_MAJOR` only as the compiled
   default where a configuration still sets it. Run `lcdprobe()`, `sdprobe()`,
   `psramprobe()` and the keyboard read only when `board_id()` names a
   PicoCalc. Rebuild and synchronize the tracked generated Makefiles of PICO
   and PICO_UART if their inputs change.
5. **Gate.** Add `check-board-descriptor`: a host test that compiles
   `board.c` against fixtures for a valid descriptor, a truncated one, an
   unknown board, a CRC mismatch and a missing one (erased flash, all 0xff),
   plus a round trip through `mkboarduf2.py` on a built `unix.uf2` that
   checks only the descriptor block changed. Calibrate with mutants that skip
   the CRC, accept an unknown board, and patch a second block; the gate
   passes on the branch and rejects each. Register it in the root
   `Makefile`, CI and `sys/arch/rp2040/doc/TESTING.md`.
6. **Board prediction.** Record in `docs/research/board-descriptor.md` the
   exact boot lines a bare Pico shows with the default descriptor (USB
   console, no lcd0, sd, psram or keyboard lines) and with a PicoCalc
   descriptor (panel console, keyboard version, PSRAM or `fl1` fallback). The
   orchestrator runs the board.

## Acceptance (each exits 0 with zero warnings)

    : "${PYTHON:?}"; export PYTHON
    bmake MACHINE=rp2040 logged-build
    bmake MACHINE=rp2040 check-board-descriptor
    bmake MACHINE=rp2040 check-kernel-headroom
    bmake MACHINE=rp2040 check-kernel
    shellcheck -S error on every changed shell script; ruff check and
    ruff format --check on every changed Python file

## Standing rules

- Work only in the `PICO-TO-DOS` worktree. Git metadata is read-only in the sandbox: use
  the commit protocol below and never retry a Git write.
- No network, no package installs. Never open `/dev/ttyACM*` or
  `/dev/serial/*`, and never run `discobsd-flash` or a `picotool` command
  that talks to a device; host-only `picotool uf2 convert` is permitted.
- C builds with `-Wall -Wextra -Werror`; shell is POSIX `sh` with `set -eu`;
  Python runs as `${PYTHON}`.
- Checked-in text is emoji-free American English with `--` for dashes. Source
  comments state the invariant, not the history. A new file under
  `sys/arch/rp2040` carries `Copyright (c) 2026 DiscoBSD` and the ISC notice;
  other new files carry no copyright line.
- Commit subjects use a component prefix and the mechanism, bodies one to
  five sentences. No AI attribution trailer or line of any kind.
- Keep secrets, local absolute paths and private hostnames out of every file.

## Commit protocol

For each commit, in series order, write `.commit-requests/NN.paths` (one
repository-relative path per line) and `.commit-requests/NN.msg` (subject,
blank line, body), NN from 01. A path belongs to one request. When the series
is written, write `.commit-requests/READY`. The orchestrator commits in order
and writes each hash to `.commit-requests/NN.hash`.

## Done

The gate passes and rejects its mutants, acceptance passes, every change is in
a commit request, and `docs/research/board-descriptor.md` ends with a Handoff
section of at most fifteen lines.

## Shared worktree (added by the orchestrator)

The owner set `PICO-TO-DOS` as the one worktree and branch for this work.
Another Codex session (exchange-headroom admission, `brief-06.md`) and a board
bring-up agent edit the same tree. Edit only what this brief's work needs.
Re-read a shared file (the root `Makefile`, `tests/kernel/Makefile`, CI
workflows, `sys/arch/rp2040/doc/TESTING.md`) immediately before each edit and
change only the lines your work adds; never rewrite a whole file or revert a
line you did not write. Leave every other modified or untracked file as you
find it, and run no `git checkout`, `restore`, `reset`, `stash` or `clean`.
Write commit requests under `.commit-requests/descriptor/`; list a shared file
in a request's paths and the orchestrator stages only this work's hunks of it.
