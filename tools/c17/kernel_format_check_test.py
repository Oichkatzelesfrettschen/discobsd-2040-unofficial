"""Calibrate kernel_format_check.py against marked accepted and rejected calls.

Every line of fixtures/kernel_formats.c marked "expect: reject" must be
reported and no other line may be. Known-bad checkers, one that accepts every
conversion GCC accepts, one allowing l on every conversion, one reading every
call as a declaration, one that reads escapes undecoded and one that drops
the literal-format requirement, must each fail the fixture.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kernel_format_check  # noqa: E402

FIXTURE = HERE / "fixtures" / "kernel_formats.c"


def reported(text):
    problems, checked = kernel_format_check.check_text("fixture.c", text)
    return {int(p.split(":")[1]) for p in problems}, checked


def main():
    text = FIXTURE.read_text()
    marked = {n for n, line in enumerate(text.splitlines(), 1) if "expect: reject" in line}
    failures = []
    got, checked = reported(text)
    if len(marked) < 10:
        failures.append("fixture lost its expectation markers")
    for line in sorted(marked - got):
        failures.append(f"accepted marked line {line}")
    for line in sorted(got - marked):
        failures.append(f"rejected unmarked line {line}")
    if checked < 10:
        failures.append(f"only {checked} literal formats were checked")

    saved = kernel_format_check.ALLOWED
    kernel_format_check.ALLOWED = re.compile(r".*")
    try:
        if reported(text)[0] == got:
            failures.append("a checker accepting every conversion matched the fixture")
    finally:
        kernel_format_check.ALLOWED = saved
    for label, name, pattern in (
        ("l on every conversion", "ALLOWED", r"[-+#0]*(\d+|\*)?(\.(\d+|\*))?l?[cdopsuxX]\Z"),
        ("every call read as a declaration", "DECLARATION_PREFIX", r""),
    ):
        saved_pattern = getattr(kernel_format_check, name)
        setattr(kernel_format_check, name, re.compile(pattern))
        try:
            if reported(text)[0] == got:
                failures.append(f"a checker with {label} matched the fixture")
        finally:
            setattr(kernel_format_check, name, saved_pattern)

    saved_decode = kernel_format_check.decode_literal
    kernel_format_check.decode_literal = lambda body: body
    try:
        if reported(text)[0] == got:
            failures.append("a checker reading escapes undecoded matched the fixture")
    finally:
        kernel_format_check.decode_literal = saved_decode

    saved_literal = kernel_format_check.literal_format
    kernel_format_check.literal_format = lambda argument: saved_literal(argument) or ""
    try:
        if reported(text)[0] == got:
            failures.append("a checker accepting non-literal formats matched the fixture")
    finally:
        kernel_format_check.literal_format = saved_literal

    if failures:
        for failure in failures:
            print("FAIL " + failure, file=sys.stderr)
        return 1
    print(
        f"PASS kernel format calibration: {len(marked)} rejected calls reported, "
        f"{checked} literal formats read, known-bad checkers rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
