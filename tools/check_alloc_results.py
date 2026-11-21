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

"""Hold every allocation in the scanned C files to a reviewed NULL check.

tools/alloc-sites.txt pins each reviewed allocation site: a block names the
file and the reason its NULL handling holds, and carries the exact source
text of the allocation and that handling. The check collapses runs of white
space in the file and in each block, then requires every block's text to
occur exactly once in its file and every mention of an allocator (malloc,
calloc, realloc, reallocf, reallocarray, strdup, strndup, aligned_alloc,
posix_memalign, valloc, alloca; in code, comments or directives alike) to lie inside a pinned
text. It judges no C semantics: a new or edited site fails until a reviewer
reads its NULL handling and writes or updates its block, so the verdict
rests on that review and on the text staying as reviewed.

usage: check_alloc_results.py FILE...
       check_alloc_results.py --selftest
"""

import re
import sys
from pathlib import Path

ALLOCATOR = re.compile(
    r"\b(?:malloc|calloc|realloc|reallocf|reallocarray|strdup|strndup"
    r"|aligned_alloc|posix_memalign|valloc|alloca)\b"
)
PINS = Path(__file__).resolve().parent / "alloc-sites.txt"


def parse_pins(text):
    """Return [(path, reason, snippet)] from the pinned-site file."""
    pins = []
    current = None
    for line in text.split("\n"):
        if line.startswith("@@ "):
            path, _, reason = line[3:].partition(" -- ")
            if not reason.strip():
                raise ValueError(f"pin {line!r} gives no reason")
            current = [path.strip(), reason.strip(), []]
            pins.append(current)
        elif current is not None:
            current[2].append(line)
        elif line.strip() and not line.startswith("#"):
            raise ValueError(f"text before the first pin: {line!r}")
    return [(path, reason, "\n".join(lines).strip()) for path, reason, lines in pins]


def collapse(text):
    """Collapse white space runs; return the text and each character's source offset."""
    chars, offsets = [], []
    for index, char in enumerate(text):
        if char.isspace():
            if chars and chars[-1] != " ":
                chars.append(" ")
                offsets.append(index)
        else:
            chars.append(char)
            offsets.append(index)
    return "".join(chars), offsets


def check(files, pins):
    """Return the disagreements between the files and the pinned sites."""
    errors = []
    by_label = {label: text for label, text in files}
    for path, _, _ in pins:
        if path not in by_label:
            errors.append(f"pin for {path}, which the check does not scan")
    sites = 0
    for label, text in files:
        flat, offsets = collapse(text)
        covered = []
        for path, reason, snippet in pins:
            if path != label:
                continue
            want, _ = collapse(snippet)
            if not ALLOCATOR.search(want):
                errors.append(f"{path}: pin ({reason}) names no allocator")
                continue
            found = [m.start() for m in re.finditer(re.escape(want), flat)]
            if len(found) != 1:
                errors.append(
                    f"{path}: the reviewed text of a pin ({reason}) occurs {len(found)} "
                    "times; review the site and update tools/alloc-sites.txt"
                )
                continue
            start = offsets[found[0]]
            end = offsets[found[0] + len(want) - 1] + 1
            covered.append((start, end))
        for mention in ALLOCATOR.finditer(text):
            sites += 1
            if not any(start <= mention.start() < end for start, end in covered):
                line = text.count("\n", 0, mention.start()) + 1
                errors.append(
                    f"{label}:{line}: {mention.group(0)} outside every reviewed site; "
                    "review its NULL handling and pin it in tools/alloc-sites.txt"
                )
    return errors, sites


GOOD = "char *\nf(void)\n{\n\tp = malloc(4);\n\tif (p == NULL)\n\t\texit(1);\n\treturn p;\n}\n"
GOOD_PINS = "@@ a.c -- exit(1) on NULL\np = malloc(4);\nif (p == NULL)\n\texit(1);\n"
CASES = {
    "known-good": (GOOD, GOOD_PINS, False),
    "re-indented reviewed text": (GOOD.replace("\t", "    "), GOOD_PINS, False),
    "an unreviewed allocation": (
        GOOD.replace("return p;", "q = malloc(8);\n\treturn p;"),
        GOOD_PINS,
        True,
    ),
    "an edited reviewed site": (GOOD.replace("exit(1);", 'puts("x");'), GOOD_PINS, True),
    "the reviewed text twice": (GOOD + GOOD.replace("f(void)", "g(void)"), GOOD_PINS, True),
    "an unpinned alloca": (
        GOOD.replace("return p;", "q = alloca(8);\n\treturn p;"),
        GOOD_PINS,
        True,
    ),
    "an allocator named in a comment": (GOOD + "/* strdup it later */\n", GOOD_PINS, True),
    "a pin for an unscanned file": (
        GOOD,
        GOOD_PINS + "@@ b.c -- exit(1) on NULL\nq = malloc(1);\n",
        True,
    ),
    "a pin that names no allocator": (GOOD, GOOD_PINS + "@@ a.c -- nothing\nreturn p;\n", True),
    "a pin without a reason": (GOOD, GOOD_PINS.replace(" -- exit(1) on NULL", ""), True),
}


def selftest():
    failures = []
    for name, (source, pins_text, bad) in CASES.items():
        try:
            errors, _ = check([("a.c", source)], parse_pins(pins_text))
        except ValueError as error:
            errors = [str(error)]
        if bool(errors) != bad:
            failures.append(
                f"{name}: expected {'rejection' if bad else 'acceptance'}, got {errors}"
            )
    for failure in failures:
        print(f"check-alloc-results selftest: {failure}", file=sys.stderr)
    if failures:
        return 1
    bad = sum(1 for _, _, b in CASES.values() if b)
    print(
        f"check-alloc-results selftest: {len(CASES) - bad} known-good accepted, "
        f"{bad} known-bad rejected"
    )
    return 0


def main(argv):
    if argv == ["--selftest"]:
        return selftest()
    if not argv:
        print("usage: check_alloc_results.py FILE... | --selftest", file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parent.parent
    files = []
    for name in argv:
        path = Path(name).resolve()
        try:
            label = path.relative_to(root).as_posix()
        except ValueError:
            label = path.as_posix()
        files.append((label, path.read_text(encoding="latin-1")))
    try:
        errors, sites = check(files, parse_pins(PINS.read_text(encoding="utf-8")))
    except ValueError as error:
        errors, sites = [f"{PINS.name}: {error}"], 0
    for error in errors:
        print(f"check-alloc-results: {error}", file=sys.stderr)
    if errors:
        return 1
    print(
        f"check-alloc-results: {sites} allocator mentions in {len(files)} files "
        "lie in reviewed sites"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
