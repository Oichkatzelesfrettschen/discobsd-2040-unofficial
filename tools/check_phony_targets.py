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
# Require every check-* target of the root Makefile, and of the makefiles it
# includes, to be declared .PHONY. A check target names no file; undeclared,
# a file or directory of the same name in the tree (a test's output, a
# checked-out tool) makes bmake treat the target as up to date and skip the
# gate without a word. Declarations are read from every .PHONY line,
# continuation lines joined, in all the scanned files together, so a target
# defined in one file may be declared in another.
#
# usage: check_phony_targets.py [--self-test] [MAKEFILE...]

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = (
    "Makefile",
    "share/mk/architecture.mk",
    "share/mk/source-date.mk",
    "distrib/rp2040/Makefile.inc",
    "sys/arch/stm32/conf/Makefile.inc",
)
# A rule line: one or more names, then ":" or "::" not followed by "=", so a
# ":=" or "::=" assignment is not a rule. Recipe lines start with a tab.
RULE = re.compile(r"^([^\s:#=.][^:#=]*?)\s*::?(?!=)", re.M)
PHONY = re.compile(r"^\.PHONY\s*:((?:[^\n]*\\\n)*[^\n]*)", re.M)


def undeclared(texts):
    """Return the check targets of TEXTS that no .PHONY line names."""
    targets, phony = set(), set()
    for text in texts:
        for names in RULE.findall(text):
            targets |= {name for name in names.split() if name.startswith("check-")}
        for match in PHONY.finditer(text):
            phony |= set(match.group(1).replace("\\\n", " ").split())
    if not targets:
        raise ValueError("no check-* target found")
    return sorted(targets - phony), len(targets)


def self_test():
    cases = [
        ("a declared target", ["check-a:\n\techo a\n\n.PHONY: check-a\n"], 0),
        ("an undeclared target", ["check-a:\n\techo a\n.PHONY: check-b\ncheck-b:\n"], 1),
        (
            "a declaration on a continuation line",
            [".PHONY: check-a \\\n\t\tcheck-b\ncheck-a:\ncheck-b:\n"],
            0,
        ),
        ("a target declared in another file", ["check-a: all\n", ".PHONY: check-a\n"], 0),
        ("the second target of a two-target rule", ["check-a check-b: all\n.PHONY: check-a\n"], 1),
        ("a variable assignment is not a target", ["check-a:= x\ncheck-b:\n.PHONY: check-b\n"], 0),
    ]
    failed = 0
    for label, texts, expected in cases:
        missing, _ = undeclared(texts)
        ok = len(missing) == expected
        failed += not ok
        print(
            f"self-test: {label}: {len(missing)} undeclared, expected {expected}: "
            f"{'ok' if ok else 'FAIL'}"
        )
    try:
        undeclared(["all:\n"])
        print("self-test: a makefile without check targets is an error: FAIL")
        failed += 1
    except ValueError:
        print("self-test: a makefile without check targets is an error: ok")
    print(f"check_phony_targets self-test: {'pass' if not failed else f'{failed} failed'}")
    return 1 if failed else 0


def main(argv):
    if argv[1:2] == ["--self-test"]:
        return self_test()
    paths = [Path(arg) for arg in argv[1:]] or [ROOT / name for name in DEFAULT]
    texts = []
    for path in paths:
        try:
            texts.append(path.read_text(encoding="utf-8"))
        except OSError as error:
            print(f"ERROR phony-targets: {path}: {error.strerror}", file=sys.stderr)
            return 2
    try:
        missing, total = undeclared(texts)
    except ValueError as error:
        print(f"ERROR phony-targets: {error}", file=sys.stderr)
        return 2
    for name in missing:
        print(f"FAIL phony-targets: {name} is not declared .PHONY")
    if missing:
        return 1
    print(f"PASS phony-targets: {total} check targets, every one declared .PHONY")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
