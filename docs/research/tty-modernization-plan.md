# TTY modernization plan: Issue 8 termios subset, UTF-8 and color console, virtual terminals

Revision measured: origin/main at 376ebd583 (2026-11-19; kernels rebuilt on that merge). This note extends
docs/research/tty-console-driver-survey.md, which holds the per-feature cost
table, the virtual-console cost model and the driver inventory; it repeats none
of them and changes no code. Evidence tags: (R) read in source, (M) measured by
a build, size tool or compile on this revision, (E) estimated, (I) inferred.
A donor citation marked [V] was opened at the cited line for this note; an
unmarked donor citation comes from a source reading that was not sample-checked
and carries no weight in a design choice (section 9 lists both sets).

## Recommended design

Keep the sgtty kernel interface and the 32-block clist pool; supply POSIX
termios as a libc shim over the existing ioctls (0 kernel text), and add only
what the shim cannot express to the kernel: VMIN and VTIME in the two padding
bytes of struct tty (0 RAM, +0.40-0.55 KB text), UTF-8-aware erase behind one
retired local-mode bit (+0.15-0.25 KB) and a drain request (+0.04-0.08 KB), as
steps small enough to review one at a time (section 7).
Extend the existing lcdterm.c console (4,335 B text, 164 B of parser state)
instead of importing teken (10,675 B text, 132 B of state, M): add 256-color and
24-bit SGR reduced to the 16-entry palette at parse time, OSC consumption, a
UTF-8 decoder whose output is always one of ASCII, a mapped bank code or '?',
and a cell store that stays at 2 B per cell. Add virtual consoles last, beginning with one frozen background console whose
3,556 B saved copy sits in kernel RAM (3,656 B with its tty) and later in
PSRAM, before any resident shadows. The
numbers that decide it: PICOCALC has 3,448 B of flash headroom, of which
2,424 B lie above the 1,024 B margin the gate enforces, against an estimated
1.42-2.25 KB for the termios and console work and 0.6-1.0 KB more for the frozen
console (the two together can exceed PICOCALC and fit PICOCALC_SD, which has
34,680 B); kernel RAM left past the last section is 13,328 B on PICOCALC, so
the frozen console (3,656 B) and two extra resident shadows (7,312 B) fit and a 4 B-per-cell truecolor store (6,784 B per
console) does not; and a native termios struct in the kernel costs an estimated
2.5-3.5 KB, more than PICOCALC has, for states the shim already carries.

## 1. Measured baseline

| Quantity | Value | Evidence |
|---|---|---|
| struct tty | 100 B; t_chars at offset 76, 14 B; t_winsize 8 B; bytes 98-99 padding | (M) static assertions in tests/kernel/tty_layout_contract.c; padding by arithmetic (I) |
| ttys in a PICOCALC kernel | 4: cnttys, lcdttys, uartttys, usbttys, 100 B each | (M) nm -S on the PICOCALC unix.elf |
| clist pool | cfree 1,024 B: 32 blocks of 32 B, 28 data bytes each, shared by every tty; TTYHOG 255 | (M) nm -S; sys/sys/tty.h clist, sys/kern/tty_subr.c |
| tty.o, tty_subr.o | 5,992 and 1,308 B text | (M) arm-none-eabi-size, GCC 16.2.0, PICOCALC objects |
| lcdterm.o, lcdcons.o, lcdfont.o | 4,335, 2,262, 855 B text; 4,452 B and 2,376 B bss | (M) same |
| console cell store | 32 rows x 53 columns = 1,696 cells; cells[] 1,696 B + attrs[] 1,696 B = 3,392 B; glyph bank 896 B packed | (M) nm -S (cells 0x6a0, attrs 0x6a0, glyph_bank 0x380) |
| cell geometry | 6 x 10 pixels; the font draws 9 rows; the cell byte is ASCII 0x20-0x7e or bank 0x80-0xff; the attribute byte holds fg 0-15, bg 0-7, underline | (R) sys/arch/rp2040/dev/lcdterm.h |
| palette | 16 entries of 8-bit RGB, converted to RGB565 per drawn cell | (R) sys/arch/rp2040/dev/st7365p.c lcd_palette, pal565 |
| flash headroom, margin 1,024 B | PICO 32,440; PICO_UART 40,384; PICOCALC 3,448; PICOCALC_LEAN 11,128; PICOCALC_SD 34,680 (RP2040-sd.ld, FLASH 160K-256) | (M) tools/check_kernel_headroom.py on a fresh build of this revision |
| kernel RAM free (end of last section to the U0AREA base, 0x2003E800) | PICO 23,344; PICO_UART 39,776; PICOCALC 13,328; PICOCALC_LEAN 14,160; PICOCALC_SD 13,264 | (M) arm-none-eabi-size -A section addresses |

The headroom check reports free flash, and 1,024 B of it is the gate's margin:
PICOCALC has 3,448 B free and 2,424 B above the margin. The survey's "3.4 KB
above the margin" for PICOCALC is the free figure; this note budgets the
2,424 B figure. The survey's per-feature estimates (E) are reused below and
keep that tag; the 5.0 B per comment-stripped C line basis comes from tty.o.

## 2. Target interface: POSIX.1-2024 (Issue 8) chapter 11 subset

Source: the Issue 8 base definitions text, chapter 11 (11.2.2 to 11.2.6), and
the Issue 8 function pages for tcsetattr, tcgetwinsize and tcgetsid. Status
words: required (named in chapter 11), now (the port honors it today through
sgtty), planned (step named in section 7), declined (reason given), report-only (a fixed value the shim reports and cannot change), set only (a request has an effect but the state cannot be read back). A row marked now names its get and set ioctl paths. tcsetattr
sets the attributes the implementation supports, leaves the others unchanged,
and returns -1 with EINVAL only when no part of the request can be honored
[V: tcsetattr page, "If no part of the request can be honored"]; chapter 11.2.4
lets an implementation ignore modes that do not apply to a non-serial terminal
[V: 11.2.4 text]. A declined bit is therefore cleared on read-back, and the
shim returns EINVAL for a request that asks for no supported change.

### Test codes

RT: round trip, tcsetattr then tcgetattr, against a model of the sgtty ioctls
(the model implements the get and set paths each row names). RV: the shim
reports the stated constant for the row. RF: a request the row declines leaves
the state unchanged, and a request with no supported part returns EINVAL
[V: tcsetattr page]. KM: a kernel-side host test of the production tty.c
function the row changes.

### c_iflag (11.2.2), one row per bit

| Item | Issue 8 | Today (sgtty source of truth) | Shim mapping | Kernel change: text / RAM (E) | Test |
|---|---|---|---|---|---|
| BRKINT | required | no bit; tty.c has no break-input path (search, I) | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| ICRNL | required | CRMOD sys/sys/ioctl.h:137; input map sys/kern/tty.c:1002 | ICRNL and ONLCR move together onto CRMOD; a split request is part-honored | none; a split bit costs +30-50 B (E) and is built only on a program need | RT |
| IGNBRK | required | no bit | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| IGNCR | required | no bit; sys/kern/tty.c:1002 maps CR to NL only | report clear; set unchanged | declined; +30 B if built (E) | RV, RF |
| IGNPAR | required | no bit; EVENP and ODDP sys/sys/ioctl.h:139-141 have no reader in sys/kern/tty.c (search) | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| INLCR | required | no bit; ttyinput has no NL-to-CR map (read) | report clear; set unchanged | declined; +30 B if built (E) | RV, RF |
| INPCK | required | no bit, as IGNPAR | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| ISTRIP | required | PASS8 sys/sys/ioctl.h:161; strip at sys/kern/tty.c:1053 | ISTRIP is the inverse of PASS8 | none | RT |
| IXANY | required | DECCTQ sys/sys/ioctl.h:164; only-start-restarts test at sys/kern/tty.c:1243 | IXANY is the inverse of DECCTQ (a correction to the earlier "declined": the bit exists) | none | RT |
| IXOFF | required | TANDEM sys/sys/ioctl.h:133; sys/kern/tty.c:250, 317, 1031 | IXOFF is TANDEM | none | RT |
| IXON | required | no bit; stop and start compared in ttyinput sys/kern/tty.c:1113-1123 | report set; clear request unchanged | declined: 0 / 0 | RV, RF |
| PARMRK | required | no bit | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| IUTF8 | extension (no hit in the Issue 8 text extract) | no bit; erase is byte-wise (sys/kern/tty.c:1158-1184, unputc) | a retired t_flags bit, 0x80000 (sys/sys/ioctl.h comment "old TILDE") | erase walks continuation bytes: +0.15-0.25 KB / 0 | RT; KM (erase of U+00E9 with and without the bit) |

### c_oflag (11.2.3), one row per bit and per delay field

| Item | Issue 8 | Today (sgtty source of truth) | Shim mapping | Kernel change: text / RAM (E) | Test |
|---|---|---|---|---|---|
| OPOST | required | RAW or LITOUT disables output processing, sys/kern/tty.c:1266 (sys/sys/ioctl.h:138, 155) | OPOST is clear when RAW or LITOUT is set; clearing OPOST sets LITOUT | none | RT |
| ONLCR | required | CRMOD; output map sys/kern/tty.c:1310 | with ICRNL | none | RT |
| OCRNL | required | no bit | report clear; set unchanged | declined; +30 B if built (E) | RV, RF |
| ONOCR | required | no bit | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| ONLRET | required | no bit | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| OFILL, OFDEL | required | no bit | report clear; set unchanged | declined: 0 / 0 | RV, RF |
| NLDLY, CRDLY, BSDLY, VTDLY, FFDLY | required (XSI) | delay bits retired from t_flags (sys/sys/ioctl.h comments at 0x100, 0x1000, 0x2000, 0x4000, 0x8000) | report value 0; non-zero request unchanged | declined: 0 / 0 | RV, RF |
| TABDLY, value TAB3 | required (XSI) | XTABS sys/sys/ioctl.h:144; sys/kern/tty.c:1289 | TAB3 is XTABS | none | RT |
| TABDLY, values TAB1 and TAB2 | required (XSI) | none | refused (unchanged) | declined: 0 / 0 | RF |

### c_cflag (11.2.4), one row per bit and per value class

| Item | Issue 8 | Today (sgtty source of truth) | Shim mapping | Kernel change: text / RAM (E) | Test |
|---|---|---|---|---|---|
| CLOCAL | required | NOHANG sys/sys/ioctl.h:158; sys/kern/tty.c:798 | CLOCAL is NOHANG | none | RT |
| CREAD | required | none | report set; clear unchanged | declined: 0 / 0 | RV, RF |
| CSIZE, value CS8 | required | none; bytes are 8 bits, PASS8 and ISTRIP act on input | report CS8 | none | RV |
| CSIZE, values CS5, CS6, CS7 | required | none | refused (unchanged) | declined: 0 / 0 | RF |
| CSTOPB | required | none | report clear; set unchanged | declined; UART0 line control planned later (E: +0.1 KB) | RV, RF |
| PARENB, PARODD | required | EVENP and ODDP sys/sys/ioctl.h:139-141 are stored in t_flags and read by nothing in sys/kern/tty.c (search) | report clear; set unchanged (reporting the stored bits would claim parity the line does not apply) | declined: 0 / 0 | RV, RF |
| HUPCL | required | TIOCHPCL sys/sys/ioctl.h:111 sets TS_HUPCLS sys/kern/tty.c:487; sys/sys/tty.h:261; no reader or clearer | report clear; a set request is passed to TIOCHPCL (set only) | planned get and clear in the VMIN and VTIME request: +20-40 B / 0 | RV; RT after the request lands |
| B0 (hang up) | required | sg_ispeed, sg_ospeed sys/kern/tty.c:546, 581; B0 effect not read | passed through; effect unexamined (not verified) | none | RT of the stored value only |
| B50 to B38400 (names required) and the stty extras to B4000000 | required (to B38400) | sg_ispeed, sg_ospeed sys/kern/tty.c:546, 581; t_ispeed below NSPEEDS, sys/sys/tty.h | stored and returned; the console ignores them | none | RT |

### c_lflag (11.2.5), one row per bit

| Item | Issue 8 | Today (sgtty source of truth) | Shim mapping | Kernel change: text / RAM (E) | Test |
|---|---|---|---|---|---|
| ECHO | required | ECHO sys/sys/ioctl.h:136; sys/kern/tty.c:864, 922 | ECHO | none | RT |
| ECHOE | required | CRTERA sys/sys/ioctl.h:152; sys/kern/tty.c:848 (rub string); CRTBS :926 | ECHOE is CRTERA; BSD "crt" also sets CRTBS (bin/stty/stty.c:231-234) | none | RT |
| ECHOK | required | CRTKIL sys/sys/ioctl.h:160; sys/kern/tty.c:1162 | ECHOK is CRTKIL; the behaviors differ (a visual kill versus a NL after KILL), so the row is approximate (I) | none | RT of the bit only |
| ECHONL | required | no bit | report clear; set unchanged | declined; one branch in ttyinput, +20 B (E) | RV, RF |
| ICANON | required | RAW and CBREAK sys/sys/ioctl.h:134, 138; sys/kern/tty.c:389, 1034 | ICANON is clear when RAW or CBREAK is set | none | RT |
| ISIG | required | CBREAK keeps signals, RAW drops them (sys/kern/tty.c:1034, 1141) | ISIG is clear iff RAW; (ICANON on, ISIG off) refused | none | RT; RF |
| IEXTEN | required | no bit; LNEXT, WERASE and REPRINT run outside RAW | report set when ICANON is set | declined: 0 / 0 | RV |
| NOFLSH | required | NOFLSH sys/sys/ioctl.h:165; sys/kern/tty.c:1103, 1130 | NOFLSH | none | RT |
| TOSTOP | required | TOSTOP sys/sys/ioctl.h:156; sys/kern/tty.c:1575 | TOSTOP | none | RT |
| ECHOCTL (extension) | no | CTLECH sys/sys/ioctl.h:162; sys/kern/tty.c:875, 950 | CTLECH | none | RT |
| ECHOPRT (extension) | no | PRTERA sys/sys/ioctl.h:151; sys/kern/tty.c:984 | PRTERA | none | RT |
| FLUSHO (extension) | no | FLUSHO sys/sys/ioctl.h:157; sys/kern/tty.c:962 | FLUSHO | none | RT |
| PENDIN (extension) | no | PENDIN sys/sys/ioctl.h:163; sys/kern/tty.c:371, 386 | PENDIN | none | RT |
| MDMBUF, CRTSCTS (extensions) | no | MDMBUF sys/kern/tty.c:781; RTSCTS sys/kern/tty.c:256, 326 | MDMBUF, CRTSCTS | none | RT |

### c_cc (11.2.6), one row per index

| Item | Issue 8 | Today (sgtty source of truth) | Shim mapping | Kernel change: text / RAM (E) | Test |
|---|---|---|---|---|---|
| VEOF | required | tc_eofc, TIOCGETC and TIOCSETC sys/kern/tty.c:599, 603 | VEOF | none | RT |
| VEOL | required | tc_brkc, TIOCGETC | VEOL | none | RT |
| VERASE | required | sg_erase, TIOCGETP sys/kern/tty.c:578 | VERASE | none | RT |
| VINTR | required | tc_intrc, TIOCGETC | VINTR | none | RT |
| VKILL | required | sg_kill, TIOCGETP | VKILL | none | RT |
| VMIN | required | none; the shim reports 1 | new request over the two pad bytes of struct tty | storage and request +0.10-0.15 KB / 0; the read semantics are a separate step (+0.30-0.40 KB) | RT after the request lands; KM |
| VQUIT | required | tc_quitc, TIOCGETC | VQUIT | none | RT |
| VSUSP | required | tc_suspc, TIOCGLTC and TIOCSLTC sys/kern/tty.c:608, 612 | VSUSP | none | RT |
| VTIME | required | none; the shim reports 0 | as VMIN | as VMIN | RT after the request lands; KM |
| VSTART | required | tc_startc, TIOCGETC | VSTART | none | RT |
| VSTOP | required | tc_stopc, TIOCGETC | VSTOP | none | RT |
| VWERASE, VREPRINT, VLNEXT, VDISCARD (extensions) | no | tc_werasc, tc_rprntc, tc_lnextc, tc_flushc, TIOCGLTC | same names | none | RT |
| VDSUSP (extension) | no | tc_dsuspc, TIOCGLTC | VDSUSP | none | RT |
| VEOL2, VSTATUS (extensions) | no | no field in struct ttychars | report disabled (_POSIX_VDISABLE, sys/sys/ttychars.h:33) | declined: 0 / 0 | RV |

### Functions, one row per function and selector

| Function and selector | Maps to | Missing | Step |
|---|---|---|---|
| tcgetattr | TIOCGETP, TIOCGETC, TIOCGLTC, TIOCLGET (sys/kern/tty.c:578, 599, 612, 631) | VMIN and VTIME read; HUPCL read | S02, K01 |
| tcsetattr, TCSANOW | TIOCSETN, TIOCSETC, TIOCSLTC, TIOCLSET (sys/kern/tty.c:541, 603, 608, 627); TIOCSETN does not flush input except across RAW (:551-554) | four ioctls are not one atomic change; VMIN and VTIME set | S03, K01 |
| tcsetattr, TCSADRAIN | drain request, then the TCSANOW path | the drain request (TIOCDRAIN over ttywait, sys/kern/tty.c:216) | S04, K04 |
| tcsetattr, TCSAFLUSH | TIOCSETP (drains output, flushes input, sys/kern/tty.c:540, 551) then TIOCSETC, TIOCSLTC, TIOCLSET | none | S04 |
| tcdrain | TIOCDRAIN; a poll of TIOCOUTQ (sys/kern/tty.c:506) is inexact because it ignores bytes in the UART FIFO | TIOCDRAIN | K04 |
| tcflush, TCIFLUSH | TIOCFLUSH with FREAD (sys/kern/tty.c:490) | none | S04 |
| tcflush, TCOFLUSH | TIOCFLUSH with FWRITE | none | S04 |
| tcflush, TCIOFLUSH | TIOCFLUSH with 0 or both | none | S04 |
| tcflow, TCOOFF | TIOCSTOP (sys/kern/tty.c:510) | none | S04 |
| tcflow, TCOON | TIOCSTART (sys/kern/tty.c:519) | none | S04 |
| tcflow, TCIOFF | unsupported: tcflow returns -1 with EINVAL ([EINVAL] is the unsupported-action error on the tcflow page) | the kernel sends flow characters only through t_outq: TANDEM puts stopc and startc there with putc (tty.c:250-253, 317-321) and ttstart refuses to draw or transmit while TS_TTSTOP is set (lcdcons.c:250, uart.c:477, usb.c:1351), so an ordinary write queues behind stopped output exactly as the kernel's own byte does; an out-of-band byte would need a priority transmit slot in each of three drivers | none (declined) |
| tcflow, TCION | unsupported, as TCIOFF | as TCIOFF | none (declined) |
| tcsendbreak | TIOCSBRK and TIOCCBRK defined (sys/sys/ioctl.h:186-187), no case in sys/kern/tty.c | a handler in the UART driver; console and USB declined | K07 |
| tcgetsid | none | sessions (no p_session in sys/); declined | none |
| tcgetpgrp | TIOCGPGRP (sys/kern/tty.c:658) | none | S05 |
| tcsetpgrp | TIOCSPGRP (sys/kern/tty.c:641; the source comment at :639 says the group is not verified) | none | S05 |
| tcgetwinsize | TIOCGWINSZ (sys/kern/tty.c:671) | none | S05 |
| tcsetwinsize | TIOCSWINSZ (sys/kern/tty.c:663; SIGWINCH only when the size changes) | none | S05 |
| cfgetispeed, cfgetospeed | struct field read of the B* index | none | S01 |
| cfsetispeed, cfsetospeed | struct field write, effective at tcsetattr | none | S01 |
| cfsetspeed, cfmakeraw (BSD extras) | user code over the same state | none | S01 |

Native TIOCGETA, TIOCSETA, TIOCSETAW and TIOCSETAF stay declined until a row
above that the shim cannot carry is needed by a program (CS5-CS7, IGNCR, ICRNL
without ONLCR, ISIG without ICANON); the estimated cost is +2.5-3.5 KB text and
+24 B per tty (E, survey), more than the 2,424 B PICOCALC has above its margin.

## 3. Queue and line-discipline design

| Donor | Queue design | RAM per tty | Evidence |
|---|---|---|---|
| NuttX serial | head/tail ring, 256 B receive and 256 B transmit by Kconfig default; termios state 3 tcflag_t plus optional cc_t; termios hardware-change path off by default | about 0.5 KB | nuttx include/nuttx/serial/serial.h:317-325 [V], drivers/serial/Kconfig:798-807 [V], :337-345 [V] |
| FreeBSD | block queues: 128 B input blocks, 252 B output blocks, grown to a quota | grows with the quota | sys/sys/ttyqueue.h:57, :68 [V]; tty_inq.c, tty_outq.c |
| Mach osfmk | cirbuf input 512 B and output 2,048 B | 2.5 KB | osfmk6.1 kernel/src/mach_kernel/device/chario.c:306-307 [V] |
| Linux n_tty | 4,096 B read ring plus 4,096 B echo ring | 8 KB | include/linux/tty.h:299, drivers/tty/n_tty.c:1601 [V] |
| BSD clist (NetBSD, OpenBSD, DragonFly) | character blocks in a shared pool | pool, not per tty | sys/kern/tty_subr.c; not opened |
| this port | clist, 32 blocks x 28 data bytes shared by every tty; struct tty 100 B | 100 B each plus 1,024 B shared | (M) section 1 |

Choice: the existing clist. A per-tty ring costs a fixed 0.5 KB at NuttX's
size, 2 KB for four ttys and 3 KB for six, against the pool's 1,024 B total,
and rings cannot lend capacity: a tty that idles still holds its bytes. The
shared pool keeps the whole queue cost of a virtual terminal at its 100 B
struct tty. The line discipline stays in sys/kern/tty.c; the donors whose
logic transfers are the read path of OpenBSD (VMIN and VTIME in
sys/kern/tty.c:1524-1532 and the timeout return at :1594 [V]) and FreeBSD
(ttydisc_read_raw_read_timer clamps VMIN to at least 1 at
sys/kern/tty_ttydisc.c:309 [V], which is the Issue 8 case table's third row),
and for UTF-8 erase FreeBSD's ttydisc_rubchar (sys/kern/tty_ttydisc.c:888
opens the IUTF8 branch [V]; its continuation-byte walk is unread past that
line). The port already has unputc (sys/kern/tty_subr.c:343), which pops the
last queued byte, so a UTF-8 erase loops unputc while the popped byte is a
continuation byte (0x80-0xbf) and then stops after one lead byte; the clist
needs no backward link.

VMIN and VTIME need a timer. The survey names the callout table (NCALL
16 + 2 x MAXUSERS) as the prerequisite; the callout array is 0x120 B (M).

### Cost against both budgets (E unless marked)

Step identifiers are those of section 7.

| Steps | Text | RAM | PICOCALC (2,424 above margin) | PICOCALC_SD (34,680) |
|---|---|---|---|---|
| S01-S05, libc shim (per program that links it) | 0 kernel; 0.7-1.1 KB for tcgetattr and tcsetattr, 0.9-1.4 KB with every function (818 B prototype of the first two, survey M) | 0 | fits | fits |
| K01, K02 (VMIN, VTIME) | 0.40-0.55 KB | 0 | fits | fits |
| K05 (IUTF8 erase) | 0.15-0.25 KB | 0 | fits | fits |
| K04, K06 (drain, HUPCL) | 0.06-0.12 KB | 0 | fits | fits |
| C03 (extended SGR) | 0.25-0.40 KB | 0 per cell | fits | fits |
| C01 (UTF-8 decoder, ACS inverse map) | 0.35-0.55 KB | 3 B per terminal | fits | fits |
| C04, C05, C06 (OSC, CSI additions, tab stops) | 0.21-0.38 KB | 7-8 B per terminal | fits | fits |
| Subtotal without virtual consoles | 1.42-2.25 KB | | 0.1-0.95 KB spare | fits |
| K07 (UART0 break) | 0.06-0.10 KB | 0 | not planned | fits |
| C02 (Latin-1 glyph ROM, 96 glyphs, 7 B packed) | 0.67 KB | 0 (loads into the 896 B bank) | declined | fits |
| V00, V01 (one frozen console) | 0.6-1.0 KB | 3,656 B | 2.02-3.25 KB with the subtotal: no fit at the upper bound | fits |
| V02 (PSRAM slot) | 0.2-0.35 KB | -3,556 B | PSRAM kernels only | fits |
| V03 (N resident shadows, survey) | 0.4-0.6 KB more | 3,656 B per console | no | fits |

PICOCALC admits the subtotal; virtual consoles follow only after the measured
growth of the preceding steps is known, and section 7 makes that measurement
the gate.

## 4. Console emulator

lcdterm.c accepts the sequences of the xterm-basic termcap entry
(sys/arch/rp2040/dev/lcdterm.c header comment): C0 BS, HT, LF, VT, FF, CR, BEL;
CSI A B C D G d H f J K L M @ P X r m h l n c; ESC [ ( ) * + 7 8 D E M c; DEC
private modes 1, 7 and 25; DEC special graphics through ESC ( 0; cursor-position
and device-attribute reports (R). Unrecognized ESC finals fall through
esc_final's default and return to the ground state.

### Donor emulators

| Donor | Size | Per-terminal state | Color depth | License |
|---|---|---|---|---|
| FreeBSD teken | teken.o 10,675 B text, -Os for Cortex-M0+ (M; generated state table and wcwidth included, memmove external); teken.c 759 and teken_subr.h 1,345 lines | sizeof(teken_t) = 132 B (M) | 38;5;n and 48;5;n stored in an unsigned char; no 38;2 (teken_subr.h:1195 [V]; teken.c:678-689 gives the 256-to-8 and 256-to-16 tables [V]) | BSD-2-Clause (teken.c:2 [V]) |
| NetBSD wsemul_vt100 | 1,114 + 987 + 213 + 155 lines | wsemul_vt100var.h:30-59 | 38 consumes the 38;2 arguments and drops them (wsemul_vt100_subr.c:566 [V]) | two-clause BSD, Drochner 1998 (header [V]) |
| OpenBSD wsemul_vt100 | 1,359 + 1,033 + 218 + 164 lines | args[10], tab bitmap | no 38 or 48 | ISC (Vallat 2007, 2013) plus the 1998 BSD block (header [V]) |
| DragonFly syscons scterm-sc | 782 lines | 5 parameters | no 38 or 48 | two-clause BSD whose condition 1 keeps the copyright "as the first lines of this file unmodified" (header [V]) |

Choice: extend lcdterm.c. teken's object is 2.5 times lcdterm.o's whole text
and drives a cell callback table that lcdterm already implements as three
panel primitives; the savings from a shared state-machine generator do not
cover a second parser. teken remains the oracle: its sequences table and
teken_256to16tab are test data, not shipped code.

### Sequence set

| Class | Status |
|---|---|
| C0 and the present CSI set above | now |
| CSI 38;5;n, 48;5;n, 38;2;r;g;b, 48;2;r;g;b, and the colon-separated forms | planned |
| CSI E, F (next and previous line), S, T (scroll up, down), s, u (save, restore cursor), I, Z (tab forward, back), b (repeat), ESC H and CSI g (tab set and clear, one bit per column, 7 B) | planned |
| OSC (ESC ] ... BEL or ESC \), DCS, APC, PM, SOS: consumed to the string terminator with a length bound | planned |
| DEC private 47, 1047, 1049 (alternate screen) | declined: it needs a second cell store of 3,392 B, the same cost as a virtual terminal |
| DECSCUSR, mouse modes 1000-1006, bracketed paste 2004, DECOM, DECCOLM | declined: no program on the port issues them; each unknown private mode is consumed and ignored as today |
| DECALN, DECDHL, double-width rendering | declined |

Two behaviors of the present parser matter to the color work. First, do_sgr
walks the parameter list flat, so ESC [38;5;196m reaches the chain with v = 5
and v = 196 (both ignored) and ESC [38;2;1;4;9m reaches it with v = 1 (bold)
and v = 4 (underline): extended-color sequences change bold and underline
today (I; lcdterm.c do_sgr, lines 511-558 read, not run). Second, an OSC
string is not consumed: ESC ] returns to the ground state in esc_final and the
payload prints as text until a BEL rings (I; same reading).

### Color depths

The stored attribute byte keeps fg in 4 bits (0-15), bg in 3 bits (0-7) and
underline in 1 bit. The plan reduces every requested color to that byte when
the SGR is parsed and adds nothing per cell:

| Request | Mapping |
|---|---|
| 30-37, 90-97, 40-47, 100-107 | present (bright background folds to base) |
| 38;5;n, 48;5;n | n below 16: direct; 16-231: the xterm cube levels 0, 95, 135, 175, 215, 255 per channel; 232-255: gray 8 + 10(n-232); then the nearest palette entry by squared RGB distance |
| 38;2;r;g;b, 48;2;r;g;b | the nearest palette entry by squared RGB distance |
| background | the nearest of the first eight entries |

The nearest-entry search is about 120 B of code over the 16 x 3 B palette
already in flash (E). Against FreeBSD's hand-tuned teken_256to16tab, this rule
agrees on 118 of the 240 indices 16-255, and on 136 when only the low three
bits (hue, ignoring brightness) are compared (M: a throwaway script over
teken.c's table and the port's lcd_palette). The rule is the principled one for
this panel because it measures against the colors the panel shows; the 49%
agreement is the reason the host test compares only the properties below, not
teken's table:

- indices 0-15 map to themselves (bg to the base color);
- the six gray-cube diagonal entries map to a gray of the palette (0, 7, 8 or 15);
- a pure primary of the cube (levels 5,0,0 and so on) maps to the matching
  bright primary of the palette;
- the mapping is total: every n in 0-255 and every r, g, b in 0-255 gives a
  value in 0-15.

Declined: RGB565 per cell (a 4 B store, +6,784 B per console, 51% of PICOCALC's
free RAM) and a 256-color byte per cell (+3,392 B per console). The measured
cell store is 2 B per cell and stays so. Hardware-visible difference between
the nearest-entry rule and teken's table is not judged here (board not run).

### UTF-8 and the glyph-coverage rule

Today lcdterm_putc has no decoder: a byte 0xa0-0xff is stored as its own bank
code and 0x80-0x9f draws '?' (lcdterm.c lcdterm_putc, last statements, R), so a
UTF-8 sequence prints as its raw bytes' bank glyphs ('?' initially). The bank
slots 0xa0-0xff are the 96 codes of ISO 8859-1 U+00A0-U+00FF; that makes the
decoder cheap and the coverage rule exact.

State: an accumulator and a continuation count, 3 B per terminal (E). The
decoder rejects overlong forms, surrogates and code points above U+FFFF by
drawing one '?' and resynchronizing on the next lead or ASCII byte; a truncated
sequence interrupted by ASCII draws one '?' before the ASCII. Code points above
U+25FF need no accumulation past 16 bits because every mapped code point lies at
or below it.

The rule: a decoded code point draws iff one of three tables holds a glyph for
it, and otherwise draws exactly one '?' cell.

1. U+0020-U+007E: the ASCII font (lcdfont.c, 95 glyphs, 855 B text, M).
2. U+00A0-U+00FF: bank code equal to the code point, valid only when the bank
   slot has been loaded from the Latin-1 ROM table (0.67 KB, planned on
   PICOCALC_SD and PICO kernels, declined on PICOCALC) or by LCDIOC_SETGLYPH.
   A slot still holding the '?' default draws '?' either way, so coverage is
   observable and never silent.
3. A sorted table of Unicode box-drawing (U+2500-U+257F subset), block and
   arrow code points that map onto the 32 DEC special graphics bank codes
   0x80-0x9f (64 B, E), the inverse of the table ESC ( 0 already uses.

Width is always one cell: combining marks (U+0300-U+036F), zero-width joiners
and East Asian wide characters take the '?' path, so the cursor column never
disagrees with the program's wcwidth for the supported set. A raw continuation
byte 0x80-0x9f never reaches the bank, preserving the invariant stated in the
lcdterm.c header (a stray continuation cannot draw a box edge). The host test
enumerates all 1,114,112 code points and every one-, two-, three- and four-byte
prefix once through the decoder with recording primitives and asserts: exactly
one cell is written per accepted code point, the code written is in tables 1-3
or is '?', and the cursor advances by exactly one column.

State per terminal after the additions: the present 164 B (M, survey) plus
3 B decoder, 7 B tab stops and 1 B string-state bound, about 175 B (E).

## 5. Virtual terminals

Measured costs (cells, attributes and struct tty from section 1; the 164 B
parser, cursor and SGR figure is the survey's measurement): one console's
state is 1,696 B cells + 1,696 B attributes + 164 B of scalars and parser =
3,556 B, and its struct tty adds 100 B, 3,656 B per extra console. The NetBSD
vcons model keeps a character array and an attribute array per screen (scr_chars,
scr_attrs in sys/dev/wscons/wsdisplay_vconsvar.h:44-60 [V]) and redraws a
screen from them (sys/dev/wscons/wsdisplay_vcons.c:664 [V]); the port already
has the same split at 2 B per cell, and lcdterm_repaint is its redraw.

Three steps, each smaller than the next: V01 freezes one background console in
kernel RAM, V02 moves the frozen store to PSRAM, and V03 generalizes to
resident shadows with live output on every console.

### Step V01: one frozen background console

Two consoles exist. Console 0 (A) is /dev/console, the kernel-message target
and the panic target; console 1 (B) is the second. One is shown; the other is
frozen: its screen state is saved, nothing is drawn for it and its writers
wait. A switch restores the frozen console and freezes the shown one.

What must be saved: the whole of lcdterm.c's file-static state, gathered into
one struct by step V00.

| State | Bytes | Where in lcdterm.c |
|---|---|---|
| cells | 1,696 | cells[LT_ROWS][LT_COLS], line 66 |
| attrs (fg, bg, underline) | 1,696 | attrs, line 67 |
| scroll ring offset and cursor | top, crow, ccol, wrapnext, mtop, mbot (lines 69-72) | within the 164 B |
| SGR rendition | sgr_fg, sgr_bg, sgr_bold, sgr_ul, sgr_rev, cattr (lines 74-75) | within the 164 B |
| modes | autowrap, insertmode, cursor_on, appcursor, gfx (line 77) | within the 164 B |
| saved cursor | s_row, s_col, s_fg, s_bg, s_bold, s_ul, s_rev, s_gfx (line 85) | within the 164 B |
| parser | state, params[8], nparam, priv, inter (lines 87-91): a console frozen mid-escape resumes mid-escape | within the 164 B |
| total saved copy | 3,556 | 3,392 + 164 (M basis, section 1) |

Not saved: the cursor overlay (cursor_drawn, cd_prow, cd_col, line 83), which
is hidden before the save and redrawn after the repaint; the palette, a const
table in flash (sys/arch/rp2040/dev/st7365p.c:121), so there is nothing to save;
and the glyph bank (896 B packed), which stays a panel-wide resource that a tile
program loads with LCDIOC_SETGLYPH, so a program that rewrote bank glyphs
changes how the other console's bank codes look (a stated limit). The tty
state (t_flags, t_chars, t_winsize, t_pgrp and the queues) lives in each
console's own struct tty, 100 B, and is not copied; V01 adds lcdttys[1].

Mechanism: an in-place exchange of the live struct with the saved slot, word by
word, with no third buffer: 889 words. At about 10 cycles per word the exchange
takes about 71 us at 125 MHz (E); the switch time is the repaint.

Where the copy lives: a 3,556 B slot in kernel bss, plus the 100 B tty. On
PICOCALC_SD that leaves 9,608 B of the 13,264 B free kernel RAM, and on
PICOCALC 9,672 B of 13,328 B (M basis, section 1). V02 moves the slot off bss.

Processes that write to the frozen console wait on the tty's own flow control.
The freeze has its own state bit, TS_HIDDEN, a new internal state bit in
sys/sys/tty.h (0x008000L is unassigned among the bits at tty.h:251-266; a grep of
sys/ for the value precedes use). TS_TTSTOP stays the line discipline's: it
records Ctrl-S, TCOOFF and TIOCSTOP, and a user stop survives a round trip.
TS_HIDDEN is tested wherever output starts: ttstart is only a call through
t_oproc (sys/kern/tty.c:355-359), so every start, including the one TIOCSTART
makes (tty.c:519-527), reaches lcdstart, whose refusal test at
sys/arch/rp2040/dev/lcdcons.c:250 gains TS_HIDDEN beside TS_TIMEOUT, TS_BUSY and
TS_TTSTOP, and the renderer's drain condition at lcdcons.c:338 gains it too. An
explicit restart of a hidden console clears TS_TTSTOP as asked and draws nothing;
the thaw clears only TS_HIDDEN and calls ttstart, so a console the user stopped
stays stopped. ttyflush with FWRITE clears TS_TTSTOP (tty.c:281), which would
have thawed a TS_TTSTOP-based freeze on tcflush(TCOFLUSH); it leaves TS_HIDDEN
alone. Output accumulates in the frozen tty's queue to TTHIWAT, capped at 336 B
(tty.c:41-53), and ttwrite then sleeps (tty.c:1548). This reuses the existing
sleep, loses no byte, draws nothing into the wrong store and keeps order: the queued bytes (at most the cap plus one 100 B chunk)
render after the restore. Processes that do not write keep running, and
readers of the frozen tty sleep because keyboard input goes to the shown tty
only. Whether the ttwrite sleep is interruptible by a signal was not read.
Stopping a frozen console's foreground group with SIGTSTP is a possible later
policy (job control exists in tty.c); it is not part of V01.

Kernel messages while console 0 is frozen: lcdputc targets console 0
(lcdcons.c:367), whose parser would draw into the live store that now holds
console 1. Rule: while the shown console is not 0, lcdputc calls no terminal or
panel primitive, counts the dropped bytes as the deferred ring counts its
drops (lcdcons.c defer_put), and the message log keeps them where it is
enabled (the TOLOG path of putchar, sys/kern/subr_prf.c).

Switch key: Ctrl+F1 shows console 0 and Ctrl+F2 shows console 1, consumed in the
keyboard path before translation (sys/arch/rp2040/dev/picokbd_map.c: F1 is 0x81,
F2 is 0x82, Ctrl is 0xa5; the combination is unverified on hardware). A key for
the console already shown does nothing, and the key is refused while lcdgfx is
open. Input follows the shown console: the keyboard session and
picokbd_evq_check read lcdttys[0] today (lcdcons.c:546, 627, 647) and take the
shown index.

Switch sequence, under the drawing flag (lcd_acquire): hide the cursor; set
TS_HIDDEN on the outgoing tty; exchange; program the scroll start from the
restored ring offset (lcd_set_top); repaint; clear TS_HIDDEN on the incoming tty;
release the flag; call ttstart on the incoming tty, which stays quiet if the user
had stopped it.

Redraw time: lcdterm_repaint draws 1,696 cells at 115-122 us each (191 bytes x
8 bits / 12.5 MHz SPI = 122 us, docs/research/picocalc-console-review.md:140-144,
:358; the 115 us figure is docs/research/picocalc-games-survey.md:87), so 195-207
ms. That is wire time derived from the SPI clock, not a board capture. It
replaces the survey's 84 us per cell (142 ms), which is below the wire time.

Panic and fault with a frozen console: lcdabandon already serves both entries
(the panic branch of lcdputc and arm_fault, sys/arch/rp2040/rp2040/fault.c:232).
Rule: after aborting the panel transfer, if the shown console is not 0 it
exchanges console 0 back in, makes console 0 shown, programs the scroll start,
repaints, and only then lets the first byte draw, with interrupts as they are at
that point, no sleep and no allocation. With the slot in kernel RAM (V01) that is
the same exchange as a switch. With the slot in PSRAM (V02) the exchange runs
only when the PSRAM driver is idle; a panic that interrupted a PSRAM frame
cannot use the PIO state machine safely, so the panic path then resets the
terminal, clears the screen and prints on a blank console 0, and console 0's
earlier text is lost. The rule costs +60-120 B text (E), and its repaint is the
195-207 ms above.

Hidden-console operations (tcsetattr TCSADRAIN and TCSAFLUSH, tcdrain, tcflush,
tcflow, tcsendbreak) are checked against the same state in "Operations that
must act while output is stopped" below. First falsifiable check: after A to B to A, console 0's cells, attributes,
cursor, ring offset and parser state equal their values before the first
switch, and likewise B after B to A to B, compared byte for byte; the frozen
console never calls a draw primitive; lcdputc draws nothing while console 1 is
shown. Falsifiers: any byte of either store differs; a draw call from the frozen
console; a panic with console 1 shown leaves console 1's text on the model panel.

Host test: the two-console model of check-picocalc-host with recording
primitives, a tty model that reaches TTHIWAT and sleeps its writer, and an
escape sequence split across the switch (ESC [ 3 8 ; 5 before, ; 1 9 6 m after),
and two switch tests: a console stopped with TIOCSTOP before the switch is still
stopped, with its queue undrawn, after a round trip; and TIOCSTART on the hidden
console clears TS_TTSTOP but calls no draw primitive until the thaw. Two further
mutants: freeze with TS_TTSTOP instead of TS_HIDDEN (the first test fails), and
omit TS_HIDDEN from the lcdstart refusal test (the second fails). Other mutants: skip the parser state in the exchange; restore without lcd_set_top;
let the renderer drain the frozen tty; omit the restore in lcdabandon.

Cost: V00 (the state struct, no behavior change) +0.3-0.5 KB text (E, survey);
V01 +0.3-0.5 KB text (E) and +3,656 B RAM.

### Operations that must act while output is stopped

Each row names how the operation behaves when the output is held (TS_TTSTOP by
the user, or TS_HIDDEN by a switch).

| Operation | Behavior while held | Rule |
|---|---|---|
| tcflush TCOFLUSH, TCIOFLUSH | ttyflush discards queued output whatever the stop state, calls the driver stop routine, wakes writers (tty.c:266-290) | works on a hidden console; clears TS_TTSTOP (tty.c:281, existing 4.3BSD behavior) and leaves TS_HIDDEN |
| tcflush TCIFLUSH | touches input queues only | unaffected |
| tcflow TCOOFF, TCOON | TIOCSTOP and TIOCSTART change TS_TTSTOP at once (tty.c:510-527) | act immediately; on a hidden console TCOON clears the stop and draws nothing until the thaw |
| tcflow TCIOFF, TCION | the byte would sit in t_outq behind held output | unsupported (EINVAL), see the function table |
| tcsendbreak | a driver ioctl on the line, not an output-queue byte; K07 sets the break bit directly | not delayed by held output; console and USB declined |
| tcdrain, TCSADRAIN, TCSAFLUSH (drain part) | ttywait loops while the queue is non-empty and sleeps on the output channel at TTOPRI, 29, above PZERO, 25 (tty.c:216-235; sys/sys/tty.h; sys/sys/param.h:43) | on a held console the call sleeps until the stop is lifted or the console is thawed, and a signal interrupts it, as a drain behind a user's Ctrl-S already does; the thaw's ttstart lets the renderer drain and wake the sleeper; a console the user never switches back to keeps its drain asleep, which the owner of that process can interrupt; whether the sleep is signal-interruptible at this priority was not read |

### Step V02: the frozen store in PSRAM

docs/research/sd-component-placement.md assigns the homes: SRAM4 and SRAM5 are
owned by the USB transmit ring and the tile atlas, the USB dual-port tail has 432 B
left after the SD partial sector, kernel SRAM holds ordinary state, and
PIO-attached PSRAM "serves explicit transfers and loses contents with power", so
a console store, which is data and not code, can live there. V02 moves the
3,556 B slot to a reserved PSRAM region (a 4 KiB carve-out beside the 2 MiB swap
and temporary-file split of sys/arch/rp2040/dev/psram_xfer.h:26-34; the carve-out
is not designed here).

The exchange streams the slot in 64 B frames (PSRAM_CHUNK, psram_xfer.h:42):
read a frame, swap it with the live bytes, write it back, 56 frames for 3,556 B.
The driver's model puts a 64 B read at 144 clocks, 4.6 us, at 31.25 MHz
(PSRAM_CLKDIV 2, sys/arch/rp2040/dev/psram_pio.h:135;
docs/research/driver-datasheet-audit.md:169-170), with interrupts masked for
each frame (psram_xfer.c header comment). The write frame time is not stated and
is assumed equal (E), so 56 x (4.6 + 4.6) = 0.52 ms of bus time, 0.3% of the
repaint. RAM falls by 3,556 B, back to the free figures of section 1 less the
100 B tty.

Open choice: without a probed PSRAM the console 1 either does not exist (the RAM
saving holds) or keeps a bss slot (no saving). The plan recommends the first for
PICOCALC and leaves it to the owner.

First falsifiable check: the exchange through the PSRAM driver model leaves the
same live and saved states as the RAM exchange for the same inputs. Falsifier: one
frame lost or swapped. Host test: the PSRAM host model of check-psram-pio
(sys/arch/rp2040/doc/PICOCALC.md:134) under the exchange, with a mutant that drops
one frame's write-back and one that panics with the driver busy. Text +0.2-0.35 KB
(E); RAM -3,556 B.

### Step V03: N resident shadows, later

Costs and rules as the survey gave them, now stated against V01 and V02:

- N consoles, N at most 3 (6,016 and 5,952 B of free kernel RAM left after two
  extra 3,656 B shadows on PICOCALC and PICOCALC_SD). Console 0 stays
  /dev/console and the panic target.
- Minor map as in the survey: 0 console, 1 lcdgfx, 2 and above terminals; the
  static assertion on LCDGFX_MINOR stays.
- The visible screen draws; an invisible screen keeps updating its shadow and calls
  no panel primitive, the same rule lcdterm_gfx(1) applies to a pixel program.
  Unlike V01, background output is processed, not held, and no writer waits.
- Switch key and input routing as in V01, with Ctrl+F1 to F3. The switch calls
  lcdterm_repaint on the new store (the same 195-207 ms) and reprograms the
  scroll start from the visible console's ring offset.
- The rules for kernel output and panic below apply to all steps.

### Kernel output while another terminal is visible

How kernel output reaches the panel today (R): printf's putchar calls cnputc
for console output (sys/kern/subr_prf.c:56), which reaches lcdputc
(sys/arch/rp2040/dev/lcdcons.c:367). lcdputc always targets lcdttys[0]. After
panic() it masks interrupts with splhigh, abandons any interrupted drawer once
through lcdabandon (lcdcons.c:151, which ends the open panel transfer, resets
the parser and frees the drawing flag), and draws synchronously through
defer_put and lcd_acquire (lcdcons.c:374-390); lcdrender returns at once under
panicstr (lcdcons.c:323). The HardFault report path calls lcdabandon from
arm_fault before its first printf (sys/arch/rp2040/rp2040/fault.c:232) and then
prints through the same lcdputc with panicstr still NULL until the final
panic("kernel fault") (fault.c:290).

With terminals, an invisible terminal draws nothing, so console 0's panic text
would reach only its shadow while console 1 or 2 is visible. Rule: lcdabandon, the
single entry both the panic branch and the fault path pass through, also makes
console 0 the visible terminal. Under the splhigh it already holds, it aborts
the panel transfer (including a pixel program's, restoring the text
configuration as lcdterm_gfx(0) does), resets console 0's parser, sets the
active index to 0, programs the scroll start from console 0's ring and repaints
it from its shadow, before the caller's first byte. It runs once (the existing
lcd_abandoned flag), with interrupts as they are at that point, no sleep, no
allocation and no lock beyond the drawing flag, so it is safe in the fault
handler's priority. Cost: +60-120 B text (E), plus a repaint of 195-207 ms (derived from SPI wire
time, V01 above) with interrupts masked, accepted because the kernel does not
resume. The abandoned terminal's shadow is left as is.

Kernel printf outside a panic follows the existing single target: lcdputc
updates console 0's terminal, and with console 0 invisible the bytes land in
its shadow only and appear when the user switches back; the message log keeps
them regardless. No forced switch (a rate-limited warning cannot preempt a
user's screen); the choice is recorded here for owner review.

Host test (the two-console model of the virtual-terminal stage): write known
text to console 0, make console 2 visible, set panicstr, call lcdputc with a panic
line, and assert the model panel ends with console 0's text plus the line and
that the model's sleep and allocation stubs, which fail the test if called,
stay uncalled. The same test enters through lcdabandon with panicstr NULL
(the fault path), and a third case prints without a panic with console 2 visible and
asserts the panel is unchanged and console 0's shadow holds the byte. Mutant:
drop the force-to-console-0 step; the panic case must then fail with console 2's text
on the panel.

### Where the shadows live

docs/research/sd-component-placement.md is the placement map. Against it, a
3,392 B shadow has these candidate homes:

| Home | Status | Reason |
|---|---|---|
| SRAM4 and SRAM5 scratch banks | excluded | the USB transmit ring (4,096 B at 0x20040000) and the tile atlas (4,096 B at 0x20041000) own both banks; the map requires a demonstrated lifetime exclusion for any other user, and console output and graphics can overlap |
| USB dual-port SRAM tail | excluded | two unassigned intervals of 256 and 944 B existed at 345265caa and the SD partial sector took 512 B of the larger, leaving 432 B; one shadow is 3,392 B |
| Kernel SRAM (58 KiB region) | the default | 13,328 B free on PICOCALC and 13,264 B on PICOCALC_SD (M, section 1); all CPU-addressable and interrupt-safe |
| PIO-attached PSRAM | candidate for suspended shadows | PICOCALC and PICOCALC_SD configure the psram device; access is by explicit bounded transfers, so a repaint would stream a row (53 cells x 2 B = 106 B) at a time rather than read an array; contents die at power-off, which suits a shadow; absent or unprobed PSRAM keeps the shadow resident; the carve-out beside the 2 MiB swap and 6 MiB temporary split is not designed here |
| Swap device | candidate, reading 2 below | needs the new client described there |

The frozen console of V01 sits in kernel SRAM bss and moves to PSRAM in V02;
resident shadows (V03) stay in kernel SRAM, and the swap home serves only a
suspended terminal (V04).

### What "suspended to swap" can mean on this kernel

Three readings, with their status:

1. The process that owns the terminal is swapped out. This exists: swapout
   (sys/kern/vm_swap.c:799) writes a process image to the swap device through
   swap_with_buf_result (sys/kern/vm_swp.c:21) and swapin returns it. It is
   independent of the terminal and the shadow stays resident; no VT-specific
   code is needed.
2. The terminal's shadow is written to swap. This needs a new mechanism. The
   kernel's swap interface moves process images and has no kernel-object
   client; a shadow client would allocate btod(3,392) = 4 blocks (DEV_BSIZE is 1,024,
   sys/arch/rp2040/include/machparam.h:103, so 4,096 B of swap) for the 3,392 B
   with the swap map's allocation interface
   (sys/kern/subr_rmap.c:45 malloc, :82 mfree), move them with
   swap_with_buf_result using a kernel RAM address, and must survive
   swap_lost (declared at sys/kern/vm_swp.c:17 and tested at :26) by clearing the screen
   instead of panicking. It saves 3,392 B of RAM per suspended terminal and
   costs about 0.3-0.5 KB text (E) plus a repaint on resume. On the raw-flash
   swap device (fl1) every suspend would cost one 4 KB erase sector (the four blocks equal SWAP_IMAGE_ALIGN, machparam.h:116), and the
   flash exchange logic assumes sector-aligned images (vm_swap.c
   SWAP_EXCHANGE_SECTOR_BYTES, the swap_exchange_* group), so the mechanism is
   restricted to PSRAM and SD swap and refused on fl1. Whether the swapram
   tier fronts such a client is not examined.
3. The terminal's foreground job is stopped by job control (SIGTSTP, SIGTTOU
   on a background write). This exists in tty.c today.

The plan builds readings 1 and 3 as they are and treats reading 2 as the last
stage, conditional on a measured RAM shortage after the shadows exist.

## 6. getty, stty, ttys

Present (R): bin/stty/stty.c holds struct sgttyb, tchars, ltchars and winsize and drives
TIOCGETP, TIOCSETN, TIOCGETC, TIOCSETC, TIOCLGET, TIOCLSET, TIOCGLTC,
TIOCSLTC, TIOCGWINSZ and TIOCSWINSZ (lines 128-133, 204-215, 322-330).
libexec/getty/main.c declares struct sgttyb tmode (line 28) and calls TIOCGETP
(line 219); gettytab.c reads /etc/gettytab, 164 lines. etc/ttys lists console,
tty0 and ttyUSB0 with the xterm terminal type. stty ships inside sysbox
(26,131 B) and getty is 13,971 B (survey 9b). Dispositions: kept (same word,
same effect), mapped (same effect through a termios name; the old word stays
as an alias), dropped (no effect on this port, reported as not supported),
added (new word, with its prerequisite step from section 7).

### stty(1) keywords, one row per keyword

| Keyword | Today | Disposition | Termios name or new behavior |
|---|---|---|---|
| `even` | bin/stty/stty.c:62 (EVENP or ODDP bit) | dropped on console kernels | the bits have no reader in tty.c (search); parenb and parodd are declined, so the word reports "not supported"; planned with UART0 line control |
| `-even` | bin/stty/stty.c:63 (EVENP or ODDP bit) | dropped on console kernels | the bits have no reader in tty.c (search); parenb and parodd are declined, so the word reports "not supported"; planned with UART0 line control |
| `odd` | bin/stty/stty.c:64 (EVENP or ODDP bit) | dropped on console kernels | the bits have no reader in tty.c (search); parenb and parodd are declined, so the word reports "not supported"; planned with UART0 line control |
| `-odd` | bin/stty/stty.c:65 (EVENP or ODDP bit) | dropped on console kernels | the bits have no reader in tty.c (search); parenb and parodd are declined, so the word reports "not supported"; planned with UART0 line control |
| `raw` | bin/stty/stty.c:66 | mapped | -icanon -isig -opost (RAW) |
| `-raw` | bin/stty/stty.c:67 | mapped | icanon isig opost |
| `cooked` | bin/stty/stty.c:68 | mapped | as -raw |
| `-nl` | bin/stty/stty.c:69 | mapped | icrnl onlcr (CRMOD set; the BSD name is inverted) |
| `nl` | bin/stty/stty.c:70 | mapped | -icrnl -onlcr |
| `echo` | bin/stty/stty.c:71 | kept | echo |
| `-echo` | bin/stty/stty.c:72 | kept | -echo |
| `-tabs` | bin/stty/stty.c:73 | mapped | tab3 (XTABS) |
| `tabs` | bin/stty/stty.c:74 | mapped | tab0 |
| `tandem` | bin/stty/stty.c:75 | mapped | ixoff |
| `-tandem` | bin/stty/stty.c:76 | mapped | -ixoff |
| `cbreak` | bin/stty/stty.c:77 | mapped | -icanon isig |
| `-cbreak` | bin/stty/stty.c:78 | mapped | icanon |
| `rtscts` | bin/stty/stty.c:83 (local-mode word) | mapped | crtscts |
| `-rtscts` | bin/stty/stty.c:84 (local-mode word) | mapped | -crtscts |
| `crtbs` | bin/stty/stty.c:85 (local-mode word) | kept | crtbs (extension) |
| `-crtbs` | bin/stty/stty.c:86 (local-mode word) | kept | -crtbs |
| `prterase` | bin/stty/stty.c:87 (local-mode word) | mapped | echoprt |
| `-prterase` | bin/stty/stty.c:88 (local-mode word) | mapped | -echoprt |
| `crterase` | bin/stty/stty.c:89 (local-mode word) | mapped | echoe |
| `-crterase` | bin/stty/stty.c:90 (local-mode word) | mapped | -echoe |
| `crtkill` | bin/stty/stty.c:91 (local-mode word) | mapped | echok (approximate) |
| `-crtkill` | bin/stty/stty.c:92 (local-mode word) | mapped | -echok |
| `mdmbuf` | bin/stty/stty.c:93 (local-mode word) | kept | mdmbuf |
| `-mdmbuf` | bin/stty/stty.c:94 (local-mode word) | kept | -mdmbuf |
| `litout` | bin/stty/stty.c:95 (local-mode word) | mapped | -opost |
| `-litout` | bin/stty/stty.c:96 (local-mode word) | mapped | opost |
| `pass8` | bin/stty/stty.c:97 (local-mode word) | mapped | -istrip cs8 |
| `-pass8` | bin/stty/stty.c:98 (local-mode word) | mapped | istrip |
| `tostop` | bin/stty/stty.c:99 (local-mode word) | kept | tostop |
| `-tostop` | bin/stty/stty.c:100 (local-mode word) | kept | -tostop |
| `flusho` | bin/stty/stty.c:101 (local-mode word) | kept | flusho |
| `-flusho` | bin/stty/stty.c:102 (local-mode word) | kept | -flusho |
| `nohang` | bin/stty/stty.c:103 (local-mode word) | mapped | clocal |
| `-nohang` | bin/stty/stty.c:104 (local-mode word) | mapped | -clocal |
| `ctlecho` | bin/stty/stty.c:105 (local-mode word) | mapped | echoctl |
| `-ctlecho` | bin/stty/stty.c:106 (local-mode word) | mapped | -echoctl |
| `pendin` | bin/stty/stty.c:107 (local-mode word) | kept | pendin |
| `-pendin` | bin/stty/stty.c:108 (local-mode word) | kept | -pendin |
| `decctlq` | bin/stty/stty.c:109 (local-mode word) | mapped | -ixany |
| `-decctlq` | bin/stty/stty.c:110 (local-mode word) | mapped | ixany |
| `noflsh` | bin/stty/stty.c:111 (local-mode word) | kept | noflsh |
| `-noflsh` | bin/stty/stty.c:112 (local-mode word) | kept | -noflsh |
| `dcd` | bin/stty/stty.c:117-126 (TIOCMGET, TIOCMSET) | kept | modem-line ioctl, not a termios item; meaningful on UART0 only |
| `-dcd` | bin/stty/stty.c:117-126 | kept | as dcd |
| `dsr` | bin/stty/stty.c:117-126 (TIOCMGET, TIOCMSET) | kept | modem-line ioctl, not a termios item; meaningful on UART0 only |
| `-dsr` | bin/stty/stty.c:117-126 | kept | as dsr |
| `dtr` | bin/stty/stty.c:117-126 (TIOCMGET, TIOCMSET) | kept | modem-line ioctl, not a termios item; meaningful on UART0 only |
| `-dtr` | bin/stty/stty.c:117-126 | kept | as dtr |
| `cts` | bin/stty/stty.c:117-126 (TIOCMGET, TIOCMSET) | kept | modem-line ioctl, not a termios item; meaningful on UART0 only |
| `-cts` | bin/stty/stty.c:117-126 | kept | as cts |
| `rts` | bin/stty/stty.c:117-126 (TIOCMGET, TIOCMSET) | kept | modem-line ioctl, not a termios item; meaningful on UART0 only |
| `-rts` | bin/stty/stty.c:117-126 | kept | as rts |
| `erase` | bin/stty/stty.c:143-156 (special[]) | kept | verase |
| `kill` | bin/stty/stty.c:143-156 (special[]) | kept | vkill |
| `intr` | bin/stty/stty.c:143-156 (special[]) | kept | vintr |
| `quit` | bin/stty/stty.c:143-156 (special[]) | kept | vquit |
| `start` | bin/stty/stty.c:143-156 (special[]) | kept | vstart |
| `stop` | bin/stty/stty.c:143-156 (special[]) | kept | vstop |
| `eof` | bin/stty/stty.c:143-156 (special[]) | kept | veof |
| `brk` | bin/stty/stty.c:143-156 (special[]) | mapped | veol (mapped) |
| `susp` | bin/stty/stty.c:143-156 (special[]) | kept | vsusp |
| `dsusp` | bin/stty/stty.c:143-156 (special[]) | kept | vdsusp (extension) |
| `rprnt` | bin/stty/stty.c:143-156 (special[]) | kept | vreprint |
| `flush` | bin/stty/stty.c:143-156 (special[]) | kept | vdiscard |
| `werase` | bin/stty/stty.c:143-156 (special[]) | kept | vwerase |
| `lnext` | bin/stty/stty.c:143-156 (special[]) | kept | vlnext |
| `crt` | bin/stty/stty.c:231-235 | kept | echoe echok echoctl crtbs, -echoprt |
| `sane` | bin/stty/stty.c:236-240 (clears CBREAK; sets ECHO, CRMOD, XTABS) | kept, widened | icanon isig iexten echo icrnl onlcr opost tab3 ixon, after S03 |
| `dec` | bin/stty/stty.c:241-247 | kept | erase ^?, kill ^U, intr ^C, then crt |
| `flushout` | bin/stty/stty.c:262-265 (TIOCFLUSH) | mapped | tcflush TCIOFLUSH |
| `hup` | bin/stty/stty.c:266-269 (TIOCHPCL) | kept | hupcl, set only until the get and clear request lands |
| `rows N` | bin/stty/stty.c:270-274 | mapped | tcsetwinsize |
| `cols N, columns N` | bin/stty/stty.c:275-279 | mapped | tcsetwinsize |
| `size` | bin/stty/stty.c:280-284 | mapped | tcgetwinsize |
| `speed` | bin/stty/stty.c:289-298 | kept | cfgetospeed lookup |
| `everything, all, -a, -e` | bin/stty/stty.c:180-186, 223-227 | kept | prints the termios state, then the sgtty extras |
| `-f FILE` | bin/stty/stty.c:187-192 | kept | as today |
| `0` | bin/stty/stty.c:23 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr (B0 hangs up; effect unexamined) |
| `50` | bin/stty/stty.c:24 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `75` | bin/stty/stty.c:25 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `150` | bin/stty/stty.c:26 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `200` | bin/stty/stty.c:27 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `300` | bin/stty/stty.c:28 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `600` | bin/stty/stty.c:29 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `1200` | bin/stty/stty.c:30 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `1800` | bin/stty/stty.c:31 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `2400` | bin/stty/stty.c:32 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `4800` | bin/stty/stty.c:33 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `9600` | bin/stty/stty.c:34 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `19200` | bin/stty/stty.c:35 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `38400` | bin/stty/stty.c:36 (speeds[]) | kept | cfsetispeed and cfsetospeed, then tcsetattr |
| `57600` | bin/stty/stty.c:37 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `115200` | bin/stty/stty.c:38 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `230400` | bin/stty/stty.c:39 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `460800` | bin/stty/stty.c:40 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `500000` | bin/stty/stty.c:41 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `576000` | bin/stty/stty.c:42 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `921600` | bin/stty/stty.c:43 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `1000000` | bin/stty/stty.c:44 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `1152000` | bin/stty/stty.c:45 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `1500000` | bin/stty/stty.c:46 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `2000000` | bin/stty/stty.c:47 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `2500000` | bin/stty/stty.c:48 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `3000000` | bin/stty/stty.c:49 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `3500000` | bin/stty/stty.c:50 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `4000000` | bin/stty/stty.c:51 (speeds[]) | kept | extension beyond B38400; cfsetispeed and cfsetospeed, then tcsetattr |
| `min N` | absent | added | vmin and vtime; needs K01 and K02 |
| `time N` | absent | added | vmin and vtime; needs K01 and K02 |
| `icanon, -icanon` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `isig, -isig` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `iexten, -iexten` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `opost, -opost` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `icrnl, -icrnl` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `onlcr, -onlcr` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `ixoff, -ixoff` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `ixany, -ixany` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `istrip, -istrip` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `clocal, -clocal` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `echoe, -echoe` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `echok, -echok` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `noflsh, -noflsh` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `tostop, -tostop` | absent (reachable only through the BSD names above) | added | the termios name for a row already in section 2; needs S03 |
| `iutf8, -iutf8` | absent | added | needs K05 |
| `hupcl, -hupcl` | absent | added | -hupcl needs K06 |

### getty and gettytab, one row per capability read

libexec/getty/init.c lists the capabilities getty reads (strings at lines 19-42, numbers at
47-54, flags at 59-79); each row cites the line that applies it.

| Capability | Kind | Applied at | Disposition | Note |
|---|---|---|---|---|
| `nx` | string | libexec/getty/main.c:286-287 | kept | next table on timeout or break |
| `cl` | string | libexec/getty/main.c:251-252 | kept | screen clear; xterm sequence works on the console |
| `im` | string | libexec/getty/main.c:254-255 | kept | initial message |
| `lm` | string | libexec/getty/main.c:458 | kept | login prompt |
| `er` | string | libexec/getty/subr.c:96-119 (setchars) | mapped | verase |
| `kl` | string | libexec/getty/subr.c:96-119 | mapped | vkill |
| `et` | string | libexec/getty/subr.c:96-119 | mapped | veof |
| `pc` | string | libexec/getty/main.c:423 | kept | pad character for putpad |
| `tt` | string | libexec/getty/subr.c:287-288 | kept | TERM |
| `ev` | string | libexec/getty/subr.c:291 | kept | environment |
| `lo` | string | libexec/getty/main.c:280 | kept | login program |
| `hn` | string | libexec/getty/subr.c:194 | kept | host name |
| `he` | string | libexec/getty/main.c:253 | kept | host name edit |
| `in` | string | libexec/getty/subr.c:96-119 | mapped | vintr |
| `qu` | string | libexec/getty/subr.c:96-119 | mapped | vquit |
| `xn` | string | libexec/getty/subr.c:96-119 | mapped | vstart |
| `xf` | string | libexec/getty/subr.c:96-119 | mapped | vstop |
| `bk` | string | libexec/getty/subr.c:96-119 | mapped | veol |
| `su` | string | libexec/getty/subr.c:96-119 | mapped | vsusp |
| `ds` | string | libexec/getty/subr.c:96-119 | mapped | vdsusp |
| `rp` | string | libexec/getty/subr.c:96-119 | mapped | vreprint |
| `fl` | string | libexec/getty/subr.c:96-119 | mapped | vdiscard |
| `we` | string | libexec/getty/subr.c:96-119 | mapped | vwerase |
| `ln` | string | libexec/getty/subr.c:96-119 | mapped | vlnext |
| `is` | number | libexec/getty/main.c:227-228 | kept | input speed |
| `os` | number | libexec/getty/main.c:231-232 | kept | output speed |
| `sp` | number | libexec/getty/main.c:229-233 | kept | both speeds |
| `to` | number | libexec/getty/main.c:261-263 | kept | login timeout |
| `f0` | number | libexec/getty/subr.c:129-135 | kept | raw sgtty flag word for output; stays, since sgtty remains the kernel interface |
| `f1` | number | libexec/getty/subr.c:136-139 | kept | raw flag word for input |
| `f2` | number | libexec/getty/subr.c:140-143 | kept | raw flag word for the user mode |
| `pf` | number | libexec/getty/main.c:317-320 | kept | flush delay at the first prompt |
| `ht` | flag | libexec/getty/subr.c:166 | mapped | -tab3 when false |
| `nl` | flag | libexec/getty/subr.c:155 | mapped | icrnl onlcr |
| `ep` | flag | libexec/getty/subr.c:151 | dropped on console kernels | EVENP stored, no effect |
| `op` | flag | libexec/getty/subr.c:149 | dropped on console kernels | ODDP stored, no effect |
| `ap` | flag | libexec/getty/subr.c:147 | dropped on console kernels | ANYP stored, no effect |
| `ec` | flag | libexec/getty/subr.c:178 | mapped | echo |
| `co` | flag | libexec/getty/main.c:459 | kept | console special case |
| `cb` | flag | libexec/getty/subr.c:170 | kept | crtbs |
| `ck` | flag | libexec/getty/subr.c:174 | mapped | echok |
| `ce` | flag | libexec/getty/subr.c:172 | mapped | echoe |
| `pe` | flag | libexec/getty/subr.c:176 | mapped | echoprt |
| `rw` | flag | libexec/getty/subr.c:159 | mapped | -icanon -isig versus cbreak on the read mode |
| `xc` | flag | libexec/getty/subr.c:180 | mapped | echoctl |
| `ig` | flag | libexec/getty/main.c:366 | kept | ignore garbage characters in the login name |
| `ps` | flag | libexec/getty/main.c:247 | kept | port selector |
| `hc` | flag | libexec/getty/main.c:239-240 | mapped | hupcl, set only (TIOCHPCL) |
| `ub` | flag | libexec/getty/main.c:439 | kept | unbuffered output |
| `ab` | flag | libexec/getty/main.c:241 | kept | autobaud (UART0 only in effect) |
| `dx` | flag | libexec/getty/subr.c:182 | mapped | -ixany |
| `hf` | flag | libexec/getty/subr.c:153 | mapped | crtscts |
| `np` | flag | libexec/getty/subr.c:184 | mapped | -istrip cs8 |
| `u8` | flag | new | added | sets iutf8 for UTF-8 capable terminals; needs K05; the name is proposed here, not a donor capability |

### ttys, login, resize

- ttys: virtual terminals appear as additional lines, off by default (ttyv1 is
  the proposed node name, after FreeBSD vt(4); a recommendation, not a measured
  fact). The present lines (console, tty0, ttyUSB0, all xterm) stay.
- login: login/tty_modes.c reads the TIOC* calls directly (survey, R) and moves
  to the shim in step U03.
- resize(1): the FIONREAD poll gives way to VMIN 0 and VTIME 1 after K02
  (survey).

## 7. Staged order

Each step is one reviewable change, about 100-300 lines or one mechanism. The
size column is changed lines (E). Every step names its first falsifier (the
observation that refutes its prediction) and a host test that runs without the
board; board runs stay behind the hardware opt-in. Row references in the
falsifier column point at section 2 and section 6 rows.

### Termios and userland

| Step | Mechanism | Needs | Lines | Bytes (E) | First falsifier | Host test |
|---|---|---|---|---|---|---|
| S01 | termios.h, cfgetispeed, cfgetospeed, cfsetispeed, cfsetospeed, cfsetspeed, cfmakeraw | none | 120 | 0 kernel; 0.1 KB per linking program | a name or value differs from the chapter 11 tables, or two bits of one register overlap | compile-time distinctness assertions; accessor round trip |
| S02 | tcgetattr over TIOCGETP, TIOCGETC, TIOCGLTC, TIOCLGET | S01 | 150 | 0.3-0.5 KB per program | an sgtty state in a "now" row maps to a different termios value than the row says | sgtty-model states to termios, every row of section 2 |
| S03 | tcsetattr TCSANOW with the refusal rules (EINVAL only when no part is honored) | S01, S02 | 200 | 0.4-0.6 KB per program | a "now" row fails RT, or a declined request returns success | RT and RF cases for every row |
| S04 | tcsetattr TCSADRAIN and TCSAFLUSH, tcflush (3 selectors), tcflow (TCOOFF and TCOON; TCIOFF and TCION return EINVAL) | S03 (TCSADRAIN also K04) | 120 | 0.15-0.25 KB per program | a selector flushes the wrong queue, TCSAFLUSH keeps queued input, or TCIOFF reports success | model with input and output queues; each selector and action; EINVAL for the two input-side actions |
| S05 | tcgetpgrp, tcsetpgrp, tcgetwinsize, tcsetwinsize | S01 | 60 | 0.1 KB per program | SIGWINCH on an unchanged size | model of TIOCSWINSZ |
| K01 | VMIN and VTIME in the two pad bytes of struct tty, with the get and set request | none | 60 | +0.10-0.15 KB text, 0 RAM | struct tty grows past 100 B | tests/kernel/tty_layout_contract.c updated by design; request round trip |
| K02 | ttread honors VMIN and VTIME (the four Issue 8 cases, with a callout) | K01 | 90 | +0.30-0.40 KB | tty.o grows more than 0.45 KB over K01, or queued bytes are lost | four cases on a model clock; a queue-preservation case |
| K04 | TIOCDRAIN over ttywait (tty.c:216) | none | 15 | +0.04-0.08 KB | the call returns with a non-empty queue, or blocks forever on a hidden console after its thaw | model with a transmit-complete flag; drain on a hidden console sleeps, wakes at the thaw and is interrupted by a signal |
| K05 | IUTF8 bit and UTF-8 erase through unputc | none (the shim row needs S03) | 40 | +0.15-0.25 KB | an erase after U+00E9 leaves one byte queued | KM: erase with and without the bit |
| K06 | HUPCL get and clear in the K01 request | K01 | 15 | +0.02-0.04 KB | TS_HUPCLS is not cleared by the request | KM on t_state |
| K07 | UART0 break (TIOCSBRK, TIOCCBRK case and the line-control bit) | none | 40 | +0.06-0.10 KB; not on PICOCALC | the break lasts outside 0.25-0.5 s for duration 0 | UART model |
| U01 | stty on the shim for the kept and mapped words of section 6 | S02, S03 | 250 | stty +0.3-0.5 KB in sysbox | stty prints a different state than today's stty for the same sgtty state | golden output of the old and new stty on each state |
| U02 | stty added words (min, time, icanon and the other names, iutf8, hupcl) | U01 and the kernel step each word names | 100 | +0.1-0.2 KB | an added word changes a bit the section 2 row does not name | option table against the termios model |
| U03 | getty and login read and apply the line through the shim | S03 | 150 | +0.2-0.4 KB each | a gettytab entry gives a different line state than today's | parse of every capability row into the state it gives today |
| U04 | gettytab u8 capability | K05, U03 | 20 | +0.05 KB | u8 absent sets iutf8 | getty parse |

### Console emulator

| Step | Mechanism | Needs | Lines | Bytes (E) | First falsifier | Host test |
|---|---|---|---|---|---|---|
| C01 | UTF-8 decoder and the coverage tables of section 4 | none | 150 | +0.35-0.55 KB; 3 B per terminal | a code point draws other than one covered cell or '?', or a stray continuation byte reaches the bank | enumeration of all code points and prefixes; mutants on each reject rule |
| C02 | Latin-1 glyph ROM loaded into bank slots 0xa0-0xff | C01 | 120 (data) | +0.67 KB flash; PICOCALC_SD and PICO only | a loaded slot draws '?' | bank-load test |
| C03 | 38 and 48 sub-parameters (; and :), nearest-palette reduction | none | 120 | +0.25-0.40 KB | known-bad: ESC [38;5;196m sets bold on today's lcdterm.c (I); the test fails there before and passes after | properties of section 4 (identity below 16, total, primaries) with mutants |
| C04 | OSC, DCS, APC, PM and SOS consumed to the terminator with a length bound | none | 50 | +0.06-0.10 KB | an OSC payload prints on today's lcdterm.c (I) | model with an OSC title, then text |
| C05 | CSI E, F, S, T, s, u, I, Z, b | none | 120 | +0.10-0.18 KB | a sequence moves the cursor differently from its definition | per-sequence table through recording primitives |
| C06 | tab stops: ESC H, CSI g and the HT stop test, one bit per column | C05 | 60 | +0.05-0.10 KB; 7 B per terminal | HT ignores a set stop | stop set and clear sequences |

### Virtual consoles

| Step | Mechanism | Needs | Lines | Bytes (E unless M) | First falsifier | Host test |
|---|---|---|---|---|---|---|
| V00 | gather lcdterm.c's static state into one struct | none | 200 | +0.3-0.5 KB | any difference in the model-panel log | check-picocalc-host logs identical |
| V01 | one frozen second console, in-place exchange, switch key, writer wait, panic restore | V00 | 250 | +0.3-0.5 KB; +3,656 B RAM (3,556 slot, 100 tty; M basis) | A to B to A changes console 0's stores, a frozen console draws, or a panic with console 1 shown leaves its text | section 5 test and four mutants |
| V02 | the frozen store in PSRAM, 64 B frame exchange | V01 | 150 | +0.2-0.35 KB; -3,556 B RAM | the PSRAM exchange differs from the RAM exchange, or a panic with the driver busy hangs | PSRAM host model; frame-drop and driver-busy mutants |
| V03 | N resident shadows, live output on every console | V00 (V01 for the key and the panic rule) | 250 | +0.4-0.6 KB more; 3,656 B per console | an invisible console calls a draw primitive | N-console model: repaint equals store |
| V04 | shadow swap-out (reading 2 of section 5) | V03 | 200 | +0.3-0.5 KB; -3,392 B per suspended console | the swap read differs from the write, or swap loss panics | swap model with loss injection; refusal on fl1 |

### Which steps are independent

- Independent of every other step: S01, K01, K04, K05, K07, C01, C03, C04, C05, V00.
- Termios chain: S01 then S02 then S03 then S04; S05 needs only S01. The kernel
  steps K01 to K07 do not wait for the shim: the kernel changes are testable by
  KM tests alone, and the shim rows that name them (VMIN, VTIME, IUTF8, HUPCL,
  drain) turn from "report-only" to "now" when their step lands.
- U01 to U04 follow the shim: U01 needs S02 and S03, U03 needs S03, U02 and U04
  wait on the kernel step each word or capability names.
- Console chain: C02 needs C01; C06 needs C05. Console steps do not depend on the
  termios or userland steps, except K05 wanting C01 for the decoder it erases.
- Virtual consoles: V01 needs V00, V02 needs V01, V03 needs V00, V04 needs V03.
  V01 does not need V03; V03 does not wait for V02.

PICOCALC admits the termios and console steps (1.42-2.25 KB, E, against its
2,424 B above the margin: 0.1-0.95 KB spare) and takes V00 and V01 (+0.6-1.0 KB)
only if the measured growth of the preceding steps leaves room; otherwise V00
and V01 land on PICOCALC_SD and PICO kernels first.

Hazards that carry over from the survey: until K01 and K02 land, the shim reports
VMIN 1 and VTIME 0; TIOCSETN is the only no-flush set request; the new struct tty
bytes change tests/kernel/tty_layout_contract.c by design.

## 8. Licenses

| Donor | Header evidence | Use |
|---|---|---|
| FreeBSD teken (teken.c, teken_subr.h, sequences) | BSD-2-Clause, Schouten 2008-2009 (teken.c:2-4 [V]) | test data and behavior; no import planned |
| FreeBSD tty_ttydisc.c, tty.c | BSD-2-Clause SPDX, Schouten 2008 (header [V]) | importable with the header kept whole; consult for the VMIN and IUTF8 logic |
| FreeBSD dev/vt (vt_core.c) | BSD-2-Clause, FreeBSD Foundation 2009, 2013 (header [V]) | behavior reference for the switch protocol |
| OpenBSD kern/tty.c | three-clause, Berkeley (conditions 1-3 at lines 16-21 [V]) | importable; read path source for VMIN and VTIME |
| NetBSD kern/tty.c | NetBSD Foundation two-clause plus a Berkeley three-clause block (header [V]) | importable; fills the survey's unread-header gap |
| DragonFly kern/tty.c | three-clause, Berkeley (conditions 1-3 at lines 15-20 [V]) | importable; fills the survey's unread-header gap |
| NetBSD wsemul_vt100.c | two-clause BSD, Drochner 1998 (header [V]) | consult only; no import planned |
| NetBSD wsdisplay_vcons.c | BSD, Lorenz 2005, 2006 (conditions 1 and 2 listed; header partly read) | consult only |
| OpenBSD wsemul_vt100.c | ISC (Vallat 2007, 2013) plus 1998 BSD (header [V]) | consult only |
| DragonFly scterm-sc.c | two-clause BSD; condition 1 requires the copyright lines as the first lines of the file unmodified (header [V]) | consult only; an import carries the header verbatim |
| NuttX drivers/serial/serial.c | Apache-2.0 (SPDX line [V]) | consult only; Apache-2.0 patent terms differ from the port's BSD and ISC grants |
| Linux n_tty.c | header not read | consult only |
| Mach osfmk chario.c | not read | consult only |
| POSIX.1-2024 | standard text | the interface specification, quoted for names and rules only |

Licenses inherited from the survey (Reno, Net/2, SunOS, Schillix) are unchanged.
New code written for this port carries the GizmoBSD header the repository
requires.

## 9. Citation record

Opened at the cited line for this note (all matched the earlier source readings):
sys/sys/ttyqueue.h:57 and :68; sys/kern/tty_ttydisc.c:309 and :888;
sys/teken/teken.h:112-122; teken_subr.h:1195-1220; teken.c:2, :678-689;
OpenBSD sys/kern/tty.c:1524-1532 and :1594; NuttX serial.h:317-325 and
drivers/serial/Kconfig:337-345, :798-808; Linux include/linux/tty.h:299 and
drivers/tty/n_tty.c:1601; Mach chario.c:306-307; NetBSD
wsdisplay_vcons.c:664-690, wsdisplay_vconsvar.h:44-60, wsemul_vt100_subr.c:566;
FreeBSD vt_core.c:3054; and the license headers listed in section 8.
POSIX chapter 11 lines read from the Issue 8 text extract: 11.2.1 at 14883,
c_lflag at 15286-15318, TOSTOP at 14555, the tcgetwinsize prototype at 35268.
Also read for the frozen-console step: sys/arch/rp2040/dev/psram_xfer.h:26-42, psram_pio.h:135,
docs/research/driver-datasheet-audit.md:169-170, docs/research/picocalc-console-review.md:140-144 and :358, and the lcdterm.c static declarations at lines 66-91. No citation was found wrong. Line counts, struct names and function names in the
donor tables that carry no mark were not reopened.

## 10. Not verified

- Every (E) figure, including the 1.42-2.25 KB subtotal and the per-step text
  growth; none of the planned code exists.
- The 49% agreement figure is a comparison of two tables, not a visual judgment
  on the panel.
- The OSC and extended-SGR misbehavior of the present parser is from reading
  lcdterm.c, not from running it.
- Padding bytes 98-99 of struct tty: the offsets are asserted, the padding is
  arithmetic.
- Whether a PSRAM write frame takes as long as the 4.6 us read frame the driver
  model states, the PSRAM carve-out for the frozen store, and whether the ttwrite
  sleep on a frozen console is signal-interruptible;
- Signal interruptibility of the ttywait sleep (TTOPRI above PZERO), and that bit 0x008000L is free of other users in the tree;
- The 195-207 ms repaint is derived from SPI wire time, not captured on a board;
- The IXANY inverse of DECCTQ and the "no reader" of EVENP and ODDP rest on
  searches of the C sources;
- The Ctrl+F1 to F3 combination on hardware; the swapram tier's treatment of a
  kernel-object swap client.
- Donor files beyond the lines listed in section 9: tty_inq.c, tty_outq.c,
  tty_subr.c of the BSDs, rubchar bodies, wsdisplay_vcons.c switch code, the
  NuttX serial.c termios path, Mach tty server design (none found), seL4 (no
  tree found), L4 (no tty or termios found).
- The port's stty.1 and getty.8 were not compared option by option with the
  donors'.
- POSIX text beyond 11.2.1-11.2.6, the tcsetattr, tcgetwinsize and tcgetsid
  pages.
- PICOCALC_SD RAM and flash figures describe the kernel of this revision before
  any stage lands; HEAD moves.
