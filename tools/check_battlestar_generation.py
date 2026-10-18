"""Calibrate and verify Battlestar's shared message-generation ordering."""

import argparse
import subprocess
import tempfile
from pathlib import Path

OUTPUTS = "battle_strings _globals.c _dayfile.c _nightfile.c"
ORDER_DECLARATION = f".ORDER: {OUTPUTS}"
SHARED_RULE = f"{OUTPUTS}: mkstr globals.c dayfile.c nightfile.c"


def run_fixture(make_command, ordered):
    with tempfile.TemporaryDirectory() as temporary_directory:
        fixture_root = Path(temporary_directory)
        makefile_lines = [f"OUTPUTS= {OUTPUTS}", "all: ${OUTPUTS}"]
        if ordered:
            makefile_lines.append(".ORDER: ${OUTPUTS}")
        makefile_lines.extend(
            [
                "${OUTPUTS}: input",
                "\t@printf '%s\\n' ${.TARGET} >> invocations",
                "\t@sleep 0.1",
                "\t@touch ${OUTPUTS}",
            ]
        )
        (fixture_root / "Makefile").write_text(
            "\n".join(makefile_lines) + "\n", encoding="utf-8"
        )
        (fixture_root / "input").write_text("fixture\n", encoding="utf-8")
        result = subprocess.run(
            [make_command, "-j3", "all"],
            cwd=fixture_root,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"fixture make failed with status {result.returncode}: "
                f"{result.stdout}{result.stderr}"
            )
        return (fixture_root / "invocations").read_text(
            encoding="utf-8"
        ).splitlines()


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--make", default="bmake")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    unordered_invocations = run_fixture(arguments.make, ordered=False)
    if len(unordered_invocations) < 2:
        raise SystemExit(
            "Battlestar generation calibration failed: unordered fixture "
            "did not launch overlapping output recipes"
        )
    ordered_invocations = run_fixture(arguments.make, ordered=True)
    if len(ordered_invocations) != 1:
        raise SystemExit(
            "Battlestar generation calibration failed: ordered fixture "
            f"launched {len(ordered_invocations)} recipes"
        )

    makefile_path = arguments.root / "games/battlestar/Makefile"
    makefile = makefile_path.read_text(encoding="utf-8")
    if SHARED_RULE not in makefile:
        raise SystemExit("Battlestar shared message-generation rule changed")
    if ORDER_DECLARATION not in makefile:
        raise SystemExit(
            "Battlestar message outputs lack an ordering declaration"
        )
    print(
        "PASS Battlestar generation ordering: unordered fixture launched "
        f"{len(unordered_invocations)} recipes; ordered fixture launched one"
    )


if __name__ == "__main__":
    main()
