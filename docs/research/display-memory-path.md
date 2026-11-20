# The display memory path, from a program's write to a lit pixel

Is there any cache between the display controller and the glass, or between the program and the controller? No memory in either place caches frame data. Between the controller and the glass the only storage is the controller's own frame RAM, 320 x 480 x 18 bit = 2,764,800 bits (345,600 bytes packed), which the glass is scanned from (an inference from the spec's block diagram and one sentence on page 248, not a stated rule); the data latch and source buffers on the block diagram are not described as holding frame data. Between a program and the controller there is no frame cache either: the bytes in flight sit in a 64-byte kernel stack chunk (`lcdgfx.c`), the PL022's 8-entry transmit FIFO (16 bits wide, about 8 bytes of payload at 8-bit frames) and the 8 bits in its shift register, while the kernel's text store (cells 1,696 B and attributes 1,696 B, 3,392 B in all) is a model of the console's screen that the driver redraws from, not a shadow of the panel. The one spare frame store in the machine is the controller's 160 lines the 320-line glass does not show (115,200 B), and the spec gives it no copy command.

Provenance. Tree: `b03afa7ac` (main, clean); kernels built with GCC 16.2.0 (`arm-none-eabi-gcc`), `-Os` per the kernel Makefiles, by `bmake MACHINE=rp2040 -C sys/arch/rp2040/compile/PICOCALC all` and the same for `PICOCALC_SD`, measured with `arm-none-eabi-nm -S`, `arm-none-eabi-size` and `readelf -SW`. PDF pages are the pages printed in each document's footer unless marked "pdf". Tags: (R) read in the source or document, (M) measured, (I) inferred. Every page and line cited below was opened for this note; the earlier inventories (Haiku readings) were used as leads only.

## 1. The process

- libwin (`lib/libwin/win.h`) keeps no copy of the screen (header comment). A `struct win_scene` is 5,712 B (M: `sizeof` compiled for Cortex-M0+): 8 windows of 52 B, a z list, 8 damage rectangles, the backend and a strip buffer of 5,136 B (`WIN_STRIP_BYTES` 5,120 plus two 8-byte guards). The strip is 16 full-width rows of one-byte palette indexes (`LCDG_WIN_INDEX`). Owner: the program; lifetime: the scene's.
- `win_lcdgfx.c` hands each strip to the kernel as one `LCDG_WINDOW` ioctl plus `write(2)` calls of up to w x h bytes; the process holds nothing else per pixel. `win.c` includes the kernel's `lcdfont.h`, so the 855-byte font table lies in the program's text (R).
- A program lives in one flat 192 KiB window (196,608 B) for text, data, bss and stack (`sys/arch/rp2040/doc/MPU.md` owns the map). Any shadow a program keeps is carved from it: a 4-bit index shadow of 320 x 320 is 51,200 B and a 1-byte shadow 102,400 B (`docs/research/picocalc-windowing-layer.md` options table).

## 2. The system call boundary

- `lcdgfx_write()` copies the user buffer with `uiomove()` in chunks of `LCDGFX_CHUNK` = 64 B on the kernel stack (`dev/lcdgfx.c:69`, buffers at `:483` and `:606` for the sprite stream). `lcdgfx_read()` decodes into `raw[LCDG_RD_RAW_BYTES(LCDG_READ_MAX)]` = 62 B on the stack (`:539`, `sys/lcdgfx.h:180-183`; 20 pixels a command). The tile atlas and its row buffer take no staging copy (`tw_row`, 40 B).
- `ioctl()` copies a payload into a union on the kernel stack (`kern/sys_generic.c:203-217`), sized by its largest member; `PICOCALC.md` states at most 12 bytes for a `LCDG_*` command (R; the union's `sizeof` was not computed).
- The console path: a program's `write(2)` fills the tty output queue `t_outq`, which lives in the shared clist pool: `NCLIST` 32 blocks of `CBLOCK` 32 B (28 data bytes each), 1,024 B for every tty, with an output high-water cap of 336 B (`sys/sys/param.h:70-71`, `machparam.h:89`, `kern/tty.c:48`). `lcdstart()` pends a software interrupt; `lcdrender()` runs on its own 2,048-byte stack (`lcd_render_stack`, `dev/lcdcons.c:90-93`), pops one byte at a time (`getc(&t_outq)`) and calls `lcdterm_putc()`. Kernel `putc` that finds the drawing flag held appends to a 128-byte deferred ring (`:77`, `:116`); terminal replies wait in a 64-byte ring (`:101-103`).

## 3. Kernel state

Measured sizes of the objects of both PicoCalc kernels, rebuilt for this note (the 13 November objects the earlier inventory used predate the packed glyph bank).

| Object (symbol) | PICOCALC | PICOCALC_SD | Section | Source |
| --- | --- | --- | --- | --- |
| `cells` 32 x 53 | 1,696 | 1,696 | bss | `dev/lcdterm.c:66` |
| `attrs` 32 x 53 | 1,696 | 1,696 | bss | `dev/lcdterm.c:67` |
| `glyph_bank` 128 x 7 packed | 896 | 896 | bss | `dev/lcdterm.c:150` (`LT_GLYPH_PACKED`) |
| `lcd_render_stack` | 2,048 | 2,048 | bss | `dev/lcdcons.c:93` |
| `defer`, `reply`, `lcdttys` | 128, 64, 100 | same | bss | `dev/lcdcons.c:103,109,116` |
| `gfx_pal` 16 x 2, `tw_row` | 32, 40 | 32, 40 | bss | `dev/lcdgfx.c` |
| `lcdterm.o` bss in all | 4,452 | 4,456 | bss | `size lcdterm.o` |
| `lcdcons.o` bss in all | 2,376 | 2,376 | bss | `size lcdcons.o` |
| `lcdgfx.o` bss in all | 140 | 196 | bss | `size lcdgfx.o` |
| `st7365p.o` bss | 4 | 12 | bss | `size st7365p.o` |
| `atlas` 128 x 32 | 4,096 | 4,096 | `.scratch` (SRAM4) | `dev/lcdgfx.c:100`, `kern.ldscript:217-226` |
| `lcdfont` 95 x 9 | 855 | 855 | flash text | `dev/lcdfont.c` |
| `lcd_palette` 16 x 3 | 48 | 48 | flash | `dev/st7365p.c` |
| `dec_graphics` | 288 | 288 | flash | `dev/lcdterm.c` |

The glyph bank is 896 B, 128 glyphs of 7 packed bytes (`lcdterm.c:142` says 896), not the 1,152 B of the earlier inventory. The two scratch banks (SRAM4 and SRAM5, `0x20040000`, 8 KiB) hold the USB transmit ring at 4,096 B (`-DUSB_TXRING=4096` in both kernel Makefiles) and the atlas at 4,096 B: `readelf` shows `.scratch` 0x2000 in both kernels, so no scratch is free. The kernel's data plus bss end at `0x200373f0` on PICOCALC (`.bss` 0x20030480 + 0x6f70); how that sits against the kernel region of `RP2040.ld` is not re-derived here.

## 4. The PL022 (SPI1)

- FIFOs: transmit and receive are each 16 bits wide and 8 locations deep (ARM DDI 0194H 2.2.4 and 2.2.5, page 2-4). The driver uses 8-bit frames (`SSPCR0` DSS 7), so a transmit FIFO holds 8 payload bytes; the driver polls `SSPSR.TNF` before each byte (`dev/st7365p.c` `spi_put`) and uses no DMA.
- Clock: the bit clock is SSPCLK / (CPSDVSR x (1 + SCR)) with CPSDVSR even, 2-254, and SCR 0-255 (pages 2-4, 2-8, 3-4, 3-8). The driver derives 125 MHz / (2 x 5) = 12.5 MHz (`st7365p.c` `LCD_PRESCALE`, `LCD_SCR`). Limits (page 2-7): master SSPCLK at least 2 x the fastest bit clock, at most 254 x 256 x the slowest, and SSPCLK no faster than PCLK. The ST7365P limits the write clock to 15.15 MHz (66 ns, page 54) and the read clock to 6.67 MHz (150 ns); the read path runs at 125 / (2 x 20) = 3.125 MHz, 20 pixels a command.
- BSY: `SSPSR.BSY` is set while a frame is in transmission or reception or while the transmit FIFO is not empty (3.3.4, page 3-8). The driver waits for it to clear before it changes D/C, so the shift register and FIFO are empty when the panel sees a D/C edge.
- DMA the driver does not use: `SSPTXDMASREQ` asserts with one or more empty transmit locations and `SSPTXDMABREQ` with four or fewer characters queued; the receive requests assert at one character and at four or more (pages 2-19, 2-20, Table 2-1); `SSPDMACR` bit 1 TXDMAE and bit 0 RXDMAE enable them (3.3.10, page 3-12). The RP2040 DREQ numbers are SPI1 TX 18 and SPI1 RX 19 (RP2040 datasheet Table 119, page 95). The measured 320 x 320 fill takes 197,000 us at 66h against 196,615 us of bit time (`PICOCALC.md`), so a DMA transfer has no bus time left to recover; DMA would save CPU cycles only.

## 5. The wire

Read from the schematic's text layer (`clockwork_Mainboard_V2.0_Schematic.pdf`, sheet 1, `pdftotext -bbox`), not traced: in the band between the RP2040 header and J401 the net labels `SPI1_SCK`, `SPI1_TX`, `SPI1_RX`, `SPI1_CS`, `LCD_DC` and `LCD_RST` sit at the vertical positions of J401's `WR/SCL`, `SDA`, `SDO`, `CS`, `DCX` and `RESET`; no part label, buffer or level shifter text lies in that band (the only other labels are C301 and ALDO2, PSRAM supply items). So `SPI1_RX` pairs with `SDO` (GP12: panel data out reaches the RP2040). `TE` at J401 has no net label at the label column, unlike its neighbors; whether a wire is drawn was not traced (I).

## 6. The controller (ST7365P_SPEC_V1.0.pdf)

- Memory: "integrated 320x480x18-bit graphic type static RAM ... 2,764,800-bit" (page 248, 10.1.1; pages 7 and 8). 18 bits is the stored format for every input format; for 16 bpp input EM (B7h) EPF[1:0] picks how the 5- and 5-bit channels widen (page 228; the 4-line SPI RGB565 write format is on page 96). On the measured board the stored low bits of red and blue stay clear whatever EPF the driver writes (`PICOCALC.md` results of 2026-11-19).
- Block diagram (page 40): Interface, Instruction Register, Display RAM, Data Latch, 960 Source Buffer, 480 Gate Buffer, level shifters, DAC, gamma, NVM. No line buffer or write FIFO is described. Is the glass scanned from RAM? Page 7 says the RAM is read and written "with no external clock", page 248 draws a line address counter and a scan address counter on the RAM and says "there will be no abnormal visible effect on the display when there is a simultaneous Panel Read and Interface Read or Write to the same location", and DISPOFF (28h, page 171) disables "the output from Frame Memory". No sentence says in words that the panel refreshes itself every frame; that is an inference (I), supported by the board: the glass stays lit between host writes and `LCDG_WAITLINE` reads a scan line that advances (`PICOCALC.md`).
- Sleep and standby: SLPIN (10h, page 163) stops the DC/DC converter, oscillator and scanning while "the memory keeps its contents" (measured on the board: 0 of 320 pixels of line 200 differ after SLPIN and SLPOUT, `PICOCALC.md`). DISPOFF keeps the contents. Deep standby (EM B7h DSTB=1, page 228) removes SRAM power and the contents are lost; exit is six CSX low pulses or a RESX pulse. Power-on contents are random and neither software nor hardware reset clears them (pages 178, 179), which `PANELTEST.md` pattern 7 saw as undrawn RAM noise.
- Display modes: VSCRDEF (33h, pages 183-184) and VSCSAD (37h, pages 190-191) re-map which RAM line is the first shown; the sum TFA + VSA + BFA must be 480 and MV must be 0; default VSA 01E0h. MADCTL (36h, page 188) sets the RAM write/read direction (MY, MX, MV) apart from the refresh order (ML, MH) and the color order (RGB). PTLON (12h) and PTLAR (30h, page 181) limit which RAM rows are displayed; IDMON (39h, page 193) shows 8 colors "using MSB of each R,G and B in the Frame Memory"; INVON (21h) inverts the display. None of these is stated to change RAM contents (I; the spec is silent, not explicit).
- Command set: the user commands (9.2, pages 145-217) and the extended commands (9.3, pages 218-246) contain no RAM-to-RAM copy; RAM addressing is CASET (2Ah), RASET (2Bh), RAMWR (2Ch), RAMRD (2Eh), WRMEMC (3Ch) and RDMEMC (3Eh, heading on page 198). RDFCS (AAh, page 213) returns an 8-bit checksum of the user registers and frame memory, 150 ms after the last register write; it is an integrity check, not a copy.
- Read path: the first parameter of RAMRD (pages 178-179) is a dummy; data follow as D[17:0], and RDMEMC always returns 18-bit pixels (page 198). The board shows the reply follows COLMOD in 4-line SPI: after an 8-bit lead, three bytes a pixel under 66h and two under 55h (`PICOCALC.md`, kernels 4399 and 4401), so the spec's 18-bit statement does not describe the measured link under 55h. The serial read cycle is 150 ns against the 66 ns write cycle (pages 53-54, at VDDI = 1.8 V); the driver reads at 320 ns a bit (3.125 MHz) and measured whole-memory read-back at 1,797-1,915 ms under 66h and 1,321 ms under 55h for 153,600 pixels, against 340 ms (66h) and 242 ms (55h) to write them: 11.7-12.5 us a pixel read against 2.2 us written under 66h.

## 7. The glass

The glass shows 320 of the 480 lines; the driver programs VSCRDEF 0 / 320 / 160 (`PICOCALC.md`, `dev/st7365p.c`). Evidence: the photograph of `paneltest` pattern 7, a 480-line scroll area started at RAM line 160, shows bands 5-9 and "undrawn RAM noise below" (`PANELTEST.md` results, E7), which agrees with 320 visible lines; the vendor listing's 480 x 480 glass is refuted horizontally and open vertically in `hardware-reference-coverage.md` (a pixel-pitch measurement would decide it). The 160 lines (RAM lines 320-479, 115,200 B) are exposed only by the PICOCALC_SD kernel: `LCDG_RAMFILL`, `LCDG_RAMWIN`, `LCDG_READ`, `LCDG_SPRITE` reach lines up to 480 (`sys/lcdgfx.h:99-130`, `LCD_GFX_EXT`), while `LCDG_WINDOW`, `LCDG_FILL` and the other drawing commands are bounded at 320 lines (`dev/lcdgfx.c`, `window_begin(..., LCDG_HEIGHT)`), so on the base PICOCALC kernel a program can only scroll the hidden lines into view (`LCDG_SCRDEF`, `LCDG_SCROLL`), not draw them; the extended commands answer ENOTTY there.

## Every byte of display memory, by location

| Location | Bytes | What |
| --- | --- | --- |
| Process (libwin scene) | 5,712 | scene plus 5,136-byte strip; inside the 196,608-byte window |
| Kernel stack in flight | 64 + 62 | write chunk, read reply buffer (one at a time) |
| Kernel SRAM bss (PICOCALC / PICOCALC_SD) | 6,972 / 7,040 | `lcdterm.o` + `lcdcons.o` + `lcdgfx.o` + `st7365p.o` bss, including the 2,048-byte render stack |
| Shared tty clist pool (all ttys) | 1,024 | output cap 336 |
| Scratch SRAM4/5 | 4,096 | tile atlas (the other 4,096 is the USB ring, not display) |
| Flash (read through XIP) | 1,191 | `lcdfont` 855 + `lcd_palette` 48 + `dec_graphics` 288 |
| PL022 | 16 + 16 | two 8-entry, 16-bit FIFOs (8 payload bytes each at 8-bit frames) |
| Controller RAM, whole | 345,600 | 320 x 480 x 18 bit |
| Controller RAM, glass | 230,400 | 320 x 320 x 18 bit |
| Controller RAM, off-screen | 115,200 | 320 x 160 x 18 bit |
| PSRAM | 8 MiB | not display memory (swap and `/tmp`); a candidate home only |

Totals: host-side display RAM is about 7 KB of kernel bss plus 4 KB of scratch, against 345,600 B in the controller; the controller holds about 49 times what the kernel's display objects do.

## What can act as a cache

(a) The controller's 160 off-screen lines. The command lists have no copy command, so a line can move only by sending its pixels over the wire twice (a read at 11.7-12.5 us a pixel, 5 times the cost of a write and superuser only, then a write) or by changing which lines show. Scrolling is the one thing the controller does for free: with VSCRDEF top 160 and bottom 0, `LCDG_SCROLL` between 160 and 320 flips two 160-line pages under a fixed half, a double buffer without a copy (`picocalc-windowing-layer.md` records it as one untested board hypothesis). It exists only on PICOCALC_SD, and filling the hidden page costs a write at 2.2 us a pixel. As a cache of glyphs or icons it fails: reading a pixel back costs more than rewriting it from the tile atlas.

(b) A kernel cell shadow per text console. One console's cell store is 3,392 B plus about 132 B of parser and cursor scalars (4,452 - 3,392 - 896 - 32, M); the glyph bank and font are shared. A frozen background console costs about 3.5 KB of kernel bss and a refactor of the file-static state of `lcdterm.c` and the single-tty assertion of `lcdcons.c` (`LCDCONS_NTTY == 1`). Scratch has no room (section 3), so the cost lands in striped kernel RAM, where text and RAM are both tight; the headroom of today's build is not re-derived here.

(c) PSRAM through bounded transfers. Frames are `PSRAM_CHUNK` = 64 B (`dev/psram_xfer.h`), with the clock `clk_sys / (2 x PSRAM_CLKDIV)` = 125 / 4 = 31.25 MHz and four data lines (`psram_pio.h:134-135`), so the wire carries 15.6 MB/s; the `check-psram-pio` model gives 4.45 us a 64-byte write frame and 4.72 us a read frame (`memory-devices.md` quad row), 14.4 and 13.6 MB/s, each masked from interrupts for at most 5.2 us. A 3,392-byte shadow is 53 frames, about 0.24 ms to write and 0.25 ms to read by that model. The end-to-end rate including word packing is unmeasured. A shadow of the whole glass at 4 bits a pixel (51,200 B) would take 800 frames, about 3.6-3.8 ms by the model. PSRAM is a probed device that is not on every board.

(d) A process-side shadow. Cheapest in latency (no wire, no read): 51,200 B at 4 bits a pixel or 102,400 B at 8 bits inside the 196,608-byte window; libwin deliberately keeps none, using damage rectangles and a 5,120-byte strip.

## Consequences

- libwin never needs a read-back: damage lists plus a re-render from the scene replace a frame cache, and the controller keeps the picture between flushes, across process exit and across DISPOFF or sleep. The picture is lost only with ALDO3 or deep standby, and power-on RAM is random (the driver turns the display on only after the first clear, `st7365p.c` header).
- A frozen virtual console has no source for its picture other than a kernel cell shadow: panel read-back is superuser only, takes 0.88-1.28 s for the 102,400 visible pixels (11.7-12.5 us a pixel under 66h; 8.6 us under 55h) and exists only on PICOCALC_SD.
- A screenshot (`usr.bin/scrshot`) reads `/dev/lcdgfx` as the superuser on PICOCALC_SD (its Makefile says so) and takes about 1.2 s of wire time for the glass under 66h.

## Not verified

- The wire in section 5 is read from text-layer coordinates, not traced; `TE` unconnected is a label absence.
- That the glass is scanned from RAM is inferred (section 6); the glass height of 320 lines rests on one photograph of pattern 7 and the vendor drivers' 320 x 320 sequence.
- The union size of the `ioctl` payload, the headroom of the kernel RAM region on today's build, and the PSRAM end-to-end rate were not measured. Both kernels measured are built with `-DPSRAM_ENABLED`; whether PSRAM probes at boot on a given board was not checked.
- Spec statements that RAM is unchanged by partial, idle and inversion modes were not found as sentences; the page 248 sentence on simultaneous reads is the only direct support for scan-from-RAM. Waveform figures have no text layer and were not read.
- No board was used. Read-back and write times are the 2026-11-19 board results in `PICOCALC.md`.

## Corrections to the inputs and falsification result

- G1: the row command is RASET (not PASET); RDMEMC is on page 198 (not 199).
- G4: the glyph bank is 896 B (packed), not 1,152 B; libwin exists (`lib/libwin`); the line that 16 KB of PSRAM is not display state is wrong (PSRAM is 8 MiB); the sizes came from objects older than the sources.
- G2: of nine flagged facts, three were already in the port (R104 3.4 kOhm in `hardware-reference-coverage.md`, the 0x21000000 alias in `MPU.md` and `BOOT-MAP.md`, the W25Q 133 MHz rating in `rp2040-clock-configuration.md`), one lacked only its BOM part number (the crystal), and five were absent and are folded in (ALDO range, ICHG, VREG, L101, L401 and L601). The AXP2101 default is resolved, not inconsistent (`picocalc-battery-charging.md`).
- G3: it reported 10 of 30 absent; its categories sum to 31. After untruncated greps, six of its ten are absent and folded (DMA thresholds, SSPDMACR, interrupt masks, the 62.5 Mbit/s figure, the 15-cycle interrupt latency, the instruction cycle table; the latency is stated in the Cortex-M0+ TRM page 3-10, contrary to the report), the DREQ numbers (an item it did not search) are folded, FIFO depth and the prescale range were present without a TRM page, the 1.8432 Mbit/s worked example is an illustration not folded, and the empty-FIFO line behavior has no rule in the text read (only the SPH=0 and SPH=1 frame-signal rules). Its RP2040 datasheet pages are pdf pages, one more than the printed ones. The datasheet's statement that 133 MHz gives 62.5 Mbit/s is arithmetic for 125 MHz (133 / 2 is 66.5).
- Result: the notes repository supplied 5 of 40 facts the port lacked (6 counting the BOM part number); the gororoba library supplied 7 of 31. On the display path the port's documents were already complete: `st7365p-register-reference.md` and `PICOCALC.md` carried the frame RAM size, write and read cycles, sleep behavior, dummy read parameter, 480-line access and board measurements, ahead of the inputs. The defects were stale text (`memory-devices.md` still said "no RAMRD") and a wrong page citation in comments (page 77 for a figure on page 54, which `st7365p-register-reference.md` had listed but the sources still carried); both are corrected in this change set.
