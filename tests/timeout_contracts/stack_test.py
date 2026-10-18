#!/usr/bin/env python3
"""Calibrate and enforce the Cortex-M0+ timeout static-stack bound."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

MAIN_FRAME_LIMIT = 128
PARSE_DURATION_FRAME_LIMIT = 48
PRE_FORK_CHAIN_LIMIT = MAIN_FRAME_LIMIT + PARSE_DURATION_FRAME_LIMIT
STACK_MUTATION_ANCHOR = "struct timeout_duration {"
DOCUMENTED_LIMITS_FRAGMENT = (
    f"a {MAIN_FRAME_LIMIT}-byte `main` frame, a "
    f"{PARSE_DURATION_FRAME_LIMIT}-byte `parse_duration` frame, and a derived "
    f"{PRE_FORK_CHAIN_LIMIT}-byte pre-fork call-chain ceiling"
)


class StackFailure(RuntimeError):
    pass


def require(condition: bool, description: str) -> None:
    if not condition:
        raise StackFailure(description)


def require_stack_limit(
    description: str, measured: int, limit: int, components: str
) -> None:
    require(
        measured <= limit,
        f"{description}: {components}, total={measured}, limit={limit}",
    )


def validate_production_frames(main_frame: int, parse_duration_frame: int) -> int:
    pre_fork_chain = main_frame + parse_duration_frame
    require_stack_limit(
        "timeout main static frame",
        main_frame,
        MAIN_FRAME_LIMIT,
        f"main={main_frame}",
    )
    require_stack_limit(
        "timeout parse_duration static frame",
        parse_duration_frame,
        PARSE_DURATION_FRAME_LIMIT,
        f"parse_duration={parse_duration_frame}",
    )
    require_stack_limit(
        "timeout pre-fork static call chain",
        pre_fork_chain,
        PRE_FORK_CHAIN_LIMIT,
        f"main={main_frame}, parse_duration={parse_duration_frame}",
    )
    return pre_fork_chain


def validate_documented_limits(testing_text: str) -> None:
    require(
        DOCUMENTED_LIMITS_FRAGMENT in testing_text,
        "timeout TESTING policy omits the measured stack limits",
    )


def compile_source(compiler: str, root: Path, source: Path, output: Path) -> Path:
    command = [
        compiler,
        "-std=c17",
        "-pedantic-errors",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wstrict-prototypes",
        "-Wold-style-definition",
        "-Wmissing-prototypes",
        "-fno-common",
        "-mcpu=cortex-m0plus",
        "-mabi=aapcs",
        "-mlittle-endian",
        "-mthumb",
        "-mfloat-abi=soft",
        "-ffreestanding",
        "-nostdinc",
        "-isystem",
        str(root / "include"),
        "-Os",
        "-fstack-usage",
        "-c",
        str(source),
        "-o",
        str(output),
    ]
    result = subprocess.run(command, capture_output=True, check=False)
    require(
        result.returncode == 0,
        "Cortex-M0+ stack compile failed:\n" + result.stderr.decode(errors="replace"),
    )
    return output.with_suffix(".su")


def stack_usage(path: Path) -> dict[str, tuple[int, str]]:
    usage: dict[str, tuple[int, str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        require(len(fields) == 3, f"malformed stack-usage row: {line}")
        function = fields[0].rsplit(":", 1)[-1]
        require(function not in usage, f"duplicate stack-usage function: {function}")
        usage[function] = (int(fields[1]), fields[2])
    return usage


def main() -> int:
    require(len(sys.argv) == 3, "usage: stack_test.py ROOT ARM_COMPILER")
    root = Path(sys.argv[1]).resolve()
    compiler = sys.argv[2]
    production_source = root / "usr.bin" / "timeout" / "timeout.c"
    source_text = production_source.read_text(encoding="utf-8")
    testing_text = (root / "sys" / "arch" / "rp2040" / "doc" / "TESTING.md").read_text(
        encoding="utf-8"
    )
    validate_documented_limits(testing_text)
    require(
        source_text.count(STACK_MUTATION_ANCHOR) == 1,
        "duration-structure stack mutation has a stale source anchor",
    )

    with tempfile.TemporaryDirectory(prefix="timeout-stack-") as temporary:
        build_directory = Path(temporary)
        production_usage = stack_usage(
            compile_source(
                compiler,
                root,
                production_source,
                build_directory / "timeout-production.o",
            )
        )
        require("main" in production_usage, "production stack report omits main")
        require("parse_duration" in production_usage, "stack report omits parse_duration")
        require(
            all(kind == "static" for _, kind in production_usage.values()),
            "production stack report contains a dynamic frame",
        )
        main_frame = production_usage["main"][0]
        parse_duration_frame = production_usage["parse_duration"][0]
        pre_fork_chain = validate_production_frames(
            main_frame, parse_duration_frame
        )

        bad_source = build_directory / "timeout-known-bad.c"
        bad_source.write_text(
            source_text.replace(
                STACK_MUTATION_ANCHOR,
                "struct timeout_duration {\n\tchar oversized_frame[1024];",
            ),
            encoding="utf-8",
        )
        bad_usage = stack_usage(
            compile_source(
                compiler,
                root,
                bad_source,
                build_directory / "timeout-known-bad.o",
            )
        )
        require("main" in bad_usage, "known-bad stack report omits main")
        require(
            bad_usage["main"][0] > MAIN_FRAME_LIMIT,
            "known-bad duration structures pass the main-frame bound",
        )

    print(
        "PASS timeout Cortex-M0+ stack: "
        f"main={main_frame}, pre-fork-chain={pre_fork_chain}, "
        f"mutation-main={bad_usage['main'][0]}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except StackFailure as error:
        print(f"timeout stack: {error}", file=sys.stderr)
        raise SystemExit(1) from error
