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
"""Calibrate the decoder's row move against memmove, in both overlap directions.

Row-copy operations move rows with a source before or after the destination. A forward word
copy is right when the source follows the destination and wrong when the source precedes it
and overlaps; the known-bad case below is that forward copy. Build the host library with
    cc -O1 -Wall -Wextra -Werror -shared -fPIC -o $OUT/dec_kernels_host.so dec_kernels.c
"""

import ctypes
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


def forward_copy(buf, src, dst, n):
    """The copy rowmove used to be: one word at a time from the low address up."""
    for i in range(n):
        buf[dst + i] = buf[src + i]


def main():
    pl.configure("Calibrate rowmove against memmove.", need_corpus=False)
    lib = ctypes.CDLL(str(pl.out_file("dec_kernels_host.so")))
    lib.rowmove.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32]
    cases = 0
    bad_forward = 0
    overlap_down = 0
    for n in range(1, 41):
        for sh in range(-n - 3, n + 4):  # source = destination + sh, in words
            base = np.arange(200, dtype=np.uint32) * 7 + 1
            dst = 80
            src = dst + sh
            want = base.copy()
            want[dst : dst + n] = base[src : src + n].copy()  # memmove
            got = base.copy()
            lib.rowmove(got.ctypes.data + 4 * src, got.ctypes.data + 4 * dst, n)
            if not (got == want).all():
                raise SystemExit("FAIL: rowmove differs from memmove at n=%d shift=%d" % (n, sh))
            fwd = base.copy()
            forward_copy(fwd, src, dst, n)
            overlaps_down = sh < 0 and -sh < n
            overlap_down += int(overlaps_down)
            bad_forward += int(not (fwd == want).all())
            if overlaps_down and (fwd == want).all():
                raise SystemExit("FAIL: the forward copy should break at n=%d shift=%d" % (n, sh))
            if not overlaps_down and not (fwd == want).all():
                raise SystemExit("FAIL: the forward copy should hold at n=%d shift=%d" % (n, sh))
            cases += 1
    print(
        "rowmove matches memmove in %d cases; the forward copy fails in %d, exactly the"
        " %d with the source before and overlapping the destination"
        % (cases, bad_forward, overlap_down)
    )


main()
