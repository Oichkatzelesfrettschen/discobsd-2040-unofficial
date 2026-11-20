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
retired local-mode bit (+0.15-0.25 KB) and a drain request (+0.04-0.08 KB).
Extend the existing lcdterm.c console (4,335 B text, 164 B of parser state)
instead of importing teken (10,675 B text, 132 B of state, M): add 256-color and
24-bit SGR reduced to the 16-entry palette at parse time, OSC consumption, a
UTF-8 decoder whose output is always one of ASCII, a mapped bank code or '?',
and a cell store that stays at 2 B per cell. Add virtual terminals last, as
additional cell shadows of 3,656 B each, resident on PICOCALC_SD first. The
numbers that decide it: PICOCALC has 3,448 B of flash headroom, of which
2,424 B lie above the 1,024 B margin the gate enforces, against an estimated
1.34-2.13 KB for the termios and console work and 0.9-1.4 KB more for virtual
terminals (the two together can exceed PICOCALC and fit PICOCALC_SD, which has
34,680 B); kernel RAM left past the last section is 13,328 B on PICOCALC, so
two extra shadows (7,312 B) fit and a 4 B-per-cell truecolor store (6,784 B per
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
sgtty), planned (stage named in section 7), declined (reason given). tcsetattr
sets the attributes the implementation supports, leaves the others unchanged,
and returns -1 with EINVAL only when no part of the request can be honored
[V: tcsetattr page, "If no part of the request can be honored"]; chapter 11.2.4
lets an implementation ignore modes that do not apply to a non-serial terminal
[V: 11.2.4 text]. A declined bit is therefore cleared on read-back, and the
shim returns EINVAL for a request that asks for no supported change.

### c_iflag (11.2.2)

| Flag | Status | Mechanism or reason |
|---|---|---|
| ICRNL | required; now | CRMOD couples input CR-to-NL with output NL-to-CRNL (sys/kern/tty.c:872, 1310); the shim sets both from ICRNL and ONLCR together and refuses a request that splits them (EINVAL part-honored) until a kernel bit is split (planned, only if a program needs it) |
| IXON, IXOFF | required; now | stop and start characters compared against the input in ttyinput (tty.c:1113-1123); TANDEM sends stop and start for input (tty.c:250, 317), so TANDEM is IXOFF; IXON has no switch (always on outside RAW) |
| IXANY | required; declined | DECCTQ is the opposite sense (only start restarts); no program on the port asks for it; cost is a branch in the output-restart path |
| ISTRIP | required; now | PASS8 clear strips to 7 bits (tty.c:1053) |
| BRKINT, IGNBRK | required; declined on console and USB; planned on UART0 | the panel keyboard and CDC-ACM raise no break; UART0 break detect needs the UART error path |
| IGNPAR, INPCK, PARMRK | required; declined | no console device carries parity (I); the EVENP and ODDP bits of t_flags stay the only parity surface |
| IGNCR, INLCR | required; declined | no caller; two input-path branches saved |
| IUTF8 | extension (no hit in the Issue 8 text extract); planned | takes the retired t_flags bit 0x80000 (sys/sys/ioctl.h comment "old TILDE"); makes ttyrub and the erase path step over UTF-8 continuation bytes |

### c_oflag (11.2.3)

| Flag | Status | Mechanism or reason |
|---|---|---|
| OPOST | required; now | RAW and LITOUT disable output processing (tty.c:1266) |
| ONLCR | required; now | CRMOD, see ICRNL |
| OCRNL, ONOCR, ONLRET, OFILL, OFDEL | required; declined | no caller; the delay fields below are the same class |
| NLDLY, CRDLY, TABDLY, BSDLY, VTDLY, FFDLY | required (XSI); declined | the panel and CDC-ACM need no fill; XTABS covers TAB3 (tty.c:1289); the shim maps TABDLY=TAB3 to XTABS and ignores the others |

### c_cflag (11.2.4)

| Flag | Status | Mechanism or reason |
|---|---|---|
| CS8, CSIZE | required; now (CS8 only, I) | the console devices carry 8-bit bytes; CS5-CS7 declined |
| CLOCAL, CREAD, HUPCL | required; now (I) | CLOCAL maps to NOHANG and HUPCL to TIOCHPCL; CREAD is always set |
| CSTOPB, PARENB, PARODD | required; declined on the console, planned on UART0 | UART0 line control exists only as a driver constant; the stty option set shrinks to speeds on the console |
| Baud B0-B38400 and cfset/cfget speed | required; now | t_ispeed and t_ospeed hold B* indices below NSPEEDS (sys/sys/tty.h); the console ignores them |

### c_lflag (11.2.5)

| Flag | Status | Mechanism or reason |
|---|---|---|
| ECHO | required; now | ECHO |
| ECHOE, ECHOK | required; now (I) | CRTERA and CRTKIL, with CRTBS for the backspace form |
| ECHONL | required; declined | the console always echoes NL under ECHO; separate bit needs one branch in ttyinput |
| ICANON | required; now | the inverse of CBREAK and RAW together |
| ISIG | required; now | RAW and CBREAK differ in it: CBREAK keeps signals, RAW drops them; the shim maps (ICANON off, ISIG on) to CBREAK, (off, off) to RAW and refuses (ICANON on, ISIG off) |
| IEXTEN | required; now | LNEXT, WERASE, REPRINT are processed unless RAW; the shim reports IEXTEN set whenever ICANON is set |
| NOFLSH, TOSTOP | required; now | NOFLSH, TOSTOP (sys/sys/ioctl.h) |

### c_cc (11.2.6)

| Index | Status | Mechanism or reason |
|---|---|---|
| VEOF, VERASE, VINTR, VKILL, VQUIT, VSUSP, VSTART, VSTOP, VEOL | required; now | struct ttychars; VEOL is tc_brkc; _POSIX_VDISABLE is defined (sys/sys/ttychars.h:33) |
| VMIN, VTIME | required; planned | stored in the two padding bytes of struct tty, so 0 RAM; an uncommon request on a RAW-less read changes ttread (stage names in section 7) |
| VWERASE, VREPRINT, VLNEXT, VDISCARD | extensions; now | tc_werasc, tc_rprntc, tc_lnextc, tc_flushc in struct ttychars |
| VEOL2, VSTATUS | extensions; declined | no field in struct ttychars |

### Functions

| Function | Status | Mechanism |
|---|---|---|
| tcgetattr, tcsetattr TCSANOW | planned (shim) | TIOCGETP, TIOCGETC, TIOCGLTC, TIOCLGET and TIOCSETN (no flush on a RAW transition, which the survey's hazard note requires) |
| tcsetattr TCSADRAIN, TCSAFLUSH | planned | drain request then TIOCSETN; TCSAFLUSH is TIOCSETP (drain and flush, tty.c:551-554 per the survey) |
| tcflush | now (shim) | TIOCFLUSH with FREAD or FWRITE (tty.c:490) |
| tcflow TCOOFF, TCOON | now (shim) | TIOCSTOP, TIOCSTART (tty.c:510, 519) |
| tcflow TCIOFF, TCION | planned | write of VSTOP or VSTART through the tty |
| tcdrain | planned | a TIOCDRAIN case calling ttywait (tty.c:216) |
| tcsendbreak | declined on console and USB; planned on UART0 | TIOCSBRK and TIOCCBRK are defined (sys/sys/ioctl.h:186-187) and no case in tty.c handles them; the UART driver would set the line-control break bit |
| tcgetpgrp, tcsetpgrp | now (shim) | TIOCGPGRP, TIOCSPGRP (tty.c:641, 658) |
| tcgetsid | declined | the port has no sessions (rg finds no p_session, setsid, TIOCSCTTY in sys/); +0.5-0.9 KB (E, survey) for one login does not pay |
| tcgetwinsize, tcsetwinsize | planned (shim) | TIOCGWINSZ, TIOCSWINSZ; SIGWINCH goes to the foreground group only when the size changes (tty.c:662-667), which is the Issue 8 rule [V: tcgetwinsize page] |
| O_TTY_INIT | declined | needs an open-flag bit and a mode reset in open; getty and login set modes after open |
| cfmakeraw, cfsetspeed (BSD extras) | planned (libc) | pure user code over the state above |

Native TIOCGETA, TIOCSETA, TIOCSETAW and TIOCSETAF stay declined until a
program needs a state the shim cannot express (CS5-CS7, IGNCR, ICRNL without
ONLCR, ISIG without ICANON). The estimated cost is +2.5-3.5 KB text and +24 B
per tty (E, survey); on PICOCALC that exceeds the 2,424 B above the margin.

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

| Item | Text | RAM | PICOCALC (2,424 above margin) | PICOCALC_SD (34,680) |
|---|---|---|---|---|
| libc termios shim (per program that links it) | 0 kernel; 0.8-1.2 KB per program (818 B prototype, survey M) | 0 | fits | fits |
| VMIN, VTIME, non-flushing set | 0.40-0.55 KB | 0 | fits | fits |
| IUTF8 erase | 0.15-0.25 KB | 0 | fits | fits |
| TIOCDRAIN | 0.04-0.08 KB | 0 | fits | fits |
| lcdterm: extended SGR | 0.25-0.40 KB | 0 per cell | fits | fits |
| lcdterm: UTF-8 decoder and ACS inverse map | 0.35-0.55 KB | 3 B per terminal | fits | fits |
| lcdterm: OSC consumption, CSI additions, tab stops | 0.15-0.30 KB | 7-8 B per terminal | fits | fits |
| tcsendbreak on UART0 (stage-table row, not in the subtotal) | 0.06-0.10 KB | 0 | not planned for PICOCALC | fits |
| Subtotal without virtual terminals | 1.34-2.13 KB | | 0.3-1.1 KB spare | fits |
| Latin-1 glyph ROM (96 glyphs, 7 B packed) | 0.67 KB | 0 (loads into the 896 B bank) | declined on PICOCALC | fits |
| Virtual terminals (survey) | 0.9-1.4 KB | 3,656 B per extra terminal (M) | 2.24-3.53 KB total: no fit at the upper bound | fits |

PICOCALC admits the subtotal and takes virtual terminals only after the
measured growth of the preceding stages is known; the stage order in section 7
makes that measurement the gate.

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

Costs per extra terminal (cells, attributes and struct tty measured in section 1;
the 164 B parser figure is the survey's measurement): 1,696 B cells + 1,696 B attributes + 164 B parser, cursor and SGR
+ 100 B struct tty = 3,656 B. The NetBSD vcons model keeps a character array and
an attribute array per screen (scr_chars, scr_attrs in
sys/dev/wscons/wsdisplay_vconsvar.h:44-60 [V]); the port already has the
same split at 2 B per cell. vcons_redraw_screen (sys/dev/wscons/wsdisplay_vcons.c:664
[V]) walks the shadow and draws each cell; the port's equivalent is
lcdterm_repaint (survey, 142 ms for 1,696 cells at 84 us per cell, E). The
visible screen draws; an invisible screen updates its shadow and calls no panel
primitive, the same rule lcdterm_gfx(1) applies to a pixel program.

Design, in the order the data moves:

- N consoles, N at most 3 (13,328 and 13,264 B of free kernel RAM leave 6,016 and
  5,952 B after two extra shadows). Console 0 stays /dev/console and the
  panic target.
- Minor map as in the survey: 0 console, 1 lcdgfx, 2 and above terminals; the
  static assertion on LCDGFX_MINOR stays.
- Switch key: Ctrl with F1-F3, consumed in the keyboard path before
  translation (picokbd_map.c: F1-F9 are 0x81-0x89; Ctrl is 0xa5, R). Hardware
  availability of the combination is unverified; the stage's board check names
  the ten key codes. The switch is refused while lcdgfx is open.
- Input follows the active terminal: picokbd_evq_check and the keyboard
  session read lcdttys[0] today (lcdcons.c:546, 627, 647); they take the active
  index.
- Switch: mark the old shadow invisible, make the new one visible, call
  lcdterm_repaint on the new store; drawing already in flight finishes before
  the flag changes (the drawing flag of lcdcons.c).
- Output to an invisible terminal updates its shadow only; DEC cursor state and
  the scroll ring belong to the terminal, so lcd_set_top is programmed from the
  visible terminal on every switch.

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

Resident shadows in kernel SRAM are the first stage; the PSRAM and swap homes
serve only a suspended terminal.

### What "suspended to swap" can mean on this kernel

Three readings, with their status:

1. The process that owns the terminal is swapped out. This exists: swapout
   (sys/kern/vm_swap.c:799) writes a process image to the swap device through
   swap_with_buf_result (sys/kern/vm_swp.c:21) and swapin returns it. It is
   independent of the terminal and the shadow stays resident; no VT-specific
   code is needed.
2. The terminal's shadow is written to swap. This needs a new mechanism. The
   kernel's swap interface moves process images and has no kernel-object
   client; a shadow client would allocate 7 blocks of 512 B for the 3,392 B
   (E, DEV_BSIZE 512 assumed) with the swap map's allocation interface
   (sys/kern/subr_rmap.c:45 malloc, :82 mfree), move them with
   swap_with_buf_result using a kernel RAM address, and must survive
   swap_lost (declared at sys/kern/vm_swp.c:17 and tested at :26) by clearing the screen
   instead of panicking. It saves 3,392 B of RAM per suspended terminal and
   costs about 0.3-0.5 KB text (E) plus a repaint on resume. On the raw-flash
   swap device (fl1) every suspend would cost a 4 KB erase sector, and the
   flash exchange logic assumes sector-aligned images (vm_swap.c
   SWAP_EXCHANGE_SECTOR_BYTES, the swap_exchange_* group), so the mechanism is
   restricted to PSRAM and SD swap and refused on fl1. Whether the swapram
   tier fronts such a client is not examined.
3. The terminal's foreground job is stopped by job control (SIGTSTP, SIGTTOU
   on a background write). This exists in tty.c today.

The plan builds readings 1 and 3 as they are and treats reading 2 as the last
stage, conditional on a measured RAM shortage after the shadows exist.

## 6. getty, stty, ttys

Present (R): bin/stty/stty.c holds struct sgttyb, tchars, ltchars and winsize
(lines 128-133) and drives TIOCGETP, TIOCSETN, TIOCGETC, TIOCSETC, TIOCLGET,
TIOCLSET, TIOCGLTC, TIOCSLTC, TIOCGWINSZ, TIOCSWINSZ; its mode tables are at
lines 61, 82 and 116. libexec/getty/main.c declares struct sgttyb tmode
(line 28) and calls TIOCGETP (line 219); gettytab.c reads /etc/gettytab, 164
lines. etc/ttys lists console, tty0 and ttyUSB0 with the xterm terminal type.
stty ships inside sysbox (26,131 B) and getty is 13,971 B (survey 9b).

Donor option sets (source reading, unmarked): FreeBSD, NetBSD and OpenBSD stty use
tcgetattr and tcsetattr and add crt, newcrt, ctlecho, echoke, echoctl, oxtabs
and iutf8 to the POSIX set; none calls tcgetwinsize.

Changes:

- stty: move the mode tables to the shim's termios state after the shim lands;
  add min N, time N, icanon, isig, iexten, echoe, echok, ixon, ixoff, icrnl,
  onlcr, opost, iutf8, and print the state as the POSIX table names it. size,
  rows and cols stay. Options for declined flags report "not supported" and exit
  non-zero for the case where nothing could be honored, matching tcsetattr.
  Risk: stty grows by the shim's 0.8-1.2 KB inside sysbox (E).
- getty: keep the sgttyb path until the shim exists, then read the line state
  through it; add one gettytab boolean, u8, that sets IUTF8 for entries whose
  terminal is UTF-8 capable (name proposed here, not a donor capability). The
  rest of gettytab stays.
- ttys: virtual terminals appear as additional lines, off by default
  (ttyv1 and ttyv2 are the proposed node names, following FreeBSD vt(4)
  naming; the names are a recommendation, not a measured fact).
- login: login/tty_modes.c reads the TIOC* calls directly (survey, R); it moves
  to the shim in the same stage as getty.
- resize(1): replace the FIONREAD poll with VMIN 0 and VTIME 1 once the kernel
  stage lands (survey).

## 7. Staged order

Stages are named by content. Every stage gives its first falsifier (the
observation that refutes the stage's prediction) and a host test that runs
without the board. Board runs stay behind the hardware opt-in.

| Stage | Prerequisite | Prediction (E unless marked) | First falsifier | Host test |
|---|---|---|---|---|
| termios.h and libc shim | none | 0 kernel text; 0.8-1.2 KB per linking program | a representable state does not round-trip through tcsetattr then tcgetattr, or an unrepresentable request returns success | round trip of every representable state against a model of the sgtty ioctls; EINVAL cases from the tables of section 2 |
| VMIN, VTIME and non-flushing set | shim | tty.o grows 0.40-0.55 KB; struct tty stays 100 B; PICOCALC headroom stays above 1,024 B | tty.o grows more than 0.6 KB, or queued bytes vanish across a mode change | the four Issue 8 cases on a model clock; a queue-preservation case; the layout contract test updated for the two new bytes |
| UTF-8 decoder and glyph coverage | none | +0.35-0.55 KB; no per-cell growth | any code point draws other than one cell in tables 1-3 or '?', or a stray continuation byte draws a bank glyph | the enumeration of section 4 through lcdterm.c with recording primitives; mutants on each reject rule |
| IUTF8 erase | UTF-8 decoder | +0.15-0.25 KB; one retired t_flags bit | an erase after a two-byte character leaves one byte queued | cooked queue model: "e" plus U+00E9, erase, with and without IUTF8 |
| extended SGR, OSC, CSI additions | none | +0.40-0.70 KB combined | known-bad: ESC [38;5;196m on the present lcdterm.c sets bold (I); the test must fail there before the change and pass after | check-picocalc-host through the recording primitives with the properties of section 4, and mutants of each branch |
| stty, getty, login on the shim | shim, VMIN and VTIME | stty +0.8-1.2 KB in sysbox | a mode set through stty differs on read-back from the tcgetattr state | stty option table against a termios model; getty u8 parse |
| TIOCDRAIN, tcsendbreak on UART0 | shim | +0.10-0.18 KB | tcdrain returns before ttywait's queue is empty | model with a transmit-complete flag |
| lcdterm per-console struct, one console | UTF-8 and SGR stages | +0.3-0.5 KB (survey) | any difference in the model-panel log | check-picocalc-host logs identical |
| second and third terminal, switch key, minor map | per-console struct | +0.6-0.9 KB more; 3,656 B per terminal | an invisible terminal calls a draw primitive; a switch changes the active shadow's content | two-console model: repaint equals store; wiring checks; PICOCALC_SD first, PICOCALC after a measured fit against 2,424 B |
| shadow swap-out (reading 2) | virtual terminals | +0.3-0.5 KB; 3,392 B RAM per suspended terminal | the swap read returns a shadow that differs from the one written, or swap loss panics | swap model with a loss injection; refusal on fl1 |

The stage "second and third terminal" lands on PICOCALC only after the
preceding stages' measured text growth shows at least 2.24 KB of the 2,424 B
unspent; otherwise it stays on PICOCALC_SD and PICO kernels.

Hazards that carry over from the survey: the shim reports VMIN 1 and VTIME 0
until the kernel stage lands; TIOCSETN is the only no-flush set request; the
new struct tty bytes change tests/kernel/tty_layout_contract.c by design.

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
No citation was found wrong. Line counts, struct names and function names in the
donor tables that carry no mark were not reopened.

## 10. Not verified

- Every (E) figure, including the 1.34-2.13 KB subtotal and the per-stage text
  growth; none of the planned code exists.
- The 49% agreement figure is a comparison of two tables, not a visual judgment
  on the panel.
- The OSC and extended-SGR misbehavior of the present parser is from reading
  lcdterm.c, not from running it.
- Padding bytes 98-99 of struct tty: the offsets are asserted, the padding is
  arithmetic.
- Whether the PSRAM transfer path can stream a 106 B row at repaint speed, and the PSRAM carve-out for shadows;
- The Ctrl+F1-F3 combination on hardware; the swapram tier's treatment of a
  kernel-object swap client; DEV_BSIZE 512 for the shadow block count.
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
