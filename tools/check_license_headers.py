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

"""Compare every GizmoBSD file header with the license words of NOTICE section 1.

NOTICE section 1 names the BSD 3-Clause conditions and states that a file
carrying a GizmoBSD copyright line is under them. A header whose words stop
early (a truncated disclaimer) or drift from that text grants less than the
file claims, so a redistributor cannot rely on it. This gate reads the
conditions and the GizmoBSD year range from NOTICE, the year range from
etc/COPYRIGHT, and checks each tracked file whose first lines hold a
"Copyright ... GizmoBSD" line:

- the line has the exact form "Copyright (c) YEARS GizmoBSD" (YEARS: 2026,
  2020-2026 or a comma list of both forms);
- every year lies inside the GizmoBSD range of NOTICE and of etc/COPYRIGHT;
- the conditions of NOTICE section 1 follow the line word for word, from
  "Redistribution and use" through "POSSIBILITY OF SUCH DAMAGE." Comment
  leaders (#, *, //, ;, the roff comment) and list markers (bullet or
  "1.") are not words; wrapping and spacing are not either.

NOTICE and etc/COPYRIGHT are the reference, never subjects. A file that
mentions the holder in prose (a document, an instruction file) lacks a
copyright line at the head of a line and is outside the population.

Exit status: 0 every header matches, 1 a header differs (each named with its
line), 2 the reference or the file list cannot be read, or no header was
found, so an empty population never passes.

usage: check_license_headers.py [--root DIR]
       check_license_headers.py --selftest
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import textwrap

HEADER_LINES = 40
REGION_LINES = 120
REFERENCE_PATHS = ("NOTICE", "etc/COPYRIGHT")
START = "Redistribution and use in source and binary forms, with or without"
END = "POSSIBILITY OF SUCH DAMAGE."
CLAUSES = (
    "Redistributions of source code must retain the above copyright notice",
    "Redistributions in binary form must reproduce the above copyright notice",
    "Neither the name of the copyright holder nor the names of its",
)

COMMENT_LEADER = re.compile(r'^\s*(?:/\*+|\*+/?|//+|#+|;+|\.\\"|dnl\b|--+|@+|%+)?[ \t]?')
COMMENT_TRAILER = re.compile(r"\s*\*/\s*$")
LIST_MARKER = re.compile(r"(?<![A-Za-z0-9])(?:\*|[123]\.)\s+(?=Redistributions|Neither)")
GIZMO_LINE = re.compile(r"^Copyright\b.*GizmoBSD")
EXACT_LINE = re.compile(r"^Copyright \(c\) ([0-9][0-9, -]*) GizmoBSD$")
COPYRIGHT_FILE_LINE = re.compile(r"\(c\) ([0-9][0-9, -]*) GizmoBSD")


class ReferenceError_(Exception):
    """The reference text or the file list cannot supply a verdict."""


def strip_leader(line):
    return COMMENT_TRAILER.sub("", COMMENT_LEADER.sub("", line, count=1))


def words(lines):
    """Join lines without comment leaders and list markers into one spaced string."""
    joined = " ".join(strip_leader(line) for line in lines)
    return LIST_MARKER.sub("", " ".join(joined.split()))


def expand_years(spec):
    """Return the set of years in "2026", "2020-2026" or a comma list of both."""
    years = set()
    for part in spec.replace(" ", "").split(","):
        bounds = part.split("-")
        if not all(b.isdigit() and len(b) == 4 for b in bounds) or len(bounds) > 2:
            raise ValueError(f"unreadable year list {spec!r}")
        low, high = int(bounds[0]), int(bounds[-1])
        if high < low:
            raise ValueError(f"descending year range {spec!r}")
        years.update(range(low, high + 1))
    return years


def reference_from_text(notice_text, copyright_text):
    """Return (conditions, NOTICE years, etc/COPYRIGHT years) or raise ReferenceError_."""
    flat = " ".join(notice_text.split())
    flat = LIST_MARKER.sub("", flat)
    start = flat.find(START)
    end = flat.find(END, start) if start >= 0 else -1
    if start < 0 or end < 0:
        raise ReferenceError_("NOTICE: the conditions of section 1 are not found")
    conditions = flat[start : end + len(END)]
    for clause in CLAUSES:
        if clause not in conditions:
            raise ReferenceError_(f"NOTICE: section 1 lacks the clause {clause!r}")
    notice_years = None
    for line in notice_text.splitlines():
        match = EXACT_LINE.match(line.strip())
        if match:
            notice_years = expand_years(match.group(1))
            break
    if notice_years is None:
        raise ReferenceError_("NOTICE: no 'Copyright (c) YEARS GizmoBSD' line")
    match = COPYRIGHT_FILE_LINE.search(copyright_text)
    if match is None:
        raise ReferenceError_("etc/COPYRIGHT: no '(c) YEARS GizmoBSD' entry")
    return conditions, notice_years, expand_years(match.group(1))


def check_header(lines, conditions, notice_years, copyright_years):
    """Return (copyright line number or None, problems) for the head of one file."""
    head = lines[:HEADER_LINES]
    for number, line in enumerate(head):
        if not GIZMO_LINE.match(strip_leader(line).strip()):
            continue
        stripped = strip_leader(line).strip()
        match = EXACT_LINE.match(stripped)
        if match is None:
            return number + 1, [
                f"copyright line is not 'Copyright (c) YEARS GizmoBSD': {stripped!r}"
            ]
        problems = []
        try:
            years = expand_years(match.group(1))
        except ValueError as error:
            return number + 1, [str(error)]
        for label, allowed in (("NOTICE", notice_years), ("etc/COPYRIGHT", copyright_years)):
            outside = sorted(years - allowed)
            if outside:
                problems.append(f"year {outside[0]} is outside the GizmoBSD range of {label}")
        after = words(lines[number + 1 : number + 1 + REGION_LINES])
        if conditions not in after:
            problems.append(missing_words(after, conditions))
        return number + 1, problems
    return None, []


def missing_words(after, conditions):
    """Name where the file's words stop matching the conditions."""
    anchor = after.find(START)
    if anchor < 0:
        return "the BSD 3-Clause conditions do not follow the copyright line"
    have = after[anchor:].split()
    want = conditions.split()
    index = 0
    while index < min(len(have), len(want)) and have[index] == want[index]:
        index += 1
    found = repr(have[index]) if index < len(have) else "no more words"
    return (
        f"the conditions differ from NOTICE section 1 at word {index + 1} of {len(want)}: "
        f"the file has {found}, NOTICE has {want[index]!r}"
    )


def git(root, *args):
    result = subprocess.run(["git", "-C", root, *args], capture_output=True, check=False)
    return result


def tracked_candidates(root):
    result = git(root, "grep", "-I", "-l", "-z", "-E", "Copyright.*GizmoBSD")
    if result.returncode == 1:
        return []
    if result.returncode != 0:
        raise ReferenceError_("git grep failed: " + result.stderr.decode(errors="replace").strip())
    names = [n for n in result.stdout.decode(errors="surrogateescape").split("\0") if n]
    return sorted(n for n in names if n not in REFERENCE_PATHS)


def read_text(root, name):
    try:
        with open(os.path.join(root, name), "rb") as handle:
            return handle.read().decode("utf-8", errors="replace")
    except OSError as error:
        raise ReferenceError_(f"{name}: unreadable: {error}") from error


def run(root):
    """Return (checked, findings); raise ReferenceError_ when no verdict is possible."""
    conditions, notice_years, copyright_years = reference_from_text(
        read_text(root, "NOTICE"), read_text(root, "etc/COPYRIGHT")
    )
    checked = 0
    findings = []
    for name in tracked_candidates(root):
        lines = read_text(root, name).splitlines()
        number, problems = check_header(lines, conditions, notice_years, copyright_years)
        if number is None:
            continue
        checked += 1
        findings.extend(f"{name}:{number}: {problem}" for problem in problems)
    if checked == 0:
        raise ReferenceError_("no tracked file carries a GizmoBSD copyright line")
    return checked, findings, notice_years


# Fixtures: the standard BSD 3-Clause conditions, wrapped and led in each style
# the tree uses, so that the selftest does not depend on the live NOTICE.
FIXTURE_CONDITIONS = (
    "Redistribution and use in source and binary forms, with or without "
    "modification, are permitted provided that the following conditions are met: "
    "{one}Redistributions of source code must retain the above copyright notice, this "
    "list of conditions and the following disclaimer. "
    "{two}Redistributions in binary form must reproduce the above copyright notice, "
    "this list of conditions and the following disclaimer in the documentation "
    "and/or other materials provided with the distribution. "
    "{three}Neither the name of the copyright holder nor the names of its "
    "contributors may be used to endorse or promote products derived from "
    "this software without specific prior written permission. "
    'THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" '
    "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE "
    "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE "
    "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE "
    "FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL "
    "DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR "
    "SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER "
    "CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, "
    "OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE "
    "OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
)
MARKERS = {"none": ("", "", ""), "bullet": ("* ", "* ", "* "), "number": ("1. ", "2. ", "3. ")}
STYLES = {
    "hash": ("# ", None, None),
    "star-block": (" * ", "/*", " */"),
    "line-slash": ("// ", None, None),
    "semicolon": ("; ", None, None),
    "roff": ('.\\" ', None, None),
    "bare": ("", None, None),
}


def fixture_body(marker, cut=None):
    one, two, three = MARKERS[marker]
    text = FIXTURE_CONDITIONS.format(one=one, two=two, three=three)
    return text if cut is None else text[:cut]


def fixture_header(style, marker, copyright_line="Copyright (c) 2026 GizmoBSD", body=None):
    leader, opener, closer = STYLES[style]
    text = fixture_body(marker) if body is None else body
    wrapped = textwrap.wrap(text, 74)
    lines = [copyright_line, "All rights reserved.", ""] + wrapped
    out = [leader + line if line else leader.rstrip() for line in lines]
    if opener:
        out = [opener] + out + [closer.strip()]
    return out


def selftest():
    def make_notice(holder_line="Copyright (c) 2026 GizmoBSD", body=None):
        text = fixture_body("bullet") if body is None else body
        return "\n".join(
            ["1. DiscoBSD", "", holder_line, "All rights reserved.", ""] + textwrap.wrap(text, 74)
        )

    notice = make_notice()
    conditions, notice_years, copyright_years = reference_from_text(
        notice, "Copyright (c) 2020-2026 DiscoBSD; (c) 2026 GizmoBSD; (c) 2014 RetroBSD;\n"
    )
    if notice_years != {2026} or copyright_years != {2026}:
        raise SystemExit(f"selftest: the year ranges read {notice_years} and {copyright_years}")
    cases = 0
    for style in STYLES:
        for marker in MARKERS:
            header = fixture_header(style, marker) + ["", "code follows"]
            number, problems = check_header(header, conditions, notice_years, copyright_years)
            if number is None or problems:
                raise SystemExit(f"selftest: good header rejected ({style}, {marker}): {problems}")
            cases += 1

    def must_reject(label, header):
        number, problems = check_header(header, conditions, notice_years, copyright_years)
        if number is None or not problems:
            raise SystemExit(f"selftest: bad header accepted: {label}")
        print(f"mutant rejected: {label}")

    full = fixture_body("bullet")
    for style in ("hash", "star-block"):
        for label, cut_text in (
            ("disclaimer ends at 'ADVISED OF THE'", full[: full.rindex("POSSIBILITY")].rstrip()),
            (
                "disclaimer ends at 'OUT OF THE USE'",
                full[: full.rindex("OF THIS SOFTWARE")].rstrip(),
            ),
            ("final word dropped", full[: -len("DAMAGE.")].rstrip()),
            (
                "clause 3 dropped",
                full[: full.index("* Neither")] + full[full.index("THIS SOFTWARE IS") :],
            ),
            (
                "clause 2 changed",
                full.replace("in binary form must reproduce", "in binary form may reproduce"),
            ),
            ("disclaimer reworded", full.replace('"AS IS"', '"AS-IS"')),
            ("conditions absent", "Permission is hereby granted."),
        ):
            must_reject(f"{style}: {label}", fixture_header(style, "none", body=cut_text))
    must_reject(
        "year outside the range", fixture_header("hash", "none", "Copyright (c) 2027 GizmoBSD")
    )
    must_reject(
        "range reaches past the range",
        fixture_header("hash", "none", "Copyright (c) 2025-2026 GizmoBSD"),
    )
    must_reject("no (c)", fixture_header("hash", "none", "Copyright 2026 GizmoBSD"))
    must_reject("holder suffix", fixture_header("hash", "none", "Copyright (c) 2026 GizmoBSD Inc"))
    must_reject(
        "year list unreadable", fixture_header("hash", "none", "Copyright (c) 20x6 GizmoBSD")
    )
    blank = ["# Copyright (c) 2026 GizmoBSD", "#"] + ["# unrelated"] * 5
    must_reject("copyright line without conditions", blank)
    if (
        check_header(
            ["# usage: tool", "# a prose mention of GizmoBSD"],
            conditions,
            notice_years,
            copyright_years,
        )[0]
        is not None
    ):
        raise SystemExit("selftest: a file with no copyright line was taken for a subject")

    for bad in ("2026-2020", "26", "2026-", ""):
        try:
            expand_years(bad)
        except ValueError:
            continue
        raise SystemExit(f"selftest: year list {bad!r} was accepted")
    whole = fixture_body("bullet")
    for broken, label in (
        (
            make_notice(body=whole.replace("POSSIBILITY OF SUCH DAMAGE.", "")),
            "NOTICE without its last words",
        ),
        (make_notice(holder_line="Copyright (c) 2026 DiscoBSD"), "NOTICE without a GizmoBSD line"),
        (
            make_notice(body=whole.replace("Neither the name of the copyright holder", "Nobody")),
            "NOTICE without clause 3",
        ),
        (
            make_notice(body=START + " " + END),
            "NOTICE with empty conditions",
        ),
    ):
        try:
            reference_from_text(broken, "(c) 2026 GizmoBSD")
        except ReferenceError_:
            print(f"reference rejected: {label}")
            continue
        raise SystemExit(f"selftest: reference accepted: {label}")

    selftest_tree(notice)
    print(f"selftest: ok ({cases} good headers)")


def selftest_tree(notice):
    """Run the whole gate over throwaway repositories: a pass, a failure and no verdict."""
    with tempfile.TemporaryDirectory() as root:
        if git(root, "init", "-q").returncode != 0:
            raise SystemExit("selftest: git init failed")

        def put(name, lines):
            path = os.path.join(root, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("\n".join(lines) + "\n")
            git(root, "add", name)

        put("NOTICE", notice.splitlines())
        put("etc/COPYRIGHT", ["Copyright (c) 2020-2026 DiscoBSD; (c) 2026 GizmoBSD;"])
        try:
            run(root)
        except ReferenceError_:
            print("tree rejected: no subject")
        else:
            raise SystemExit("selftest: a tree without any GizmoBSD header passed")
        put(
            "good.py",
            ["#!/usr/bin/env python3"] + fixture_header("hash", "number") + ["import sys"],
        )
        put("good.c", fixture_header("star-block", "bullet") + ["int main(void) { return 0; }"])
        put("doc.md", ["Files carry a Copyright (c) <year> GizmoBSD line."])
        checked, findings, _years = run(root)
        if checked != 2 or findings:
            raise SystemExit(f"selftest: clean tree gave {checked} headers and {findings}")
        short = fixture_body("none")
        short = short[: short.rindex("POSSIBILITY")].rstrip()
        put("short.py", fixture_header("hash", "none", body=short) + ["import sys"])
        checked, findings, _years = run(root)
        if checked != 3 or len(findings) != 1 or not findings[0].startswith("short.py:1:"):
            raise SystemExit(f"selftest: truncated header not named alone: {findings}")
        print("tree rejected: truncated header named " + findings[0].split(":")[0])
        os.remove(os.path.join(root, "NOTICE"))
        try:
            run(root)
        except ReferenceError_:
            print("tree rejected: no NOTICE")
        else:
            raise SystemExit("selftest: a tree without NOTICE passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    try:
        checked, findings, years = run(args.root)
    except ReferenceError_ as error:
        print(f"check-license-headers: {error}", file=sys.stderr)
        return 2
    for finding in findings:
        print(f"check-license-headers: {finding}", file=sys.stderr)
    if findings:
        print(
            f"check-license-headers: {len(findings)} defects in {checked} headers", file=sys.stderr
        )
        return 1
    span = f"{min(years)}-{max(years)}" if len(years) > 1 else str(min(years))
    print(
        f"check-license-headers: {checked} GizmoBSD headers carry the NOTICE section 1 words "
        f"(years {span})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
