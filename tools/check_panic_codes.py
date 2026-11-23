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

"""Hold kernel panic codes and sys/arch/rp2040/doc/PANICS.md to each other.

A kernel panic that reports a broken internal invariant prints a code (K for
sys/kern, D for sys/dev, R for sys/arch/rp2040, then two digits) instead of
a sentence. PANICS.md lists every code with its condition, file and
function, and lists the panics that keep their sentence. The check reads
every panic("...") call in the kernel sources (comments removed, test
directories skipped) and requires: each code to occur at one call site
whose file and function its row names; each row's code to occur in the
sources; every other panic literal to be a verbatim row with that file and
function; and the verbatim rows to match the sources one for one.

usage: check_panic_codes.py ROOT
       check_panic_codes.py --selftest
"""

import re
import sys
import tempfile
from collections import Counter
from pathlib import Path

SOURCE_DIRS = (
    "sys/kern",
    "sys/dev",
    "sys/fs",
    "sys/ufs",
    "sys/sys",
    "sys/arch/rp2040",
    "sys/libkern",
    "sys/net",
)
TABLE = "sys/arch/rp2040/doc/PANICS.md"
CODE = re.compile(r"[KDR][0-9]{2}")
AREA = {"K": "sys/kern/", "D": "sys/dev/", "R": "sys/arch/rp2040/"}
CALL = re.compile(r'\bpanic\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)')
FUNCTION = re.compile(r"^([A-Za-z_]\w*)\s*\(", re.M)


def strip_comments(text):
    """Blank C comments, keeping newlines and string literals."""
    out = []
    i = 0
    while i < len(text):
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = len(text) if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = len(text) if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text[i] == '"':
            j = i + 1
            while j < len(text) and text[j] not in '"\n':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def function_at(code, position):
    """Name of the last function definition that starts before position."""
    name = "?"
    for match in FUNCTION.finditer(code, 0, position):
        line_end = code.find("\n", match.start())
        if not code[match.start() : line_end].rstrip().endswith(";"):
            name = match.group(1)
    return name


def source_calls(root):
    """Return [(message, file, function, line)] for every panic literal."""
    calls = []
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.[ch]")):
            relative = path.relative_to(root).as_posix()
            if "/test/" in relative:
                continue
            code = strip_comments(path.read_text(encoding="latin-1"))
            for match in CALL.finditer(code):
                line = code.count("\n", 0, match.start()) + 1
                calls.append((match.group(1), relative, function_at(code, match.start()), line))
    return calls


def table(text, marker, columns):
    """Return the rows between the marker comments as tuples of cells."""
    match = re.search(rf"<!-- {marker}:begin -->\n(.*?)<!-- {marker}:end -->", text, re.S)
    if match is None:
        raise ValueError(f"{TABLE} lacks the {marker} markers")
    rows = []
    for line in match.group(1).splitlines()[2:]:
        cells = [c.strip() for c in re.split(r"(?<!\\)\|", line.strip())[1:-1]]
        if len(cells) != columns:
            raise ValueError(f"{TABLE}: malformed row {line!r}")
        rows.append(tuple(c.replace("\\|", "|").strip("`") for c in cells))
    return rows


def check(root):
    """Return the list of disagreements between the sources and the table."""
    errors = []
    if not (root / TABLE).is_file():
        raise ValueError(f"{TABLE} is missing")
    text = (root / TABLE).read_text(encoding="utf-8")
    coded = table(text, "panic-codes", 4)
    verbatim = table(text, "panic-verbatim", 3)
    rows = {}
    for code, _condition, path, function in coded:
        if not CODE.fullmatch(code):
            errors.append(f"{TABLE}: {code} is not a panic code")
        elif not path.startswith(AREA[code[0]]):
            errors.append(f"{TABLE}: {code} names {path}, outside {AREA[code[0]]}")
        if code in rows:
            errors.append(f"{TABLE}: {code} has two rows")
        rows[code] = (path, function.removesuffix("()"))
    calls = source_calls(root)
    seen = Counter()
    others = Counter()
    for message, path, function, line in calls:
        if CODE.fullmatch(message):
            seen[message] += 1
            if message not in rows:
                errors.append(f"{path}:{line}: {message} has no row in {TABLE}")
            elif rows[message] != (path, function):
                errors.append(
                    f"{path}:{line}: {message} is in {function}(); "
                    f"its row names {rows[message][0]} {rows[message][1]}()"
                )
        else:
            others[(message, path, function)] += 1
    for code, count in seen.items():
        if count > 1:
            errors.append(f"{code} occurs at {count} call sites")
    for code in rows:
        if code not in seen:
            errors.append(f"{TABLE}: {code} occurs at no call site")
    listed = Counter((m, p, f.removesuffix("()")) for m, p, f in verbatim)
    for key in others.keys() | listed.keys():
        if others[key] != listed[key]:
            message, path, function = key
            errors.append(
                f"{path}: panic {message!r} in {function}() occurs {others[key]} "
                f"times, the verbatim list names it {listed[key]} times"
            )
    return errors, len(seen), sum(others.values())


GOOD_TABLE = """# fixture
<!-- panic-codes:begin -->
| Code | Condition | File | Function |
| --- | --- | --- | --- |
| `K01` | widget lost | `sys/kern/a.c` | `alpha()` |
| `R01` | port fault | `sys/arch/rp2040/b.c` | `beta()` |
<!-- panic-codes:end -->
<!-- panic-verbatim:begin -->
| Message | File | Function |
| --- | --- | --- |
| root: missing | `sys/kern/a.c` | `gamma()` |
<!-- panic-verbatim:end -->
"""
GOOD_A = """/* panic("K99") in a comment is not a call */
void
alpha(void)
{
\tpanic("K01");
}

void
gamma(void)
{
\tpanic("root: missing");
}
"""
GOOD_B = 'static void\nbeta(int x)\n{\n\tpanic("R01");\n}\n'

MUTANTS = {
    "an uncoded sentence": ("a", GOOD_A.replace('panic("K01")', 'panic("widget lost")')),
    "a code without a row": ("a", GOOD_A.replace('panic("K01")', 'panic("K02")')),
    "a code at two sites": ("b", GOOD_B.replace('panic("R01");', 'panic("R01");\n\tpanic("R01");')),
    "a code in another function": ("a", GOOD_A.replace("alpha(void)", "delta(void)")),
    "a stale verbatim row": ("a", GOOD_A.replace('panic("root: missing");', "")),
    "a verbatim sentence twice": (
        "a",
        GOOD_A.replace(
            'panic("root: missing");', 'panic("root: missing");\n\tpanic("root: missing");'
        ),
    ),
    "a row without its site": ("b", "void\nbeta(void)\n{\n}\n"),
    "a row outside its area": (
        "table",
        GOOD_TABLE.replace("`sys/kern/a.c` | `alpha", "`sys/dev/a.c` | `alpha"),
    ),
    "a malformed code": ("table", GOOD_TABLE.replace("`K01`", "`K1`")),
}


def fixture(directory, table_text, a_text, b_text):
    (directory / "sys/kern").mkdir(parents=True, exist_ok=True)
    (directory / "sys/arch/rp2040/doc").mkdir(parents=True, exist_ok=True)
    (directory / TABLE).write_text(table_text, encoding="utf-8")
    (directory / "sys/kern/a.c").write_text(a_text, encoding="latin-1")
    (directory / "sys/arch/rp2040/b.c").write_text(b_text, encoding="latin-1")


def selftest():
    with tempfile.TemporaryDirectory(prefix="panic-codes-") as temporary:
        base = Path(temporary)
        good = base / "good"
        fixture(good, GOOD_TABLE, GOOD_A, GOOD_B)
        errors, _, _ = check(good)
        if errors:
            raise SystemExit(f"check-panic-codes selftest: known-good rejected: {errors}")
        for index, (name, (which, text)) in enumerate(MUTANTS.items()):
            tree = base / f"bad{index}"
            fixture(
                tree,
                text if which == "table" else GOOD_TABLE,
                text if which == "a" else GOOD_A,
                text if which == "b" else GOOD_B,
            )
            try:
                errors, _, _ = check(tree)
            except ValueError as error:
                errors = [str(error)]
            if not errors:
                raise SystemExit(f"check-panic-codes selftest: {name} was accepted")
    print(f"check-panic-codes selftest: known-good accepted, {len(MUTANTS)} known-bad rejected")
    return 0


def main(argv):
    if argv == ["--selftest"]:
        return selftest()
    if len(argv) != 1:
        print("usage: check_panic_codes.py ROOT | --selftest", file=sys.stderr)
        return 2
    try:
        errors, codes, verbatim = check(Path(argv[0]))
    except ValueError as error:
        errors, codes, verbatim = [str(error)], 0, 0
    for error in errors:
        print(f"check-panic-codes: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"check-panic-codes: {codes} coded panics and {verbatim} verbatim panics match {TABLE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
