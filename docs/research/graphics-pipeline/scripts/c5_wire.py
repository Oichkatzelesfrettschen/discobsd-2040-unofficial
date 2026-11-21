# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# c5_wire.py -- stage throughput and frame-budget model of the PicoCalc panel path (computed from
# the cited constants)
f = 12.5e6
bit = 1 / f
fcpu = 125e6
px16 = 16 * bit
px24 = 24 * bit
setup = 11 * 8 * bit
print(
    "wire: RGB565 %.3f us/px, RGB666/888 %.3f us/px, window setup (CASET+RASET+RAMWR, "
    "11 bytes) %.2f us = %.2f px(565)" % (px16 * 1e6, px24 * 1e6, setup * 1e6, setup / px16)
)
print(
    "CPU cycles available per pixel at the wire rate (125 MHz): 565 %.0f, 666 %.0f"
    % (px16 * fcpu, px24 * fcpu)
)
print(
    "limit of ST7365P write cycle 66 ns -> %.2f MHz; headroom over 12.5 MHz: %.2fx"
    % (1 / 66e-9 / 1e6, (1 / 66e-9) / f)
)
print()
print("area A -> frame time and ceiling fps, one window per frame")
print("%-10s %8s | %9s %7s | %9s %7s" % ("viewport", "pixels", "565 ms", "fps", "666 ms", "fps"))
for w, h in [
    (320, 320),
    (320, 240),
    (320, 200),
    (256, 192),
    (240, 160),
    (192, 144),
    (160, 160),
    (160, 120),
    (128, 128),
]:
    A = w * h
    t16 = setup + A * px16
    t24 = setup + A * px24
    print(
        "%-10s %8d | %9.1f %7.1f | %9.1f %7.1f"
        % ("%dx%d" % (w, h), A, t16 * 1e3, 1 / t16, t24 * 1e3, 1 / t24)
    )
print()
print(
    "tiles (8x8) per frame at a target frame rate, 565 / 666, best case (one run) and "
    "worst case (one window each)"
)
for fps in (60, 30, 20, 15):
    T = 1.0 / fps
    t565 = 64 * px16
    t666 = 64 * px24
    print(
        "%2d fps: %5.0f tiles / %5.0f tiles (565) ; %5.0f / %5.0f (666) of 1600 ; "
        "worst-case windows: %5.0f (565)"
        % (fps, T / t565, T / (t565 + setup), T / t666, T / (t666 + setup), T / (t565 + setup))
    )
print()
print(
    "hardware scroll: shifting the glass by n lines = rewriting n new lines only (320 "
    "px each, one window)"
)
for n in (1, 2, 4, 8, 16):
    print(
        "  n=%2d: 565 %.0f us, 666 %.0f us; full redraw instead: %.0f ms (565)"
        % (
            n,
            setup * 1e6 + n * 320 * px16 * 1e6,
            setup * 1e6 + n * 320 * px24 * 1e6,
            (setup + 320 * 320 * px16) * 1e3,
        )
    )
print()
print("strip overhead for strips of h rows, full width (565): setup/(setup+320*h*px)")
for h in (1, 2, 4, 8, 16):
    print("  h=%2d: %.2f%%" % (h, 100 * setup / (setup + 320 * h * px16)))
print(
    "column strips (MADCTL MV=1, one column of H px per window): overhead for H=240: "
    "%.2f%%, H=160: %.2f%%"
    % (100 * setup / (setup + 240 * px16), 100 * setup / (setup + 160 * px16))
)
print(
    "break-even gap between two spans of one row (merge instead of a second window): "
    "%.1f px (565), %.1f px (666)" % (setup / px16, setup / px24)
)
print()
print(
    "measured anchors: 320x320 fill 131,000 us (565) / 197,000 us (666) [PICOCALC.md]; "
    "model %.0f / %.0f us" % ((setup + 320 * 320 * px16) * 1e6, (setup + 320 * 320 * px24) * 1e6)
)
print(
    "i86 320x200 frame: %.1f ms (565) [scan-out note: 81.9 ms]" % ((setup + 320 * 200 * px16) * 1e3)
)
print()
print(
    "expansion cost vs the wire (cycles per pixel; the loops are compiled in "
    "c5_loops.c; counts are inferences from the Thumb-1 listing, README.md)"
)
for name, cyc in [
    ("per-pixel palette, polled FIFO (c5_loops.c expand_c, 29 or more)", 29),
    ("per-pixel palette into a RAM bounce buffer (c5_loops.c expand_a)", 15),
    ("256-entry pair table, 2 px per load (c5_loops.c expand_b, 14 per pair)", 7),
]:
    print(
        "  %-72s %5.1f cyc/px -> %5.1f%% of the 565 wire time"
        % (name, cyc, 100 * cyc / (px16 * fcpu))
    )
