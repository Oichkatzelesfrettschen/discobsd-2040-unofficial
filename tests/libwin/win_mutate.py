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
"""Write one mutated copy of a libwin source per fault, and baselines.

Usage: win_mutate.py WIN_C WIN_INPUT_C WIN_INPUT_DEV_C WIN_LCDGFX_C WIN_WIDGET_C
       OUTPUT_DIRECTORY

Each mutation replaces text that must occur exactly once, so a refactor of
win.c that moves or duplicates a site fails here instead of silently
producing an unmutated "mutant". The output files are win_NAME.mutant.c for
win.c, inp_NAME.mutant.c for win_input.c and dev_NAME.mutant.c for
win_input_dev.c, lcd_NAME.mutant.c for win_lcdgfx.c and wgt_NAME.mutant.c for
win_widget.c; the five baselines are fault-free and must pass the gate, which
calibrates the harness against a known-good input. The font header include
is rewritten to a bare name so the copy builds away from lib/libwin.
"""

import sys
from pathlib import Path

FONT_INCLUDE = '#include "../../sys/arch/rp2040/dev/lcdfont.h"'

# (name, original text, replacement)
MUTATIONS = (
    (
        "bits_on_by_default",
        "\ts->be = *be;\n",
        "\ts->be = *be;\n\ts->bits = win_lcdgfx_bits;\n",
    ),
    ("baseline", "", ""),
    (
        "same_title_damages",
        "\tif (title_same(win, title))\n\t\treturn 0;\n",
        "\tif (0 && title_same(win, title))\n\t\treturn 0;\n",
    ),
    (
        "same_title_ignores_end",
        "\treturn w->title[i] == '\\0';\n}",
        "\treturn 1;\n}",
    ),
    (
        "same_title_short_cut",
        "title[i] != '\\0' && i < WIN_TITLE_MAX - 1;\n\t    i++)\n\t\tif",
        "title[i] != '\\0' && i < WIN_TITLE_MAX - 2;\n\t    i++)\n\t\tif",
    ),
    (
        "same_title_after_make_room",
        "\tif (title_same(win, title))\n\t\treturn 0;\n"
        "\tif (make_room(s, WIN_DAMAGE_CALL) < 0)\n\t\treturn -1;\n",
        "\tif (make_room(s, WIN_DAMAGE_CALL) < 0)\n\t\treturn -1;\n"
        "\tif (title_same(win, title))\n\t\treturn 0;\n",
    ),
    (
        "dropped_destroy_damage",
        "\twin->used = 0;\n\tdamage_shown(s, win);",
        "\twin->used = 0;\n\t(void)damage_shown;",
    ),
    (
        "dropped_move_damage",
        "\twin->y = y;\n\tdamage_rect(s, &old);",
        "\twin->y = y;",
    ),
    (
        "dropped_raise_damage",
        "\t\tdamage_crossed(s, j, have, &box, follow);\n",
        "\t\t(void)have;\n\t\tdamage_crossed(s, j, 0, &box, 0);\n",
    ),
    (
        "dropped_lower_damage",
        "\tdamage_crossed(s, j, have, &box, pick >= 0 && pick != j);\n",
        "\t(void)have;\n\tdamage_crossed(s, j, 0, &box, 0);\n",
    ),
    (
        "raise_damages_the_whole_window",
        "\t\tdamage_crossed(s, j, have, &box, follow);\n",
        "\t\t(void)have;\n\t\tdamage_shown(s, win);\n",
    ),
    (
        "crossed_reads_the_wrong_side",
        "have = crossed_box(s, j, i + 1, s->nwin, &box);",
        "have = crossed_box(s, j, 0, i, &box);",
    ),
    (
        "title_left_out_of_the_box",
        "\t\tdamage_crossed(s, j, have, &box, follow);\n",
        "\t\tdamage_crossed(s, j, have, &box, 0);\n",
    ),
    (
        "crossed_ignores_hidden",
        "\t\tif (s->wins[s->z[k]].hidden)\n\t\t\tcontinue;\n",
        "",
    ),
    (
        "overflow_loses_damage",
        "\tif (s->ndamage > WIN_DAMAGE_MAX - n)\n\t\treturn win_flush(s);",
        "\tif (s->ndamage > WIN_DAMAGE_MAX - n)\n\t\treturn s->ndamage = 0;",
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
        # The (5,5) move must merge and the far corner pair must stay apart.
        "cost_compare_inverted",
        "\t\t\t\tif (rect_cost(s, &box) <=\n",
        "\t\t\t\tif (rect_cost(s, &box) >\n",
    ),
    (
        # The break-even pair of run_cost needs the window command term.
        "cost_strip_term_dropped",
        "return (long)s->bpp * w * h + (long)WIN_CMD_BYTES * strips;",
        "return (long)s->bpp * w * h + (long)WIN_CMD_BYTES * strips * 0;",
    ),
    (
        # The break-even pair merges at 2 bytes a pixel and splits at 3.
        "cost_bpp_ignored",
        "return (long)s->bpp * w * h + (long)WIN_CMD_BYTES * strips;",
        "return (long)(s->bpp | 3) * w * h + (long)WIN_CMD_BYTES * strips;",
    ),
    (
        # A divisor that is not a whole number of rows: 5120 / 34 is 150.6 and
        # repaint sends 150 rows a strip, so a rectangle 22x128 and one 51x245
        # that overlap stay apart; the unfloored count merges them.
        "cost_strips_not_floored",
        "rows = (long)WIN_STRIP_BYTES / w;\n\tstrips = (h + rows - 1) / rows;",
        "rows = 0;\n\tstrips = (h * w + WIN_STRIP_BYTES - 1) / WIN_STRIP_BYTES + rows;",
    ),
    (
        # The pair [10,110)x[0,100) and [0,120)x[50,150) costs 34,044 bytes at
        # bpp 2 once the first is trimmed to 50 rows and 44,055 untrimmed; the
        # 36,044-byte box wins only against the untrimmed sum.
        "cost_vs_untrimmed_sum",
        "\t\t\t    (rect_trim(&n, e) || rect_trim(e, &n))) {",
        "\t\t\t    0 && (rect_trim(&n, e) || rect_trim(e, &n))) {",
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
        "\t    y != clampi(y) || make_room(s, WIN_DAMAGE_CALL) < 0)\n\t\treturn NULL;",
        "\t    y != clampi(y))\n\t\treturn NULL;\n\t(void)make_room(s, WIN_DAMAGE_CALL);",
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
        "\tif (s->focus == j) {\n\t\ts->focus = -1;\n"
        "\t\tfocus_set(s, focus_pick(s, p - 1));\n\t}\n\treturn 0;",
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
        "reserve_ignores_count",
        "\treturn make_room(s, n);\n}",
        "\treturn make_room(s, 1);\n}",
    ),
    (
        "focus_change_skips_old_title",
        "\tdamage_title(s, s->focus);\n\ts->focus = slot;",
        "\ts->focus = slot;",
    ),
    (
        "bits_lsb_first",
        "\t\t\t\tb = (unsigned char)(b << 1);\n\t\t\t\tif (x * 8 + k < w && *fg != *bg &&\n"
        "\t\t\t\t    (buf[y * w + x * 8 + k] & PIXEL_MASK) == *fg)\n\t\t\t\t\tb |= 1;",
        "\t\t\t\tif (x * 8 + k < w && *fg != *bg &&\n"
        "\t\t\t\t    (buf[y * w + x * 8 + k] & PIXEL_MASK) == *fg)\n"
        "\t\t\t\t\tb |= (unsigned char)(1 << k);",
    ),
    (
        "bits_fg_bg_swapped",
        "s->bits(s->be.self, x, y, w, h, fg, bg, buf)",
        "s->bits(s->be.self, x, y, w, h, bg, fg, buf)",
    ),
    (
        "bits_rows_not_padded",
        "int rb = (w + 7) / 8, x, y, k, v;",
        "int rb = w / 8, x, y, k, v;",
    ),
    (
        "bits_three_colors_accepted",
        "\t\tif (*fg != *bg)\n\t\t\treturn 0;\n\t\t*fg = v;",
        "\t\t*fg = v;",
    ),
    (
        "bits_never_used",
        "if (s->bits != NULL && pack_bits(buf, w, h, &fg, &bg) != 0)",
        "if (0 && s->bits != NULL && pack_bits(buf, w, h, &fg, &bg) != 0)",
    ),
    (
        "bits_one_column_packed",
        "\tif ((size_t)rb * (size_t)h >= n)\n\t\treturn 0;\n",
        "\tif ((size_t)rb * (size_t)h > n)\n\t\treturn 0;\n",
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
        "\t\tif (win_set_geometry(s, in->mode_win, in->sx, in->sy, in->sw,\n"
        "\t\t    in->sh) < 0)\n\t\t\treturn -1;\n",
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
        "\tif (in->mode != WIN_MODE_NONE)\n"
        "\t\treturn mode_event(in, s, ev) < 0 ? -1 : WIN_DISP_MANAGER;",
        "\tif (in->mode != WIN_MODE_NONE)\n\t\t(void)mode_event(in, s, ev);",
    ),
    (
        "esc_before_byte_lost",
        "\t\tput(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n\t\treturn n + tty_plain(",
        "\t\treturn n + tty_plain(",
    ),
    (
        "close_without_handler_chance",
        "\t\tr = w->on_event(s, w, &ev, w->on_event_arg);\n\t\tif (r < 0)\n\t\t\treturn -1;\n"
        "\t\tif (r != 0)\n\t\t\treturn 0;",
        "\t\tr = w->on_event(s, w, &ev, w->on_event_arg);\n\t\tif (r < 0)\n\t\t\treturn -1;",
    ),
    (
        "release_treated_as_press",
        "kind = WIN_EV_RELEASE;",
        "kind = WIN_EV_PRESS;",
    ),
    (
        "cancel_not_atomic",
        "\t\tif (win_set_geometry(s, in->mode_win, in->sx, in->sy, in->sw,\n\t\t    in->sh) < 0)",
        "\t\tif (win_resize(s, in->mode_win, in->sw, in->sh) < 0 ||\n"
        "\t\t    win_move(s, in->mode_win, in->sx, in->sy) < 0)",
    ),
    (
        "mode_survives_slot_reuse",
        "\t    (!win_is_live(s, in->mode_win) || in->mode_win->gen != in->mode_gen))",
        "\t    !win_is_live(s, in->mode_win))",
    ),
    (
        "decode_failure_continues",
        "\t\tif (win_input_event(in, s, &ev[i]) < 0)\n\t\t\treturn -1;\n\treturn 0;",
        "\t\t(void)win_input_event(in, s, &ev[i]);\n\treturn 0;",
    ),
    (
        "cycle_not_reserved",
        "\tif (win_reserve(s, 2 * WIN_DAMAGE_CALL) < 0)\n\t\treturn -1;\n",
        "",
    ),
    (
        "decoder_writes_past_events",
        "\t\tif (in->esc == ST_ESC)\n"
        "\t\t\tput(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n"
        "\t\tin->esc = ST_ESC;\n\t\treturn n;",
        "\t\tif (in->esc == ST_ESC)\n"
        "\t\t\tput(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n"
        "\t\tput(&ev[n + 2], WIN_KEY_ESC, 0, WIN_EV_PRESS);\n"
        "\t\tin->esc = ST_ESC;\n\t\treturn n;",
    ),
    (
        "modal_not_gated",
        "\tif (in->modal != NULL && win_is_live(s, in->modal) &&\n"
        "\t    in->modal->gen == in->modal_gen &&\n"
        "\t    (action == WIN_ACT_NEXT || action == WIN_ACT_PREV))\n\t\treturn 0;\n",
        "",
    ),
    (
        "modal_generation_ignored",
        "\t    in->modal->gen == in->modal_gen &&\n",
        "",
    ),
    (
        "handler_failure_swallowed",
        "\tif (i < 0)\n\t\treturn -1;\n\treturn i != 0 ?",
        "\treturn i != 0 ?",
    ),
    (
        "close_failure_swallowed",
        "\t\tif (r < 0)\n\t\t\treturn -1;\n\t\tif (r != 0)\n\t\t\treturn 0;\n\t}\n"
        "\treturn win_destroy",
        "\t\tif (r != 0)\n\t\t\treturn 0;\n\t}\n\treturn win_destroy",
    ),
)


# win_input_dev.c: (name, original text, replacement)
DEV_MUTATIONS = (
    ("baseline", "", ""),
    (
        "tty_bytes_not_drained",
        "n = typed(src, buf, sizeof(buf));",
        "n = src->raw ? 0 : typed(src, buf, sizeof(buf));",
    ),
    (
        "lost_count_ignored",
        "\t\tif (ev.lost != 0)\n\t\t\twin_input_lost(in);",
        "",
    ),
    (
        "raw_session_not_polled",
        "(src->raw && !src->wake) || ",
        "",
    ),
    (
        "wake_kernel_still_polled",
        "(src->raw && !src->wake) || ",
        "src->raw || ",
    ),
    (
        "wake_assumed_after_fallback",
        "\t\treturn -1;\n\tsrc->raw = 1;\n\treturn 0;",
        "\t\treturn -1;\n\tsrc->raw = 1;\n\tsrc->wake = 1;\n\treturn 0;",
    ),
    (
        "wake_request_not_made",
        "int on = 2;",
        "int on = 1;",
    ),
    (
        "plain_session_not_offered",
        "\tif (errno != EINVAL)\n\t\treturn -1;",
        "\tif (1)\n\t\treturn -1;",
    ),
    (
        "empty_tty_queue_read",
        "\tif (src->wake) {\n\t\tif (ioctl(",
        "\tif (0) {\n\t\tif (ioctl(",
    ),
    (
        "wake_kept_by_dead_session",
        "\t\t\tsrc->raw = 0;\n\t\t\tsrc->wake = 0;",
        "\t\t\tsrc->raw = 0;",
    ),
    (
        "wake_kept_by_close",
        "\tsrc->raw = 0;\n\tsrc->wake = 0;\n}",
        "\tsrc->raw = 0;\n}",
    ),
    (
        "lone_esc_never_resolved",
        "} else if (r == 0 && !src->raw && in->esc != 0) {",
        "} else if (0) {",
    ),
    (
        "raw_dispatch_failure_swallowed",
        "\t\t\t\twin_input_lost(in);\n\t\t\t\treturn -2;",
        "\t\t\t\twin_input_lost(in);\n\t\t\t\tcontinue;",
    ),
    (
        "tty_dispatch_failure_swallowed",
        "\t\t\t\t\t\twin_input_lost(in);\n\t\t\t\t\t\treturn -1;",
        "\t\t\t\t\t\twin_input_lost(in);",
    ),
    (
        "batch_not_drained",
        "} while (ev.n == LCD_KEYEV_N);",
        "} while (0);",
    ),
    (
        "timeout_charged_only_on_select_timeout",
        "\t\tif (timeout_ms >= 0) {\n\t\t\t(void)gettimeofday(&tv, NULL);",
        "\t\tif (timeout_ms >= 0 && r == 0) {\n\t\t\t(void)gettimeofday(&tv, NULL);",
    ),
)


# win_lcdgfx.c: (name, original text, replacement)
LCD_MUTATIONS = (
    (
        "baseline",
        "",
        "",
    ),
    (
        "bits_colors_swapped",
        "\twin.fg = (u_char)fg;\n\twin.bg = (u_char)bg;",
        "\twin.fg = (u_char)bg;\n\twin.bg = (u_char)fg;",
    ),
    (
        "bits_count_unpadded",
        "\tleft = (size_t)((w + 7) / 8) * (size_t)h;",
        "\tleft = (size_t)(w / 8) * (size_t)h;",
    ),
    (
        "bits_sent_as_index",
        "\twin.fmt = LCDG_WIN_BITS;",
        "\twin.fmt = LCDG_WIN_INDEX;",
    ),
    (
        # The model refuses pixels sent before LCDG_COLMOD 0x55.
        "colmod_not_sent",
        "\tif (ioctl(st->fd, LCDG_COLMOD, &cm) < 0)\n\t\treturn -1;\n",
        "\t(void)cm;\n\t(void)st;\n",
    ),
    (
        "colmod_wrong_format",
        "cm.fmt = LCDG_COLMOD_RGB565;",
        "cm.fmt = LCDG_COLMOD_RGB666;",
    ),
    (
        # run_cost expects the 2-byte pixel price after the call.
        "colmod_bpp_not_recorded",
        "\ts->bpp = WIN_BPP_RGB565;\n",
        "\ts->bpp = WIN_BPP_DEFAULT;\n",
    ),
)


# win_widget.c: (name, original text, replacement)
WIDGET_MUTATIONS = (
    (
        "dialog_checks_one_button_slot",
        "if (d->form.n + 1 + d->has_cancel > WGT_MAX)",
        "if (d->form.n + 1 > WGT_MAX)",
    ),
    (
        "field_enter_always_consumed",
        "\t\treturn r < 0 ? -1 : r > 0 ? 0 : 1;\n\t}\n\tif (k >= 0x20",
        "\t\treturn r < 0 ? -1 : 1;\n\t}\n\tif (k >= 0x20",
    ),
    ("baseline", "", ""),
    (
        "traversal_skips_a_control",
        "for (step = 1; step <= f->n; step++) {",
        "for (step = 1; step <= f->n; step += 2) {",
    ),
    (
        "traversal_visits_disabled",
        "return w->kind >= WGT_BUTTON && (w->flags & (WGT_F_HIDDEN | WGT_F_DISABLED)) == 0;",
        "return w->kind >= WGT_BUTTON && (w->flags & WGT_F_HIDDEN) == 0;",
    ),
    (
        "mnemonic_reaches_wrong_control",
        "\t\tif (w->mnem != letter)\n\t\t\tcontinue;",
        "\t\tif (w->mnem == 0)\n\t\t\tcontinue;",
    ),
    (
        "field_cursor_off_by_one",
        "\tw->len++;\n\tw->cur++;\n",
        "\tw->len++;\n",
    ),
    (
        "list_scroll_off_by_one",
        "\t\ttop = index - w->rows + 1;",
        "\t\ttop = index - w->rows + 2;",
    ),
    (
        "noop_check_repaints",
        "\tif (w->checked == on)\n\t\treturn 0;\n",
        "",
    ),
    (
        "dialog_leaves_focus_behind",
        "\t\t\t(void)win_focus(s, d->opener);\n",
        "\t\t\t(void)s;\n",
    ),
    (
        "dialog_stays_modal",
        "\twin_input_set_modal(d->in, NULL);\n\td->form.win = NULL;",
        "\td->form.win = NULL;",
    ),
    (
        "list_row_unclipped",
        "\t\t    list_row_w(w), WGT_LIST_ROW_H);\n\t\tif (i == l->sel)",
        "\t\t    4000, 4000);\n\t\tif (i == l->sel)",
    ),
    (
        "field_repaints_whole_field",
        "\trl_add(&l, w->head.x + 2 + WGT_CELL_W * lo, w->head.y + 2,\n"
        "\t    WGT_CELL_W * (hi - lo + 1), WGT_CELL_H);",
        "\trl_add(&l, w->head.x + 2 + WGT_CELL_W * lo, w->head.y + 2,\n"
        "\t    w->head.w + (hi - hi), WGT_CELL_H);",
    ),
    (
        "field_reserve_skipped",
        "\treturn win_reserve(f->s, WIN_DAMAGE_CALL);\n}\n\nint\nwgt_field_set_text",
        "\t(void)f;\n\treturn 0;\n}\n\nint\nwgt_field_set_text",
    ),
    (
        "activation_hook_after_store",
        "\tif (f->act != NULL && f->act(f, &k->head, k->head.id, on) < 0)\n\t\treturn -1;\n"
        "\tif (focus_to >= 0)\n\t\tfocus_apply(f, focus_to);\n\tk->checked = (unsigned char)on;",
        "\tif (focus_to >= 0)\n\t\tfocus_apply(f, focus_to);\n\tk->checked = (unsigned char)on;\n"
        "\tif (f->act != NULL && f->act(f, &k->head, k->head.id, on) < 0)\n\t\treturn -1;",
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
    if len(argv) != 7:
        print(__doc__, file=sys.stderr)
        return 2
    source = Path(argv[1]).read_text(encoding="utf-8")
    inp = Path(argv[2]).read_text(encoding="utf-8")
    dev = Path(argv[3]).read_text(encoding="utf-8")
    lcd = Path(argv[4]).read_text(encoding="utf-8")
    wgt = Path(argv[5]).read_text(encoding="utf-8")
    out = Path(argv[6])
    if source.count(FONT_INCLUDE) != 1:
        print("win_mutate: font include not found exactly once", file=sys.stderr)
        return 1
    source = source.replace(FONT_INCLUDE, '#include "lcdfont.h"')
    sets = (
        (source, MUTATIONS, "win"),
        (inp, INPUT_MUTATIONS, "inp"),
        (dev, DEV_MUTATIONS, "dev"),
        (lcd, LCD_MUTATIONS, "lcd"),
        (wgt, WIDGET_MUTATIONS, "wgt"),
    )
    total = 0
    for text, mutations, prefix in sets:
        if not write_set(text, mutations, prefix, out):
            return 1
        total += len(mutations)
    print("win_mutate: wrote %d files" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
