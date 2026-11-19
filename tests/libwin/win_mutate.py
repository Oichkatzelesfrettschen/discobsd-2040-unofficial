#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Write one mutated copy of win.c per fault, and an unmutated baseline.

Usage: win_mutate.py WIN_C OUTPUT_DIRECTORY

Each mutation replaces text that must occur exactly once, so a refactor of
win.c that moves or duplicates a site fails here instead of silently
producing an unmutated "mutant". The output files are win_NAME.mutant.c;
win_baseline.mutant.c carries no fault and must pass the gate, which
calibrates the harness against a known-good input. The font header include
is rewritten to a bare name so the copy builds away from lib/libwin.
"""

import sys
from pathlib import Path

FONT_INCLUDE = '#include "../../sys/arch/rp2040/dev/lcdfont.h"'

# (name, original text, replacement)
MUTATIONS = (
    ("baseline", "", ""),
    (
        "dropped_destroy_damage",
        "\twin->used = 0;\n\treturn damage_window(s, win);",
        "\twin->used = 0;\n\treturn 0;",
    ),
    (
        "dropped_move_damage",
        "\twin->y = y;\n\trc = damage_rect(s, &old);",
        "\twin->y = y;\n\trc = 0;",
    ),
    (
        "dropped_raise_damage",
        "\ts->z[s->nwin - 1] = (unsigned char)j;\n\treturn damage_window(s, win);",
        "\ts->z[s->nwin - 1] = (unsigned char)j;\n\treturn 0;",
    ),
    (
        "overflow_loses_damage",
        "\tif (s->ndamage == WIN_DAMAGE_MAX) {\n\t\trc = win_flush(s);",
        "\tif (s->ndamage == WIN_DAMAGE_MAX) {\n\t\trc = 0;\n\t\ts->ndamage = 0;",
    ),
    (
        "inexact_merge",
        "a->x0 <= b->x1 &&\n\t    b->x0 <= a->x1) {",
        "a->x0 <= b->x1 + 8 &&\n\t    b->x0 <= a->x1 + 8) {",
    ),
    (
        "trim_off_by_one",
        "\t\t\ta->x0 = b->x1;\n",
        "\t\t\ta->x0 = b->x1 + 1;\n",
    ),
    (
        "client_clip_right_edge",
        "\tcx1 = w->x + w->w - 1;\n\tcy1 =",
        "\tcx1 = w->x + w->w;\n\tcy1 =",
    ),
    (
        "client_clip_top_edge",
        "\tcy0 = ty1;\n",
        "\tcy0 = ty1 - 1;\n",
    ),
    (
        "z_order_reversed",
        "\t\tfor (i = 0; i < s->nwin; i++)\n\t\t\tpaint_window(",
        "\t\tfor (i = s->nwin - 1; i >= 0; i--)\n\t\t\tpaint_window(",
    ),
    (
        "strip_overrun",
        "rows = WIN_STRIP_BYTES / w;",
        "rows = WIN_STRIP_BYTES / w + 1;",
    ),
    (
        "title_unclipped",
        "\tctx_clip(c, tx0, ty0, tx1, ty1);\n\tfill_abs(",
        "\tctx_clip(c, c->sx, c->sy, c->sx + c->sw, c->sy + c->sh);\n\tfill_abs(",
    ),
    (
        "title_clipped_to_window_only",
        "\tctx_clip(c, tx0, ty0, tx1, ty1);\n\tfill_abs(",
        "\tctx_clip(c, w->x, w->y, w->x + w->w, w->y + w->h);\n\tfill_abs(",
    ),
    (
        "glyph_row_shifted",
        "\t\t\tpy = gy + row;",
        "\t\t\tpy = gy + row + 1;",
    ),
    (
        "hline_one_short",
        "win_fill(c, x, y, len, 1, color);",
        "win_fill(c, x, y, len - 1, 1, color);",
    ),
)


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    source = Path(argv[1]).read_text(encoding="utf-8")
    out = Path(argv[2])
    if source.count(FONT_INCLUDE) != 1:
        print("win_mutate: font include not found exactly once", file=sys.stderr)
        return 1
    source = source.replace(FONT_INCLUDE, '#include "lcdfont.h"')
    for name, old, new in MUTATIONS:
        if old:
            if source.count(old) != 1:
                print(
                    "win_mutate: %s: pattern found %d times, expected 1"
                    % (name, source.count(old)),
                    file=sys.stderr,
                )
                return 1
            text = source.replace(old, new)
        else:
            text = source
        (out / ("win_%s.mutant.c" % name)).write_text(text, encoding="utf-8")
    print("win_mutate: wrote %d files" % len(MUTATIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
