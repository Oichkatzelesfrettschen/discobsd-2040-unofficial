"""Exercise the selected DPUCP-2024 byte-locale printf utility contract."""

import os
import signal
import subprocess
import sys


def run_printf(program, *arguments, stdout=subprocess.PIPE):
    return subprocess.run(
        [program, *arguments],
        stdout=stdout,
        stderr=subprocess.PIPE,
        check=False,
        restore_signals=False,
    )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: behavior_test.py /path/to/printf")

    program = sys.argv[1]
    checks = 0
    failures = []

    def check(name, condition, detail):
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(f"{name}: {detail}")

    def case(name, arguments, expected_output, expected_status=0,
             diagnostic=False):
        result = run_printf(program, *arguments)
        check(
            f"{name} output",
            result.stdout == expected_output,
            f"expected {expected_output!r}, got {result.stdout!r}",
        )
        check(
            f"{name} status",
            result.returncode == expected_status,
            f"expected {expected_status}, got {result.returncode}",
        )
        if diagnostic:
            check(
                f"{name} diagnostic",
                bool(result.stderr),
                "expected a diagnostic on standard error",
            )

    case(
        "percent-b escapes",
        ("abc%b%bZ", r"def\n", r"ghi\t"),
        b"abcdef\nghi\tZ",
    )
    case(
        "percent-b backslash-c",
        ("pre%bpost%s", r"one\ctwo", "unused"),
        b"preone",
    )
    case(
        "percent-b embedded NUL",
        ("%bX", r"A\000B"),
        b"A\x00BX",
    )
    case(
        "percent-b precision counts bytes",
        ("%.2bX", r"A\000B"),
        b"A\x00X",
    )
    case(
        "percent-b width",
        ("%5.3b", "ab"),
        b"   ab",
    )
    case("format reuse", ("[%s]", "a", "b"), b"[a][b]")
    case(
        "numbered conversions",
        ("%3$s:%1$d", "7", "unused", "tail"),
        b"tail:7",
    )
    case(
        "numbered format reuse",
        ("%2$s:%1$s ", "a", "b", "c", "d"),
        b"b:a d:c ",
    )
    case(
        "literal format does not reuse",
        ("literal", "unused"),
        b"literal",
    )
    case("percent format does not reuse", ("%%", "unused"), b"%")
    case(
        "malformed numeric continues",
        ("%d:%d", "12x", "7"),
        b"12:7",
        1,
        True,
    )
    case(
        "nonnumeric operand continues",
        ("%u:%s", "word", "tail"),
        b"0:tail",
        1,
        True,
    )
    case(
        "quoted byte numeric operand",
        ("%d:%u", "'A", '"B'),
        b"65:66",
    )
    case(
        "quoted byte ignores trailing data",
        ("%d:%s", "'+3", "tail"),
        b"43:tail",
    )
    case(
        "quoted empty operand uses NUL",
        ("%d", "'"),
        b"0",
    )
    case(
        "unsigned decimal accepts target maximum",
        ("%u", "4294967295"),
        b"4294967295",
    )
    case(
        "unsigned hexadecimal accepts target maximum",
        ("%x", "0xffffffff"),
        b"ffffffff",
    )
    case(
        "literal width overflow",
        ("%2147483648s", "x"),
        b"",
        1,
        True,
    )
    case(
        "profile width bound",
        ("%65536s", "x"),
        b"",
        1,
        True,
    )
    case(
        "literal precision overflow",
        ("%.2147483648s", "x"),
        b"",
        1,
        True,
    )
    case(
        "profile precision bound",
        ("%.65536s", "x"),
        b"",
        1,
        True,
    )
    case(
        "operand width overflow",
        ("%*s", "2147483648", "x"),
        b"",
        1,
        True,
    )
    case(
        "operand width rejects narrowing wrap",
        ("%*s", "4294967296", "x"),
        b"",
        1,
        True,
    )
    case(
        "missing conversion operand",
        ("%d:%s",),
        b"0:",
    )
    case(
        "malformed format",
        ("before%qafter", "x"),
        b"before",
        1,
        True,
    )
    case(
        "trailing percent",
        ("before%",),
        b"before",
        1,
        True,
    )
    case(
        "repeated flags stay bounded",
        ("%-0-0-0-0-0-0-0-0-0-0-5s", "x"),
        b"x    ",
    )
    case("dash format", ("--", "-n"), b"-n")

    read_descriptor, write_descriptor = os.pipe()
    os.close(read_descriptor)
    try:
        failed_output = run_printf(
            program,
            "%s",
            "x" * 8192,
            stdout=write_descriptor,
        )
    finally:
        os.close(write_descriptor)
    check(
        "output failure status",
        failed_output.returncode == 1,
        f"expected 1, got {failed_output.returncode}",
    )
    check(
        "output failure diagnostic",
        bool(failed_output.stderr),
        "expected a diagnostic on standard error",
    )

    if failures:
        for failure in failures:
            print(f"FAIL {failure}", file=sys.stderr)
        print(
            f"printf contracts: {len(failures)} of {checks} checks failed",
            file=sys.stderr,
        )
        return 1
    print(f"printf contracts: {checks} checks passed", file=sys.stderr)
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)
    raise SystemExit(main())
