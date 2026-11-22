# Graphics pipeline mathematics: wire budget, copy order, color and dither

This note records ten proposals for the PicoCalc and `i86` display paths, ranked by gain per byte of RAM and flash, and the computations behind them. Evidence labels: [C] a computation the scripts in `scripts/` rerun, [S] a source read at the cited location, [I] inference. The results were computed at `0eeb8636b` (main) and `50f159c5b` (the tip of branch `usr.bin/i86-ems-edges`). The display sources cited below (`lib/libwin`, `usr.bin/matezinho`, `usr.bin/i86/gfx.c`, `st7365p.c`, `lcdgfx.c`, `mpu.c` and `MPU.md`) are identical at `0eeb8636b` and `5d5bfb8ea` (`git diff --stat 0eeb8636b 5d5bfb8ea` over them is empty), and `usr.bin/i86/ems.c` and `mem.c` are identical at `50f159c5b` and the PR #848 merge `3366e0256`. Line numbers cite `0eeb8636b` unless a revision is named.

Not run: octree quantization, run-length coding, any board measurement, passagemath, OCI and gcloud.

## Ranked proposals (gain per byte of RAM and flash)

| # | Proposal | Difference from now | Evidence | Falsifier | Cost | Expected gain |
|---|---|---|---|---|---|---|
| 1 | Select RGB565 (`LCDG_COLMOD` 0x55) in every wire-bound program | Only `usr.bin/i86/gfx.c:251` sets it. `sys/sys/lcdgfx.h` says an open starts in 0x66, so libwin, matezinho and ridge/gametile pay 3 wire bytes a pixel. | [S] grep of `LCDG_COLMOD` over `games`, `usr.bin`, `lib`, `tests` finds one program that sets 0x55 (`i86`; `usr.bin/i86/tests/panel.c` is its test model). [C] 131.1 ms against 196.6 ms for a 320x320 fill (`scripts/c5_out.txt`). [S] `PICOCALC.md` measured 131,000 us and 197,000 us. | A board redraw time that does not drop about 1.5x. Stored R and B low bits stay clear (white reads 62,63,62 of 63, `PICOCALC.md`). | 0 RAM, about 10 B flash per program [I] | 1.5x on every full or partial redraw. A tile goes from 122.9 to 81.9 us (64 px at 1.920 and 1.280 us a pixel, [C] arithmetic of `c5_wire.py`'s constants). |
| 2 | libwin `damage_add`: replace two partially overlapping rectangles by their bounding box when it is cheaper | A diagonal move paints old and new in full. `rect_join` and `rect_trim` handle only axis-aligned cases. | [C] real `lib/libwin/win.c` with a counting backend: a 100x100 move by (5,5) paints 20,000 px, by (5,0) paints 10,500 (`scripts/c6_win_real_out.txt`). The Python port gives 25.63 ms against 14.13 ms with bounding-box merge (`scripts/c6_out.txt`). | Board time of a diagonal windemo drag does not fall. | about 40 B flash [I], 0 RAM | up to 1.8x for small diagonal drags. Large deltas (50,50) gain nothing. |
| 3 | EMS copy-order fix: `alias0()` and `staged()` in `usr.bin/i86/ems.c` (merged as PR #848, merge `3366e0256`) | Before the merge, `move()` at `0eeb8636b` returns 00h and corrupts bytes when two physical pages show one logical page. | [C] the harness below: 256 to 16,384 bytes differ at `0eeb8636b` and 0 at `50f159c5b`. | Any harness case that differs on the merged file. | 0 RAM. `NSTAGE` is 64 page-file pages (`ems.c:76` at `50f159c5b`). | Correctness. |
| 4 | matezinho: per-band x-range next to the 40 band flags | `mz_render` (`core.c:353-363`) redraws a whole 320x8 band, and `write_band` (`main.c:87-93`) sends 5,120 B per dirty band. | [S] those lines. [C] a full band is 3.3 ms at 565, so a 64 px edit (64x8 px) is 0.66 ms. How often edits are small is [I]. | A profile of matezinho redraws that shows most are band-wide. | 160 B RAM, about 100 B flash [I] | up to 5x on small updates. |
| 5 | Make hardware scroll the standard path for vertical scrollers | `LCDG_SCRDEF` and `LCDG_SCROLL` exist and only `usr.bin/paneltest` calls them [S]; no game does. The note records one untested board hypothesis. | [C] a 4-line scroll costs 1.6 ms of new rows, against 131 ms for a full redraw. [S] VSCRDEF p. 183 and VSCSAD p. 190 of the ST7365P datasheet (`docs/research/st7365p-register-reference.md`). | A board scroll that tears or loses lines. | 0 RAM, no new kernel code | up to 80x for vertical scrollers. |
| 6 | Put wire-budget constants in the game ABI | Callers pick strips, tiles and viewports blind. | [C] the viewport table in section 4. | A documentation change cannot fail a measurement; the constants follow from item 1 and the viewport table. | 0 | Frame-rate planning. |
| 7 | Packed RGB565 blend with the 0x07E0F81F spread | Absent today. | [C] exhaustive: 0 mismatches over alpha 0, 1, 8, 16, 31, 32 (`scripts/c2_out.txt`, section E). | Any mismatch. | about 20 B code [I] | 3 channels per multiply for translucent sprites. |
| 8 | Fixed 16-color palette with error diffusion, or an adaptive palette with no dither or a light Bayer spread | Offline asset policy. | [C] the sweep in section 2, two photos. | A different image set reversing the order. | about 2 KB process RAM for line buffers [I] | +11 to +17 dB blurred PSNR on RGBI. |
| 9 | Pair-table (1 KB) or interpolator palette expansion | Reject for now. | [C] expansion costs 4.4 to 18 percent of wire time (cycle counts are [I], section 2). | Measurement 2 of `docs/research/i86-core1-scanout.md` showing more than about 100 cycles a pixel. | 1 KB RAM, which the kernel lacks | 0 throughput. It frees CPU time only. |
| 10 | QAOA or Grover for palette selection | Reject. | [C] section 6. | None found. | n/a | 0 |

Items 1, 2, 4, 5 and 6 call for one design note, not yet written, on the panel wire budget, the COLMOD policy, the game-ABI constants and cost-aware damage merging. Item 3's dependency-graph statement of copy hazards belongs with the D203 EMS work it explains. The other items stay in this record.

## Reproducing the figures

Every command runs from the repository root. Tools: Sage (`sage`), Singular, PARI/GP (`gp`), Python 3 with numpy, scipy, scikit-learn, Pillow and qiskit 2.6.2, a host C compiler, and `arm-none-eabi-gcc` for the Thumb-1 listing. The Python scripts open `sys/arch/rp2040/dev/st7365p.c` by that relative path (the console palette) and read only the sample photographs: `c7_quant_qaoa.py` and `c7b_bayer_sweep.py` use scikit-learn's bundled sample photographs `china.jpg` and `flower.jpg`, so the image set is the library's own and `c7b_bayer_sweep.py` takes the sample name as its argument.

The Python rows run under the interpreter the caller selects, as every Python tool of the repository does:

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON

| Script | Run | Saved output |
|---|---|---|
| `scripts/c1_copy_order.py` | `sage docs/research/graphics-pipeline/scripts/c1_copy_order.py` | `c1_out.txt` |
| `scripts/c1b_permuted_windows.py` | `sage docs/research/graphics-pipeline/scripts/c1b_permuted_windows.py` | `c1b_out.txt` |
| `scripts/c2_color_dither.py` | `sage docs/research/graphics-pipeline/scripts/c2_color_dither.py` | `c2_out.txt` |
| `scripts/c3.sing`, `scripts/t.sing` | `Singular -q docs/research/graphics-pipeline/scripts/c3.sing` and `Singular -q docs/research/graphics-pipeline/scripts/t.sing` | `c3_sing_out.txt`, `t_sing_out.txt` |
| `scripts/c3.gp` | `gp -q docs/research/graphics-pipeline/scripts/c3.gp` | `c3_gp_out.txt` |
| `scripts/c5_wire.py` | `"$PYTHON" -I docs/research/graphics-pipeline/scripts/c5_wire.py` | `c5_out.txt` |
| `scripts/c5_loops.c` | `arm-none-eabi-gcc -Os -mcpu=cortex-m0plus -mthumb -S docs/research/graphics-pipeline/scripts/c5_loops.c` | sizes below |
| `scripts/c6_damage.py` | `"$PYTHON" -I docs/research/graphics-pipeline/scripts/c6_damage.py` | `c6_out.txt` |
| `scripts/c6_win_real.c` | see below | `c6_win_real_out.txt` |
| `scripts/c7_quant_qaoa.py` | `"$PYTHON" -I docs/research/graphics-pipeline/scripts/c7_quant_qaoa.py` | `c7_out.txt` |
| `scripts/c7b_bayer_sweep.py` | `"$PYTHON" -I docs/research/graphics-pipeline/scripts/c7b_bayer_sweep.py china.jpg` (and `flower.jpg`) | `c7b_china_out.txt`, `c7b_flower_out.txt` |
| `scripts/c8_sprite_wire.py` | `"$PYTHON" -I docs/research/graphics-pipeline/scripts/c8_sprite_wire.py` | `c8_out.txt` |
| `scripts/f1/f1_harness.c`, `scripts/f1/f1b_harness.c` | see below | `f1/f1_out.txt`, `f1/f1_tip.txt`, `f1/f1b_head.txt`, `f1/f1b_tip.txt` |

Timings printed by `c7_quant_qaoa.py` vary from run to run, and the COBYLA starts of the p=2 QAOA row move its fourth digit (0.0177 saved, 0.0176 on a rerun). The saved outputs of `c1b`, `c3`, `c5`, `c7b`, `t.sing` and `f1_tip` are the captures taken when this record was filed. `c1_out.txt` holds `c1_copy_order.py` alone.

The libwin counting harness links the real `lib/libwin/win.c` against a one-line font stub, building into a scratch directory `T` outside the repository:

    T=$(mktemp -d)
    cc -std=c17 -Wall -Wextra -Werror -Ilib/libwin -Isys/arch/rp2040/dev \
        docs/research/graphics-pipeline/scripts/c6_win_real.c \
        docs/research/graphics-pipeline/scripts/c6_lcdfont_stub.c \
        lib/libwin/win.c -o "$T/c6_win_real" && "$T/c6_win_real"

The EMS harnesses `#include` `ems.c` so that its statics are reachable, and link the revision's `mem.c` with a one-function shim for `unlinkat_beneath`. `T` is the scratch directory of the libwin block, and `argv[1]` names the page-store file:

    mkdir "$T/head" "$T/tip"
    git archive 0eeb8636b usr.bin/i86 | tar -x -C "$T/head"
    git archive 50f159c5b usr.bin/i86 | tar -x -C "$T/tip"
    for r in head tip; do
        cc -std=c17 -Wall -Wextra -Werror \
            -include docs/research/graphics-pipeline/scripts/f1/shim.h \
            -I"$T/$r/usr.bin/i86" \
            docs/research/graphics-pipeline/scripts/f1/f1b_harness.c \
            "$T/$r/usr.bin/i86/mem.c" \
            docs/research/graphics-pipeline/scripts/f1/shim.c -o "$T/f1b_$r"
        "$T/f1b_$r" "$T/store.bin"
    done

Replace `f1b_harness.c` by `f1_harness.c` for the earlier, smaller case set. The `head` run reproduces `f1/f1b_head.txt` and the `tip` run `f1/f1b_tip.txt`.

The Thumb-1 sizes of `c5_loops.c` at `-Os`, from `arm-none-eabi-gcc -Os -mcpu=cortex-m0plus -mthumb -c -o c5_loops.o docs/research/graphics-pipeline/scripts/c5_loops.c` and then `arm-none-eabi-nm -S c5_loops.o`: `expand_a` 28 B, `expand_b` 24 B, `expand_c` 54 B [C].

## 1. Aliasing and byte sharing

**Definition.** Let B be the backing store of N bytes. A window is an injective map from addresses to B, and its image is an idempotent diagonal matrix e. Two windows are independent when e_A·e_B = 0, a zero-divisor pair in the ring F^N. The shared size is trace(e_A·e_B). A copy is a partial injective map with 0/1 matrix C. Executing in place equals the buffered result exactly when the order is a linear extension of the graph with an edge u to t whenever u reads the cell t writes.

**Results [C] (`scripts/c1_copy_order.py`, `scripts/c1_out.txt`).**
- `ems.c` `alias()` (404-424 at `0eeb8636b`) agrees with byte-set intersection on 359,490 random page maps: 13,931 overlapping cases, 0 disagreements.
- For a shift copy of L bytes by d, the graph is a union of paths. The count of valid orders equals the multinomial L!/∏(chain lengths)! (210 for L=7, d=3). C is nilpotent with index ceil(L/d)+1. C²=0 exactly when source and destination are disjoint.
- A rotation of an n-ring by d has gcd(n,d) cycles of length n/gcd and admits no valid order. One temporary byte, processed cycle by cycle, reproduces the buffered result.
- An overlapped exchange of two ranges is ill-defined: 4 to 8 distinct results over all orders. Status 97h on the merged file is therefore correct.
- The one-byte copy matrix T = I + e_d(e_s − e_d)^T is idempotent (20 of 20). T and I−T are a zero-divisor pair. Two such matrices commute iff s1≠d2, s2≠d1 and (d1≠d2 or s1=s2), with 0 mismatches over 400 pairs.
- A move between windows whose pages are permuted (A shows pages 0,1; B shows 1,0) is a page swap (`scripts/c1b_permuted_windows.py`). Every chunk size below the region fails in either direction, so a chunked copy needs a buffer as large as the whole region. With cycle-leader exchange, one temporary byte is enough, since all four cycles have length 2.

**Defect fixed by PR #848 [C, real code].** At `0eeb8636b`, `move()` sets `back` only inside the conventional-address overlap test, so two windows on one logical page choose the wrong direction. `alias0()` and `staged()` exist on `usr.bin/i86-ems-edges` (located by `git log --all --oneline -G'alias0|staged\(' -- usr.bin/i86`) and on `origin/main` through the merge `3366e0256`. The harness builds `ems.c` at both revisions:

| Case (chunk 256 B) | 0eeb8636b | 50f159c5b |
|---|---|---|
| same window, +256 and −256, len 512 | 0 differ | 0 |
| cross windows, storage dst +256, len 512 | 256 differ, status 00h | 0 |
| cross windows, +256, reversed windows | 256 differ | 0 |
| cross windows, +128, len 768 | 256 differ | 0 |
| cross windows, −256, len 512 | 0 | 0 |
| cross windows, +256, len 256 (one chunk) | 0 | 0 |
| exchange (57h al=1), +256 | 768 differ | refused, 97h |
| page swap A to B, 32 KB | 16,384 differ | 0 |
| page 0 over page 1, 16 KB | 0 | 0 |

"Correct" means the buffered result, the criterion the branch commits adopt [I]. The 57h rule text was not read for this record. The refused exchange at `50f159c5b` reports 512 differing bytes in `f1/f1_tip.txt` because the harness compares against a buffered copy and the refusal leaves memory unchanged; the status 97h is the result. A guest reaches the case because `map()` and `show()` leave a logical page mapped at another physical page.

**Other structures.**
- [C] Segment:offset (16·s + o mod 2^20) gives every linear byte exactly 4,096 names (`scripts/c3.gp`). The kernel lattice is generated by (1, −16). `gfx.c` keys its dirty bits by linear page, so this aliasing is harmless there.
- [C] Z/2^k is a local ring: its only idempotents are 0 and 1, and every even element is nilpotent. In Z/2^32, z = 2^8 has index 4. SWAR lane isolation is therefore an inequality (guard bits), not a ring splitting.
- [C] Singular computed annihilators in (Z/256)[x]/(x^8): ann(x³) = (x⁵) and ann(2) = (128) (`scripts/c3.sing`, `scripts/t.sing`). These confirm window truncation and byte wrap and match what Sage already gives. The F2[x]/(x^n−1) factor counts in `c3.sing` do not bear on the proposals.

## 2. Constrained computing for graphics

- **Interpolator.** A lane computes (A >> s) & mask, so each lane is a shift followed by an idempotent projection. The lane model reproduces every datasheet example [C]: masks, signed masks, blend 500 to 998, clamp 0 to 255 (RP2040 datasheet pp. 34-38; `scripts/c2_out.txt`, section G). A lane with shift 4k−1 and mask bits 1..4, plus BASE0, yields table + 2·nibble_k for k=1..7 [C]. BLEND exists on INTERP0 only and CLAMP on INTERP1 only (CTRL_LANE0 tables, around p. 54) [S]. The divider takes 8 cycles (p. 31) [S]. MPU region 3 grants only SIO 0x060-0x07f, so the divider is user-reachable and the interpolators are closed (`sys/arch/rp2040/doc/MPU.md:79`, `sys/arch/rp2040/rp2040/mpu.c:53`) [S]. Interpolator work is kernel or core-1 only unless the map changes and per-process state is added.
- **Packed lerp** cannot use the interpolator: blend shifts the whole product by 8, so fields need about 22 bits of spacing [I]. The SWAR spread is exact for alpha ≤ 32. Alpha 33 overflows green (63·33 = 2079 ≥ 2048) [C].
- **Expansion cost, from `-Os` Thumb-1 output.** The sizes are [C] (`scripts/c5_loops.c`). The cycle counts are [I]: they apply the Cortex-M0+ table (ALU 1, load/store 2, taken branch 2), which was not re-read against the TRM. The wire budget is 160 cycles a pixel at 565 [C]. A recount of the `expand_c` listing gives 27 cycles with no bus wait, against the 29 recorded here as a floor ("or more"); the share is 17 to 18 percent either way.

| Loop | Size | Cycles per pixel | Share of wire time |
|---|---|---|---|
| per-pixel palette to a bounce buffer | 28 B | 15 | 9% |
| 256-entry pair table (14 per pair) | 24 B | 7 | 4% |
| current style, polled FIFO, no wait | 54 B | 29 or more | 18% |

- **Dither.**
  - The Bayer matrix is M_{2n} = 4·(M_n⊗J₂) + J_n⊗[[0,2],[3,1]]. In closed form M(r,c) = Σ 4^k(2(r_k⊕c_k) + r_k), a bit interleave of r⊕c with r (m=1..4 verified). Level l lights exactly l pixels of a 4x4 cell. The 8x8 matrix has 28 of 63 nonzero non-DC DFT coefficients, against 62-63 for random permutations [C].
  - Sweep, PSNR in dB of the 5x5-blurred result against the blurred original, on two sample photos (a proxy metric) [C] (`scripts/c7b_china_out.txt`, `scripts/c7b_flower_out.txt`):

    | Palette | No dither | Bayer, best spread | Floyd-Steinberg |
    |---|---|---|---|
    | console RGBI (china / flower) | 21.55 / 15.55 | 24.57 / 19.18 (still rising at spread 96) | 34.95 / 32.97 |
    | k-means (china / flower) | 32.41 / 32.91 | 33.03 / 33.28 | 32.00 / 33.26 |
    | median cut (china / flower) | 30.00 / 29.92 | 30.38 / 30.34 | 27.23 / 29.42 |

  - A fixed palette needs error diffusion. An adaptive palette takes plain nearest-color mapping or a spread of about 16 to 24.
- **Color counts [C].** Mixing k pixels from the 8 RGB corners gives (k+1)³ distinct means, so k=9 gives 1,000 and k=15 gives 4,096. The console's 16 colors give all 136 means at k=2 and all 3,876 at k=4 (the table before the color-vision palette gave 131 and 3,233).
- **Wire floor [C].** Window setup (11 B) costs 7.04 us, or 5.5 px at 565. A gap shorter than 5.5 px costs less to send than a new window.
- **Voxel and 2.5D.** A column renderer needs only a few ms of CPU per frame [I]. The wire sets the frame rate, as the table in section 4 shows.

## 3. EGA, CGA, VGA and the books

**Mechanisms with citations.**
- Latch copy (write mode 1): one MOV moves a byte of all four planes through the 32-bit latch (Ferraro, section 2.3.2). Wilton describes the latches and the Data Rotate/Function Select register in chapter 5, Pixel Programming (p. 119); Kliewer moves windows through the latch registers with write mode 1 (p. 84); Sutty gives a bit-blit routine (p. 202).
- Chain 4 (address bits 0 and 1 select the plane, mode 13h): Sutty, Sequencer index 4, the Memory Mode register (p. 52).
- Odd/even addressing and CGA emulation: Kliewer, The Sequencer Registers (p. 151).
- CGA interleave: the bank formula 8192·(y mod 2) + 80·⌊y/2⌋ + ⌊x/4⌋ addresses 16,000 bytes with 384 unused [C] (`scripts/c3.gp`). `gfx.c:385-440` emulates it with two banks [S].
- CGA composite colors (VileR, pp. 6-14): one 4-hdot pattern is a 4-point DFT, X1 = (p0−p2) − i(p1−p3). Delaying a pattern by one hdot multiplies X1 by −i, a 90 degree hue step. The 16 patterns fall into 15 (luma, chroma) classes. The patterns 0101 and 1010 coincide in this model, while VileR p. 8 counts 16 solid colors, so the model is incomplete [C, I] (`scripts/c2_out.txt`, section C). The 1,024 colors are 4 character waveforms times 16 foreground times 16 background (pp. 11-14).
- **The ST7365P lacks a latch copy.** The datasheet omits any RAM-to-RAM command (`docs/research/display-memory-path.md`, section 6). Its analogue is scrolling plus the 160 off-screen lines. COLMOD offers only 16, 18 and 24 bits a pixel (p. 157), so a pixel never costs less than 16 wire bits. IDMON shows 8 colors on the glass (p. 193) but saves no wire bytes. Page numbers are those of the Sitronix ST7365P datasheet V1.0 (2023/02) as transcribed in `docs/research/st7365p-register-reference.md`.

**References.** Page numbers are the printed pages of these editions. Richard F. Ferraro, Programmer's Guide to the EGA, VGA and Super VGA Cards, 3rd ed. Richard Wilton, Programmer's Guide to PC and PS/2 Video Systems (1987). Bradley Dyck Kliewer, EGA/VGA: A Programmer's Reference Guide, 2nd ed. (1990). George Sutty and Steve Blair, Advanced Programmer's Guide to SuperVGAs (1990). VileR, CGA in 1024 Colors. Michael Abrash's Graphics Programming Black Book, and with it the Mode X material, is outside this record.

## 4. The display pipeline as a dataflow model

The ST7365P omits a write FIFO and a line buffer (datasheet p. 40, block diagram) [S]. Its GRAM (345,600 B) is the only frame cache, so a differential pipeline sends only changed pixels.

| Stage | Rate | Class |
|---|---|---|
| guest page-dirty bits, `line_expand` | CPU, a few cycles a pixel | [S][I] |
| `write(2)` copy in 64 B chunks | CPU | [S] |
| palette expansion, `put_color` | 15 to 29+ cycles a pixel | [C][I] |
| PL022 FIFO, 8 entries of 8-bit frames, polled on TNF (`st7365p.c:198-203`) | 8 B drained per 5.12 us | [S] |
| SPI at 12.5 MHz | 0.78 Mpx/s at 565, 0.52 at 666 | [C] |
| GRAM write cycle 66 ns (ceiling 15.15 MHz, 1.21x headroom) | not limiting | [S][C] |
| glass scan from GRAM | 57.9 Hz | [S] |

The serializer is the bottleneck under every option, because the CPU stages are 5 to 10 times faster [I]. The model's 131,079 us agrees with the 131,000 us measurement.
- **Today:** the CPU is saturated, mostly spinning on a full FIFO.
- **D198, core-0 DMA:** the same wire time with the spin removed. Expansion remains at 4 to 18 percent of the wire time.
- **D202, core-1 scan-out:** the same wire time with expansion also removed. The interpolator is not needed for 16 palette entries.
- Changing the wire time requires fewer bytes: COLMOD 55h, dirty rectangles, hardware scroll, or a smaller viewport.

Measurement 2 of `docs/research/i86-core1-scanout.md` decides D198 against D202. Falsifier for this model: expansion at 100 cycles or more a pixel.

| Viewport | 565 fps | 666 fps |
|---|---|---|
| 320x320 | 7.6 | 5.1 |
| 320x240 | 10.2 | 6.8 |
| 256x192 | 15.9 | 10.6 |
| 240x160 | 20.3 | 13.6 |
| 192x144 | 28.3 | 18.8 |
| 160x160 | 30.5 | 20.3 |

At 30 fps and 565 the budget is 375 to 407 of the 1,600 tiles. Writing is 7.6 times slower than the beam, so the beam laps the writer. A full-width write that starts on the row the beam has just left stays behind the scan for x = 320 v_w / (v_b - v_w) rows, with v_b = 320 rows a 17.3 ms frame (57.9 Hz) and v_w = 320 rows in 131.1 ms: about 48 rows, 19.9 ms [I, arithmetic of those two figures]. A narrower window advances its rows faster in proportion to 320 over its width, so v_w grows and the beam laps it later. `LCDG_WAITLINE` sets only the starting phase, so a tear-free update waits for the scan line again before each band of at most about 48 full-width rows; a longer update without those waits tears where the beam passes it. A w=1 `LCDG_WINDOW` fills downward, so column strips work without MADCTL MV [S][I]: the `tile_row` comment (`lcdgfx.c:405-410`) states that a window fills left to right, top to bottom, and the width-1 case follows. Per-window setup overhead is 0.1 to 1.7 percent for strips of 1 to 16 rows [C] (`scripts/c5_out.txt`).

## 5. Callers and what a game ABI could standardize

| Caller | Draws with | Notes |
|---|---|---|
| libwin | 16-row index strips (`WIN_STRIP_BYTES` 5,120), `LCDG_WIN_INDEX` or 1-bpp bits, 8 damage rectangles | scene is 5,712 B |
| matezinho | its own renderer, 40 bands, full-width RGB565 windows | does not use libwin |
| i86 | `gfx.c` windows of palette indexes | sets COLMOD 55h |
| ridge | gametile 40x40 shadow map, 128-tile atlas, sprites composited into atlas slots (`RG_BOX` 3, `RG_DYN` 64) | tile run costs 123 us at 666 |

A native game ABI could standardize, from what these callers do now:
1. COLMOD policy per open (item 1).
2. Wire-cost constants: bytes a pixel, 11 B window setup, merge break-even, viewport table.
3. One dirty algebra: gametile's tile bitmap and libwin's rectangle list, with cost-aware merging.
4. Sprite runs and tile runs, already present as `LCDG_SPRITE` and `LCDG_TILEWIN`.
5. The scroll ring for vertical scrollers.
6. A frame fence: `LCDG_WAITLINE` exists and sets the starting phase; the fence waits again before each band of at most about 48 full-width rows (section 4).

## 6. Quantum and novel algorithms (qiskit 2.6.2, simulation only)

Maximum-dispersion selection of 4 of 12 candidate colors is a real QUBO, solved with a numpy statevector and cross-checked against a qiskit `Statevector` of the same circuit (max |ΔP| 1.98e-16, 192 gates; a rerun gives 1.11e-16). Classical methods dominate [C] (`scripts/c7_quant_qaoa.py`, `scripts/c7_out.txt`).

| Method | Result |
|---|---|
| exhaustive, 495 subsets | optimum |
| greedy, 0.04 ms | optimum |
| annealing, 3,000 flips, 2.7 ms | optimum |
| QAOA p=1 | 0.0009 on the optimum, against 0.00024 uniform |
| QAOA p=2 | 0.0177 |
| QAOA p=3 | 0.0759 (COBYLA with 10 starts, not exhaustive) |
| Grover | about 17 oracle calls against 495 trivial evaluations |

The dispersion proxy is poor: its optimum ranks 363 of 495 by true image error. The exact p-median objective is not quadratic. The 4-point DFT of the CGA model is a 2-qubit QFT, an advantage of zero.

## 7. Sprites: box compositing, LCDG_SPRITE and opaque cells

Ridge draws a moving sprite by compositing the 3 by 3 cells of its box into atlas slots and pointing every box cell at a slot (`rg_composite`, `games/ridge/ridge.c`), so a step sends the union of the old and new boxes: 9 tiles inside a cell, 12 across a cell edge, as `games/ridge/tests/render_test.c` checks. `scripts/c8_sprite_wire.py` reads the sprite art (`rg_art`) and prices three methods in RGB565 wire bytes, 2 a pixel and 11 a window, averaged over the 64 positions of the sprite inside a cell [C] (`scripts/c8_out.txt`, computed at `e32078513`). The model reads `RG_BOX` from `ridge.c` and the windows and tiles of the one-pixel step and the cell-edge step from `render_test.c`, and exits before printing when its box geometry does not reproduce them; a copy of `ridge.c` with `RG_BOX` 2 and a copy of `render_test.c` expecting 10 tiles both stop it.

| Sprite, move | Box atlas (now) | Opaque-cell atlas | `LCDG_SPRITE`, unfenced |
|---|---:|---:|---:|
| climber running, 1 px | 1,233 | 787 | 1,167 |
| climber running, 1 px, frame 1 to 2 | 1,233 | 815 | 1,169 |
| climber running, fall 4 px | 1,382 | 894 | 1,167 |
| climber jumping, 1 px | 1,233 | 843 | 1,235 |
| climber jumping, fall 4 px | 1,382 | 966 | 1,235 |
| crawler, 1 px | 1,233 | 619 | 880 |
| crawler, 1 px, frame 4 to 5 | 1,233 | 619 | 878 |
| crawler, fall 4 px | 1,382 | 754 | 880 |

The running and crawling frames alternate (`rg_sprites`), so the model also prices steps that change the frame, with the old frame's cells before the step and the new frame's pixels after it; `c8_out.txt` holds every row.

- `LCDG_SPRITE` repaints the cells under the old sprite with their world tiles and then sends the new sprite's opaque runs, one window a run. It saves up to 36 percent against the box atlas, and nothing on the jumping climber's 1 px step (1,235 bytes against 1,233). The repaint shows the world without the sprite until the sprite's runs arrive, so it needs an `LCDG_WAITLINE` fence to avoid flicker [I]. The column counts drawing bytes alone: the fence polls GSCAN with four bytes a poll until the scan reaches its span, a count the scan phase decides, so the fenced cost is higher by an amount the model leaves out.
- The opaque-cell atlas composites only the cells that hold a pixel of the old or the new sprite and leaves the other box cells on their world tiles, which already show the right pixels. It saves 30 to 50 percent against the box atlas and 14 to 33 percent against `LCDG_SPRITE`, writes every cell once with its final pixels, and changes `ridge.c` alone.
- Limits: one sprite at a time. Overlapping sprites share slots in the box atlas and would need a redraw order under `LCDG_SPRITE`. The atlas methods also copy 32 bytes a tile into the kernel with `LCDG_TILESET`, which costs a system call and CPU time but no wire bytes. Board timing was not measured.
- Falsifier: a board run of ridge in which the opaque-cell atlas does not cut the bus time of a sprite step against the box atlas.

## Open questions

How often matezinho redraws are small (item 4) is unmeasured, and whether the Bayer and Floyd-Steinberg ranking holds on other images is untested.
