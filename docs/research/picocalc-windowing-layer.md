# A windowing and graphics layer for the PicoCalc: Microwindows port against a purpose-built layer

Finding. As of 2026-11-19 the tree has a pixel device and a launcher, not a windowing system. /dev/lcdgfx (sys/sys/lcdgfx.h, sys/arch/rp2040/dev/lcdgfx.c, 841 lines) draws on a panel with no RAM frame store. MATEzinho (usr.bin/matezinho, 712 lines) repaints 8-line bands from a retained scene (core.c:11, main.c:88). Input is raw key events plus tty bytes; there is no pointer.

Recommendation: build our own small BSD-licensed layer. It has retained-mode widgets, a window stack, exact damage rectangles composited in a strip buffer, and an optional per-window 4-bit backing store for canvases. Run one application at a time in the 192 KiB window, with the toolkit linked into each. Borrow Microwindows only as architecture: a GdXxx engine layered under an API, the clip cache, and expose-by-repaint.

Reasons, measured:
- A Microwindows port through its framebuffer subdrivers, the route examined here, carries a full 51,200 B shadow screen. A screen driver that maps each primitive to an lcdgfx command and reads pixels back with LCDG_READ was not built or measured (section 3).
- It links to 59,152 B (floor) to 90,400 B (unpatched) before libc, leaving 47-78 KB of the window for the application.
- A purpose-built layer has an estimated fixed overhead of about 20 KB (section 1), most of it estimate: the only measured part is a 144-line prototype core of 1,240 B, and that prototype uses a 4-bit shadow, not the exact-rectangle strip compositor recommended here, which is unbuilt and unmeasured. The port figures are links against dummy symbols, so they are floors, not runnable images.
- MPL 1.1 files contradict NOTICE's "every component is permissive" (NOTICE:10-11). Adopting them is a licensing decision for the tree.

Board-measured fill times: a 320 by 320 fill takes 197.0 ms under RGB666 and 131.0 ms under RGB565 (PICOCALC.md:296-297). The tile atlas holds 128 tiles (lcdgfx.h:161).

Method: arm-none-eabi-gcc 16.2.0, -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ffunction-sections, compile only. Link sizes use -nostdlib --gc-sections -lgcc, with libc, GdError, kbddev, mousedev and the unused fblinear8-32 tables defined as absolute dummies. They are therefore floors for the exercised closure (a ten-call stub), not runnable images. Times are derived from bit time (12.5 MHz, 0.64 us/byte, 11-byte window setup 7 us; st7365p.c:24-26) and agree with the measured fills. Window = 196,608 B holding text, data, bss, heap and stack (RP2040.ld:47).

## 1. Frame store

| Option | RAM in window | One 6x9 char | 100x60 move by 8 px | Full repaint (RGB666 / RGB565) | Notes |
|---|---|---|---|---|---|
| (a) direct, full-width 8-line bands (MATEzinho) | 5,120 B band | 9.8 / 6.6 ms | 44.3 / 29.6 ms | 196.9 / 131.4 ms | no shadow; row-granular damage |
| (a') direct, exact rectangles composited in a strip | about 5,120 B | 0.11 / 0.08 ms | 13.9 / 9.3 ms (7,216 px, 3 windows) | same | retained widgets repaint from state |
| (b) 4-bit shadow, flushed as LCDG_WIN_INDEX | 51,200 B + 160 B bands + row buffer | 0.20 / 0.14 ms (2 bands x 6 px) | 15.0 / 10.0 ms (band extents) | same, plus 102,400 B copied to the kernel | INDEX is 1 B/pixel (lcdgfx.h:34), so flush expands nibbles; CPU cost estimated under 10 ms |
| (c) 1-bit shadow, LCDG_WIN_BITS | 12,800 B | as (b) | as (b) | wire time unchanged; 12.8 KB payload | two colors per flush rectangle (lcdgfx.h:36-38) |
| (e) tile map (games/gametile.h) | 1,808 B | 0.13 ms | 12.8 ms (104 tiles) | 196.8 ms | 8 px placement, 8x8 cells |

Wire time is identical across shadows. The panel always receives 3 (RGB666) or 2 (RGB565) bytes per pixel, so (b) and (c) save only the user-side repaint. LCDG_COLMOD 0x55 cuts every row by one third and is available now (lcdgfx.h:57-61); the console already runs 565 (st7365p.c:29).

Option (d), the panel's off-screen lines:
- lcdgfx.h exposes no copy command (the controller's own command table was not read), so these lines cannot cache glyphs or icons.
- Read-back was measured on the board: 153,600 pixels in 1,797-1,915 ms under 66h and 1,321 ms under 55h (PICOCALC.md:310,336). That is 8.6-12.5 us/pixel against 1.6-2.2 us for writes, so a 100x60 read costs 52-70 ms against 7.7-11.5 ms to write.
- Reads are superuser-only (lcdgfx.h:113).
- Lines 320-479 and LCDG_WAITLINE exist only with options LCD_GFX_EXT (PICOCALC_SD, lcdgfx.h:96-97).
- Hypothesis worth one board test: LCDG_SCRDEF top=160, bottom=0 makes lines 160-479 a 320-line ring, and LCDG_SCROLL between 160 and 320 flips two 160-line pages under a fixed 160-line top half. The nonzero top area has never been programmed (earlier note), so the falsifier is a wrong picture or tear in lcdexttest.

Tearing:
- Writes advance 1.63 lines/ms (614 us/line at 66h). The scan advances 24 lines/ms at 74.9 Hz (PANELTEST.md:231) or 18.5 at 57.95 Hz.
- An update longer than one frame period (13.3 ms: about 6,950 px at 66h, 10,400 at 55h) is crossed by the scan and tears. Smaller ones tear unless phased with LCDG_WAITLINE.
- The panel's tearing-effect output is reported unwired; no document on main establishes it.
- A 100x60 window (6,000 px, 11.5 ms) just fits at 66h.

Room: fixed overhead F is about 20 KB (1.2 KB core measured, 0.9 KB font, 10 KB toolkit estimated, 7.9 KB libc floor measured earlier). The application then keeps about 171 KB under (a'), 163 KB under (c), 125 KB under (b), 175 KB under (e).
- word needs 38 KB text and 48 KB bss (estimate, origin/usr.bin/word-c17@95785568f476:docs/research/word-graphical-mode.md:11). That is 149 KB with (b), which fits.
- uLisp sizes its workspace to the rest of the window (origin/usr.bin/ulisp-c17@6532f1dbf6ba:usr.bin/ulisp/ulisp.c:19). A whole-screen shadow shrinks its heap one-for-one. It draws by LCDG_FILL and per-pixel LCDG_READ (dev_picocalc.c:73,84).

## 2. Process model

Two cooperating resident processes cannot coexist: every hand-off swaps a whole image. Estimates only: flash swap is about 0.5 s for a 33 KB image (zswap.md:131). PICOCALC swaps to PSRAM when present (PICOCALC.md:22-24) with no measured rate. No hand-off was timed.

- (a) One GUI process, applications as linked modules. Supported, but there is no dynamic loader (flat OMAGIC), so every app is compiled in and the window holds all their text and bss together. Windows are real and overlapping. Scheduling is cooperative, so a Lisp loop stalls the desktop.
- (b) Launcher forks and waits for one application at a time. Already implemented (main.c:251-257, MATEZINHO.md:160-163). The launcher image swaps out while the child runs. "Multiple windows" means dialogs, menus and panes inside one application, with chrome from the linked toolkit.
- (c) Hand-off through swap with a saved screen. The pixels survive only while /dev/lcdgfx stays open, because the last close repaints the console (lcdgfx.c:295-308). An application that inherits the launcher's open descriptor keeps them (section 5); it also needs application state files. It gives task switching by re-exec, not live background windows.

Recommendation: (b), with (c)-style state files, and (a)-style in-process views for small built-in tools. A fixed-address XIP widget library (docs/research/xip-shared-libc) can later take the toolkit text out of each window.

## 3. Port or own

Microwindows layering (Architecture paper pp. 8-11; src/include/device.h:65-126):
- SCREENDEVICE has DrawPixel, ReadPixel, DrawHorzLine, DrawVertLine, FillRect, blit entries and Update.
- The device-independent engine is GdXxx (devdraw.c, devclip2.c, devrgn.c, devfont.c).
- Two APIs: Win32-style (mwin, in-process) and Nano-X (client/server, or NONETWORK when LINK_APP_INTO_SERVER=Y; Makefile.rules:247-248, srvnonet.c:5).
- The paper says every SCREENDEVICE pointer must be non-null (p. 8).

No command-stream driver exists.
- The 18 scr_*.c drivers are memory or hardware framebuffers or host windows.
- The batching idiom is a malloc'd shadow plus psd->Update(x,y,w,h) (device.h:97; scr_ajaguar.c:51, scr_nuttx.c), and PSF_DELAYUPDATE flushed through PreSelect (scr_x11.c:575,723-739; scr_sdl2.c:180,253-270).
- Subdrivers call Update per primitive in fblin8.c (lines 55, 95, 127), but fblin4.c has no Update call (grep). A 4-bit port therefore gets damage only from blit paths and from wrappers we would write around the subdriver pointers.
- ReadPixel is used by alpha blending, stipple and the software cursor (devdraw.c:923-1030, devstipple.c:208, devmouse.c:615).
- Conclusion: a port by way of the framebuffer subdrivers needs the full shadow, option (b) or (c). The alternative is a SCREENDEVICE whose DrawPixel, line, FillRect and ReadPixel entries call LCDG_PIXEL, LCDG_LINE, LCDG_FILL and LCDG_READ directly. It needs no shadow; it pays one ioctl per primitive, read-back at 8.6-12.5 us per pixel for the paths that read (blending, stipple, the software cursor), a superuser server for LCDG_READ, and its blit entries would be written new. That driver was not written, so its size and speed are unmeasured, and the linked code size of the port (59,152 B floor) applies to it unchanged.

Measured sizes (Microwindows 36c8d0edfa5; 4-bpp palette, NONETWORK, no images/float/portrait; fonts are mutable .data):
- Engine closure of a ten-call stub: 25,100 B code and rodata, 12,560 B data (11,960 of it the two compiled fonts), 2,460 B bss. rasterops.o is 9,304 B (8,932 B of it the function GdRasterOp, which the link keeps).
- Objects: devrgn 3,280, devdraw 2,808, devfont 2,631, fblin4 2,248, devclip 768, genmem 1,006. convblit_frameb (11,716) is truecolor-only.
- Nano-X server plus nanowm, NONETWORK: 43,512 text, 12,940 data, 2,700 bss = 59,152 B linked floor. With every depth subdriver (unpatched fb.c) it is 90,400 B. Summing all objects gives about 105 KB.
- Struct sizes: 100 B per window, 100 B per GC, 32 B + 16 B per rectangle per region.
- Win32 layer objects: 26,372 B core plus 28,279 B for ten controls (compiled earlier; measure.sh does not reproduce them).
- Window accounting for the Nano-X port: 59,152 B (or 90,400) + 51,200 B screen + 7.9 KB libc = 118 KB (149 KB). That leaves 78 KB (47 KB) of 192 KiB with the app linked into the server, before heap for windows and regions. The floor needs only stub arrays written for this tree, not edits to MPL files.

Own layer.
- A 144-line prototype (4-bit shadow, damage bands, rectangle-subtraction clipping, z-order, title bars, 6x9 text) compiles to 1,240 B text and 51,396 B bss. Compile only; behavior untested; no widgets or events.
- Scope-equivalent toolkit estimate: 700-1,000 lines, 9-13 KB text. Proxies: uGUI 11,906 B whole library (no shadow, pixel callbacks), MATEzinho 13,259 B text for nine views, winlib controls 28 KB.
- LVGL (label, button, 6,400 B RGB565 10-row partial buffer, 24 KiB pool) links to 104,116 B text and 31,484 B bss, too big.

License.
- MPL 1.1 is file-level copyleft. Modified files stay MPL with source offered (3.1-3.2). An Exhibit A notice goes in each file (3.5). A source-availability notice goes in documentation shipped with images (3.6). A larger work may mix other licenses (3.7).
- NOTICE:10-11 and licensing.md:65,87 assume no copyleft, so adopting MPL changes that statement.
- devrgn.c mixes X Consortium, DEC and Wine-derived notices (lines 1-40), and devimage.c warns its GIF decoder is LGPL-only.
- Use pixman-region.c (Open Group and DEC notice, 3,023 lines, e7fd44140fe6) if band-list regions are ever wanted. Eight windows need only the rectangle-subtraction clip.

## 4. Toolkit

Minimum set:
- window with title bar, focus ring and z-order
- label, button, check box
- list and menu (bar and popup)
- single-line field and multi-line text view with cursor
- scroll bar
- modal dialog and message box
- status line

Estimated 9-13 KB text.

Keys and pointer:
- tty bytes give arrows, Enter, Tab, Esc, Ctrl letters.
- Ctrl combinations that need modifier state use raw events (lcdcons.h:41-43; main.c:228-233).
- Pointer substitute: Ctrl-arrows move an 8 px cursor.

Fonts:
- lcdfont is 95 glyphs of 6x9, 855 B (lcdfont.h:20-22).
- 8x13 ASCII costs 1,235 B and a 16-px proportional face about 3,100 B (arithmetic).
- Microwindows X6x13 and a Helvetica-like 11x13 link at 5.9 KB and 6.1 KB. X6x13 is marked public domain; the other's provenance is unverified.
- A text row of 53 columns is one 5.5 ms window; 32 rows cost the console's 195 ms.

## 5. Kernel work

Headroom: the retained PICOCALC and PICOCALC_PSRAM builds (dated 2026-11-13, not rebuilt) leave 12,586 B and 10,458 B of the 130,816 B region. A later measurement on main gives about 3.4 KB for PICOCALC; these retained products predate it. PICOCALC_SD has a 163,584 B region (RP2040-sd.ld:34). Per-object costs: lcdgfx.o 1,740 B at -O under GCC 16.2, lcdcons.o +128 (PICOCALC.md:586-).

- No keep-pixels flag is needed for the hand-off. A child keeps the launcher's /dev/lcdgfx descriptor across fork and exec unless it is marked close-on-exec (fdcloseexec() in sys/kern/kern_descrip.c closes only UF_EXCLOSE slots), and lcdgfx_close() runs only at the last close, so the console repaint does not happen while launcher or application holds the file. The convention is a fixed inherited descriptor number; the device stays owned throughout. Not exercised on a board.
- Wake select on raw events. select(2) exists (syscalls.master:95) and lcdselect serves the console (lcdcons.c:501-505), so no blocking ioctl is needed; MATEzinho polls every 20 ms (main.c:278). Modifier-only events do not reach the tty queue, so a wakeup in the event queue costs 40-100 B (estimate).
- No new clock. The next PL022 divisor (125/8 = 15.6 MHz, 64 ns) breaks the 66 ns TSCYCW (st7365p.c:24-26); RGB565 is the lever.
- LCDG_WAITLINE and the extra lines on the base kernel: code size unmeasured.
- Skip INDEX4 format (80-120 B): it saves under 10 ms of 131-197 ms.
- Skip a second text console: it costs a 3.4 KB cell store.

## 6. Plan

| Stage | User sees | Prerequisite | Size / RAM | Host test | First falsifiable check |
|---|---|---|---|---|---|
| 1 gui core: damage, exact-rect flush, text, palette | demo window with title bar | none | core estimated 1.5-3 KB (the 1.2 KB measurement is of the shadow prototype), 5 KB strip | shared model panel from lcdgfx_test (ioctl and write wrappers at lines 425, 557) | after random draws the model's pixels equal the layer's expected image; a mutant dropping a damaged band fails; board full flush 197 ms +-15% |
| 2 window stack, focus, Ctrl-Tab, key move | overlapping windows | 1 | 2-3 KB (estimate) | visible rectangles partition each window, 10,000 random layouts vs a per-pixel painter | any pixel owned twice or by none |
| 3 widgets and dialogs | forms, lists, menus | 2 | 7-9 KB (estimate) | golden images of each widget; guard bytes around the buffer | a widget writing outside its clip fails |
| 4 launcher on the toolkit; inherited graphics descriptor | desktop; app starts without console flash | 3 | no kernel change | model panel checks no text repaint on close | app switch over 2 s on board refutes (b) |
| 5 word and file manager on it | formatted text, file list | 4 | word about 149 KB (estimate) | existing word tests plus golden frames | window overflow in word with (b) |
| 6 uLisp canvas window | Lisp drawing in a window | 4 | canvas 4-bit buffer sized to the window | uLisp fill/line output equals model | canvas pixels differ after an overlapping menu |
| 7 optional page-flip experiment, RGB565 default | tear-free half-screen | 1, PICOCALC_SD | 0 RAM | none (board only) | tear or wrong picture in the camera capture |

## Donors and licenses

| Donor (revision) | Read | License | Takeaway / why unfit |
|---|---|---|---|
| Microwindows (ghaerr, 36c8d0edfa5) | source, 3 PDFs (sha256 f3561499, 2419fa79, ac8eac5b) | MPL 1.1 or GPL 2 (LICENSE); per-file headers carry copyright only | layering, Update/expose model; shadow, size, MPL |
| X11 regions: pixman-region.c (e7fd44140fe6) | header, size | Open Group / DEC permissive | band lists; 3,023 lines, size not measured |
| uGUI (ce0bccb5b7d4) | compiled 11,906 B | custom: keep notice, own risk | tiny widgets, pixel callbacks; one font engine, license non-standard |
| LVGL (622f63df2f7d) | compiled 104 KB | MIT | partial render buffer; far too large |
| MATEzinho (tree) | read | BSD 3-Clause | band-compose idea; 5 KB buffer |
| Prior window and console systems: SunView, SCO and USL X servers, XFree86, HP and Luna ddx, NetBSD, OpenBSD and FreeBSD console layers, RetroBSD gpanel (revisions not recorded) | source reading only; docs/research/window-system-donor-survey.md | proprietary (reference only), CDDL, MIT/X, BSD; per-donor lines in the survey | clipping, damage, hand-off and input mechanisms; no code taken |

## Reconciled with prior systems

Twelve rules for this layer, set against eleven earlier window and console systems read as source (docs/research/window-system-donor-survey.md; reading only, nothing run).

1. Clip to rectangles above the device and hand the device boxes only. SCO nfb window ops take boxes while the GC keeps regions, and USL SI passes a clip list in the graphics state, whereas lcdgfx rejects an out-of-range shape instead of clipping (survey 3a, 3e). This confirms stage 1 (exact-rectangle flush) and puts the clipper in the layer.
2. Recompute each window's visible area, its rectangle minus the windows above it, only on a stacking, move or resize change, and keep new visible minus old visible as the repaint set. SunView wt_setclipping builds w_rlexposed and w_rlfixup this way and X's ValidateRec carries an exposed part, as far as headers show (survey 3b). This adds a check to stage 2: besides the partition of visible rectangles, the repaint set equals new minus old visible area.
3. Build no copy on read-back. nfb genCopyRect loops ReadImage then DrawImage per row, and lcdgfx has no copy command while LCDG_READ costs 8.6-12.5 us a pixel and needs the superuser (survey 3e). This confirms that stage 1 offers no scroll or move primitive and repaints moved windows from state.
4. Keep widget state as the repaint source (expose-by-repaint) with an optional per-window retained buffer, repaired before any client repaint. SunView repairs from the retained image first and notifies the client only for what remains damaged, whereas X backing store is automatic and server-owned and its bodies are absent here (survey 3c, 3d). This confirms the 4-bit canvas buffer of stage 6 and rules out save-under: menus and dialogs repaint from widget state in stage 3.
5. Use one bounded dirty list of exact rectangles and, when it is full, flush it before storing the next rectangle; never widen damage. FreeBSD vt keeps one dirty rectangle and NetBSD ssdfb one x1..y2 box, and SunView keeps unbounded rectangle lists and collapses with rl_coalesce (survey 3a, 3c); a bounding box is cheap on a memory framebuffer, but here every widened pixel is two or three bytes of SPI time. This is the rule stage 1 implements (eight rectangles, flush on overflow, an operation refused before any change when the backend cannot take the flush), with mutants that drop or widen overflowed damage failing the model-panel test.
6. Draw only what changed, by comparing a widget's or text cell's retained state with what it last drew, not by comparing panel pixels. NetBSD vcons compares shadow cells and ssdfb compares blocks against a controller shadow that costs 12,800-51,200 B here (survey 3c). This confirms option (a') for stage 1 and adds per-widget last-drawn state to stage 3.
7. Keep the graphics device owned across the hand-off and leave the text console repaint in the kernel's last-close path. SCO, XFree86, HP and Luna restore from the owner's own exit routine, two of those bodies being empty, while FreeBSD uses a dead-owner timer and NetBSD resets the emulator on close (survey 3f). This confirms stage 4 and adds a case to its model-panel check: an owner killed mid-draw still gets the console repaint, not yet exercised on a board.
8. Take key repeat from the controller and hold modifier state in the toolkit. SCO repeats in software because its queue has make and break only, whereas raw events here already carry PRESSED, HOLD and RELEASED and apply Shift and Caps Lock before the key byte (survey 3g). This differs from the candidate of software repeat in the toolkit: stages 2 and 3 get a modifier table and a lost-count rebuild, and no repeat timer.
9. Give every control a mnemonic or tab-order slot and show one explicit focus. The UnixWare desktop clients use explicit keyboard focus, XmProcessTraversal and per-button mnemonics, with no accelerator table found (survey 3i). This adds a stage 3 test that every widget of a golden form is reachable by keys alone.
10. Confine every LCDG_* call to one small device shim of fill, window write and flush. The nfb template confines device code to a few entry points and SI and vt_driver use function tables, but lcdgfx is one device with capability found by LCDG_INFO and ENOTTY (survey 3e). This confirms the shared model panel of stage 1: the shim is the only file it replaces.
11. Send an opaque text run as one LCDG_WIN_BITS window of box, stride and one-bit glyph rows. nfbGlyphInfo batches glyphs the same way, though nfb paints the background first (survey 3h). This sets the stage 1 text path; it is unmeasured, and transparent text waits for LCD_GFX_EXT sprites.
12. Use no screen lock or clip-id protocol between processes. SunView arbitrates several drawers with WINLOCKSCREEN and a clip id, whereas lcdgfx allows one open file and the launcher waits while its child draws (survey 3c, 3f). This confirms the one-application process model of section 2 and leaves no lock in the plan.

## Not verified

- Board hand-off and swap times.
- Any hardware run of the prototype.
- The controller's command table (copy, TE).
- The VSCRDEF top=160 flip.
- The 3 KB headroom, and the PICOCALC_SD built size.
- fblin4 behavior with wrappers, and strip-render under Microwindows.
- twin, MGR, TinyGL, ELKS Nano-X in operation, and 68K QuickDraw: no source was fetched and this note states nothing about them.
- winFreeSansSerif11x13 license.
- uLisp binary size.
- Whether select wakes on modifier events.
- The reading of MPL 1.1 above is an engineer's reading of the license text, not legal advice.

Sources: docs/research/window-system-donor-survey.md; sys/sys/lcdgfx.h, lcdcons.h; sys/arch/rp2040/dev/lcdgfx.c, st7365p.c, lcdcons.c, lcdfont.h; sys/arch/rp2040/doc/PICOCALC.md, PANELTEST.md, MATEZINHO.md; sys/arch/rp2040/conf/RP2040*.ld; usr.bin/matezinho; docs/research/picocalc-calculator-and-gui.md, matezinho-validation.md, licensing.md, xip-shared-libc; tests/picocalc/lcdgfx_test.c; donors: microwindows/src/{include,engine,drivers,nanox,mwin}, MPL 1.1 page (sha256 211d9a1c). The stub screen driver, the link stubs, the prototype core and the script that rebuilds every size above are in docs/research/picocalc-windowing-layer/ (measure.sh and its README.md); the donor clones and compiled objects are not retained.
