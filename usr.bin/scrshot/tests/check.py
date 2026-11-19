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
#
# Host gate of the scrshot format writers. scrfmt_test writes each case
# through scr_header() and scr_row(); this script builds the expected PPM,
# BMP and raw bytes from the RGB565 pixel rule with struct and compares the
# files byte for byte, plus hand-computed bytes for six pinned colors. Each
# mutant is one deliberately wrong copy of scrfmt.h (a textual substitution
# that matches exactly once); the comparison must reject it, and the run
# fails when a mutant passes, because a gate that passes input it should
# reject reports nothing.
#
# usage: check.py [scrshot-dir]       (HOSTCC selects the compiler)

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CC = os.environ.get("HOSTCC", "cc")
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-Wshadow",
         "-Wconversion", "-O1"]

CASES = [("w1", 1, 3), ("w2", 2, 3), ("w3", 3, 3), ("w4", 4, 3), ("w5", 5, 3),
         ("w7", 7, 2), ("sweep", 256, 256), ("panel", 320, 320), ("ram", 320, 480),
         ("pin", 3, 2)]
PINNED = [0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0x8410]
# The pinned colors in 8-bit RGB, worked out by hand from the replication rule:
# 0x8410 is r5 = 16 (0x84), g6 = 32 (0x82), b5 = 16 (0x84).
PINNED_RGB = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (0, 0, 0),
              (0x84, 0x82, 0x84)]

# (name, old, new)
MUTANTS = [
    ("ppm-writes-blue-first", "out[n++] = (unsigned char)r;\n\t\t\tout[n++] = (unsigned char)g;\n\t\t\tout[n++] = (unsigned char)b;",
     "out[n++] = (unsigned char)b;\n\t\t\tout[n++] = (unsigned char)g;\n\t\t\tout[n++] = (unsigned char)r;"),
    ("bmp-writes-red-first", "out[n++] = (unsigned char)b;\n\t\t\tout[n++] = (unsigned char)g;\n\t\t\tout[n++] = (unsigned char)r;\n\t\t}",
     "out[n++] = (unsigned char)r;\n\t\t\tout[n++] = (unsigned char)g;\n\t\t\tout[n++] = (unsigned char)b;\n\t\t}"),
    ("bmp-rows-lack-padding", "return (w * 3 + 3) & ~3u;", "return w * 3;"),
    ("bmp-row-padding-not-written", "while (fmt == SCR_BMP && (n & 3) != 0)", "while (0 && (n & 3) != 0)"),
    ("bmp-height-positive", "n += scr_put_le32(out + n, 0ul - h);", "n += scr_put_le32(out + n, h);"),
    ("bmp-size-omits-header", "scr_put_le32(out + n, SCR_BMP_HDR + size)", "scr_put_le32(out + n, size)"),
    ("red-shift-wrong", "r = v >> 11 & 0x1f;", "r = v >> 10 & 0x1f;"),
    ("green-widened-without-replication", "g = g << 2 | g >> 4;", "g = g << 2;"),
    ("blue-widened-without-replication", "b = b << 3 | b >> 2;", "b = b << 3;"),
    ("green-takes-five-bits", "g = v >> 5 & 0x3f;", "g = v >> 5 & 0x1f;"),
    ("high-byte-read-first", "v = (unsigned)in[2 * i] | (unsigned)in[2 * i + 1] << 8;",
     "v = (unsigned)in[2 * i + 1] | (unsigned)in[2 * i] << 8;"),
    ("raw-swaps-bytes", "out[i] = in[i];", "out[i] = in[i ^ 1];"),
    ("ppm-header-width-height-swapped", "n += scr_put_dec(out + n, w);\n\t\tout[n++] = ' ';\n\t\tn += scr_put_dec(out + n, h);",
     "n += scr_put_dec(out + n, h);\n\t\tout[n++] = ' ';\n\t\tn += scr_put_dec(out + n, w);"),
]


def value(name, w, h, x, y):
    if name == "pin":
        return PINNED[y * 3 + x]
    return (y * w + x) * 40503 & 0xFFFF


def expand(v):
    r, g, b = v >> 11 & 31, v >> 5 & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def expected(name, w, h):
    """The three files of one case: {extension: bytes}."""
    ppm = [b"P6\n%d %d\n255\n" % (w, h)]
    rowlen = (w * 3 + 3) // 4 * 4
    size = rowlen * h
    bmp = [b"BM" + struct.pack("<IIIIiiHHIIiiII", 54 + size, 0, 54, 40, w, -h, 1, 24, 0,
                               size, 2835, 2835, 0, 0)]
    raw = []
    for y in range(h):
        px = [value(name, w, h, x, y) for x in range(w)]
        ppm.append(b"".join(bytes(expand(v)) for v in px))
        row = b"".join(bytes((c, b, a)) for a, b, c in (expand(v) for v in px))
        bmp.append(row + bytes(rowlen - len(row)))
        raw.append(b"".join(struct.pack("<H", v) for v in px))
    return {"ppm": b"".join(ppm), "bmp": b"".join(bmp), "raw": b"".join(raw)}


def build(src, header_dir, exe):
    cmd = [CC] + FLAGS + ["-I" + str(header_dir), "-o", str(exe), str(src)]
    return subprocess.run(cmd, capture_output=True, text=True)


def compare(exe, outdir):
    """Run the test program and return the list of mismatching files."""
    run = subprocess.run([str(exe), str(outdir)], capture_output=True, text=True)
    if run.returncode != 0:
        return [f"scrfmt_test exited {run.returncode}: {run.stderr.strip()}"]
    bad = []
    for name, w, h in CASES:
        for ext, want in expected(name, w, h).items():
            path = Path(outdir) / f"{name}.{ext}"
            got = path.read_bytes() if path.exists() else None
            if got != want:
                bad.append(f"{name}.{ext}")
    return bad


def pinned_selftest():
    """The expected-bytes builder against the hand-computed colors."""
    for v, rgb in zip(PINNED, PINNED_RGB):
        if expand(v) != rgb:
            return f"expand({v:#06x}) = {expand(v)}, hand value {rgb}"
    return None


def main(argv):
    top = Path(argv[1] if len(argv) > 1 else ".").resolve()
    src = top / "tests" / "scrfmt_test.c"
    header = (top / "scrfmt.h").read_text(encoding="utf-8")
    problem = pinned_selftest()
    if problem:
        print("check.py: " + problem, file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="scrshot-") as tmp:
        tmp = Path(tmp)
        good = tmp / "good"
        good.mkdir()
        built = build(src, top, tmp / "good-test")
        if built.returncode != 0:
            print(built.stderr, file=sys.stderr)
            return 1
        bad = compare(tmp / "good-test", good)
        if bad:
            print("check.py: shipped scrfmt.h differs in " + ", ".join(bad), file=sys.stderr)
            return 1
        print(f"scrfmt: {len(CASES)} cases x 3 formats match the independent bytes")
        failed = 0
        for name, old, new in MUTANTS:
            if header.count(old) != 1:
                print(f"mutant {name}: its anchor matches {header.count(old)} times", file=sys.stderr)
                failed += 1
                continue
            mdir = tmp / name
            (mdir / "out").mkdir(parents=True)
            (mdir / "scrfmt.h").write_text(header.replace(old, new), encoding="utf-8")
            built = build(src, mdir, mdir / "test")
            if built.returncode != 0:
                # A mutant that no longer compiles is not evidence of a rejecting test.
                print(f"mutant {name}: does not build", file=sys.stderr)
                failed += 1
                continue
            bad = compare(mdir / "test", mdir / "out")
            if bad:
                print(f"mutant {name}: rejected ({bad[0]})")
            else:
                print(f"mutant {name}: PASSED the comparison", file=sys.stderr)
                failed += 1
        if failed:
            print(f"check.py: {failed} mutant(s) not rejected", file=sys.stderr)
            return 1
        print(f"scrfmt: {len(MUTANTS)} mutants rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
