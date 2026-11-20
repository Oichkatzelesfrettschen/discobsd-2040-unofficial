#!/usr/bin/env python3
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
# Mutation calibration of tests/word_test.py. The unmutated sources must
# pass first. Each mutant is one deliberately wrong copy of a source file
# (a textual substitution that matches exactly once), built with the host
# flags of the Makefile; the PTY test must then fail in the case the mutant
# targets, which its FAIL line names. A mutant that passes, or fails only in
# some other case, means the tests cannot see that fault, and the run fails.
#
# usage: mutants.py [word-dir]       (HOSTCC selects the compiler)

import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

CC = os.environ.get("HOSTCC", "cc")
FLAGS = ["-std=c17", "-D_DEFAULT_SOURCE", "-DTERMIOS", "-Wall", "-Wextra",
         "-Werror", "-Wshadow", "-O1"]
SRCS = ["word.c", "word_buf.c", "word_find.c", "word_menus.c", "word_modes.c",
        "word_pick.c", "word_term.c"]

# (name, file, old, new, the test case that must fail)
MUTANTS = [
    ("wrap-ignores-the-last-space", "word.c",
     "if (sp != WOFF_END)\n\t\t\t\t\treturn sp + 1;",
     "if (sp == WOFF_END + 1)\n\t\t\t\t\treturn sp + 1;", "t_wrap"),
    ("undo-drops-deleted-text", "word_buf.c",
     "\t\t\tif (raw_insert(u->pos, u->ref, u->len) != 0)\n\t\t\t\treturn -1;\n"
     "\t\t\t*pos = u->pos + u->len;\n\t\t}\n\t\tupos--;",
     "\t\t\t*pos = u->pos;\n\t\t}\n\t\tupos--;", "t_vi"),
    ("spill-read-off-by-one", "word_buf.c",
     "return blk_byte(ADDBIT, ref);", "return blk_byte(ADDBIT, ref + 1);", "t_large"),
    ("save-skips-the-rename", "word_buf.c",
     "if (e == 0 && rename(tmp, path) != 0)", "if (e == 0 && 0)", "t_save_menu"),
    ("search-case-flag-ignored", "word_find.c",
     "\tif (match_case)\n\t\treturn a == b;", "\tif (match_case < 0)\n\t\treturn a == b;",
     "t_search_replace"),
    ("replace-no-still-replaces", "word.c",
     "up == 'Y' || up == 'A'", "up == 'Y' || up == 'A' || up == 'N'",
     "t_search_replace"),
    ("vi-dd-keeps-the-newline", "word_modes.c",
     "\t\tp = ed_eol(p);\n\t\tif (p < len)\n\t\t\tp++;",
     "\t\tp = ed_eol(p);\n\t\tif (p > len)\n\t\t\tp++;", "t_vi"),
    ("nano-cuts-do-not-accumulate", "word_modes.c",
     "ed_del(lo, hi, 1, cut_run);", "ed_del(lo, hi, 1, 0);", "t_nano"),
    ("piece-split-off-by-one", "word_buf.c",
     "pt[i + 1].off = pt[i].off + within;", "pt[i + 1].off = pt[i].off + within + 1;",
     "t_undo_scrap"),
    ("picker-lists-directories-last", "word_pick.c",
     "return is_dir[b] - is_dir[a];", "return is_dir[a] - is_dir[b];", "t_picker"),
    ("color-off-still-colors", "word_term.c",
     "col = scr_color && cls > 0", "col = cls > 0", "t_ansi"),
    ("spill-append-ignores-the-end", "word_buf.c",
     "if (lseek(spillfd, (off_t)spilled, SEEK_SET) < 0)", "if (0)", "t_spill_append"),
    ("save-temporary-follows-links", "word_buf.c",
     "O_WRONLY | O_CREAT | O_EXCL, 0600", "O_WRONLY | O_CREAT | O_TRUNC, 0600",
     "t_save_temp"),
    ("replace-deletes-before-it-can-insert", "word.c",
     "buf_replace(s, e - s, f_with, rl) != 0",
     "buf_delete(s, e - s, NULL) != 0 || buf_insert(s, f_with, rl) != 0",
     "t_replace_failure"),
    ("replace-all-answer-sticks", "word.c",
     "\t\t\tr_ask = 0;", "\t\t\tr_ask = r_confirm = 0;", "t_replace_all_local"),
    ("pattern-capacity-cuts-the-query", "word.h",
     "#define WORD_PAT_MAX\tWORD_FIELD_MAX", "#define WORD_PAT_MAX\t64u",
     "t_overlong_pattern"),
    ("csi-parameter-wraps", "word_term.c",
     "if (p[np] < 1000)", "if (1)", "t_csi_overflow"),
    ("signal-leaves-the-terminal-raw", "word_term.c",
     "\tif (rawmode)\n\t\ttty_reset_modes();\n\tsignal(sig, SIG_DFL);",
     "\tif (0)\n\t\ttty_reset_modes();\n\tsignal(sig, SIG_DFL);", "t_signal_restore"),
    ("nano-typing-keeps-the-selection", "word_modes.c",
     "\t\tif (lo < hi)\n\t\t\ted_replace(lo, hi, &ch, 1);", "\t\tif (0)\n\t\t\ted_replace(lo, hi, &ch, 1);",
     "t_select_replace"),
    ("selection-typing-deletes-before-inserting", "word.c",
     "\t\ted_replace(lo, hi, &ch, 1);\n\telse if (ovr",
     "\t\t{ ed_del(lo, hi, 0, 0); ed_ins(&ch, 1); }\n\telse if (ovr",
     "t_select_replace"),
    ("dirty-follows-the-undo-depth", "word_buf.c",
     "return upos > 0 ? ur[upos - 1].seq : 0xffffffffu - ulost;", "return upos;",
     "t_dirty_undo"),
    ("vi-operator-drops-its-prefix-count", "word_modes.c",
     "n = vprefix > 100000 / n ? 100000 : vprefix * n;", "n = n + 0;",
     "t_vi_counts"),
    ("vi-join-ignores-its-count", "word_modes.c",
     "for (i = n > 2 ? n - 1 : 1; i > 0; i--)", "for (i = 1; i > 0; i--)",
     "t_vi_counts"),
    ("vi-paste-ignores-its-count", "word_modes.c",
     "for (i = 0; i < n && i < 1000; i++)", "for (i = 0; i < 1; i++)",
     "t_vi_counts"),
    ("shift-del-fills-the-scrap", "word.c",
     "\tcase K_DEL | KM_SHIFT:\n\t\tdelete_sel(0);",
     "\tcase K_DEL | KM_SHIFT:\n\t\tdelete_sel(1);", "t_undo_scrap"),
]


def build(src_dir, out):
    cmd = [CC] + FLAGS + ["-o", out] + [os.path.join(src_dir, s) for s in SRCS]
    return subprocess.run(cmd, capture_output=True, text=True)


def run_test(word_dir, binary):
    return subprocess.run([sys.executable, "-I", "-u",
                           os.path.join(word_dir, "tests", "word_test.py"), binary],
                          capture_output=True, text=True, timeout=300)


def one(word_dir, m):
    name, path, old, new, case = m
    with tempfile.TemporaryDirectory(prefix="word-mutant-") as td:
        for s in SRCS + ["word.h"]:
            shutil.copy(os.path.join(word_dir, s), td)
        with open(os.path.join(td, path)) as f:
            text = f.read()
        if text.count(old) != 1:
            return name, "anchor matches %d times" % text.count(old), False
        with open(os.path.join(td, path), "w") as f:
            f.write(text.replace(old, new))
        b = build(td, os.path.join(td, "word.host"))
        if b.returncode != 0:
            return name, "does not build: " + b.stderr.strip()[:200], False
        r = run_test(word_dir, os.path.join(td, "word.host"))
        if r.returncode == 0:
            return name, "passed the tests", False
        if ("FAIL " + case) not in r.stdout:
            return name, "failed without failing " + case, False
        return name, "rejected by " + case, True


def main():
    word_dir = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    with tempfile.TemporaryDirectory(prefix="word-mutant-base-") as td:
        b = build(word_dir, os.path.join(td, "word.host"))
        if b.returncode != 0:
            print("mutants: the unmutated sources do not build\n" + b.stderr)
            return 1
        r = run_test(word_dir, os.path.join(td, "word.host"))
        if r.returncode != 0:
            print("mutants: the unmutated program fails the tests\n" + r.stdout)
            return 1
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        for name, what, ok in ex.map(lambda m: one(word_dir, m), MUTANTS):
            print("%s %s: %s" % ("ok  " if ok else "FAIL", name, what))
            bad += not ok
    if bad:
        print("mutants: %d of %d not rejected" % (bad, len(MUTANTS)))
        return 1
    print("mutants: all %d rejected" % len(MUTANTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
