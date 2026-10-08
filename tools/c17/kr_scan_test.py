"""Calibrate kr_scan.py and the inventory reconciliation.

The fixture marks every line the scan must report. The scan must report
exactly those lines, and the host compiler, run with -Wold-style-definition
and -Wstrict-prototypes, must report only marked lines. Each known-bad
variant must be rejected:

  - a scan that drops a calibrated shape: attributes, declaration lists,
    pointer qualifiers, array subscripts, nested grouping, returned
    function types, line splices or block-scope declarations;
  - a scan without #if alternative tracking, or one keeping a one-armed
    #if's depth after #endif;
  - a scan blanking whole string literals, reading block-scope calls as
    declarations, or reading a directive inside a comment;
  - a reconciliation handed a compiler finding the scan lacks;
  - a diagnostic parser that requires the English severity label;
  - an inventory whose --build compiles no requested source.
"""

import os
import re
import shlex
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import c17_inventory  # noqa: E402
import kr_scan  # noqa: E402

FIXTURE = HERE / "fixtures" / "kr_shapes.c"
EXPECT = re.compile(r"expect: (definition|declaration) (\w+)")
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def expected_findings(path):
    result = set()
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        for match in EXPECT.finditer(line):
            result.add((lineno, match.group(1), match.group(2)))
    return result


def scanned(text):
    return {(f.line, f.kind, f.name) for f in kr_scan.scan_text("fixture.c", text)}


def main():
    text = FIXTURE.read_text()
    expected = expected_findings(FIXTURE)
    check(len(expected) >= 12, "fixture lost its expectation markers")
    got = scanned(text)
    for item in sorted(expected - got):
        check(False, f"scan missed line {item[0]}: {item[1]} {item[2]}")
    for item in sorted(got - expected):
        check(False, f"scan reported unmarked line {item[0]}: {item[1]} {item[2]}")

    # Known-bad scans: each calibrated shape must be load-bearing.
    saved_words = kr_scan.ATTRIBUTE_WORDS
    kr_scan.ATTRIBUTE_WORDS = set()
    try:
        check(scanned(text) != got, "a scan blind to attributes matched the fixture")
    finally:
        kr_scan.ATTRIBUTE_WORDS = saved_words
    original = kr_scan.definition_follows
    kr_scan.definition_follows = lambda tokens, k: k < len(tokens) and tokens[k][0] == "{"
    try:
        check(scanned(text) != got, "a scan without declaration lists matched the fixture")
    finally:
        kr_scan.definition_follows = original
    original = kr_scan.apply_conditionals
    kr_scan.apply_conditionals = lambda kinds, brace, stack: brace
    try:
        check(scanned(text) != got, "a scan blind to #if alternatives matched the fixture")
    finally:
        kr_scan.apply_conditionals = original
    original = kr_scan.strip_source

    def raw_directives(source, conditionals=None):
        if conditionals is not None:
            for lineno, line in enumerate(source.split("\n"), 1):
                match = re.match(r"[ \t]*(#.*)", line)
                if match and kr_scan.CONDITIONAL.match(match.group(1)):
                    conditionals.append((lineno, kr_scan.CONDITIONAL.match(match.group(1))[1]))
        return original(source)

    kr_scan.strip_source = raw_directives
    try:
        check(scanned(text) != got, "a scan reading directives inside comments matched the fixture")
    finally:
        kr_scan.strip_source = original
    original = kr_scan.declaration_prefix
    kr_scan.declaration_prefix = lambda tokens, k: False
    try:
        check(scanned(text) != got, "a scan blind to block-scope declarations matched the fixture")
    finally:
        kr_scan.declaration_prefix = original
    kr_scan.declaration_prefix = lambda tokens, k: True
    try:
        check(scanned(text) != got, "a scan reading block-scope calls as declarations matched")
    finally:
        kr_scan.declaration_prefix = original
    original = kr_scan.apply_conditionals

    def first_alternative(kinds, brace, stack):
        for kind in kinds:
            if kind in ("if", "ifdef", "ifndef"):
                stack.append([brace, None])
            elif kind in ("elif", "else") and stack:
                if stack[-1][1] is None:
                    stack[-1][1] = brace
                brace = stack[-1][0]
            elif kind == "endif" and stack:
                opened, first = stack.pop()
                if first is not None:
                    brace = first
        return brace

    kr_scan.apply_conditionals = first_alternative
    try:
        check(scanned(text) != got, "a scan keeping a one-armed #if's depth matched the fixture")
    finally:
        kr_scan.apply_conditionals = original
    original = kr_scan.strip_source
    kr_scan.strip_source = lambda source, conditionals=None: (
        original(source, conditionals).replace('"', " ").replace("'", " ")
    )
    try:
        check(scanned(text) != got, "a scan blanking whole literals matched the fixture")
    finally:
        kr_scan.strip_source = original
    original = kr_scan.grouped_span

    def single_group(tokens, k):
        span = original(tokens, k)
        return (
            span
            if span
            and tokens[k + 1][0] != "("
            and "(" not in {tok for tok, _ in tokens[k + 1 : span[0]]}
            else None
        )

    kr_scan.grouped_span = single_group
    try:
        check(scanned(text) != got, "a scan reading one grouping layer matched the fixture")
    finally:
        kr_scan.grouped_span = original
    original = kr_scan.join_splices
    kr_scan.join_splices = lambda source: source
    try:
        check(scanned(text) != got, "a scan blind to line splices matched the fixture")
    finally:
        kr_scan.join_splices = original
    original = kr_scan.subscripts_end
    kr_scan.subscripts_end = lambda tokens, j: j
    try:
        check(scanned(text) != got, "a scan blind to array declarators matched the fixture")
    finally:
        kr_scan.subscripts_end = original
    saved_qualifiers = kr_scan.POINTER_QUALIFIERS
    kr_scan.POINTER_QUALIFIERS = set()
    try:
        check(scanned(text) != got, "a scan blind to pointer qualifiers matched the fixture")
    finally:
        kr_scan.POINTER_QUALIFIERS = saved_qualifiers
    original = kr_scan.grouped_declarator
    kr_scan.grouped_declarator = lambda tokens, k: (
        original(tokens, k) if k + 1 < len(tokens) and tokens[k + 1][0] == "*" else -1
    )
    try:
        check(scanned(text) != got, "a scan reading only (*name)() matched the fixture")
    finally:
        kr_scan.grouped_declarator = original

    # The compiler oracle sees active code only and must stay inside the marks.
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    result = subprocess.run(
        compiler
        + [
            "-std=gnu17",
            "-fsyntax-only",
            "-Wold-style-definition",
            "-Wstrict-prototypes",
            str(FIXTURE),
        ],
        capture_output=True,
        text=True,
    )
    check(result.returncode == 0, "host compiler rejected the fixture:\n" + result.stderr)
    marked_lines = {line for line, _, _ in expected}
    compiler_lines = set()
    for line in result.stderr.splitlines():
        match = c17_inventory.DIAGNOSTIC.match(line)
        if match and c17_inventory.classify(match["option"], match["text"]):
            compiler_lines.add(int(match["line"]))
    check(compiler_lines, "host compiler reported no K&R diagnostics on the fixture")
    for line in sorted(compiler_lines - marked_lines):
        check(False, f"compiler reported unmarked fixture line {line}")
    inactive = {line for line, _, name in expected if name == "inactive_branch"}
    check(not (compiler_lines & inactive), "compiler reported code under an inactive #ifdef")

    # End to end: the fixture's scan reconciles with this compiler's report.
    raw = {}
    c17_inventory.parse_diagnostics(result.stderr, Path.cwd(), Path.cwd(), raw)
    report = {key: c17_inventory.interfaces(counts) for key, counts in raw.items()}
    totals = sum(report.values(), Counter())
    check(
        totals["definition"] > 0 and totals["declaration"] > 0,
        f"the compiler report parsed to {dict(totals)}; both kinds must be classified",
    )
    fixture_path = os.path.relpath(FIXTURE, Path.cwd())
    scan_rows = {}
    for finding in kr_scan.scan_text(fixture_path, text):
        scan_rows.setdefault((fixture_path, finding.line), []).append((finding.kind, finding.name))
    gaps, rows = c17_inventory.reconcile(scan_rows, report)
    check(not gaps, "the fixture does not reconcile with the compiler: " + "; ".join(gaps))
    check(
        rows.get(f"{fixture_path} definition inactive_branch scan") == 1,
        "the inactive definition was not labeled scan-only",
    )

    # Known-bad reconciliation: a compiler finding the scan lacks is a gap,
    # including a second interface on a line where the scan found one.
    scan_rows = {("a.c", 3): [("definition", "f")]}
    gaps, rows = c17_inventory.reconcile(scan_rows, {("a.c", 3, 1): Counter(definition=1)})
    check(not gaps and rows == {"a.c definition f both": 1}, "agreeing oracles were rejected")
    gaps, _ = c17_inventory.reconcile(scan_rows, {("a.c", 9, 1): Counter(definition=1)})
    check(gaps == ["a.c:9: compiler reports 1 definition, scan 0"], "a scan gap went undetected")
    gaps, rows = c17_inventory.reconcile(scan_rows, {})
    check(not gaps and rows == {"a.c definition f scan": 1}, "a scan-only row was mislabeled")
    pair = {("b.c", 5): [("declaration", "first"), ("declaration", "second")]}
    two = {("b.c", 5, 8): Counter(declaration=2)}
    gaps, rows = c17_inventory.reconcile(pair, two)
    check(
        not gaps and rows == {"b.c declaration first both": 1, "b.c declaration second both": 1},
        "two interfaces on one line were not both recorded",
    )
    gaps, _ = c17_inventory.reconcile({("b.c", 5): [("declaration", "first")]}, two)
    check(
        gaps == ["b.c:5: compiler reports 2 declaration, scan 1"],
        "a lost same-line interface went undetected",
    )
    clang_empty = {("c.c", 7, 17): Counter(declaration=1)}
    gaps, rows = c17_inventory.reconcile({("c.c", 7): [("definition", "e")]}, clang_empty)
    check(
        not gaps and rows == {"c.c definition e both": 1},
        "a Clang-style declaration report rejected an empty-parenthesis definition",
    )
    gcc_pair = Counter({"old-style": 1, "not-prototype": 1})
    check(
        c17_inventory.interfaces(gcc_pair) == Counter(definition=1),
        "GCC's paired definition diagnostics counted as two interfaces",
    )
    header = "sys/h.h:4:5: warning: function declaration isn't a prototype [-Wstrict-prototypes]\n"
    merged = {}
    for _unit in range(2):
        unit = {}
        c17_inventory.parse_diagnostics(header, Path.cwd(), Path.cwd(), unit)
        for key, counts in unit.items():
            merged[key] = merged.get(key, Counter()) | counts
    check(
        list(merged.values()) == [Counter({"not-prototype": 1})],
        "a header diagnostic repeated by a second translation unit was counted twice",
    )
    # GCC translates the severity label under a localized LANGUAGE or
    # LC_MESSAGES; the position and option tag stay the same.
    localized = (
        "x.c:3:5: Warnung: Funktionsdeklaration ist kein Prototyp [-Wstrict-prototypes]\n"
        "x.c:9:1: attention : d\u00e9finition de fonction de style ancien "
        "[-Wold-style-definition]\n"
    )
    found = {}
    c17_inventory.parse_diagnostics(localized, Path.cwd(), Path.cwd(), found)
    check(
        sorted((key[1], counts) for key, counts in found.items())
        == [(3, Counter({"not-prototype": 1})), (9, Counter({"old-style": 1}))],
        f"translated diagnostics parsed to {found}",
    )
    saved_diagnostic = c17_inventory.DIAGNOSTIC
    c17_inventory.DIAGNOSTIC = re.compile(
        r"^(?P<path>[^:\s]+):(?P<line>\d+):(?P<column>\d+): warning: (?P<text>.*)"
        r"\[-W(?P<option>[\w-]+)(?:,[^\]]*)?\]\s*$"
    )
    try:
        english = {}
        c17_inventory.parse_diagnostics(localized, Path.cwd(), Path.cwd(), english)
        check(english != found, "a parser requiring 'warning:' read translated diagnostics")
    finally:
        c17_inventory.DIAGNOSTIC = saved_diagnostic
    for option, message, kind in (
        ("strict-prototypes", "function declaration isn\u2019t a prototype ", "not-prototype"),
        ("strict-prototypes", "function declaration isn't a prototype ", "not-prototype"),
        ("old-style-definition", "old-style function definition ", "old-style"),
        ("deprecated-non-prototype", "a function definition without a prototype ", "old-style"),
        ("deprecated-non-prototype", "passing arguments to 'f' without a prototype ", None),
    ):
        check(
            c17_inventory.classify(option, message) == kind,
            f"-W{option} {message!r} classified wrongly",
        )
    one_line = {
        (f.line, f.kind, f.name)
        for f in kr_scan.scan_text("p.c", "struct p { int (*a)(), (*b)(); };")
    }
    check(len(one_line) == 2, "the scan collapsed two declarators on one line")

    # Every --build must contribute compiler coverage; a configuration that
    # compiles nothing must fail the gate even when the ledger would match.
    saved_findings, saved_argv = c17_inventory.compiler_findings, sys.argv
    with tempfile.TemporaryDirectory() as tmp:
        ledger_path = Path(tmp) / "ledger.txt"
        source = os.path.relpath(FIXTURE, Path.cwd())
        coverage = {"full": [source], "empty": []}
        c17_inventory.compiler_findings = lambda make, root, build, requested: (
            {},
            coverage[build.name],
        )
        try:
            results = []
            for builds in (["full", "full"], ["full", "empty"]):
                sys.argv = ["c17_inventory.py", "--ledger", str(ledger_path), "--update"]
                for build in builds:
                    sys.argv += ["--build", str(Path(tmp) / build)]
                sys.argv.append(source)
                with open(os.devnull, "w") as quiet:
                    saved_out, saved_err, sys.stdout, sys.stderr = (
                        sys.stdout,
                        sys.stderr,
                        quiet,
                        quiet,
                    )
                    try:
                        results.append(c17_inventory.main())
                    finally:
                        sys.stdout, sys.stderr = saved_out, saved_err
        finally:
            c17_inventory.compiler_findings, sys.argv = saved_findings, saved_argv
    check(results[0] == 0, "the inventory rejected builds that both compiled sources")
    check(results[1] == 1, "the inventory accepted a build that compiled no source")

    with tempfile.TemporaryDirectory() as tmp:
        ledger = Path(tmp) / "ledger.txt"
        text_out = c17_inventory.render({"a.c definition f both": 1})
        ledger.write_text(text_out)
        check(
            c17_inventory.render({"a.c definition f both": 1}) == ledger.read_text(),
            "ledger rendering is not deterministic",
        )
        check(
            c17_inventory.render({"a.c definition f both": 2}).endswith("both x2\n"),
            "duplicate rows lost their count",
        )

    if failures:
        for failure in failures:
            print("FAIL " + failure, file=sys.stderr)
        return 1
    print(
        f"PASS kr_scan calibration: {len(expected)} marked findings, "
        f"{len(compiler_lines)} compiler-reported lines inside the marks, "
        "known-bad scan and reconciliation variants rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
