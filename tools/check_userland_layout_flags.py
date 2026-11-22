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
# One root image serves every RP2040 kernel, so a program built for it sees
# only the layout options all of them share. share/mk/sys.mk passes
# KERNEL_LAYOUT_CFLAGS to every target program; each -DNAME or -DNAME=VALUE
# in it has to be a live options line of every Config the build links, with
# the same value. A name set in some kernels and not others (SINGLE_UFS_ROOT,
# NMOUNT) would give userland the structures of half the kernels.
#
# The same rule fails when a userland source names one of KERNEL_ONLY: a
# program that tests the mount count or the single-root option would read a
# layout no root image can promise. C and lex/yacc comments are ignored, so a
# note that mentions a name is not a use.
#
# usage: check_userland_layout_flags.py --flags FLAGS --root ROOT
#            --sources DIR... --configs CONFIG...
#        check_userland_layout_flags.py --self-test

import argparse
import re
import sys
import tempfile
from pathlib import Path

KERNEL_ONLY = ("NMOUNT", "SINGLE_UFS_ROOT")
SOURCE_SUFFIXES = (".c", ".h", ".y", ".l")
OPTION = re.compile(
    r'options\s+(?:"([A-Za-z_][A-Za-z0-9_]*)(?:=([^"]*))?"'
    r"|([A-Za-z_][A-Za-z0-9_]*))\s*$"
)
FLAG = re.compile(r"-D([A-Za-z_][A-Za-z0-9_]*)(?:=(\S*))?$")
LEXEME = re.compile(r'"(?:\\.|[^"\\\n])*"' r"|'(?:\\.|[^'\\\n])*'" r"|/\*.*?\*/|//[^\n]*", re.S)


class Unreadable(Exception):
    """An input the gate cannot read; the verdict is an error, not a pass."""


def config_options(path):
    """Map each live option of one Config to its value (None if bare)."""
    try:
        text = path.read_text()
    except OSError as error:
        raise Unreadable(f"{path}: {error.strerror}") from error
    options = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line.startswith("options"):
            continue
        match = OPTION.match(line)
        if not match:
            raise Unreadable(f"{path}: unparsed options line: {line}")
        name = match.group(1) or match.group(3)
        options[name] = match.group(2) if match.group(1) else None
    if not options:
        raise Unreadable(f"{path}: no options lines")
    return options


def flag_defects(flags, configs):
    """Return each defect of FLAGS against the parsed CONFIGS."""
    tokens = flags.split()
    if not tokens:
        raise Unreadable("KERNEL_LAYOUT_CFLAGS is empty")
    if not configs:
        raise Unreadable("no Config named")
    defects = []
    for token in tokens:
        match = FLAG.match(token)
        if not match:
            defects.append(f"{token}: not a -DNAME or -DNAME=VALUE flag")
            continue
        name, value = match.group(1), match.group(2)
        for path, options in configs.items():
            if name not in options:
                defects.append(f"{token}: {path} does not set {name}")
            elif options[name] != value:
                have = name if options[name] is None else f"{name}={options[name]}"
                defects.append(f"{token}: {path} sets {have}")
    return defects


def blank(match):
    """Replace one literal or comment by its quotes or its newlines."""
    lexeme = match.group(0)
    if lexeme[0] in "\"'":
        return lexeme[0] * 2
    return "\n" * lexeme.count("\n")


def source_defects(root, directories):
    """Return each userland source line that names a KERNEL_ONLY option."""
    pattern = re.compile(r"\b(" + "|".join(KERNEL_ONLY) + r")\b")
    defects = []
    scanned = 0
    for directory in directories:
        base = root / directory
        if not base.is_dir():
            raise Unreadable(f"{base}: not a directory")
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            scanned += 1
            try:
                text = path.read_text(errors="replace")
            except OSError as error:
                raise Unreadable(f"{path}: {error.strerror}") from error
            # A string or character literal is consumed first, so a comment
            # delimiter inside one never starts a comment; its text is not a use.
            # Each comment becomes its own newlines, keeping line numbers.
            text = LEXEME.sub(blank, text)
            for number, line in enumerate(text.splitlines(), 1):
                for match in pattern.finditer(line):
                    defects.append(
                        f"{path.relative_to(root)}:{number}: "
                        f"names the kernel-only option {match.group(1)}"
                    )
    if scanned == 0:
        raise Unreadable("no userland source found")
    return defects


def verdict(flags, root, sources, config_paths):
    configs = {path: config_options(root / path) for path in config_paths}
    return flag_defects(flags, configs) + source_defects(root, sources)


def self_test():
    shared = (
        "options         LINEAR_INODE_CACHE           # Scan 24 entries.\n"
        "options         COMPACT_SWAPMAP              # 16-bit entries.\n"
    )
    pico = shared + (
        'options         "NMOUNT=1"                  # One UFS root.\n'
        "options         SINGLE_UFS_ROOT             # Derive identity.\n"
    )
    calc = shared + (
        'options         "NMOUNT=3"                  # Three mounts.\n'
        "# SINGLE_UFS_ROOT (which needs NMOUNT=1) is not set.\n"
    )
    common = "-DLINEAR_INODE_CACHE -DCOMPACT_SWAPMAP"
    cases = [
        ("the options every Config shares", common, {}, 0),
        (
            "SINGLE_UFS_ROOT and NMOUNT=1 beside a Config without them",
            common + " -DSINGLE_UFS_ROOT -DNMOUNT=1",
            {},
            2,
        ),
        ("a value that differs", common + " -DCOMPACT_SWAPMAP=1", {}, 2),
        ("a token that is not a definition", common + " -UNMOUNT", {}, 1),
        (
            "a userland source testing NMOUNT",
            common,
            {"bin/probe/probe.c": "#if NMOUNT > 1\nint x;\n#endif\n"},
            1,
        ),
        (
            "a userland source naming SINGLE_UFS_ROOT in code",
            common,
            {"sbin/probe/probe.h": "int SINGLE_UFS_ROOT_seen = SINGLE_UFS_ROOT;\n"},
            1,
        ),
        (
            "a comment opener inside a string before a use",
            common,
            {"bin/probe/probe.c": 'const char *m = "/*"; int n = NMOUNT; /* end */\n'},
            1,
        ),
        (
            "a line comment opener inside a string before a use",
            common,
            {"bin/probe/probe.c": 'const char *u = "a//b"; int n = NMOUNT;\n'},
            1,
        ),
        (
            "an escaped quote in a character literal before a use",
            common,
            {"bin/probe/probe.c": "char q = '\\''; char *c = \"/*\"; int n = NMOUNT;\n"},
            1,
        ),
        (
            "a name inside a string literal",
            common,
            {"bin/probe/probe.c": 'const char *s = "NMOUNT";\n'},
            0,
        ),
        (
            "a comment that mentions NMOUNT",
            common,
            {"bin/probe/probe.c": "/* NMOUNT is the\n   kernel's */ int x; // SINGLE_UFS_ROOT\n"},
            0,
        ),
    ]
    failed = 0
    for label, flags, files, expected in cases:
        with tempfile.TemporaryDirectory(prefix="userland-layout-") as name:
            root = Path(name)
            for directory in ("bin", "sbin"):
                (root / directory).mkdir()
            (root / "bin/true.c").write_text("int main(void) { return 0; }\n")
            for relative, text in files.items():
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_text(text)
            (root / "PICO").write_text(pico)
            (root / "PICOCALC").write_text(calc)
            defects = verdict(flags, root, ["bin", "sbin"], ["PICO", "PICOCALC"])
        ok = len(defects) == expected
        failed += not ok
        print(
            f"self-test: {label}: {len(defects)} defects, expected {expected}: "
            f"{'ok' if ok else 'FAIL'}"
        )
        for defect in defects if not ok else ():
            print(f"  {defect}")
    for label, flags, config in (
        ("empty flags", "", calc),
        ("a Config with no options", common, "# empty\n"),
    ):
        with tempfile.TemporaryDirectory(prefix="userland-layout-") as name:
            root = Path(name)
            (root / "bin").mkdir()
            (root / "bin/true.c").write_text("int main(void) { return 0; }\n")
            (root / "C").write_text(config)
            try:
                verdict(flags, root, ["bin"], ["C"])
                ok = False
            except Unreadable:
                ok = True
        failed += not ok
        print(f"self-test: {label} is an error: {'ok' if ok else 'FAIL'}")
    if failed:
        print(f"check_userland_layout_flags self-test: {failed} failed")
        return 1
    print("check_userland_layout_flags self-test: pass")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--flags")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--sources", nargs="+")
    parser.add_argument("--configs", nargs="+")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    if (
        arguments.flags is None
        or not arguments.root
        or not arguments.sources
        or not arguments.configs
    ):
        parser.error("--flags, --root, --sources and --configs are required")
    try:
        defects = verdict(arguments.flags, arguments.root, arguments.sources, arguments.configs)
    except Unreadable as error:
        print(f"ERROR userland-layout-flags: {error}", file=sys.stderr)
        return 2
    for defect in defects:
        print(f"FAIL userland-layout-flags: {defect}")
    if defects:
        return 1
    print(
        f"PASS userland-layout-flags: {arguments.flags} set alike in "
        f"{len(arguments.configs)} Configs; no userland source names "
        f"{' or '.join(KERNEL_ONLY)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
