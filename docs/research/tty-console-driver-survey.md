# Terminal layer, virtual consoles and PicoCalc drivers: survey and staged plan

Revision surveyed: origin/main at 4e6883edb (2026-11-19). This note is a survey and a proposal; it changes no code. Every figure carries its evidence class.

Tags: (R) read, (M) measured, (E) estimated, (I) inferred. Port paths are repository-relative. Donor roots: RENO = 4.3RENO srcsys, N2 = Net/2 sys, SCH = Schillix uts/common, SUN = SunOS 4.1.3 sys, OBSD, DFLY = OpenBSD and DragonFly sys. Size basis: object sizes are those of a full PICOCALC kernel build of this revision made by the generated Makefile, with the size flags it applies to `sys/kern` sources (arm-none-eabi-gcc 16.2.0; `arm-none-eabi-size` on the objects in the compile directory; tty.o 5,992 text bytes). The one exception is `tty_pty.c`, which no configuration builds and which does not compile; its figures come from a scratch compile with a `devspec` stub and warnings off, so they are approximate. Flash headroom for PICOCALC is taken as 3.4 KB above the 1,024-byte margin (check-kernel-headroom on that revision); the free-RAM and PICO headroom figures come from a kernel image built on 2026-11-13 and are marked where used.

## 1. The port's tty layer today

- Surface: 4.3BSD "new tty driver" via 2.11BSD, sgtty only. One 32-bit t_flags (sys/sys/tty.h:65), tchars, ltchars, a 14-byte struct ttychars (ttychars.h:15-30). No struct termios; include/termios-todo.h is Linux-numbered and uninstalled; lib/libtermlib/tcattr.c is #if 0 (R). sys/kern/tty_conf.c and sys/sys/termios.h do not exist here.
- One line discipline: TIOCSETD with nonzero returns ENXIO (tty.c:464-474); nldisp is declared (tty.h:126), no linesw (R).
- clist: cfree[32] blocks of 32 bytes, 28 data (machparam.h:89, param.h:70-71), 1,024 B bss (M). TTYHOG 255, TTYBLOCK 128, TTYUNBLOCK 64, output high water capped at 336 (tty.c:37-61) (R).
- struct tty is 100 B (M: lcdttys 0x64; tests/kernel/tty_layout_contract.c:5). rp2040-tty-local-mode-arithmetic.md says 120: stale (R). PICOCALC at HEAD has four ttys (console switch, lcd, usb, uart0) (I).
- Job control by process group only: SIGTTIN (tty.c:1367-1372), SIGTTOU (448-453, 1574-1578), SIGTSTP/INT/QUIT (1106, 1133); first open sets t_pgrp (732-734). No sessions: rg finds no setsid, TIOCSCTTY, p_session in sys/ (R).
- Window size: TIOCGWINSZ/SWINSZ with SIGWINCH (tty.c:662-672), 8 B per tty; the panel sets 53x32 at open (lcdcons.c:460-463); resize(1) polls FIONREAD on serial lines (usr.bin/resize/resize.c:77-94).
- Read modes: RAW, CBREAK, cooked (tty.c:1383-1470); no VMIN, VTIME.
- PTY: tty_pty.c is "optional pty"; no Config enables it; cdevsw 8 and 9 are reserved. It does not build: struct devspec is defined nowhere in sys/ (error at tty_pty.c:26), and -Werror rejects unused parameters (M). Stubbed, four units cost 2,395 text and 464 bss (M).
- Callers (R): stty, getty, login/tty_modes.c use TIOC* directly; bin/sh/edit.c, usr.bin/{stevie,kilo,menu,picotop}, games/gametty.h and lib/libreadline keep a host termios path beside the target sgtty path.

## 2. Feature decomposition

Method: tty.o is 5,992 text bytes for 1,196 comment-stripped code lines, 5.0 B per line; donor lines counted the same way (E).

| Feature | State added | Code added | Model donor | Depends on | Unlocks |
|---|---|---|---|---|---|
| termios + tc* as a libc shim over TIOCGETP/GETC/GLTC/LGET | none in kernel; 44 B stack per call | 0.8-1.2 KB per program that links it; 818 B prototype of tcgetattr+tcsetattr (M, a scratch prototype with known defects) | N2 tty_compat.c mapping tables, inverted; POSIX 11.2 | nothing | drops seven dual paths; a real termios.h |
| native TIOCGETA/SETA(W,F), plus sgtty shim | +24 B per tty (+96 B on PICOCALC) | +2.5-3.5 KB (E: N2 tty_compat 331 lines x 5.0 plus ttyinput rewrite) | N2 tty.c, tty_compat.c | libc shim first | exact TCSADRAIN; defer |
| VMIN/VTIME | 0 (bytes 98-99 of struct tty are padding, I) | +0.4-0.55 KB (E: 75-line block, ioctl pair, callout) | OBSD tty.c:1526-1600, 1220; DFLY tty.c:1747-1756; POSIX 11.1.7 cases A-D | callout table (NCALL 16+2*MAXUSERS) | read timeouts without FIONREAD polling: resize, stevie ESC, uLisp get-key |
| VERASE VKILL VWERASE VREPRINT VLNEXT; winsize, SIGWINCH | 0 | 0 | present (tty.c:1084, 1180, 1200, 662-672) | | |
| sessions, ctty, revoke | +8 B per tty | +0.5-0.9 KB (E) | 4.4BSD book pp.343-344; N2 tty.h:68, 89 | proc.p_session | setsid; low value with one login |
| flow control, queue bounds | 0 (present: 32 clist blocks of 32 B, one pointer and 28 data bytes each) | 0 | present, tty.c:300-330 | | type-ahead |
| PTY | 464 B bss (M) | 2,395 B text (M) | tty_pty.c; N2 tty_pty.c | devspec repair | script, multiplexers |

Hazards (R): TIOCSETP always drains output and flushes input; TIOCSETN does so too when RAW is set before or after (tty.c:551-554). A shim that maps !ISIG to RAW therefore loses type-ahead on a TCSANOW through a RAW transition, and bin/sh/edit.c chose TIOCSETN to avoid exactly that. TCSADRAIN has no sgtty analogue, and VMIN/VTIME cannot be stored (the shim reports 1 and 0).

## 3. Virtual consoles on the panel

- The split exists. lcdcons.c is tty glue for one tty (LCDCONS_NTTY == 1, lcdcons.c:107; polled lcdputc/lcdgetc in cdevsw[10]); lcdterm.c is parser and cell store without kernel headers; st7365p.c supplies three primitives, lcd_draw_cell, lcd_fill_cells, lcd_set_top (lcdterm.h:60-62). That is Solaris tem over visual_io (vis_consdisplay, conscopy, conscursor, visual_io.h:210-300). The difference: lcdterm state is file-static; tem keeps a struct tem_vt_state per console beside shared tem_state (SCH tem_impl.h) (R).
- Per-console RAM (M, HEAD lcdterm.o): cells 1,696 + attrs 1,696 = 3,392; parser, cursor, SGR 164; tty 100; 3,656 B per extra console. Shared: glyph bank 896, render stack 2,048, defer 128, reply 64. Solaris spends 3 B per cell (screen, fg, bg buffers), the port 2.
- Switching: vt_switch calls tem_switch, which flips the active pointer and redraws from that VT's buffer (SCH vcons.c:670-770, tem.c:991-1014). The port already has the redraw, lcdterm_repaint(): about 142 ms for 1,696 cells at the document's 84 us per cell (E). An inactive console needs no new mode: "update the store, call no draw primitive" is lcdterm_gfx(1), already verified by check-picocalc-host (R).
- /dev/lcdgfx: minor 1 of major 10, and the static assertion keeps LCDGFX_MINOR >= LCDCONS_NTTY. ABI-preserving map: minor 0 console, 1 lcdgfx, 2..N consoles, tty index minor-1 (design). Refuse the hotkey while lcdgfx is open; lcdcons_gfx() keeps returning EAGAIN.
- Panic: lcdabandon() already ends the panel transfer, resets parser state, frees the flag; after panicstr output draws with interrupts masked (R). Add: make console 0 active and reset it before the first byte. SCH tem_safe (CALLED_FROM_STANDALONE) and vis_polledio are the behavior reference.
- Input: picokbd_evq keeps 32 {state,key} events (64 B), drops the oldest and counts, with the session tied to lcdttys[0] (lcdcons.c:546, 627, 647). VUID's Firm_event is 16 B (SUN vuid_event.h:316-334) in a time-ordered linked pool that drops the newest (vuid_queue.c). Keep the compact event, route the session to the active console's tty, add a 16-bit tick stamp (+64 B) only if a game needs hold timing. Hotkeys: the firmware applies Alt itself and reports Ctrl as 0xA5 (picokbd_map.c:31); F1-F9 are 0x81-0x89 and F10 is 0x90, with 0x8a-0x8f unassigned (picokbd_map.c), so a hotkey test names the ten codes and not a range; Ctrl+F1..Fn consumed before translation is the candidate (I, board check).
- Cost (E): +0.9-1.4 KB text (lcdterm 4,335 x 8-12%, lcdcons 2,262 x 12-18%, hotkeys 100-200 B); +3.66 KB RAM per extra console. The stale elf had 11,256 B kernel RAM free (HEAD unmeasured). Begin with one extra console.

## 4. Bus and light drivers

- I2C1 (GP6/GP7, target 0x1F): every IC_* register and its split-phase poller lives in picokbd.c:84-115; picocalc_reg.h shares only reset, pad and timer helpers. I2C0 is unused. SPI1 (panel) defines PL022 registers in st7365p.c:57-68, SPI0 (SD) again in sd.c:95-109; sdspi.h's transport table (xfer, select, speed, micros) is the one layer boundary (R). No shared bus layer exists.
- Lights and power: LCD backlight 0x05, keyboard backlight 0x0A, battery 0x0B, power-off 0x0E, through a mailbox (picokbd_req.c) and sysctl machdep.picocalc.{battery,charging,lcd_backlight,kbd_backlight,poweroff} (picokbd.c:626-648). No caps/num light driver.
- Vendor register set (picocalc_keyboard reg.h:7-23, receiveEvent .ino:136-210, BIOSVERSION 0x16): used: VER 01, BKL 05, FIF 09, BK2 0A, BAT 0B, OFF 0E. Served but unused: KEY 04 (FIFO count, caps, num), C64_MTX 0C (9 bytes), C64_JS 0D, RST 08 (resets the MCU; avoided on purpose). CFG 02, INT 03, DEB 06, FRQ 07 are in the register file but fall to the default case and answer zeros (R).
- Shared layer: moving the I2C register code into i2c_init/phase/recover costs 0 for the keyboard and pays only with a user node (+0.5-0.7 KB, E). A shared PL022 init saves about 100-200 B (E) and touches the render-critical panel path; do it inside the user-node stage only.

## 5. C17

Ledger (tools/c17/source-denominator.json; 3,309 rows: 2,533 unreviewed, 543 excluded, 170 accepted, 63 migration-open) (R): no in-scope file is accepted. migration-open: sys/kern/tty.c, sys/sys/tty.h (the unsigned local-mode seam only). unreviewed: tty_subr.c, tty_pty.c, tty_tty.c, ioctl.h, ttychars.h, ttydev.h; every named sys/arch/rp2040/dev file; bin/stty, libexec/getty, usr.bin/login, lib/libtermlib, include/sgtty.h, termios-todo.h. Kernel units build gnu17-freestanding, userland strict-c17-and-gnu17.

New code (STYLE-GUIDE.md): f(void) and full prototypes (T01); _Static_assert layouts (T08); fixed-width register and wire types, no bit-field registers (T03, H03); tabs, 80 columns (F02-F03); peripheral and pin ownership recorded (H01); bounded interrupt work (H09). Donor code arrives as K&R and follows the section 4 migration unit (definitions with declarations, GNU17 plus strict C17, Cortex-M0+ compile, footprint). New files carry the GizmoBSD header.

## 6. Staged order

| # | Stage | Prerequisite | Text / RAM | Kernels | Host test | First falsifier |
|---|---|---|---|---|---|---|
| 1 | termios.h (N2 numbering) + libc shim beside the sgtty paths, which stay: `kilo` and `menu` ask for VMIN 0 and VTIME 1, which the shim cannot store, and TIOCSETN flushes input on any change that involves RAW, so the seven dual paths are dropped only after stage 3 | none | 0 / 0 kernel; 0.8-1.2 KB per program that links it, the range of the feature table (818 B prototype, M) | all | round-trip each representable state against a sgtty ioctl model | a program that needs VMIN, VTIME or a RAW change without a flush still builds and runs on its sgtty path; an unrepresentable state gives EINVAL from the shim |
| 2 | Repair tty_pty.c build; compile gate | none | 2.4 KB / 0.46 KB when enabled (M) | PICO, PICO_UART, SD | compile under the kernel profile | build fails today (M) |
| 3 | VMIN/VTIME, and a set request that does not flush input on a RAW change; the dual paths of stage 1 go once both exist | 1 helps | +0.4-0.55 KB / 0 (E) | all; PICOCALC if headroom allows | POSIX cases A-D on a model clock | tty.o growth over 0.6 KB |
| 4 | lcdterm per-console struct, one console | none | +0.3-0.5 KB / 0 (E) | PICOCALC family | check-picocalc-host logs identical | any model-panel log difference |
| 5 | Second console, hotkey, minor map, panic to console 0 | 4 | +0.6-0.9 KB more / +3.66 KB per console | PICOCALC_SD first; PICOCALC after a HEAD RAM measurement | two-console model: inactive output draws nothing, repaint equals store; wiring.sh cuts | inactive console calls a draw primitive |
| 6 | Shared I2C layer; KEY 0x04 | none | about 0 / 0 (E) | PICOCALC family | kbdreq_test with a register model | text growth over 100 B |
| 7 | machdep.adc.N, GPIO_PWM | none | +0.4-0.6 KB / 4 B (E) | PICOCALC family | check-gpio-requests, adc_conversion extended | PWM accepted on an unowned pin |
| 8 | /dev/i2c*, /dev/spi* | 6, 7 | +1.0-1.3 KB / 14 B (E) | PICOCALC_SD; bare-Pico use needs the union kernel | register-model transfers | a node answers on a PicoCalc |

Board runs stay behind the opt-in. PICO and PICO_UART had 29,304 and 38,576 B of headroom (M, stale elf), enough for stages 1-3 and 7-8.

## 7. Licenses

| Donor | Header evidence (R) | Use |
|---|---|---|
| RENO kern/tty.c, tty_subr.c, tty_compat.c, tty_conf.c, sys/tty.h, ttychars.h | "Berkeley software License Agreement" legend, no grant text | not a three-clause grant; it is the legend NOTICE section 2 already carries; an import needs an owner decision |
| RENO kern/tty_pty.c, sys/termios.h, ioctl_compat.h | "Redistribution is only permitted until one year after the first shipment of 4.4BSD" | grant expired by its own words (I); do not import |
| N2 kern/tty.c, tty_subr.c, tty_compat.c, tty_pty.c, sys/termios.h, tty.h, ttydefaults.h | three-clause | importable per header; Net/2 file history not checked |
| OBSD kern/tty.c | three-clause, lines 5-28 | importable |
| DFLY kern/tty.c, NetBSD kern/tty.c | headers not read | consult only |
| SCH ldterm, ttcompat, ptem, ptm, pts, tem*, vcons, wscons, conskbd | CDDL (ldterm.c) | behavior only |
| SUN tty_ldterm, tty_ttcompat, vuid_*, kbd, bwtwo | SMI copyright, sccsid | behavior only |
| 2.11BSD tty*.c | Berkeley legend | already in tree |

kern/tty.c has no VMIN or VTIME in RENO, N2, 4.4BSD-Lite2, LiteBSD or 211bsd (rg, zero hits). It has them in OpenBSD, NetBSD and DragonFly; DragonFly's struct clist differs from the port's linked blocks (tty.h:45-47), so only the ttread logic transfers.

## 8. Hardware access for programs

Today (R): /dev/porta, /dev/portb (cdevsw 12, DEV_GPIO, root only; CONFIN, CONFOUT, DECONF, STORE, SET, CLEAR, INVERT, POLL; read and write return ENODEV; open drain EINVAL); /dev/lcdgfx (LCDG_*); the console's LCDIOC_SETGLYPH, GETKEY, KEYEVS; machdep.picocalc.*; sysctl CPU_ADC_TEMP and CPU_ADC_VSYS (cpu.h:131-132; adc_read serves inputs 0-4, only 3 and 4 have a node, only GPIO29's pad is prepared); /dev/audio (8 kHz u8; slice 6 is its sample clock, slice 5 the bell). Majors 12 and 13 exist only with PICOCALC_ENABLED and SD_ENABLED (conf.c:46-60), so PICO and PICO_UART have no GPIO node.

Pin finding (I): PICOCALC.md:154 and gpio.h:gpio_board_pins() call GP8 and GP9 the expansion header, so root may drive them. The notes repository docs/picocalc/INDEX.md:21-22 says they are on neither J702 nor J703 and J703 carries GP2-GP5, GP21, GP28; the schematic text (lines 95-96) prints UART1_TX beside M_UART3_RX and UART1_RX beside M_UART3_TX, which reads as Pico UART1 wired to the keyboard MCU's UART3 through the switch block; PICOCALC Config says UART1's pins belong to the board. If so, gpio.c lets root drive lines wired to the keyboard MCU. The deciding check: continuity or a scope on J702/J703 pins 4 and 5, or the schematic PDF page behind those lines. GP8: SPI1 RX, UART1 TX, I2C0 SDA, PWM4 A; GP9: SPI1 CSn, UART1 RX, I2C0 SCL, PWM4 B; GP28: SPI1 RX, UART0 TX, I2C0 SDA, PWM6 A (datasheet text 736-737, 787); GP28 is ADC2 (datasheet pin list, line 46153).

| Node or ioctl | Text / RAM (E) | Safety rule | uLisp restored |
|---|---|---|---|
| sysctl machdep.adc.N, N 0..4, read only | +120-180 B / 0; GP28 pad setup +40 B | refuse N 0 and 1 on PicoCalc (amplifier pins) | analogread |
| GPIO_PWM request (free command bit 25), argument duty<<16 or pin mask | +250-350 B / 2 B | owned pins only; refuse slice 5 on PicoCalc; GP28 is slice 6, shared with /dev/audio, so EBUSY while audio is open | analogwrite |
| /dev/i2c0, /dev/i2c1, one XFER ioctl, 32 B each way | +0.5-0.7 KB / 8 B | PicoCalc: I2C1 refused (keyboard bus). I2C0 GP8/GP9: refused if board-owned (likely), offered if J702/J703 carry them. Bare Pico: both pins in the free mask and unowned; refuse addresses 0x00-0x07, 0x78-0x7f; 100 ms poll bound with abort recovery as picokbd | with-i2c, restart-i2c |
| /dev/spi0, /dev/spi1, one XFER ioctl, 64 B, chip select via the GPIO node | +0.45-0.6 KB / 6 B | PicoCalc: both refused (SD, panel). Bare Pico: pin mask | with-spi |
| device uart1 (NUART=2) | 0 / about 100 B per tty | PicoCalc refused unless GP8/GP9 prove free; UART0 stays the console | with-serial |
| LCDG_ORIENT, LCDG_INVERT (MADCTL 0x36, INVON/INVOFF) | +100-150 B / 0 | lcdgfx open rules | set-rotation, invert-display |

If GP8/GP9 are board-owned, a PicoCalc owner gets GPIO, ADC and PWM on GP28 alone, with no I2C, SPI or serial; if they reach a header, I2C0 and UART1 join. analogreference and the resolution functions need only userland scaling (I). The first four rows add 1.3-1.9 KB, which PICOCALC at 3.4 KB headroom cannot also spend on consoles; PICOCALC_SD or a bare-Pico kernel can.

## 9a. Driver inventory

Text bytes are HEAD recompiles (M); "scratch" means the SRAM scratch bank, not flash.

| Device or bus | State | File (text) | Kernels | User interface | Test evidence |
|---|---|---|---|---|---|
| UART0 | present | uart.c (1,410) | all | /dev/tty0 | uart_test model, 8 mutants; Renode boot |
| UART1 | source only | uart.c | none (NUART=1) | none | none |
| USB CDC-ACM | present | usb.c (3,963 + 4,096 scratch) | PICO, PICOCALC family | /dev/ttyUSB0 | check-usb-* host gates |
| ST7365P, SPI1 | present | st7365p.c (3,184) | PICOCALC family | via lcdterm, lcdgfx | check-picocalc-host; board bytes 2026-11-17 |
| Console terminal | present | lcdterm.c (4,335), lcdcons.c (2,262) | PICOCALC family | /dev/console | check-picocalc-host, mutants |
| Pixel device | present | lcdgfx.c (2,024 + 4,096 scratch) | PICOCALC, SD | /dev/lcdgfx | lcdgfx_test, 37 mutants |
| Keyboard, I2C1 | present | picokbd.c (2,152), evq (592), map (482), req (260) | PICOCALC family | tty; LCDIOC_GETKEY | kbdreq_test, kbdevq_test; no bus test |
| Backlights, battery, power-off | present | picokbd.c | PICOCALC family | sysctl | mailbox host test; board checks open |
| KEY, C64 registers | absent | none | none | none | none |
| SD, SPI0 | present | sd.c (3,540), sdspi.c (1,352) | PICOCALC family | sd0, rsd0 | sdspi_test, sdstrat_test; board sd_detach closed |
| PSRAM, PIO0 | present | psram.c (698), psram_xfer.c (1,948) | PICOCALC, LEAN, SD | block major 3 | check-psram-pio; board not run |
| Bell, /dev/audio | present | picoaudio.c (276), picopcm.c (544) | PICOCALC family; pcm in PICOCALC, SD | /dev/audio | picoaudio_test, picopcm_test; unheard |
| GPIO | present, root only | gpio.c (408) | PICOCALC, SD | /dev/porta, portb | check-gpio-requests; board not run |
| ADC | partial | adc.c (172) | PICO, PICO_UART, PICOCALC, SD (not LEAN) | two sysctls | adc_conversion |
| I2C0, PWM, user SPI | absent | none | none | none | none |
| PTY | source only, broken | tty_pty.c | none | none | none |

## 9b. Software list

Flash root, full profile, built 2026-11-13 (988 blocks, 217 free; sizes are stored inode bytes, M via fsutil on distrib/rp2040/sdcard.img; the PICO board's image).

| Group | Path | Members | Bytes |
|---|---|---|---|
| shell and editors | /bin/sh, /bin/ed, /usr/bin/stevie (vi, vim), /usr/bin/menu | | 31,073; 7,239; 14,671; 9,284 |
| box | /bin/box | cat chgrp chmod cp echo hostname kill ln ls mkdir mv pwd rm rmdir sleep sync test [ chown | 27,899 |
| sysbox | /bin/sysbox | date dd df stty mknod mount umount | 26,131 |
| adminbox | /sbin/adminbox | shutdown reboot sysctl halt bootloader true false nohup timeout | 23,880 |
| utilbox | /usr/bin/utilbox | expr md5 id whoami groups logname more uniq head tr wc cmp sort basename env printf touch xargs tty resize uname md time | 44,987 |
| textbox | /usr/bin/textbox | cut paste seq dirname nl cksum expand unexpand uuencode uudecode readlink realpath fold rev comm look deroff colrm | 32,190 |
| separate tools | /usr/bin, /bin, /sbin | awk 35,891; sed 12,312; tar 23,790; fsck 23,652; ps 14,440; find 14,434; du 6,967; fstat 8,774; cpio 9,093; grepbox 10,196; compress family 11,763 | |
| login chain | /sbin/init, /usr/libexec/getty, /usr/bin/{login,su,passwd} | | 15,025; 13,971; 18,486; 11,803; 11,616 |
| toolchain | /usr/bin/{as,ld,cc}, /usr/libexec/smlrc, /usr/lib/{libc.a,crt0.o} | | 23,303; 15,620; shell driver; 41,769; 45,318 |
| games | /usr/games/gamebox; /usr/games/puzzlebox | fifteen keen bubble fourfall coil; flood samegame unruly lightup | 21,658; 36,083 |
| tests | /usr/bin/{coremark,mputest} | | 13,293; 9,656 |
| PicoCalc closures (profiles file; no built PicoCalc image exists) | /usr/bin/{bc,dc,calc,picofetch,picotop}, ridge, /dev/lcdgfx, sdcard nodes, savecore, /sbin/mkfs | | bc 31,176 and dc 18,644 packed, bc.library 5,125; calc 33 blocks, picofetch 14, picotop 15, ridge 11, sdcard 11, psramtmp 9; matezinho opt-in profile |
| PicoCalc SD root | tools/picocalc/mksdroot.sh | the whole install tree plus a tty0 getty, on a 32 MiB partition | none built |

## What else under gororoba-documents-technical computer-science/systems bears on this

Titles only, none read. bsd: 4.3BSD users reference manual (tty(4), stty, termcap); McKusick architecture manual (termios interface); Leffler and Karels config tutorial. unix: Bach (terminal drivers); Lions and unix_tty0.c (V6 tty); Thompson and Ritchie V7 papers (character queues); Hettrick, Porting the UNIX Kernel (console bring-up); USENIX 1987 proceedings. posix: POSIX 2007 draft (second source for chapter 11); SUS guide. sun-solaris: McDougall, Solaris Internals (STREAMS tty path); STREAMS device-driver guide (ldterm/ttcompat); Writing Device Drivers.

## Not verified

Every (E) figure; hotkey availability on hardware; struct tty padding at 98-99 (from field offsets, not a compile); installed keyboard firmware revision; HEAD RAM headroom and flash growth since 2026-11-13 (no kernel build run); Net/2 provenance; DFLY and NetBSD tty.c license headers; the GP8/GP9 destination (schematic text, not the PDF page); 4.3BSD book beyond its chapter 9 outline (pp.259-278) and p.263; 4.4BSD chapter 10 pp.337-348 (markdown transcription; the PDF text layer ends earlier); POSIX 11.1.7 and 11.2.1 only; Microwindows for layering only (screen, mouse and keyboard drivers under a core engine, pp.6-11), no sizes; libcurses.

## Sources read

- This repository at the revision named below: `sys/kern/tty*.c`, the tty and ioctl headers under `sys/sys`, `sys/arch/rp2040` (dev, conf, the compile Configs, doc), `distrib/rp2040`, `tools/c17`, and the research notes this one builds on.
- The uLisp port as staged for pull request 728 at commit aa4fe6038 (`docs/research/ulisp-c17-port.md`).
- The notes repository (discobsd-2040-notes) at 71b56d3ffe56: its PicoCalc index, the vendor keyboard firmware, and the datasheet and schematic text.
- Donor trees, each at the revision read. Git checkouts: 2.11BSD fork 0fe700318f86 (patch level 500), 4.4BSD-Lite2 b10da34408c8, LiteBSD 03934b78b7c7, OpenBSD 8849c5d46b9b, NetBSD 1e8e727e461b, DragonFly 94448f622a06. Archives, by their BLAKE3 prefix in the archive catalog: 4.3BSD-Reno `srcsys.tar.gz` 5b278958068d898a, Net/2 `net2.tar.Z` 8cc301e2f31087ad, SunOS 4.1.3 `sunsrc.tar.gz` a2cf572b79f5e76c. The Schillix OS/Net tree was read from an unpacked collection whose archive identity was not recorded, so its line references are reproducible only against that copy.
- Books: The Design and Implementation of the 4.4BSD Operating System (chapter 10, pp. 337-348), the 4.3BSD volume (chapter 9 outline), POSIX.1-2024 chapter 11 (11.1.7, 11.2.1), and Haerr's Microwindows architecture paper.
