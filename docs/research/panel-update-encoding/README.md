# Reproducing the panel-update-encoding tables

This directory holds the scripts, the counts and the instrumentation behind
`docs/research/panel-update-encoding.md`. The scripts read two recorded
i86 sessions and compute how many bytes each panel-update method sends. They
reproduce the wire table, the stream table, the kernel counts, the scroll
replay, the 320x240 figures and the Cortex-M0+ decoder cycle counts of that note.

The scripts need Python 3 with NumPy, a C compiler, and, for the cycle counts,
`arm-none-eabi-gcc`. Python runs as `python3 -I`. Each script finds the
other modules through its own location, so run it from any directory.

## Corpora

A corpus is four files per session, named `dos` and `win`:

- `<session>.dump` holds one record per refresh that changed something. A record is
  1 byte of kind (`T` text or `G` graphics), 1 byte of columns or bytes a line (80),
  the guest data, then the model's panel frame. Text data is 4000 bytes of cells
  (character and attribute, 80x25). Graphics data is 8000 bytes of the even-line
  bank, then 8000 bytes of the odd-line bank (CGA layout, 80 bytes a line). The
  panel frame is 320x320 little-endian RGB565, row-major, 204,800 bytes. The guest's
  200 lines sit at panel lines 60 to 259.
- `<session>.log` holds one line per refresh: kind, pixels sent, fill pixels,
  changed units, span-exact pixels, best shift-compensated pixels, best shift.

The DOS session is SvarDOS at 80 columns. The Windows session is Windows 3.00a in CGA
mode 6 (640x200, 1 bit a pixel), booted to Program Manager. The Windows disk is
owner-supplied and not redistributable, so neither dump is in the tree. Only the counts
derived from them are (`data/`).

SHA-256 of the dumps the tables came from:

    137dfd382692365b85bec174fd78ea5f68e460f03f1d3a8c7afbb2bf4e9a8280  dos.dump
    ad2ab60c19eb66211f3a6762e1cef17da9e0e3418265aee92c293c61d5bebccf  win.dump

## Making the corpora

`measure-hooks.patch` adds the instrumentation: a `measure()` hook in
`usr.bin/i86/gfx.c` that logs each refresh and dumps the guest memory and the
panel frame, and a frame dump in `usr.bin/i86/tests/panel.c`, the host model of
the panel. It applies to `usr.bin/i86` at commit `e45e5c3c6`. In a scratch
checkout of that commit:

    patch -p1 < docs/research/panel-update-encoding/measure-hooks.patch
    cd usr.bin/i86
    cc -std=gnu17 -D_DEFAULT_SOURCE -DHOSTBUILD -O1 -I. -idirafter ../../sys \
        -DGFX_MODEL -o i86-meas main.c pc.c gfx.c serial.c ems.c cpu.c mem.c \
        clock.c tests/panel.c

Run the binary with `I86_MLOG` naming the log and `I86_MDUMP` the dump, the
keys on `-k`, and the options `-g -T -c 286 -e 2048 -a blank.img -h <disk>`
(`<disk>` is the SvarDOS hard-disk image with Windows installed;
`blank.img` is an empty floppy image).

- DOS: the keys `dir c:\windows<CR>mem<CR>type c:\autoexec.bat<CR>dir c:\ /w<CR>dir c:\windows\system<CR>ver<CR>`.
- Windows: the keys `c:\windows\win<CR>`, then 60 spaces, 10 escapes, 60 spaces,
  with a 150 s timeout.

`<CR>` is a carriage return. The refresh count depends on host timing, so a new
recording differs from the hashed dumps above in detail.

## Commands, in order

    CORPUS=<directory with dos.dump dos.log win.dump win.log>
    OUT=<scratch directory for derived files>
    HERE=docs/research/panel-update-encoding

    cc -O2 -Wall -Wextra -Werror -shared -fPIC -o "$OUT/librectopt.so" $HERE/rectopt.c
    python3 -I $HERE/test_rect.py   --out "$OUT"
    python3 -I $HERE/calib_D.py     --corpus "$CORPUS" --out "$OUT"
    python3 -I $HERE/analysis_a.py  --corpus "$CORPUS" --out "$OUT"
    python3 -I $HERE/analysis_b.py  --corpus "$CORPUS" --out "$OUT"
    python3 -I $HERE/analysis_m2b.py --corpus "$CORPUS" --out "$OUT"
    python3 -I $HERE/analysis_c.py  --corpus "$CORPUS" --out "$OUT"
    python3 -I $HERE/analysis_v2.py --corpus "$CORPUS" --out "$OUT"
    python3 -I $HERE/analysis_240.py --corpus "$CORPUS" --out "$OUT"
    arm-none-eabi-gcc -Os -mcpu=cortex-m0plus -mthumb -Wall -Wextra -Werror -S \
        -o "$OUT/dec_kernels.s" $HERE/dec_kernels.c
    python3 -I $HERE/cyc.py --asm "$OUT/dec_kernels.s"
    python3 -I $HERE/summarize.py   --corpus "$CORPUS" --out "$OUT"

`--font-header` defaults to `usr.bin/i86/font.h` of the same tree. Everything up
to `analysis_c.py` takes under a minute; `analysis_v2.py` takes about 5 minutes and
`analysis_240.py` about 7 (they are independent, so run them side by side). The
last step writes `per_refresh_<session>.tsv`, `summary_wire.tsv`,
`summary_streams.tsv` and `summary_misc.tsv` to `$OUT`; `data/` holds a copy of
the files from the run behind the note, next to `stream_per_refresh_<session>.tsv`.

## What each script computes

- `rectopt.c` is the rectangle cover. A damage mask is covered by stripes: bands of
  rows, inside each band clusters of columns, each cluster one window, priced by
  `cost(w, h) = 2wh + 11 * ceil(h / floor(5120 / w))`. The cost is minimal inside that
  family (a dynamic program over bands and clusters, rows and columns both tried), not a
  global optimum. Row cuts forbid a window across a boundary (the scroll wrap seam).
- `panellib.py` loads the corpora, evaluates D (font render of text cells, pair sums of
  mode 6 bits, the adapter palette), wraps the cover and reproduces today's redraw rules
  (cell runs of one attribute in text, 1 KB write-watch pages of 80-byte lines in
  graphics).
- `test_rect.py` calibrates the cover: known costs, gap bridging, cuts, and on random
  masks that the rectangle list covers every damaged pixel and sums to the program's cost.
- `calib_D.py` checks D against the recorded frames: text must match the font render with
  at most one cursor cell, graphics must be a function of the pair-sum level. It writes the
  cursor cell of each text refresh.
- `analysis_a.py` computes methods 1 to 3 for each refresh: today's bytes
  (`2 * (sent + fill)`), guest-space span rectangles, panel-space rectangles.
- `analysis_b.py` adds today's window headers (a simulation of `gfx.c`, checked against
  the logged pixel counts), the kernel of D (changed guest cells and bytes that leave the
  panel unchanged, pairs 01 and 10 that swap) and the mixed pairs a 2:1 halving merges.
- `analysis_m2b.py` recomputes the log's span definition (first to last changed byte on a
  line) and lists the refreshes whose panel area is black while the guest memory is not.
- `analysis_v2.py` is method 4. It tries shift 0 and the best-matching shifts of the
  200-line area on every refresh, prices VSCSAD (3 bytes) and the first VSCRDEF (7), cuts
  windows at the wrap seam, and replays the choice on a controller model that must
  reproduce every recorded frame. A wrong shift and a missing seam cut must fail.
- `analysis_240.py` repeats methods 3 and 4 on 240 lines, each guest line repeated so
  that 5 lines become 6.
- `analysis_c.py` sizes the streams that do not cross the wire, with decoders (see its
  docstring). The `xz` column is `xz` at preset 9e on the whole z3 stream; it is a
  reference, not a bound.
- `dec_kernels.c` and `cyc.py` give the decoder cycle counts. `cyc.py` adds the
  Cortex-M0+ cycles of each loop in the compiler's assembly, leaving out the function
  epilogue. For a nested loop, an outer iteration costs outer - inner + n * inner (the
  `bits1` byte: 8 pixel iterations of 11 to 12 cycles inside a 22 to 24 cycle outer pass,
  about 12.5 cycles a pixel). The literal-copy loop of `tokdec` is the six instructions
  at `.L56` and `.L57` of the listing (`cmp`, `bne`, `ldrb`, `strb`, `adds`, `b`):
  10 cycles a byte; `cyc.py` prints a larger enclosing span for that function. The
  figures assume zero-wait SRAM and no bus contention; they are estimates, and a counted
  run on the board would test them.
- `summarize.py` joins the outputs into the tables of `data/`.

## Expected values

Wire bytes (method 1 is the logged pixel bytes; methods 2 to 5 include 11-byte windows):

| Method | DOS | Windows |
| --- | --- | --- |
| 1 today | 2,430,336 | 3,361,088 |
| 2 guest-space span rectangles | 2,494,198 | 1,405,344 |
| 3 panel-space rectangles | 1,414,148 | 447,553 |
| 4 plus best vertical scroll | 442,045 | 447,553 |
| 5 clear dropped (panel already black) | 237,234 | 242,742 |

Stream bytes a session:

| Encoding | DOS | Windows |
| --- | --- | --- |
| today's write(2) | 236,204 | 1,306,116 |
| panel-space rectangles, packed | 501,165 | 31,059 |
| guest-state delta, skip and literal | 79,318 | 32,110 |
| plus row-copy operations | 11,747 | 28,676 |
| plus per-line predictors | 11,747 | 6,796 |
| `xz -9e` of the last stream | 3,324 | 4,172 |

`summary_misc.tsv` carries the remaining counts the note quotes (scroll refreshes,
kernel counts, mixed pairs, 320x240 figures, replay results).

## Limits

- Both corpora come from the host panel model, not a board. The scroll gain (972,103 bytes
  on DOS) rests on the controller applying VSCRDEF 60/200/220 and VSCSAD as the datasheet
  describes; a board read-back of the scroll area after a shift would test it.
- Both sessions are CGA mode 6 or text. No mode 4 or 5 session exists, so four-color
  figures would be analytic.
- `data/` holds counts only. The stream bytes the encoders build derive from the guest
  memory and are not written to disk.
