#!/usr/bin/env python3
# Mutation calibration of the picofetch and picotop host tests. The unmutated
# programs must pass render.py first. Each mutant is one deliberately wrong
# copy of a source file (a textual substitution that matches exactly once),
# built with the host flags of the Makefiles; render.py must reject it. A
# mutant that builds and passes means the tests cannot see that fault, and the
# run fails.
#
# usage: mutants.py [picofetch-dir]       (HOSTCC selects the compiler)

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CC = os.environ.get("HOSTCC", "cc")
# "-m32" when the Makefile's probe found an ILP32 host build, else empty. The
# target's long is 32 bits, so the 32-bit build is the one that shows an
# overflow; without it the overflow mutant is reported as not run.
M32 = os.environ.get("PICOFETCH_M32", "").split()
FLAGS = ["-std=c17", "-pedantic-errors", "-D_DEFAULT_SOURCE", "-DHOSTBUILD", "-Wall",
         "-Wextra", "-Werror", "-Wshadow", "-Wconversion", "-O1", "-I."]

# (name, file, old, new)
MUTANTS = [
    ("bar-glyph-one-column-short", "pcdraw.c",
     "(0xfc << (6 - n) & 0xfc)", "(0xfc << (7 - n) & 0xfc)"),
    ("graph-glyph-fills-from-the-top", "pcdraw.c",
     "for (i = PC_GLYPH_ROWS - n; i < PC_GLYPH_ROWS; i++)", "for (i = 0; i < n; i++)"),
    ("mosaic-rows-share-one-pair", "pcdraw.c",
     "int pair = n >> (i / 3 * 2);", "int pair = n >> (i / 3);"),
    ("glyph-sets-a-forbidden-bit", "pcdraw.c",
     "rows[3] = rows[4] = rows[5] = 0xfc;", "rows[3] = rows[4] = rows[5] = 0xfe;"),
    ("sixths-drop-the-remainder", "pcdraw.c",
     "part / total * span + part % total * span / total", "part / total * span"),
    ("bar-edge-keeps-its-own-background", "pcdraw.c",
     "pc_bg(t, next >= 0 ? col[next] : 0);", "pc_bg(t, 0);"),
    ("color-off-still-colors", "pcdraw.c",
     "\tif (t->color)\n\t\tprintf(\"\\033[%dm\", (color & 8 ? 90 : 30) + (color & 7));",
     "\tif (t->color || t->tier != PC_PLAIN)\n"
     "\t\tprintf(\"\\033[%dm\", (color & 8 ? 90 : 30) + (color & 7));"),
    ("bright-colors-mapped-to-base", "pcdraw.c",
     "(color & 8 ? 90 : 30)", "30"),
    ("gigabytes-printed-as-megabytes", "pcdraw.c",
     "else if (kb < 1000L * 1024)", "else if (kb >= 0)"),
    ("uptime-loses-the-days", "pcdraw.c",
     "if (secs >= 86400)", "if (secs >= 864000)"),
    ("plain-tier-writes-escapes", "pcdraw.c",
     "\tif (t->tier != PC_PLAIN)\n\t\tfputs(\"\\033[0m\", stdout);",
     "\tif (t->tier != PC_BANK)\n\t\tfputs(\"\\033[0m\", stdout);"),
    ("absent-and-failed-swapped", "picofetch.c",
     'return r->state == RD_ABSENT ? "not in this kernel" : "read failed";',
     'return r->state == RD_FAILED ? "not in this kernel" : "read failed";'),
    ("crc-compare-inverted", "picofetch.c",
     'hw.image_crc.v == hw.image_crc_stored.v ?\n\t\t    "ok" : "MISMATCH"',
     'hw.image_crc.v != hw.image_crc_stored.v ?\n\t\t    "ok" : "MISMATCH"'),
    ("battery-colored-as-a-load", "picofetch.c",
     "bar(l, hw.battery.v, 100, 1);", "bar(l, hw.battery.v, 100, 0);"),
    ("bank-tier-newline-without-return", "pcdraw.c",
     "\tif (t->tier == PC_BANK)\n\t\tputchar('\\r');",
     "\tif (t->tier == PC_PLAIN)\n\t\tputchar('\\r');"),
    ("rows-below-the-logo-stay-indented", "picofetch.c",
     "indent = logo && row < LOGO_ROWS ? LOGO_COLS + LOGO_GAP : 0;",
     "indent = logo ? LOGO_COLS + LOGO_GAP : 0;"),
    ("logo-kept-on-a-narrow-terminal", "picofetch.c",
     "term.cols < LOGO_COLS + LOGO_GAP + 30", "term.cols < 0"),
    ("logo-facet-colors-split", "picofetch.c",
     "facet[((cx + 1) / 2 + row) % 6]", "facet[(cx / 2 + row) % 6]"),
    ("logo-circle-off-center", "picofetch.c",
     "int dx = 2 * x - 28,", "int dx = 2 * x - 27,"),
    ("watchdog-bits-swapped", "picofetch.c",
     'hw.watchdog_reason.v & 1 ? " timeout" : ""', 'hw.watchdog_reason.v & 2 ? " timeout" : ""'),
    ("xip-hit-rate-in-percent-not-tenths", "picofetch.c",
     "acc / 1000 > 0 ? hit / (acc / 1000) : 0", "acc / 100 > 0 ? hit / (acc / 100) : 0"),
    ("swap-shows-free-as-used", "picofetch.c",
     "bar(l, hw.nswap.v - hw.swapfree.v, hw.nswap.v, 0);", "bar(l, hw.swapfree.v, hw.nswap.v, 0);"),
    ("chip-absent-and-failed-swapped", "picofetch.c",
     'hw.flash_id.state ==\n\t\t    RD_ABSENT ? "not in this kernel" : "read failed"',
     'hw.flash_id.state ==\n\t\t    RD_ABSENT ? "read failed" : "not in this kernel"'),
    ("chip-unread-shown-as-an-identity", "picofetch.c",
     "else if (!hw.flash_id.valid)", "else if (0)"),
    ("chip-unique-id-loses-a-byte", "picofetch.c",
     "for (i = 3; i < 11; i++) {\n\t\t\tn = strlen(l->text);",
     "for (i = 3; i < 10; i++) {\n\t\t\tn = strlen(l->text);"),
    ("audio-open-and-idle-swapped", "picofetch.c",
     'hw.pcm.open ? "in use" :\n\t\t    "idle"', 'hw.pcm.open ? "idle" :\n\t\t    "in use"'),
    ("audio-queue-and-ring-swapped", "picofetch.c",
     "hw.pcm.queued, hw.pcm.ring);", "hw.pcm.ring, hw.pcm.queued);"),
    ("audio-absent-and-failed-swapped", "picofetch.c",
     'hw.pcm.state ==\n\t\t    RD_ABSENT ? "not in this kernel" : "read failed"',
     'hw.pcm.state ==\n\t\t    RD_ABSENT ? "read failed" : "not in this kernel"'),
    ("equal-bus-clock-shown", "picofetch.c",
     "hw.bus_khz.v != hw.cpu_khz.v", "hw.bus_khz.v == hw.cpu_khz.v"),
    ("busy-counts-idle", "../picotop/picotop.c",
     "busy = permille(sum - d[3], sum);", "busy = permille(sum - d[0], sum);"),
    ("shares-since-boot-every-frame", "../picotop/picotop.c",
     "(have_prev ? (unsigned long)prev_cp[i] : 0)", "0UL"),
    ("process-share-in-percent-not-tenths", "../picotop/picotop.c",
     "proc_share[i] = permille(hw.proc[i].ticks - was, sum);",
     "proc_share[i] = permille(hw.proc[i].ticks - was, sum) / 10;"),
    ("share-product-overflows-32-bits", "../picotop/picotop.c",
     "\twhile (whole > 1UL << 20) {", "\twhile (whole > 1UL << 31) {"),
    ("process-order-ascending", "../picotop/picotop.c",
     "return proc_share[a] > proc_share[b];", "return proc_share[a] < proc_share[b];"),
    ("size-order-ignored", "../picotop/picotop.c",
     "return hw.proc[a].bytes > hw.proc[b].bytes;", "return hw.proc[a].pid < hw.proc[b].pid;"),
    ("graph-drawn-upside-down", "../picotop/picotop.c",
     "lvl = h - (grows - 1 - g) * 9;", "lvl = h - g * 9;"),
    ("graph-newest-at-the-left", "../picotop/picotop.c",
     "int at = first + c - (term.cols - (nhist - first));", "int at = first + c;"),
    ("disk-rate-not-per-second", "../picotop/picotop.c",
     "dx = (hw.disk[i].xfer - prev_disk[i].xfer) /\n\t\t\t    (unsigned long)dsecs;",
     "dx = (hw.disk[i].xfer - prev_disk[i].xfer);"),
    ("row-not-clipped", "../picotop/picotop.c",
     "\tif (n > left)\n\t\tn = left;", ""),
    ("tty-left-raw-on-quit", "../picotop/picotop.c",
     "\tfflush(stdout);\n\ttty_restore();\n\tpc_done();\n\treturn 0;",
     "\tfflush(stdout);\n\tpc_done();\n\treturn 0;"),
    ("tty-left-raw-on-a-signal", "../picotop/picotop.c",
     "\ttty_restore();\n\tpc_done();\n\t_exit(128 + sig);", "\tpc_done();\n\t_exit(128 + sig);"),
    ("full-row-erased-at-its-last-cell", "../picotop/picotop.c",
     'fputs(left > 0 ? "\\033[0m\\033[K" : "\\033[0m", stdout);',
     'fputs("\\033[0m\\033[K", stdout);'),
    ("exit-scrolls-the-frame", "../picotop/picotop.c",
     'printf("\\033[%d;1H\\033[K", term.rows);', 'printf("\\033[%d;1H\\n", term.rows + 1);'),
    ("interval-unbounded", "../picotop/picotop.c",
     "else if (c == '+' && interval < 60)", "else if (c == '+')"),
    ("interval-bound-unchecked", "../picotop/picotop.c",
     "if (interval < 1 || interval > 60)", "if (interval < 0)"),
    ("battery-colored-as-a-load-in-picotop", "../picotop/picotop.c",
     'level("Bat", hw.battery.v, 100, 1, cells);', 'level("Bat", hw.battery.v, 100, 0, cells);'),
]


def build(work, out, srcs):
    return subprocess.run([CC, *FLAGS, *M32, "-o", out, *srcs], cwd=work, capture_output=True,
                          text=True)


def main():
    src = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    top = src.parent / "picotop"
    render = [sys.executable, "-I", str(src / "tests" / "render.py")]
    failed = []
    with tempfile.TemporaryDirectory(prefix="picofetch-mutants") as tmp:
        tmp = Path(tmp)

        def stage(name):
            work = tmp / name / "picofetch"
            shutil.copytree(src, work,
                            ignore=shutil.ignore_patterns("*.o", "*-host", "__pycache__"))
            (work.parent / "picotop").mkdir()
            shutil.copy(top / "picotop.c", work.parent / "picotop" / "picotop.c")
            return work

        def run(work):
            for out, srcs in (("picofetch-host", ["picofetch.c", "pcdraw.c",
                                                  "tests/hwinfo_fixture.c"]),
                              ("picotop-host", ["../picotop/picotop.c", "pcdraw.c",
                                                "tests/hwinfo_fixture.c"])):
                b = build(work, out, srcs)
                if b.returncode != 0:
                    return "build", b.stderr
            t = subprocess.run([*render, "./picofetch-host", "./picotop-host"], cwd=work,
                               capture_output=True, text=True)
            return ("pass" if t.returncode == 0 else "fail"), t.stderr.strip()

        verdict, detail = run(stage("unmutated"))
        if verdict != "pass":
            print(f"the unmutated sources do not pass: {verdict}: {detail}", file=sys.stderr)
            return 1
        for name, rel, old, new in MUTANTS:
            if name == "share-product-overflows-32-bits" and not M32:
                print(f"not run {name}: no ILP32 host build, where the overflow shows")
                continue
            work = stage(name)
            path = (work / rel).resolve()
            text = path.read_text()
            if text.count(old) != 1:
                failed.append(f"{name}: the anchor matches {text.count(old)} times in {rel}")
                continue
            path.write_text(text.replace(old, new))
            verdict, detail = run(work)
            if verdict == "fail":
                print(f"rejected {name}: {detail.splitlines()[-1] if detail else ''}")
            elif verdict == "build":
                failed.append(f"{name}: does not build: {detail[-300:]}")
            else:
                failed.append(f"{name}: the tests pass")
    for f in failed:
        print(f"MUTANT NOT REJECTED {f}", file=sys.stderr)
    if failed:
        return 1
    print(f"{len(MUTANTS) - (0 if M32 else 1)} mutants rejected"
          + ("" if M32 else ", 1 not run"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
