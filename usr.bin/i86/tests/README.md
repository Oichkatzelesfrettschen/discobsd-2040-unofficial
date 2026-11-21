# i86 tests

| File | Role |
| --- | --- |
| `check.py` | The host gate `bmake -C usr.bin/i86 test` runs: the guest program on every processor model and page-frame count, the terminal replay, the processor vectors of the sample, then the mutants. |
| `guest.asm`, `guest.hexdump` | The guest program and its assembly by `nasm -f bin`, 32 bytes a line. `check.py` compares the two when nasm is installed. |
| `ems.asm`, `ems.hexdump` | The guest of the expanded memory test: it calls interrupt 67h, records one line for each result, and writes the lines to sectors 1 and 2 of head 1 of its own disk, where `check.py` reads them after runs with eight and 64 resident pages and, in the panel display test, with a graphics mode borrowing frames. `../dos/ems.asm` is the DOS driver EMS.SYS, checked against `../dos/ems.hexdump` the same way. |
| `mouse.asm`, `mouse.hexdump` | The guest of the mouse test: it drives COM1 as a serial mouse driver does and prints each register it reads and each report and key it receives. |
| `panel.py` | The panel display test `check.py` runs on the emulator built with `panel.c`: the frame each stage must leave, computed from the adapter's rules, and its mutants. |
| `panel.c` | A model of `/dev/lcdgfx` that writes its frame and counters at every close. |
| `panel.asm`, `panel.hexdump` | The guest of the panel display test and its assembly. |
| `sstrun.c` | Executes single instructions on `cpu.c` for `check.py` and `sst.py`. |
| `sst8088-sample.txt.gz` | 4,485 single-instruction vectors: twelve for each of the 279 documented instruction forms of the 8088, three hundred for DAA, DAS and each IDIV form. `mksample.py` states the format. |
| `sst80286-sample.txt.gz` | 3,047 vectors of the 80286 in real mode: eight for each documented form, three hundred for DAA and for DAS, less the classes `mksample.py` leaves out by rule. |
| `mksample.py` | Writes either sample from its full vector set. |
| `sst.py` | Runs a whole suite, which this tree does not carry (the 8088 one is about 800 MB): `bmake -C usr.bin/i86 test-sst SST_SUITE=8088 SST_DIR=<8088/v2>`, or `SST_SUITE=80286 SST_DIR=<80286 checkout>`; `SST_ARGS="--limit 1500"` runs the first 1,500 vectors of each form. |

## Origin of the fonts

`../font/4x6-cp437.bdf` and `../font/5x8-cp437.bdf` hold the glyphs of
code page 437 from the Misc-Fixed 4x6 and 5x8 fonts of the ucs-fonts
collection, https://www.cl.cam.ac.uk/~mgk25/ucs-fonts.html, each of which
carries the line `COPYRIGHT "Public domain font.  Share and enjoy."`. The
4x6 font has no glyph for fifteen of the pictures of code page 437 below
32 (01h, 02h, 08h to 0Ch, 0Fh to 11h, 13h, 16h, 17h, 1Eh, 1Fh) or for
7Fh; `../font/mkfont.py` draws them, and stops on any other character
without a glyph. The kernel console's 6x9 font is from the same family.

## Origin of the vectors

`sst8088-sample.txt.gz` is derived from the SingleStepTests 8088 suite,
version 2, https://github.com/SingleStepTests/8088 at revision
`aea84484abc79d09639d855b7b0ab32bc9e4dbeb`. `sst80286-sample.txt.gz` is
derived from the SingleStepTests 80286 suite, `v1_real_mode`,
https://github.com/SingleStepTests/80286 at revision
`37c73caf53dcd22d3dd369ff09305d13d117a4fe`. Each vector records the state a
processor on a bus-level test rig held before and after one instruction.
Both suites are distributed under this license, the 80286 suite with the
copyright line "Copyright (c) 2025 Daniel Balsom":

    MIT License

    Copyright (c) 2024 SingleStepTests

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

The sample compares flags under the suite's mask of defined flags. After a
divide error (`D4`, `F6`, `F7` vectors that push an interrupt frame) it
also leaves the arithmetic flags uncompared, in the register and in the
pushed image, because the 8088 leaves them in states no manual defines.
