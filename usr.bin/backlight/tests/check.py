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
# Host gate of backlight(1). backlight.c is compiled against the controller
# model of tests/bl_fixture.c, which stands in for bl_sysctl.c, and each case
# runs the program in a fresh directory, where BACKLIGHT_FILE names a
# relative file, and compares its exit status, output, the requests the model
# received, the levels it kept and the saved boot level. Each mutant is one
# deliberately wrong copy of backlight.c (a textual substitution that matches
# exactly once); some case must reject it, and the run fails when a mutant
# passes, because a gate that passes input it should reject reports nothing.
#
# usage: check.py [backlight-dir]     (HOSTCC selects the compiler)

import os
import subprocess
import sys
import tempfile
from pathlib import Path

CC = os.environ.get("HOSTCC", "cc")
FLAGS = [
    "-std=c17",
    "-pedantic-errors",
    "-D_DEFAULT_SOURCE",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wshadow",
    "-Wconversion",
    "-O1",
    '-DBACKLIGHT_FILE="boot.level"',
]

USAGE = "usage: backlight [-s] [lcd | kbd] [level]\n"
NOKBD = "backlight: this kernel has no PicoCalc keyboard controller\n"
NOROOT = "backlight: -s: only root sets the boot level\n"

# (label, argv, environment, initial state, initial boot file,
#  status, stdout, stderr, requests, final state, final boot file)
# A boot file of None means absent; "<dir>" makes the path a directory and
# "<full>" a symbolic link to /dev/full.
CASES = [
    ("both lights", [], {}, "32 0", None, 0, "lcd 32\nkbd 0\n", "", "", "32 0", None),
    ("screen set", ["160"], {}, "32 0", None, 0, "lcd 160\n", "", "set lcd 160\n", "160 0", None),
    (
        "screen kept level",
        ["88"],
        {},
        "32 0",
        None,
        0,
        "lcd 80\n",
        "",
        "set lcd 88\n",
        "80 0",
        None,
    ),
    ("screen floor", ["0"], {}, "32 0", None, 0, "lcd 16\n", "", "set lcd 0\n", "16 0", None),
    ("screen top", ["240"], {}, "32 0", None, 0, "lcd 240\n", "", "set lcd 240\n", "240 0", None),
    ("leading zero", ["0096"], {}, "32 0", None, 0, "lcd 96\n", "", "set lcd 96\n", "96 0", None),
    (
        "named screen",
        ["lcd", "48"],
        {},
        "32 0",
        None,
        0,
        "lcd 48\n",
        "",
        "set lcd 48\n",
        "48 0",
        None,
    ),
    ("keys set", ["kbd", "64"], {}, "32 0", None, 0, "kbd 64\n", "", "set kbd 64\n", "32 64", None),
    (
        "keys kept level",
        ["kbd", "40"],
        {},
        "32 0",
        None,
        0,
        "kbd 32\n",
        "",
        "set kbd 40\n",
        "32 32",
        None,
    ),
    ("keys off", ["kbd", "31"], {}, "32 96", None, 0, "kbd 0\n", "", "set kbd 31\n", "32 0", None),
    ("screen read", ["lcd"], {}, "112 64", None, 0, "lcd 112\n", "", "", "112 64", None),
    ("keys read", ["kbd"], {}, "112 64", None, 0, "kbd 64\n", "", "", "112 64", None),
    ("above range", ["241"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("far above range", ["2400"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("long number", ["99999999999999999999"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("negative", ["-1"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("plus sign", ["+16"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("hex", ["0x10"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("trailing byte", ["16x"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("leading space", [" 16"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("empty", [""], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("unknown light", ["led", "16"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("upper case light", ["LCD", "16"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("two lights", ["lcd", "kbd"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("extra argument", ["lcd", "16", "17"], {}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("option late", ["16", "-s"], {"BL_UID": "0"}, "32 0", None, 2, "", USAGE, "", "32 0", None),
    ("save as user", ["-s", "160"], {}, "32 0", None, 1, "", NOROOT, "", "32 0", None),
    ("save current as user", ["-s"], {}, "32 0", None, 1, "", NOROOT, "", "32 0", None),
    (
        "save as root",
        ["-s", "160"],
        {"BL_UID": "0"},
        "32 0",
        None,
        0,
        "lcd 160\n",
        "",
        "set lcd 160\n",
        "160 0",
        "160\n",
    ),
    (
        "save kept level",
        ["-s", "lcd", "88"],
        {"BL_UID": "0"},
        "32 0",
        None,
        0,
        "lcd 80\n",
        "",
        "set lcd 88\n",
        "80 0",
        "80\n",
    ),
    ("save current", ["-s"], {"BL_UID": "0"}, "96 0", None, 0, "lcd 96\n", "", "", "96 0", "96\n"),
    (
        "save shorter level",
        ["-s", "16"],
        {"BL_UID": "0"},
        "32 0",
        "240\n",
        0,
        "lcd 16\n",
        "",
        "set lcd 16\n",
        "16 0",
        "16\n",
    ),
    (
        "save keys",
        ["-s", "kbd", "64"],
        {"BL_UID": "0"},
        "32 0",
        None,
        2,
        "",
        USAGE,
        "",
        "32 0",
        None,
    ),
    (
        "save unwritable",
        ["-s", "64"],
        {"BL_UID": "0"},
        "32 0",
        "<dir>",
        1,
        "lcd 64\n",
        "backlight: cannot write boot.level\n",
        "set lcd 64\n",
        "64 0",
        "<dir>",
    ),
    ("no controller", [], {"BL_ERR": "EOPNOTSUPP"}, "32 0", None, 1, "", NOKBD, "", "32 0", None),
    (
        "no controller set",
        ["160"],
        {"BL_ERR": "EOPNOTSUPP"},
        "32 0",
        None,
        1,
        "",
        NOKBD,
        "",
        "32 0",
        None,
    ),
    (
        "not setuid",
        ["160"],
        {"BL_ERR": "EPERM"},
        "32 0",
        None,
        1,
        "",
        "backlight: permission denied: install backlight setuid root\n",
        "",
        "32 0",
        None,
    ),
    (
        "no answer",
        ["kbd"],
        {"BL_ERR": "EIO"},
        "32 0",
        None,
        1,
        "",
        "backlight: the keyboard controller did not answer\n",
        "",
        "32 0",
        None,
    ),
    (
        "set refused",
        ["kbd", "64"],
        {"BL_ERR_SET": "EIO"},
        "32 0",
        None,
        1,
        "",
        "backlight: the keyboard controller did not answer\n",
        "",
        "32 0",
        None,
    ),
]

# One substitution each in backlight.c; each must make some case fail.
MUTANTS = [
    ("level bound off by one", "if (v > BL_LEVEL_MAX)", "if (v > BL_LEVEL_MAX + 1)"),
    ("empty level accepted", "return p == s || *p", "return *p"),
    ("trailing byte accepted", "*p != '\\0' ? -1 : v", "0 ? -1 : v"),
    ("non-root save", "if (saving && !bl_root())", "if (saving && 0)"),
    ("keyboard save", "(saving && light != BL_LCD)", "(saving && 0)"),
    ("extra argument accepted", "if (argc > 1 || (saving", "if (argc > 9 || (saving"),
    (
        "keys read as screen",
        "argv[1][0] == 'k' ? BL_KBD : BL_LCD",
        "argv[1][0] == 'l' ? BL_KBD : BL_LCD",
    ),
    (
        "requested level shown",
        "if (show(light, kept) != 0)\n\t\treturn 1;\n\tif (saving",
        "if (show(light, level >= 0 ? level : kept) != 0)\n\t\treturn 1;\n\tif (saving",
    ),
    ("requested level saved", "save(kept)", "save(level >= 0 ? level : kept)"),
    ("boot file not truncated", "O_CREAT | O_TRUNC", "O_CREAT"),
    (
        "save failure ignored",
        "error = say(fd, p);\n\treturn close(fd) != 0 || error;",
        "error = say(fd, p);\n\treturn close(fd) != 0 && error;",
    ),
    ("controller absent as no answer", "\tcase EOPNOTSUPP:\n", ""),
    (
        "set error ignored",
        "error = bl_set(light, level);\n\t\tif (error != 0)",
        "error = bl_set(light, level);\n\t\tif (0)",
    ),
    ("screen listed alone", "light <= BL_KBD; light++", "light < BL_KBD; light++"),
]

# A write that fails after the open needs a file that refuses data; Linux
# has /dev/full, and a host without it skips that case and its mutant.
if Path("/dev/full").exists():
    CASES.append(
        (
            "save write fails",
            ["-s", "64"],
            {"BL_UID": "0"},
            "32 0",
            "<full>",
            1,
            "lcd 64\n",
            "backlight: cannot write boot.level\n",
            "set lcd 64\n",
            "64 0",
            "<full>",
        )
    )
else:
    print("check: skip the failed boot-file write and its mutant: no /dev/full on this host")
    MUTANTS = [m for m in MUTANTS if m[0] != "save failure ignored"]


def build(src_dir, backlight_c, out):
    cmd = [
        CC,
        *FLAGS,
        "-I",
        str(src_dir),
        "-o",
        str(out),
        str(backlight_c),
        str(src_dir / "tests" / "bl_fixture.c"),
    ]
    return subprocess.run(cmd, capture_output=True, text=True)


def run_case(binary, case):
    (label, argv, env_extra, state, boot, status, out, err, requests, final_state, final_boot) = (
        case
    )
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / "state").write_text(state + "\n")
        if boot == "<dir>":
            (work / "boot.level").mkdir()
        elif boot == "<full>":
            (work / "boot.level").symlink_to("/dev/full")
        elif boot is not None:
            (work / "boot.level").write_text(boot)
        env = {
            "PATH": os.environ.get("PATH", ""),
            "BL_STATE": str(work / "state"),
            "BL_LOG": str(work / "log"),
            **env_extra,
        }
        proc = subprocess.run(
            [str(binary), *argv], cwd=work, env=env, capture_output=True, text=True, timeout=10
        )
        got_requests = (work / "log").read_text() if (work / "log").exists() else ""
        got_state = (work / "state").read_text().strip()
        path = work / "boot.level"
        if path.is_symlink():
            got_boot = "<full>"
        elif path.is_dir():
            got_boot = "<dir>"
        else:
            got_boot = path.read_text() if path.exists() else None
        problems = []
        for what, want, got in (
            ("status", status, proc.returncode),
            ("stdout", out, proc.stdout),
            ("stderr", err, proc.stderr),
            ("requests", requests, got_requests),
            ("state", final_state, got_state),
            ("boot file", final_boot, got_boot),
        ):
            if want != got:
                problems.append(f"{label}: {what} {got!r}, expected {want!r}")
        return problems


def run_all(binary):
    problems = []
    for case in CASES:
        problems.extend(run_case(binary, case))
    return problems


def main():
    src_dir = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    source = (src_dir / "backlight.c").read_text()
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp) / "backlight-host"
        built = build(src_dir, src_dir / "backlight.c", binary)
        if built.returncode != 0:
            print(built.stdout + built.stderr, file=sys.stderr)
            print("check: backlight.c does not build on the host", file=sys.stderr)
            return 1
        problems = run_all(binary)
        for problem in problems:
            print("check: " + problem, file=sys.stderr)
        if problems:
            return 1
        print(f"check: {len(CASES)} cases pass")

        for label, old, new in MUTANTS:
            if source.count(old) != 1:
                print(
                    f"mutant {label}: the substitution matches {source.count(old)} times, not once",
                    file=sys.stderr,
                )
                failures += 1
                continue
            mutant_c = Path(tmp) / "backlight.c"
            mutant_c.write_text(source.replace(old, new))
            mutant_bin = Path(tmp) / "mutant"
            built = build(src_dir, mutant_c, mutant_bin)
            if built.returncode != 0:
                print(f"mutant {label}: does not build", file=sys.stderr)
                print(built.stderr, file=sys.stderr)
                failures += 1
                continue
            caught = run_all(mutant_bin)
            if caught:
                print(f"mutant {label}: rejected ({caught[0]})")
            else:
                print(f"mutant {label}: passes every case", file=sys.stderr)
                failures += 1
    if failures:
        print(f"check: {failures} of {len(MUTANTS)} mutants not rejected", file=sys.stderr)
        return 1
    print(f"check: all {len(MUTANTS)} mutants rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
