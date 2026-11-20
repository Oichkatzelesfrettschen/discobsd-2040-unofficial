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
"""Write one mutated copy of win.c or win_input.c per fault, and baselines.

Usage: win_mutate.py WIN_C WIN_INPUT_C WIN_INPUT_DEV_C OUTPUT_DIRECTORY

Each mutation replaces text that must occur exactly once, so a refactor of
win.c that moves or duplicates a site fails here instead of silently
producing an unmutated "mutant". The output files are win_NAME.mutant.c for
win.c, inp_NAME.mutant.c for win_input.c and dev_NAME.mutant.c for
win_input_dev.c; the three baselines carry no fault and must pass the gate, which
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
        "\twin->used = 0;\n\tdamage_shown(s, win);",
        "\twin->used = 0;",
    ),
    (
        "dropped_move_damage",
        "\twin->y = y;\n\tdamage_rect(s, &old);",
        "\twin->y = y;",
    ),
    (
        "dropped_raise_damage",
        "\t\ts->z[s->nwin - 1] = (unsigned char)j;\n\t\tdamage_shown(s, win);",
        "\t\ts->z[s->nwin - 1] = (unsigned char)j;",
    ),
    (
        "overflow_loses_damage",
        "\tif (s->ndamage > WIN_DAMAGE_MAX - WIN_DAMAGE_RESERVE)\n\t\treturn win_flush(s);",
        "\tif (s->ndamage > WIN_DAMAGE_MAX - WIN_DAMAGE_RESERVE)\n\t\treturn s->ndamage = 0;",
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
        "widen_on_failure",
        "\t\t\ts->ndamage = n - i;",
        "\t\t\ts->ndamage = 1;\n\t\t\ts->damage[0].x0 = 0;\n"
        "\t\t\ts->damage[0].y0 = 0;\n\t\t\ts->damage[0].x1 = WIN_PANEL_W;\n"
        "\t\t\ts->damage[0].y1 = WIN_PANEL_H;",
    ),
    (
        "swallowed_create_failure",
        "\t    y != clampi(y) || make_room(s) < 0)\n\t\treturn NULL;",
        "\t    y != clampi(y))\n\t\treturn NULL;\n\t(void)make_room(s);",
    ),
    (
        "foreign_handle_accepted",
        "\t\t\treturn s->wins[i].used ? i : -1;\n\treturn -1;",
        "\t\t\treturn s->wins[i].used ? i : -1;\n\treturn w != NULL && w->used ? 0 : -1;",
    ),
    (
        "hline_one_short",
        "win_fill(c, x, y, len, 1, color);",
        "win_fill(c, x, y, len - 1, 1, color);",
    ),
    (
        "focus_not_moved_on_destroy",
        "\tif (s->focus == j) {\n\t\ts->focus = -1;\n\t\tfocus_set(s, focus_pick(s, p - 1));\n\t}\n\treturn 0;",
        "\t(void)p;\n\treturn 0;",
    ),
    (
        "focus_not_moved_on_hide",
        "\tif (s->focus == j) {\n\t\tfor (i = 0;",
        "\tif (0) {\n\t\tfor (i = 0;",
    ),
    (
        "hidden_still_occludes",
        "\tif (w->hidden)\n\t\treturn;\n\tif (w->x >=",
        "\tif (w->x >=",
    ),
    (
        "title_not_distinct",
        "focused ? WIN_C_TITLE_BG : WIN_C_TITLE_IDLE",
        "(focused & 0) ? WIN_C_TITLE_IDLE : WIN_C_TITLE_BG",
    ),
    (
        "raise_keeps_focus",
        "\tif (follow)\n\t\tfocus_set(s, j);",
        "",
    ),
    (
        "hidden_window_focusable",
        "\tif (j < 0 || win->hidden)\n\t\treturn -1;\n\tif (s->focus == j)",
        "\tif (j < 0)\n\t\treturn -1;\n\tif (s->focus == j)",
    ),
    (
        "focus_pick_takes_hidden",
        "\tfor (k = from; k >= 0; k--)\n\t\tif (!s->wins[s->z[k]].hidden)",
        "\tfor (k = from; k >= 0; k--)\n\t\tif (1)",
    ),
    (
        "dropped_hide_damage",
        "\twin->hidden = 1;\n\tdamage_window(s, win);",
        "\twin->hidden = 1;",
    ),
    (
        "dropped_show_damage",
        "\twin->hidden = 0;\n\tdamage_window(s, win);",
        "\twin->hidden = 0;",
    ),
    (
        "new_window_keeps_old_focus",
        "\tdamage_window(s, win);\n\tfocus_set(s, i);\n\treturn win;",
        "\tdamage_window(s, win);\n\treturn win;",
    ),
    (
        "focus_change_skips_old_title",
        "\tdamage_title(s, s->focus);\n\ts->focus = slot;",
        "\ts->focus = slot;",
    ),
)


# win_input.c: (name, original text, replacement)
INPUT_MUTATIONS = (
    ("baseline", "", ""),
    (
        "repeat_as_press",
        "kind = WIN_EV_REPEAT;",
        "kind = WIN_EV_PRESS;",
    ),
    (
        "cancel_not_restoring",
        "\t\tif (win_resize(s, in->mode_win, in->sw, in->sh) < 0 ||\n"
        "\t\t    win_move(s, in->mode_win, in->sx, in->sy) < 0)\n\t\t\treturn -1;\n",
        "",
    ),
    (
        "truncated_escape_as_key",
        "\tif (pending != ST_ESC)\n\t\treturn 0;",
        "\tif (pending == ST_GROUND)\n\t\treturn 0;",
    ),
    (
        "modifier_not_rebuilt",
        "\tin->mods = 0;\n\tin->held = 0;",
        "\tin->held = 0;",
    ),
    (
        "held_key_not_swallowed",
        "if (in->held != 0 && fold(ev->key) == fold(in->held) &&",
        "if (0 && in->held != 0 && fold(ev->key) == fold(in->held) &&",
    ),
    (
        "ctrl_letter_not_lowered",
        "\t\tk += 'a' - 'A';",
        "\t\tk += 0;",
    ),
    (
        "move_unclamped",
        "nx = clampi(w->x + dx, 0, WIN_PANEL_W - w->w);",
        "nx = w->x + dx;",
    ),
    (
        "resize_unclamped",
        "nx = clampi(w->w + dx, WIN_MIN_W, WIN_PANEL_W - w->x);",
        "nx = w->w + dx;",
    ),
    (
        "bound_key_reaches_window",
        "\t\t\tin->held = (short)ev->key;\n\t\t\treturn WIN_DISP_MANAGER;",
        "\t\t\tin->held = (short)ev->key;",
    ),
    (
        "mode_not_modal",
        "\tif (in->mode != WIN_MODE_NONE)\n\t\treturn mode_event(in, s, ev) < 0 ? -1 : WIN_DISP_MANAGER;",
        "\tif (in->mode != WIN_MODE_NONE)\n\t\t(void)mode_event(in, s, ev);",
    ),
    (
        "esc_before_byte_lost",
        "\t\tput(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n\t\treturn n + tty_plain(",
        "\t\treturn n + tty_plain(",
    ),
    (
        "close_without_handler_chance",
        "if (w->on_event != NULL && w->on_event(s, w, &ev, w->on_event_arg) != 0)\n\t\treturn 0;",
        "if (w->on_event != NULL)\n\t\t(void)w->on_event(s, w, &ev, w->on_event_arg);",
    ),
    (
        "release_treated_as_press",
        "kind = WIN_EV_RELEASE;",
        "kind = WIN_EV_PRESS;",
    ),
    (
        "decoder_writes_past_events",
        "\t\tif (in->esc == ST_ESC)\n\t\t\tput(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n\t\tin->esc = ST_ESC;\n\t\treturn n;",
        "\t\tif (in->esc == ST_ESC)\n\t\t\tput(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n\t\tput(&ev[n + 2], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n\t\tin->esc = ST_ESC;\n\t\treturn n;",
    ),
)


# win_input_dev.c: (name, original text, replacement)
DEV_MUTATIONS = (
    ("baseline", "", ""),
    (
        "tty_bytes_not_drained",
        "n = (int)read(src->fd, buf, sizeof(buf));",
        "n = src->raw ? 0 : (int)read(src->fd, buf, sizeof(buf));",
    ),
    (
        "lost_count_ignored",
        "\t\tif (ev.lost != 0)\n\t\t\twin_input_lost(in);",
        "",
    ),
    (
        "raw_session_not_polled",
        "if ((src->raw || in->esc != 0) && (slice < 0 || slice > WIN_POLL_MS))",
        "if (in->esc != 0 && (slice < 0 || slice > WIN_POLL_MS))",
    ),
    (
        "lone_esc_never_resolved",
        "} else if (r == 0 && !src->raw && in->esc != 0) {",
        "} else if (0) {",
    ),
    (
        "batch_not_drained",
        "} while (ev.n == LCD_KEYEV_N);",
        "} while (0);",
    ),
    (
        "timeout_spent_by_wake",
        "if (r == 0 && left >= 0) {",
        "if (left >= 0) {",
    ),
)


def write_set(source, mutations, prefix, out):
    for name, old, new in mutations:
        if old:
            if source.count(old) != 1:
                print(
                    "win_mutate: %s: pattern found %d times, expected 1"
                    % (name, source.count(old)),
                    file=sys.stderr,
                )
                return False
            text = source.replace(old, new)
        else:
            text = source
        (out / ("%s_%s.mutant.c" % (prefix, name))).write_text(text, encoding="utf-8")
    return True


def main(argv):
    if len(argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    source = Path(argv[1]).read_text(encoding="utf-8")
    inp = Path(argv[2]).read_text(encoding="utf-8")
    dev = Path(argv[3]).read_text(encoding="utf-8")
    out = Path(argv[4])
    if source.count(FONT_INCLUDE) != 1:
        print("win_mutate: font include not found exactly once", file=sys.stderr)
        return 1
    source = source.replace(FONT_INCLUDE, '#include "lcdfont.h"')
    if not write_set(source, MUTATIONS, "win", out):
        return 1
    if not write_set(inp, INPUT_MUTATIONS, "inp", out):
        return 1
    if not write_set(dev, DEV_MUTATIONS, "dev", out):
        return 1
    print(
        "win_mutate: wrote %d files"
        % (len(MUTATIONS) + len(INPUT_MUTATIONS) + len(DEV_MUTATIONS))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
