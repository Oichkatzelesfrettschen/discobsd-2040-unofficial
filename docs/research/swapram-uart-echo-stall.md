# Console echo stall in the Renode capacity gate

## Finding

`check-renode-capacity` failed on PICO_DIAG at dc112278 (the #637 merge
of the picocalc diag-kernel branch with origin/main 32927ed0), reporting
that `capacity-end` never arrived and showing the typed command echoed
one character at a time. The echo did not stall, and neither the
`uart.c` driver nor SwapRAM is at fault. The gate typed its next command
into a terminal whose raw queue still held 255 bytes of `clistbound`'s
typed input. `ttyinput()` dropped the bytes past `TTYHOG`, the
command's carriage return among them. The shell finished echoing the
partial line within about 3.3 virtual seconds and then waited, idle, for
a line end that never came. The reported 100.5 virtual seconds is the
emulation clock when the 60-second command budget ran out: the command was
typed at about 40.5 seconds. It is not how long the echo took.

`clistbound` now discards its typed input before exit, and the gate fails
by name when the shell echoes any of it.

## Mechanism

1. `clistbound` (tests/rp2040/capacity_exhaustion/clistbound.c) stops
   its terminal's output while the driver types `#` and 600 `x`
   bytes. In cooked mode `ttyinput()` keeps 255 of them and answers the
   rest with bells (sys/kern/tty.c:1208, `t_rawq.c_cc + t_canq.c_cc >=
   TTYHOG`).
2. The program exits with those 255 bytes still queued. The shell's
   line editor enters CBREAK with `TIOCSETN`, which keeps typeahead on
   purpose (bin/sh/edit.c:132-138), and reads the residue one byte at a
   time. It redraws the prompt and the whole line for each byte
   (`redraw()`, bin/sh/edit.c:272-288) and rings a bell for each byte past
   `ED_MAX` = 126 (`insert_text()`, bin/sh/edit.c:302-305). The redraws
   come to 37,803 bytes, 3.28 seconds of 115200-baud 8N1 output.
3. At dc112278 the driver waits for `"\nrc="`, types an empty line and
   waits for `#` (tools/renode/check-capacity.py:147-149). The root
   prompt, printed before the residue is read, already holds a `#`, so
   the wait returns at once, and `capacity_rows()` types
   `sysctl kern.capacity; echo capacity-end` (lines 211-212). That is 41
   bytes: the empty line's carriage return, then the 40-byte command with
   its own carriage return.
4. In CBREAK the raw queue accepts a byte while `c_cc <= TTYHOG`, which
   holds 256 bytes (sys/kern/tty.c:1141-1149). Each byte past that gets a
   bell and is dropped. With `r` residue bytes consumed, the room is
   `r + 1` and the loss is `40 - r` bytes, always from the end of the
   line. A1 lost 14 bytes, so `r` was 26.
5. Where `rc=` lands inside the 0.5-second emulation slice decides `r`.
   Renode replays one built image identically: S15's two runs of one tree
   both lost 11 bytes. A rebuilt image moves the phase. A1, at the same
   commit and configuration, lost 14 bytes, and the validation trees
   reached the login prompt 0.5 s later than the phase A trees. That
   SwapRAM does not cause the loss rests on runs B2 and B3 below, not on
   the cell pattern.
6. origin/main 34b80114 carries the same `clistbound` stage, the 0009
   models and the #586 driver, and boots PICO_UART without SwapRAM in this
   gate. It passes there by phase alone: B4 shows 48 residue bytes read
   before the gate types, room for all 41.

## Discriminating runs

The runs used Renode 1.17.0+20260907gitf1dd1b4af, the Renode_RP2040
models with patches 0001-0009 except where noted, and arm-none-eabi-gcc
14.2.1, on a WSL build host. The kernels have these sizes: PICO_DIAG text
100,829, data 272, bss 42,936; PICO_UART text 90,201, data 216, bss
18,208. The fix changes no kernel.

| Run | Kernel | Variation | Result |
| --- | --- | --- | --- |
| A1 | PICO_DIAG (SwapRAM) | gate at dc112278 | fail: one run of 14 bells, tail ends `; echo` |
| A2 | PICO_UART (no SwapRAM) | gate at dc112278 | pass, no bell run |
| S15 | PICO_UART + SwapRAM | earlier run | fail: one run of 14 bells |
| B1 | PICO_DIAG | probe at the gate's timing: raw queue 226 at `rc=` | 29 bytes read; a 0.156-second state read before typing tipped it to pass |
| B4 | PICO_UART | probe at the gate's timing | raw queue 207 at `rc=` (48 read): pass |
| B2 | PICO_DIAG (SwapRAM) | 6 s of advance before typing | pass |
| B3 | PICO_UART (no SwapRAM) | 0.05-second slices, typing right after `rc=` | fail: one run of 11 bells |
| B3 at the stall | PICO_UART | registers read | see the next section |
| C1 | PICO_DIAG | models without 0009 | pass |
| C2 | PICO_DIAG | models without 0009, typing right after `rc=` | pass: 142 redraws already done at `rc=` |
| C3 | PICO_DIAG | `uart.c` and `uart.h` from daa9228e, 0009 models | no verdict: the boot stops before the file-system check banner |
| C4 | PICO_DIAG | daa9228e driver, models without 0009 | pass, the configuration that passed at daa9228e |

B2 and B3 cross SwapRAM against timing. SwapRAM with slack passes, and no
SwapRAM without slack fails, so SwapRAM does not decide the outcome.

## State at the stall (B3)

With the line "sysctl kern.capacity; echo ca" echoed and nothing
further, `uartttys[0]` holds:

- raw, canonical and output queues all at 0;
- `t_flags` 0x140504c2, which is CBREAK with ECHO clear, the editor's
  mode;
- `t_state` 0x214, with `TS_BUSY` clear;
- `uart_txflags` at 0.

UARTFR reads 0x90 (TXFE and RXFE set), UARTIMSC 0x50 (RX and RT), and
UARTMIS 0. Twelve cpu0 PC samples 13 ms apart all fall at `idle+0x22`,
the instruction after `wfi`. One carriage return typed at that point
runs the partial command at once: the five `kern.capacity` rows print,
then `ca`. No driver state is stuck.

At `rc=` in the same run, the raw queue held 255 bytes and `t_state`
showed `TS_BUSY` with `uart_txflags` at 3 (`UART_TX_DRAIN` and
`UART_TX_TIMER`). This is the drain phase that `uartdrain()` ends.

## Why the factors correlate

- **PL011 model (patch 0009).** This is the necessary condition. The
  pre-0009 model delivered output in zero line time, so the editor
  consumed the residue in CPU time: 142 of the 255 redraws were done when
  `rc=` was first seen at a 0.05-second granularity (C2). The 0009 model
  charges ten bit times per character at the programmed divisors, so the
  residue costs 3.28 seconds of virtual time, and a command typed in that
  window overflows. The model is correct: silicon at 115200 baud takes
  the same time.
- **`uart.c` (#586).** C3 shows that the pre-#586 driver cannot boot on
  the 0009 model, the level-crossing stall that `tools/renode/check-boot.sh`
  names. That pairing cannot isolate the driver. B3 at the stall shows
  the #586 driver idle and consistent.
- **SwapRAM.** It sets the phase. A boot PC profile at 10 ms intervals
  attributes 512 of 901 samples before the login prompt to
  `heatshrink_encoder_poll` on PICO_DIAG, the SwapRAM codec compressing
  images into the pool. The encoder's caller is
  `sr_encode()` (sys/arch/rp2040/rp2040/swapram.c:286). `arm-none-eabi-nm` finds no
  heatshrink symbol in the PICO_UART kernel, so its profile has no encoder
  samples to show. The login prompt moves from 4.0 to 9.0 virtual
  seconds, and every later event, `rc=` included, lands at a different
  offset in its slice.

## Hardware prediction

The loss is ordinary `TTYHOG` behavior of the kernel and the shell
editor, and a board shows it. On the UART0 console at 115200 baud,
consider a command of n bytes typed or pasted within about 3.3 s after a
program exits with `TTYHOG` bytes of unread input. Before the fix, it
loses its last `n - r - 1` bytes, with one bell each, and the shell
then sits at the partial line. Over USB CDC-ACM the console drains faster,
so the window shrinks with the host's read rate. The editor's redraw cost
stays the same.

Proposed board test (UART0 console, parent-run). A host script runs it,
because the first redraws are short: the editor reads about 15 bytes of
residue in the first 0.1 s.

1. Send `sleep 5` and a carriage return.
2. One second later, send `#` and 300 `x` bytes.
3. When the prompt after `sleep` appears, send at once a carriage
   return, then `echo marker-end` and a carriage return. The first
   carriage return ends the residue line, as the gate's empty line did.
   The residue fills the editor's line to `ED_MAX`, so a command appended
   to it is dropped by `insert_text()` whatever the timing. The control
   has to end that line too.

The prediction is a run of bells, an echoed line that ends early, and no
`marker-end` output line. The control repeats the run with a 5-second
pause before the last send and should print `marker-end`.

The fixed `clistbound` predicts no residue echo after its exit on the
board.

## Fix and regression gate

- tests/rp2040/capacity_exhaustion/clistbound.c issues
  `ioctl(STDIN_FILENO, TIOCFLUSH, &discard)` with `discard = FREAD` after
  reaping the writer. It prints "PASS typed input discarded (TIOCFLUSH)"
  or a FAIL line, and a failure makes its exit status nonzero. The typed
  input and the clist measurement stay unchanged: the clist peak is 17 of
  32 and the held count 10 in every run. The test program's text grows
  from 6,240 to 6,336 bytes (arm-none-eabi-gcc 14.2.1); no kernel
  changes.
- tools/renode/check-capacity.py drops the empty-line step and, one slice
  after the post-exit prompt, counts the editor redraws carrying the typed
  `#` (`CB_RESIDUE`, `"# #"`). A nonzero count fails the gate with the
  cause named. The check does not depend on the slice phase, so a
  configuration that passed by phase alone now fails too.

Calibration:

| Tree | Cell | Result |
| --- | --- | --- |
| fix | PICO_DIAG (SwapRAM) | pass |
| fix | PICO_UART | pass |
| fix | PICO_DIAG without SwapRAM | pass |
| fix | PICO_UART with SwapRAM | pass |
| harness check, unflushed clistbound | PICO_DIAG | fail: "79 line-editor redraws in one slice" |
| harness check, unflushed clistbound | PICO_UART | fail: 91 redraws; the old gate passed this cell |
| fix on origin/main 34b80114 | PICO_UART, the gate's kernel there | pass |
| main's clistbound, residue check | PICO_UART on origin/main | fail: 93 redraws |

## Residuals

- The SwapRAM boot cost is a separate observation. PICO_DIAG spends about
  5.1 of its 9.0 virtual seconds before the login prompt in
  `heatshrink_encoder_poll`. The Renode cores run at 125 MIPS, one
  instruction per cycle. The Cortex-M0+ executing from XIP flash takes
  more cycles per instruction, so a board should spend at least as long.
  A boot timestamp on a SwapRAM kernel against one without SwapRAM decides
  that.
- The gate's other waits for `#` match the earliest prompt after the
  cursor. They are safe while the shell holds no typeahead. The residue
  check covers `clistbound` alone, the one stage that types into a
  running program. A new stage that does the same needs its own check.
- A surviving hypothesis links this to an open record.
  sys/arch/rp2040/doc/BOARD-CONSOLE.md ("Every typed byte redraws the
  whole prompt line") records a USB-console board stall: the 43rd of a
  series of 107- to 119-byte commands showed its complete echo and no
  response, and the resource that ran out was not identified. Typing that
  outruns the quadratic redraw output leaves typeahead in the raw queue.
  Once the backlog passes `TTYHOG` in CBREAK, the bytes past it are
  dropped. If only the carriage return falls past the limit, the line
  shows complete and never runs. The falsifier is a capture of that run
  with no bell byte before the stall. The deciding measurement is the
  same driver run with the console bytes logged raw and the bells counted.
