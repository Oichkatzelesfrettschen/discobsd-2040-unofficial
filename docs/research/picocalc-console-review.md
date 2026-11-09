# PicoCalc console: review ledger

Read-only advisory review of PR #344. No source file, build product, board or serial port was touched;
this file is the only write.

## Scope and inputs

- Tree: branch `picocalc/lcd-keyboard-console` at `98cd07b2`, diff base
  `origin/main` at `076675a8`, twelve commits `03cff20c..98cd07b2`.
- Prior rounds read in full: a local Codex review (blocker on the panic
  path, PRIMASK across rendering, Ctrl latch on FIFO overflow, reverse
  erase, test coverage, init wording, I2C enable timeout), a local Grok
  review (panic reentry, backspace at pending wrap, LF/tab at pending wrap,
  defer overflow, masked 16 ms exchange, citation and doc corrections),
  four GitHub Codex threads with the author's replies (SysTick-callout
  rendering, the authority index, mutant directory isolation, the Config
  ISC header), and four unanswered Copilot threads on `98cd07b2`
  (defer-ring marker order, `inter` before `ESC (`, IND at pending wrap,
  Ctrl reset on I2C restart).
- Vendor and silicon references: `discobsd-2040-notes/docs/picocalc/code`
  (keyboard firmware `keyboard.ino`, `picocalc_keyboard.ino`, `reg.ino`,
  `fifo.ino`, `conf_app.h`; helloworld `lcdspi.c`, `i2ckbd.h`) and
  `discobsd-2040-notes/docs/rp2040/rp2040-datasheet.txt`.
- Measurements taken here, all on the retained build products of
  `98cd07b2` (`sys/arch/rp2040/compile/PICOCALC/unix.elf`, 10:50 local,
  after the last commit at 10:51 touched only Config's header comment):
  `arm-none-eabi-size` gives text 118,312, data 304, bss 43,744;
  vector slot 42 (`0x100001a8`) holds `0x1001382d`, which is
  `SPARE_IRQ_0_Handler | 1`; `cdevsw[10]` (base `0x1001bd4c` + 440) holds
  `lcdopen`, `nullstop`, `lcdgetc` and `lcdputc`. A static stack estimate
  was computed from `arm-none-eabi-objdump -d unix.elf` (gcc 16.2.0),
  summing each function's prologue pushes and `sub sp` over direct calls,
  with the indirect edges `softclock -> kbd_poll`, `ttstart -> lcdstart`
  and `cnputc -> lcdputc` added by hand. Indirect calls through `sysent`
  and `cdevsw` are absent, so top-half depths are lower bounds.
- Not run: `check-picocalc-host`, the mutants, any build, Renode, any
  board. The read-only brief excludes them.

## Prior findings and their fixes

Verdicts: **fixed**, **partly fixed** (a residual defect remains, listed in
New findings), **open** (unanswered), or **declined**.

| Prior finding | Real? | Fix and verdict |
| --- | --- | --- |
| Codex 1 / Grok major 1: panic re-enters an open panel transfer through the `panicstr` bypass, nested release clears the flag | Real | **Partly fixed** by `7c997c50` and `a594c6ac`: `lcdabandon()` (`lcdcons.c:107-119`) ends the PL022 transfer and frees the flag, `arm_fault()` calls it before printing (`fault.c:131-134`), and `lcd_acquire()` (`lcdcons.c:127-131`) calls it once after `panic()`. The acquire-side abandon fires in whichever context first finds the flag held, including a context that preempted a live panic printer; see N2. Terminal rendition is not reset; see N3. |
| Codex 2: tty output rendered at `spltty()`, i.e. PRIMASK | Real | **Fixed** by `a594c6ac`. `lcdstart()` (`lcdcons.c:190-201`) only sets `TS_BUSY` and pends IRQ 26; the handler (`lcdcons.c:204-235`) masks only around `getc`. IRQ 26 is software-pendable (datasheet text lines 4337-4338). Priority byte 128 is `IPLTOREG(IPL_SOFTCLOCK)` (`intr.h:85-88`), below SysTick 0 and UART/USB 64 (`usb.c:1149`, `uart.c:153`, both `IPL_TTY` = 64), above PendSV 192. |
| GitHub Codex P1: callout rendering inside SysTick | Real (the intermediate `7c997c50` design) | **Fixed** by the same IRQ 26 design. The author's reply is accurate; slot 42 verified above. |
| Codex 3: FIFO overflow drops Ctrl release | Real | **Partly fixed** by `7085af08`: 2 ms phases after an event (`picokbd.c:101-104`, `kbd_poll` at 264-292) drain about 250 events/s, far above the firmware's event rate, so overflow needs a long masked interval. A dropped release from an I2C abort or stall restart still latches Ctrl; see N1. |
| Codex 4: reverse-video erase | Real against the stated design | **Fixed as designed** by `00886a52` (`erase_attr`, `lcdterm.c:105-110`). The cited authority does not decide the semantics; see N10. |
| Codex 5: test compared characters only | Real | **Fixed** (`lcdterm_test.c:139-156` compares character and attribute). Six of nine groups call it; `test_save_restore_reports`, `test_cursor_overlay` and `test_keymap` do not, so "runs after each case" in `00886a52` overstates; see N12. |
| Codex 6: "is the vendor sequence" | Real | **Fixed**: `st7365p.c:35-41` and `PICOCALC.md:98-105` list the three deliberate differences (E9h omitted, VSCRDEF before DISPON, DISPON after clear). `lcdcons_init()` orders `st7365p_init`, `lcdterm_init`, `st7365p_display_on` (`lcdcons.c:88-90`). |
| Codex 7: I2C enable timeout ignored | Real | **Fixed in structure** by `7085af08` (`i2c_enable` returns status, `i2c_restart` escalates to ABORT then block reset, `picokbd.c:128-174`). The ABORT write is likely ignored by the silicon; see N7. Recovery still spins inside SysTick; see N8. |
| Grok major 2: BS at pending wrap erases the 52nd cell | Real | **Fixed** (`lcdterm.c:702-712`), host-tested (`lcdterm_test.c:200-209`). The chosen behavior matches xterm only with reverse-wrap; it is the right choice for the tty's `\b \b` rubout and harmless to BSD curses, which emits `\n\r` after writing the last column under `xn`. |
| Grok minor: LF/VT/FF/tab leave `wrapnext` | Real | **Fixed** (`lcdterm.c:713-722`), host-tested. IND (`ESC D`) was missed; Copilot thread 3, N5. |
| Grok minor: defer ring drops silently | Real | **Fixed** with a counter and marker (`lcdcons.c:140-147`, 167-172); the marker can land after later bytes; Copilot thread 1, N5. |
| Grok minor: `kbd_exchange` masks for 16 ms | Real for a future caller | **Declined, documented** (`picokbd.c:327-333`). The only callers are `lcdprobe` before `clkstart()` (`init_main.c:69`, 115) and `boot()` after `splhigh()` (`machdep.c:708`, 758); `cngetc` has no other caller (search of `sys/kern sys/dev sys/arch/rp2040`). Acceptable. |
| Grok minor: SLPOUT citation, DISPON comment, 96 kHz, `lcdterm.h` path | Real | **Fixed** (`st7365p.c:249-255`, 302, `picokbd.c:46-51`, `lcdterm.h:26-27`, `PICOCALC.md:187-190`). |
| Grok minor: abort after the FIFO write loses one event | Real | **Documented** (`picokbd.c:41-44`, `PICOCALC.md:173-174`); the Ctrl consequence is open (N1). |
| Grok suspected: `IC_CLR_*` read while disabled | Unresolved | Still read with the block disabled (`picokbd.c:169-170`). The datasheet text found here does not say such a read is ignored; a board `IC_RAW_INTR_STAT` read-back after a forced NACK decides it. Hand-off. |
| GitHub Codex: authority index, mutant temp dir, Config header | Real | **Fixed** (`research/README.md` hunk, `mutant.sh` uses `mktemp -d`, Config header). Correct. |
| Copilot 1-4 | All real (N1, N5) | **Open**: no reply, no commit. |

## New findings

Severity: blocker / major / minor / nit. "Demonstrated" means traced from
source or a measured artifact; "suspected" means a board or reference
capture decides it.

### N1. Major, demonstrated: a lost Ctrl release still latches Ctrl, and the protocol does offer a resynchronization

`picokbd.c:150-174` (`i2c_restart`) and `picokbd.c:224-228` reset the
exchange without touching `ctrl` (`picokbd.c:119`). The controller dequeues
the event when the register write lands (`picocalc_keyboard.ino:140-144`),
so an abort or a stall restart (`picokbd.c:271-275`) between write and
collect discards a Ctrl release, and `picokbd_map.c:78-83` keeps `*ctrl = 1`
until the next Ctrl release. Every later letter becomes a control byte: `d`
at a shell prompt is EOF and logs the user out, `c` is SIGINT, `z` stops the
job. Copilot thread 4 reports the abort case.

`PICOCALC.md:166-168` says the controller "exposes no modifier state to
resynchronize from". The firmware does expose a liveness signal: with the
default `CFG_REPORT_MODS` (`reg.ino:39`), a held Ctrl is reported as
`(HOLD, 0xA5)` at the 300 ms hold time (`keyboard.h:83`) and then every
100 ms (`keyboard.ino:258-266`, callback at 195-203, which passes HOLD for
non-printable codes).

Fix: clear `ctrl` in `i2c_restart()` (cheap, Copilot's fix), and add a
lease: record the tick of each Ctrl PRESSED/HOLD and clear the latch after
about 500 ms (300 ms hold time, 100 ms repeat, up to 48 ms slow-poll
latency) with no refresh. Add a host case with a lost release and an
expired lease. Correct `PICOCALC.md:166-168`.

### N2. Minor, demonstrated: the panic bypass abandons a live panic printer

`panic()` sets `panicstr` and prints without raising priority
(`subr_prf.c:563-575`); `boot()` raises `splhigh()` only after
`rp2040_shutdown_sync()` (`machdep.c:705-708`). A top-half panic therefore
prints from PendSV (priority 192; system calls run there, `syscall.c:28-58`)
with PRIMASK clear. If SysTick's keyboard callout echoes a key
(`kbd_poll -> lcdcons_input -> ttyinput -> ttstart -> lcdstart`) or any
interrupt prints, the renderer (128) or that interrupt calls
`lcd_acquire()`, finds the panic printer holding the flag, and abandons it
(`lcdcons.c:127-131`). The printer then resumes: its remaining pixel bytes
go out with chip select high, its later `lcd_release()` runs without
ownership while another drawer can acquire, and tty output interleaves with
the report. `lcdcons.c:46-48` and `PICOCALC.md:144-148` ("will never
resume") are false for this path. The damage is lost glyphs and
interleaving, at most once per panic, because `lcd_abandoned` latches.

Fix: in `lcdputc()`, once `panicstr != NULL`, hold `splhigh()` across the
defer, acquire and release. No interrupt or renderer then runs while the
panic printer owns the flag, and the acquire-side abandon remains correct
for its real case, a lower owner preempted by the panicking context.

### N3. Minor, demonstrated: the fatal report inherits the interrupted program's rendition

`lcdterm_abort()` (`lcdterm.c:613-618`) resets only the parser state and
`cursor_drawn`. A program that left the line-drawing set selected
(`as=\E(0`, `termcap.small:84`) maps 0x5f-0x7e, every lowercase letter,
through `acs_map` (`lcdterm.c:78-83`, 780-781), so "panic: ..." renders as
`-`, `+` and `|`. A leftover scroll region makes each newline of the
report a region redraw (N4), and leftover SGR, insert mode or cursor-off
also carry over.

Fix: call `reset_modes()` (`lcdterm.c:344-362`) from `lcdterm_abort()` and
re-send `lcd_set_top(top)` so the panel's scroll start matches the store if
the abandoned owner was between the two in `scroll_up()`
(`lcdterm.c:208-213`). The cell store stays as is.

### N4. Minor, demonstrated: kernel putc costs three cells per character and up to about 200 ms per newline, at its caller's priority

`lcdcons.c:41-44` and `PICOCALC.md:142-144` say a kernel message costs one
cell per character or one row fill at a scrolling newline.

- With the cursor on (the default, `lcdterm.c:355`), each `lcdputc()` runs
  `cursor_hide()` (one cell, `lcdterm.c:129-137`, called at 739), the glyph
  (one cell), and `lcdterm_cursor_show()` from `lcd_release()` (one cell,
  `lcdcons.c:173-176`): about 366 us per character at the documented
  122 us per cell.
- With a scroll region set (`cs`, which full-screen programs use), a
  newline at the region bottom calls `scroll_up()` on the region
  (`lcdterm.c:216-220`): 30 rows of 53 cells redrawn plus one row fill,
  about 1,590 x 122 us + 6.1 ms, roughly 200 ms.

Kernel printf runs in whatever context calls it, including SysTick and
PRIMASK-masked sections, so a driver message printed while `vi` owns the
screen can hold the clock off for about 200 ms (200 lost ticks at HZ 1000,
`machparam.h:41`). The renderer path is unaffected; it is preemptible.

Fix before merge: correct both texts. Hand-off: skip the redundant
`cursor_hide()` redraw when the next glyph overwrites the cursor cell, and
decide whether non-panic kernel output should defer to the renderer when the
tty is open.

### N5. Minor, demonstrated: the four Copilot threads are correct and unanswered

- `lcdterm.c:638-640`: `ESC (` keeps `inter` from a preceding CSI; after
  `\E[!p` (DECSTR, the first sequence of `is` and `rs`,
  `termcap.small:86-89`) a directly following `\E(0` is ignored. The
  termcap strings themselves follow DECSTR with another CSI, which clears
  `inter` at `lcdterm.c:636`, so the termcap path is unaffected. Fix: clear
  `inter` in the `'('` case; add the host case.
- `lcdterm.c:653-655`: IND leaves `wrapnext` set, so the next glyph indexes
  twice. Fix: clear it, as `E` and `M` already do; add the host case.
- `lcdcons.c:140-147`: after an overflow, bytes queued once slots free are
  drawn before the marker. Fix: `if (defer_lost != 0 || full) defer_lost++`.
- `picokbd.c:150-174`: N1.

### N6. Major, suspected: the renderer adds an exception level to a 2 KB kernel stack

Exceptions stack on MSP in the 3 KB u-area; the measured kernel-stack span
is `USIZE - UAREA_STACK_OFFSET` = 2,048 bytes (`uarea_stats.h:21`,
`subr_capacity.c:136`; `zswap.md:41-42` gives 2,100 below a 972-byte
`struct user`). Static estimates from `unix.elf` (method in Scope):

| Level | Path | Bytes |
| --- | --- | --- |
| IRQ 26 renderer | 32-36 frame + `SPARE_IRQ_0_Handler -> lcd_release -> lcdterm_putc -> lcdterm_reply -> lcdcons_input -> ttyinput -> catq -> q_to_b -> wakeup` (424) | about 460 |
| SysTick | frame + `systick -> hardclock -> softclock -> kbd_poll -> lcdcons_input -> ttyinput -> ...` (384) | about 420 |
| Kernel printf from any interrupt | `printf -> prf -> putchar -> cnputc -> lcdputc -> lcd_release -> lcdterm_putc -> scroll -> lcd_draw_cell -> lcd_window -> lcd_command` | about 440, 600 with the reply path |
| HardFault report | frame + `arm_fault` (48) + the printf path | about 520-680 |

PendSV top half, then IRQ 26, then USB (64), then SysTick (0), then
HardFault is a legal nesting on ARMv6-M. The renderer level alone is about
22 percent of the stack, and a fault report now runs through `lcdterm`
rather than a USB ring. An overflow writes into `struct user` below the
stack and surfaces as an unrelated crash. CAPACITY_STATS is on in
PICOCALC (`Config`) and its sentinel scan (`subr_capacity.c:91-111`) catches
interrupt peaks, so the board run can measure this.

Fix: hand-off measurement (below). A cheap reduction: buffer `lcdterm_reply`
bytes and deliver them after `lcd_release()`, which removes the `ttyinput`
chain from every draw path.

### N7. Minor, suspected: the IC_ENABLE.ABORT escalation writes ABORT while ENABLE is 0

`picokbd.c:155-157` writes `IC_ENABLE = 0` (inside `i2c_enable(0)`), then
`IC_ENABLE = 1 | ABORT` in one store. The register description says "the
software can set this bit only when ENABLE is already set; otherwise, the
controller ignores any write to ABORT" (datasheet text lines 36515-36520).
If the silicon evaluates "already" against the stored value, the ABORT is
dropped, the self-clear poll exits at once, and recovery falls through to
the block reset (`picokbd.c:162-167`). Recovery still completes; the
documented middle step (`PICOCALC.md:174-176`) likely never acts. Fix:
write `IC_ENABLE = 1`, then `IC_ENABLE = 1 | ABORT`. The abort also
completes only "after completing the current transfer" (same text), so it
cannot free an SCL held low; the reset is the real recovery.

### N8. Minor, suspected: a stuck bus costs about 20-30 ms of SysTick every few polls

Each stall restart (`picokbd.c:271-275`) can spin 10 ms for the disable,
10 ms for the ABORT poll and 10 ms for the second disable inside the
SysTick exception. With SCL held low (keyboard controller in reset, or
SW701 in its firmware-update position, `PICOCALC.md:65-68`) the cycle
repeats every nine slow polls (144 ms), losing about 15 percent of ticks.
The stall budget also counts polls, not time, so it is 18 ms at the fast
rate. Fix (hand-off): a time-based stall budget, exponential backoff after
consecutive failures, and an escalation spread across polls.

### N9. Minor, suspected: HALTREBOOT can reboot on a stale key

`boot()` prompts and calls `cngetc()` (`machdep.c:756-758`);
`picokbd_getc()` returns the first PRESSED event in the controller FIFO
(`picokbd.c:365-379`), which can predate the prompt: a key typed while the
panic printed, or the 100 ms held-key repeats of a key the user is holding
(`keyboard.ino:195-199`). The report then vanishes on reset before it is
read. A UART console has the same exposure to buffered bytes. Fix: drain
the FIFO to `(0,0)` and empty `ring` before the first wait in a halt.

### N10. Nit, suspected: the reverse-erase rule cites an authority that does not decide it

`00886a52` says reverse video erases with the foreground color "as termcap
ut promises". Termcap `ut` names erasure with the background color. Recent
xterm's `ClearCells` applies the current color flags without INVERSE (from
recall of xterm `util.c`, not checked here), which erases to `sgr_bg`; the
Linux console includes reverse in its erase attribute. The implemented rule
is defensible; 2.11 curses does not depend on it. A capture of
`printf '\e[31;44;7m\e[K'` on xterm decides which terminal the console
matches. Record the choice in `PICOCALC.md` rather than attributing it to
`ut`; the mutant `mutant-reverse-erase` encodes the choice.

### N11. Nit, demonstrated: interrupt-level kernel messages wait for the whole render

The renderer holds the flag for the entire output queue
(`lcdcons.c:210-228`), so a kernel message from an interrupt during a long
render is drawn after all queued tty output, possibly seconds later, and
more than 128 such bytes overflow the ring. Hand-off: drain the deferred
ring between tty bytes inside the render loop.

### N12. Nit, demonstrated: documentation and commit wording

- `PICOCALC.md:171-172`: "no progress for eight polls"; the restart happens
  on the ninth (`++stall > KBD_STALL_POLLS`, `picokbd.c:272`).
- `PICOCALC.md:182`: "the tty's ICRNL mapping"; this tty uses `CRMOD`
  (`lcdcons.c:302`).
- `00886a52` body: the consistency check "runs after each case"; three of
  nine groups skip it (`lcdterm_test.c:344-418`). `TESTING.md` should say
  which groups compare the whole screen.
- `kbd_errors` counts a stall restart twice when the disable times out
  (`picokbd.c:273`, 156).
- `PICOCALC.md:132-133` calls PendSV the process switcher; it also runs
  every system call (`syscall.c:28-58`), which is why the renderer preempts
  all top-half work.
- `termcap.small:85` gives `kb=^H` while Backspace sends DEL
  (`picokbd_map.c:102-104`), as real xterm does; worth a sentence.

### Checked and found sound

- tty flow control with a render pending: `^S` sets `TS_TTSTOP`, the render
  loop tests it per byte (`lcdcons.c:221`) and clears `TS_BUSY`; `^Q` reaches
  `lcdstart` through `ttstart`. `ttyflush(FWRITE)` (`tty.c:264-272`) runs at
  `spltty`, the renderer takes bytes only at `spltty`, and `d_stop` is
  `nullstop`. `ttywait` (`tty.c:195-213`) sleeps until the renderer's
  `ttyowake` with an empty queue. `ttyclose` zeroes `t_state` at `spltty`;
  a renderer pended under that mask then finds an empty queue. Because a
  thread or the PendSV top half never runs while IRQ 26 is active, close,
  reopen and flush cannot interleave with a half-taken byte. No carrier
  transitions exist for this tty.
- The `render_wanted` handoff cannot race: the renderer finds the flag held
  only by the PendSV top half it preempted, which cannot release until the
  renderer returns; the release pends IRQ 26 under `spltty`
  (`lcdcons.c:177-184`). Self-pending at `lcdcons.c:232-233` tail-chains.
- Early boot: the first console byte is `main()`'s version banner
  (`init_main.c:68`), after `SystemClock_Config()` and with interrupts still
  masked until `spl0()` (`init_main.c:125`); `lcdcons_init()` programs IRQ 26
  then. `mdelay` and `pc_micros` use the free-running TIMER
  (`machdep.c:788-800`), not SysTick. A `panic()` inside `startup()` reaches
  the panel through the same lazy init.
- Cell-store ring: `phys(r) = (top + r) % 32` with `top` in 0..31; the
  full-screen scroll erases logical row 0 before advancing `top`
  (`lcdterm.c:208-213`); the reverse scroll erases row 31 before retreating.
  `defer_head - defer_tail` and `% 128` stay correct across `u_int`
  wrap because 128 divides 2^32.
- `wakeup()` raises `splclock()` itself (`kern_synch.c:297`), so calling
  `ttyowake` from IRQ 26 is as safe as the existing UART and USB handlers.
- The terminal reply path cannot loop: with `CTLECH` the echoed ESC prints
  as `^[`; without it, `CSI ? 6 c` is ignored as private and `CSI r;c R` has
  no handler.
- Keyboard map against firmware: key codes (`keyboard.h`), states 0-3,
  `PICOKBD_MAXSEQ` 6 against the longest 5-byte sequence, held printable
  keys re-reported as PRESSED (`keyboard.ino:195-199`).

## Recommended before merge

In priority order; each is small and source-local.

1. N1: clear `ctrl` in `i2c_restart()` and add the 500 ms Ctrl lease
   refreshed by the firmware's HOLD repeats, with host cases; correct
   `PICOCALC.md:166-168`.
2. N2: hold `splhigh()` across `lcdputc()` once `panicstr` is set; correct
   `lcdcons.c:46-48` and `PICOCALC.md:144-148`.
3. N3: `reset_modes()` and `lcd_set_top(top)` in the abandon path.
4. N5: the three remaining Copilot fixes (`inter` on `ESC (`, `wrapnext` on
   IND, drop-until-marker), with host cases for the two terminal ones, and
   replies on the four threads.
5. N4 and N12: correct the kernel-putc cost claims and the wording items;
   rewrite `00886a52`'s body (attribution of reverse erase, "after each
   case") since the branch is still in review.
6. N7: write ENABLE before ENABLE|ABORT.
7. N6: record the stack estimate in `PICOCALC.md` and add a
   `stack_minimum_free` reading to the bring-up checklist.

## Hand-off items

- Board bring-up as `PICOCALC.md:209-219` lists, plus: read CAPACITY's
  `stack_minimum_free` after a mix of `vi` region scrolling, held-key
  typing, USB console traffic and a deep file-system workload (N6); confirm
  the 2 ms write-to-read gap (`PICOCALC.md:168-169`); force a NACK and read
  `IC_RAW_INTR_STAT` after the disabled-state clears (Grok suspected item);
  hold SCL low and time the SysTick loss (N8); a panel capture for E9h.
- N4: cursor redraw saving and a policy for non-panic kernel output while
  the tty is open.
- N6: move terminal replies out of the draw path if the measurement shows
  less than about 500 bytes of margin.
- N8: time-based stall budget, backoff, non-spinning escalation.
- N9: drain stale keyboard events before the HALTREBOOT prompt.
- N10: an xterm capture to settle reverse-video erase.
- N11: drain the deferred ring inside the render loop.

## Claims verified

| Claim | Source | Result |
| --- | --- | --- |
| PICOCALC text 118,312 of 130,816, bss 43,744 (`PICOCALC.md:36`, `18c936b9`) | `arm-none-eabi-size unix.elf` | holds: 118,312 / 304 / 43,744 |
| Vector slot 42 holds the renderer (`a594c6ac`) | word at `0x100001a8` | holds: `0x1001382d` |
| Major 10 is the console row | `cdevsw` + 440 in `unix.elf` | holds |
| IRQs 26-31 are unconnected and pendable through ISPR (`lcdcons.c:27-28`) | datasheet text 4337-4338; `NVIC_ISPR` 0xe000e200 (`lcdcons.c:68`, datasheet 5564) | holds |
| Renderer 128, SysTick 0, UART/USB 64, PendSV 192 | `intr.h:52-54`, 85-88; `machdep.c:555-559`; `usb.c:1149`; `uart.c:153` | holds |
| "PendSV waits, so no switch lands inside a panel window" | priority order above; switches happen in the top half | holds for renderer windows; top-half putc windows do not sleep |
| "A panic abandons the owner, which will never resume" | `subr_prf.c:563-575`, `lcdcons.c:127-131` | false for a top-half panic (N2) |
| "Kernel putc costs one cell / one row fill" | `lcdterm.c:129-137`, 216-220; `lcdcons.c:173-176` | false (N4) |
| "No modifier state to resynchronize from" | `keyboard.ino:258-266`, `reg.ino:39` | incomplete (N1) |
| "About 250 events per second" in fast mode | two callouts of 2 ms per exchange (`picokbd.c:103`, 284-291) | holds |
| "SCL runs near 96 kHz" | 500 + 46 + 7 high, 750 + 1 low = 1,304 cycles at 125 MHz | holds |
| ABORT escalation acts (`PICOCALC.md:174-176`) | datasheet text 36515-36520 | doubtful (N7) |
| Cell store 3,392 bytes | 53 x 32 x 2 | holds |
| Cell 122 us, near 8,000 cps | 191 bytes x 8 / 12.5 MHz | holds as wire time |
| Init differences from the vendor | `lcdspi.c:600-625` against `st7365p.c:108-125`, 283-316 | holds |
| Vendor drivers request 25 MHz | `picocalc_helloworld/lcdspi/lcdspi.h:7` | holds |
| `check-picocalc-host` compares character and attribute | `lcdterm_test.c:139-156` | holds for six of nine groups (N12) |
| Mutants run in isolated directories | `mutant.sh` `mktemp -d` and `trap` | holds |
