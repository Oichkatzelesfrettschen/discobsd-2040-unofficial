"""Calibrate kr_scan.py and the inventory reconciliation.

The fixture marks every line the scan must report. The scan must report
exactly those lines; the host compiler, run with -Wold-style-definition and
-Wstrict-prototypes, must report only marked lines; and each known-bad
variant (a scan that drops a calibrated shape, a reconciliation that is
handed a compiler finding the scan lacks) must be rejected.
"""

import os
import re
import shlex
import subprocess
import sys
import tempfile
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
        if match:
            compiler_lines.add(int(match["line"]))
    check(compiler_lines, "host compiler reported no K&R diagnostics on the fixture")
    for line in sorted(compiler_lines - marked_lines):
        check(False, f"compiler reported unmarked fixture line {line}")
    inactive = {line for line, _, name in expected if name == "inactive_branch"}
    check(not (compiler_lines & inactive), "compiler reported code under an inactive #ifdef")

    # Known-bad reconciliation: a compiler finding the scan lacks is a gap.
    scan_rows = {("a.c", 3): ("definition", "f")}
    gaps, rows = c17_inventory.reconcile(scan_rows, {("a.c", 3): "definition"})
    check(not gaps and rows == {"a.c definition f both": 1}, "agreeing oracles were rejected")
    gaps, _ = c17_inventory.reconcile(scan_rows, {("a.c", 9): "definition"})
    check(gaps == ["a.c:9: compiler reports definition"], "a scan gap went undetected")
    gaps, rows = c17_inventory.reconcile(scan_rows, {})
    check(not gaps and rows == {"a.c definition f scan": 1}, "a scan-only row was mislabeled")

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
